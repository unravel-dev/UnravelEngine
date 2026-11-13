/*
 * Validation suite for the editor's update check (editor/core/update_check).
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "update check"
 *   <build-dir>/bin/unravel-tests --suite "update check" --network
 *
 * The release workflow attaches latest.json, the newest published build of every platform, to the releases; the
 * editor reads the latest release's copy and names a newer build of its own platform. This pins:
 *   - latest.json is read only in the layout the workflow writes; anything else (an HTML page, a cut-off or
 *     oversized file, another schema) is no answer, and entries that are no valid build are skipped;
 *   - a build is announced only when it is newer than the editor, counting the commits since the tag; the
 *     dismissed build is announced again only when the user asks;
 *   - the automatic check asks every 6 hours, 1 hour after an attempt without an answer;
 *   - the state kept between runs reads back as it was written, in a file of each platform's own;
 *   - only https:// addresses are fetched.
 * With --network it also fetches from GitHub: latest.json, a small file that arrives whole and a large one that the
 * size limit refuses, both after a redirect to another host, and an address that never answers, which a stop and
 * the time limit must end quickly.
 */

#include "../tests.h"

#include <update_check/https_get.h>
#include <update_check/update_fetch.h>
#include <update_check/update_manifest.h>
#include <update_check/update_schedule.h>
#include <update_check/update_state.h>

#include <chrono>
#include <cstdio>
#include <random>
#include <string>

namespace
{

using namespace unravel::update_check;

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

/// A time well after the epoch, so that every offset the cases use stays positive.
constexpr std::int64_t NOW_SECONDS = 1'800'000'000;
constexpr std::int64_t MINUTE_SECONDS = 60;
constexpr std::int64_t HOUR_SECONDS = 60 * MINUTE_SECONDS;
/// TEST-NET-1 (RFC 5737): reserved for documentation, never routed, so a connection to it never answers.
constexpr const char* UNREACHABLE_URL = "https://192.0.2.1/latest.json";
/// A large file behind a redirect from github.com to another host.
constexpr const char* LARGE_DOWNLOAD_URL = "https://github.com/unravel-dev/UnravelEngine/archive/refs/tags/v1.2.0.zip";
/// A small file behind a redirect from github.com to another host, and the text it starts with.
constexpr const char* SMALL_DOWNLOAD_URL = "https://github.com/unravel-dev/UnravelEngine/raw/refs/tags/v1.2.0/LICENSE.txt";
constexpr const char* SMALL_DOWNLOAD_START = "MIT License";
/// How https_get's refusal of another scheme starts; the platform clients never say it, so it shows that the
/// address was refused before a client started.
constexpr const char* HTTPS_REFUSAL_START = "Only https:// addresses";

auto at_seconds(std::int64_t seconds) -> std::chrono::system_clock::time_point
{
    return std::chrono::system_clock::time_point(std::chrono::seconds(seconds));
}

auto make_entry(const std::string& platform, const std::string& version, const std::string& tag, const std::string& asset)
    -> std::string
{
    return R"({"platform":")" + platform + R"(","version":")" + version + R"(","tag":")" + tag + R"(","asset":")" +
           asset + R"("})";
}

auto make_manifest(const std::string& entries, const std::string& schema = "1") -> std::string
{
    return R"({"schema":)" + schema + R"(,"builds":[)" + entries + "]}";
}

auto make_good_entry(const std::string& platform, const std::string& version, const std::string& tag) -> std::string
{
    return make_entry(platform, version, tag, "UnravelEngine-" + version + "-" + platform + ".zip");
}

auto parse_version(const char* text) -> version::engine_version
{
    return *version::parse(text);
}

auto make_state(const std::string& newest_version, const std::string& newest_tag, const std::string& dismissed = {})
    -> update_state
{
    update_state state;
    state.newest_version = newest_version;
    state.newest_tag = newest_tag;
    state.dismissed_version = dismissed;
    return state;
}

void test_manifest_read()
{
    std::printf("test_manifest_read\n");
    const auto text = make_manifest(make_good_entry("Windows-AMD64", "1.2.0-413", "v1.2.0") + "," +
                                    make_good_entry("Linux-x86_64-Ubuntu-24.04", "1.2.0-413", "v1.2.0"));
    const auto manifest = read_update_manifest(text);
    check(manifest && manifest->builds.size() == 2, "the file the workflow writes reads with both builds");
    if(!manifest)
    {
        return;
    }
    const auto windows = find_platform_build(*manifest, "Windows-AMD64");
    check(windows && windows->version == parse_version("1.2.0-413") && windows->tag == "v1.2.0" &&
              windows->platform == "Windows-AMD64",
          "a platform finds its own build");
    check(!find_platform_build(*manifest, "Linux-x86_64-Fedora-40"), "a platform without a package finds none");
    const auto with_more = R"({"schema":1,"generated":"2026-11-13","builds":[{"platform":"Windows-AMD64",)"
                           R"("version":"1.3.0","tag":"v1.3.0","asset":"UnravelEngine-1.3.0-Windows-AMD64.zip",)"
                           R"("sha256":"00"}]})";
    const auto later = read_update_manifest(with_more);
    check(later && later->builds.size() == 1 && later->builds.front().version == parse_version("1.3.0"),
          "unknown fields are ignored, and a build of the tagged commit has no count");
    check(read_update_manifest(make_manifest("")).has_value(), "a manifest without builds is an answer");
}

void test_manifest_refused()
{
    std::printf("test_manifest_refused\n");
    const auto good = make_manifest(make_good_entry("Windows-AMD64", "1.2.0-413", "v1.2.0"));
    check(!read_update_manifest(""), "empty text");
    check(!read_update_manifest("<!DOCTYPE html><html><body>Sign in to the network</body></html>"),
          "the HTML page of a portal or a proxy");
    check(!read_update_manifest(good.substr(0, good.size() / 2)), "a file cut off");
    check(!read_update_manifest(make_manifest(make_good_entry("Windows-AMD64", "1.2.0-413", "v1.2.0"), "2")),
          "another schema");
    check(!read_update_manifest(make_manifest(make_good_entry("Windows-AMD64", "1.2.0-413", "v1.2.0"), R"("1")")),
          "a schema that is no number");
    check(!read_update_manifest(R"({"builds":[]})"), "no schema");
    check(!read_update_manifest(R"({"schema":1,"builds":{}})"), "builds that are no array");
    check(!read_update_manifest("[" + good + "]"), "an array instead of an object");
    check(!read_update_manifest(good + std::string(MAX_MANIFEST_SIZE, ' ')), "a valid file larger than the limit");
}

void test_manifest_skipped_entries()
{
    std::printf("test_manifest_skipped_entries\n");
    const auto good = make_good_entry("Windows-AMD64", "1.2.0-413", "v1.2.0");
    const std::string bad_entries[] = {
        make_good_entry("Windows-AMD64", "1.2", "v1.2.0"),
        make_good_entry("Windows-AMD64", "1.2.0-413-gabcdef12", "v1.2.0"),
        make_good_entry("Windows-AMD64", "v1.2.0-413", "v1.2.0"),
        make_good_entry("Windows-AMD64", "1.2.0-0413", "v1.2.0"),
        make_good_entry("Windows-AMD64", "1.2.0-413", "1.2.0"),
        make_good_entry("Windows-AMD64", "1.2.0-413", "v1.2.0-5"),
        make_good_entry("Windows-AMD64", "1.2.0-413", "v1.2.0/../../evil"),
        make_entry("Windows-AMD64", "1.2.0-413", "v1.2.0", "UnravelEngine-1.2.0-412-Windows-AMD64.zip"),
        make_good_entry("Linux x86_64", "1.2.0-413", "v1.2.0"),
        make_good_entry("-Windows", "1.2.0-413", "v1.2.0"),
        R"({"platform":"Windows-AMD64","version":1,"tag":"v1.2.0","asset":"UnravelEngine-1-Windows-AMD64.zip"})",
        R"({"platform":"Windows-AMD64","version":"1.2.0-413","tag":"v1.2.0"})",
        R"("UnravelEngine-1.2.0-413-Windows-AMD64.zip")",
    };
    for(const auto& bad : bad_entries)
    {
        const auto manifest = read_update_manifest(make_manifest(bad + "," + good));
        check(manifest && manifest->builds.size() == 1 && manifest->builds.front().version == parse_version("1.2.0-413"),
              "an entry that is no valid build is skipped, the rest is read: " + bad);
    }
}

void test_announce()
{
    std::printf("test_announce\n");
    const auto own = parse_version("1.2.0-319-gabcdef12");
    const auto newer = get_build_to_announce(make_state("1.2.0-320", "v1.2.0"), own, false);
    check(newer && newer->version == parse_version("1.2.0-320") && newer->tag == "v1.2.0" &&
              newer->platform == get_own_platform(),
          "one commit more is a newer build, announced for this platform");
    check(!get_build_to_announce(make_state("1.2.0-319", "v1.2.0"), own, true), "the same build is not announced");
    check(!get_build_to_announce(make_state("1.2.0-300", "v1.2.0"), own, true),
          "a developer build ahead of the release is not told about the older one");
    check(get_build_to_announce(make_state("1.3.0", "v1.3.0"), parse_version("1.2.0-999"), false).has_value(),
          "a later release is newer than any commit after an earlier one");
    check(!get_build_to_announce(make_state("1.2.0-320", "v1.2.0", "1.2.0-320"), own, false),
          "the dismissed build is not announced again by itself");
    check(get_build_to_announce(make_state("1.2.0-320", "v1.2.0", "1.2.0-320"), own, true).has_value(),
          "the dismissed build is announced when the user asks");
    check(get_build_to_announce(make_state("1.2.0-321", "v1.2.0", "1.2.0-320"), own, false).has_value(),
          "dismissing a build does not silence the next one");
    check(!get_build_to_announce(make_state("", ""), own, true), "no known build announces nothing");
    check(!get_build_to_announce(make_state("1.2.0-x", "v1.2.0"), own, true), "a damaged version announces nothing");
    check(!get_build_to_announce(make_state("1.2.0-320", "../evil"), own, true), "a damaged tag announces nothing");
}

void test_schedule()
{
    std::printf("test_schedule\n");
    const auto now = at_seconds(NOW_SECONDS);
    check(is_check_due({}, now), "a check that never asked is due");
    update_state answered;
    answered.last_attempt = NOW_SECONDS - (6 * HOUR_SECONDS - 1);
    answered.last_answer = answered.last_attempt;
    check(!is_check_due(answered, now), "an answer less than 6 hours old is kept");
    answered.last_attempt = answered.last_answer = NOW_SECONDS - 6 * HOUR_SECONDS;
    check(is_check_due(answered, now), "6 hours after an answer the check is due");
    update_state failed;
    failed.last_answer = NOW_SECONDS - 10 * HOUR_SECONDS;
    failed.last_attempt = NOW_SECONDS - 59 * MINUTE_SECONDS;
    check(!is_check_due(failed, now), "an attempt without an answer waits an hour");
    failed.last_attempt = NOW_SECONDS - 61 * MINUTE_SECONDS;
    check(is_check_due(failed, now), "an hour after an attempt without an answer the check is due");
    update_state ahead;
    ahead.last_attempt = ahead.last_answer = NOW_SECONDS + 100;
    check(is_check_due(ahead, now), "an attempt in the future (the clock was set back) does not block the check");
}

void test_record_outcome()
{
    std::printf("test_record_outcome\n");
    const auto now = at_seconds(NOW_SECONDS);
    auto state = make_state("1.2.0-300", "v1.2.0");
    record_outcome(state, {true, release_build{"Windows-AMD64", parse_version("1.3.0-2"), "v1.3.0"}, {}}, now);
    check(state.last_attempt == NOW_SECONDS && state.last_answer == NOW_SECONDS && state.newest_version == "1.3.0-2" &&
              state.newest_tag == "v1.3.0",
          "an answer with a build keeps the build and both times");
    record_outcome(state, {false, hpp::nullopt, "no network"}, at_seconds(NOW_SECONDS + HOUR_SECONDS));
    check(state.last_attempt == NOW_SECONDS + HOUR_SECONDS && state.last_answer == NOW_SECONDS &&
              state.newest_version == "1.3.0-2",
          "an attempt without an answer keeps what the last answer said");
    record_outcome(state, {true, hpp::nullopt, {}}, at_seconds(NOW_SECONDS + 2 * HOUR_SECONDS));
    check(state.newest_version.empty() && state.newest_tag.empty(),
          "an answer that lists no build for this platform forgets the build");
}

void test_state()
{
    std::printf("test_state\n");
    update_state state;
    state.last_attempt = NOW_SECONDS;
    state.last_answer = NOW_SECONDS - HOUR_SECONDS;
    state.newest_version = "1.2.0-413";
    state.newest_tag = "v1.2.0";
    state.dismissed_version = "1.2.0-400";
    check(read_update_state(write_update_state(state)) == state, "the state reads back as it was written");
    check(read_update_state("") == update_state{}, "empty text is the default state");
    check(read_update_state("{ not json") == update_state{}, "a damaged file is the default state");
    fs::error_code err;
    // A folder of this run's own: runners of other build trees may run the suite at the same time.
    const fs::path folder =
        fs::temp_directory_path(err) / ("unravel-tests-update-check-" + std::to_string(std::random_device{}()));
    const fs::path file = folder / "update_check.cfg";
    check(load_update_state(file) == update_state{}, "a missing file is the default state");
    check(save_update_state(file, state) && load_update_state(file) == state,
          "the state is saved to a new folder and loads back");
    fs::remove_all(folder, err);
    const auto own_path = get_update_state_path(get_own_platform());
    check(own_path.filename().string().find(std::string(get_own_platform())) != std::string::npos &&
              own_path != get_update_state_path("Linux-x86_64-Ubuntu-22.04") &&
              own_path.parent_path() == get_update_state_path("Linux-x86_64-Ubuntu-22.04").parent_path(),
          "each platform keeps its state in a file of its own, side by side: " + own_path.string());
}

void test_addresses()
{
    std::printf("test_addresses\n");
    check(get_release_page_url("v1.2.0") == "https://github.com/unravel-dev/UnravelEngine/releases/tag/v1.2.0",
          "a release page is the repository's page of the tag");
    check(is_platform_name(get_own_platform()),
          "this build's platform is a platform name: " + std::string(get_own_platform()));
    check(get_package_file_name(parse_version("1.2.0-413"), "Windows-AMD64") == "UnravelEngine-1.2.0-413-Windows-AMD64.zip",
          "a package is named as CPack names it");
    check(is_https_url(MANIFEST_URL), "latest.json is fetched over HTTPS");
    check(!is_https_url("http://github.com/") && !is_https_url("https://") && !is_https_url("HTTPS-ish") &&
              !is_https_url("file:///etc/passwd"),
          "nothing else is an https:// address");
    const auto plain = https_get("http://github.com/unravel-dev/UnravelEngine/releases/latest/download/latest.json");
    check(!plain.body && plain.error.rfind(HTTPS_REFUSAL_START, 0) == 0,
          "an http:// address is refused before anything starts: " + plain.error);
}

/// Whether the request reached a server that has no latest.json yet: WinHTTP's status, wget's or curl's exit code
/// for an HTTP error.
auto is_missing_file_answer(const std::string& error) -> bool
{
    return error.find("status 404") != std::string::npos || error.find("wget ended with exit code 8") != std::string::npos ||
           error.find("curl ended with exit code 22") != std::string::npos;
}

/// Whether the request ended for the size limit: the check of the body, or curl's own limit.
auto is_size_refusal(const std::string& error) -> bool
{
    return error.find("larger than") != std::string::npos || error.find("curl ended with exit code 63") != std::string::npos;
}

void test_network_manifest()
{
    std::printf("test_network_manifest\n");
    const auto response = https_get(MANIFEST_URL);
    if(!response.body && is_missing_file_answer(response.error))
    {
        std::printf("  SKIP: the latest release has no latest.json yet (%s)\n", response.error.c_str());
        return;
    }
    check(response.body && read_update_manifest(*response.body).has_value(),
          "the latest release's latest.json is fetched and read: " + response.error);
    const auto outcome = fetch_update({});
    check(outcome.has_answer, "the editor's fetch reads it: " + outcome.error);
    std::printf("  this platform (%s): %s\n",
                std::string(get_own_platform()).c_str(),
                outcome.build ? (outcome.build->version.to_string() + " in " + outcome.build->tag).c_str() : "no build");
}

void test_network_whole_body()
{
    std::printf("test_network_whole_body\n");
    const auto response = https_get(SMALL_DOWNLOAD_URL);
    check(response.body && response.body->rfind(SMALL_DOWNLOAD_START, 0) == 0,
          "a small file behind a redirect to another host arrives whole: " + response.error);
}

void test_network_size_limit()
{
    std::printf("test_network_size_limit\n");
    const auto response = https_get(LARGE_DOWNLOAD_URL);
    check(!response.body && is_size_refusal(response.error),
          "a file larger than the limit, behind a redirect to another host, is refused: " + response.error);
}

void test_network_stop()
{
    std::printf("test_network_stop\n");
    const auto stop_after = std::chrono::milliseconds(300);
    auto start = std::chrono::steady_clock::now();
    const auto stopped = https_get(UNREACHABLE_URL,
                                   {},
                                   [&]()
                                   {
                                       return std::chrono::steady_clock::now() - start >= stop_after;
                                   });
    auto elapsed = std::chrono::steady_clock::now() - start;
    check(!stopped.body && elapsed < std::chrono::milliseconds(1500),
          "a stop ends a request that gets no answer within a second: " +
              std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()) + " ms");
    https_limits short_limits;
    short_limits.total_time = std::chrono::seconds(2);
    start = std::chrono::steady_clock::now();
    const auto timed_out = https_get(UNREACHABLE_URL, short_limits);
    elapsed = std::chrono::steady_clock::now() - start;
    check(!timed_out.body && elapsed < std::chrono::milliseconds(3500),
          "the time limit ends a request that gets no answer: " +
              std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()) + " ms");
}

auto run_update_check_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_manifest_read();
    test_manifest_refused();
    test_manifest_skipped_entries();
    test_announce();
    test_schedule();
    test_record_outcome();
    test_state();
    test_addresses();
    if(unravel::tests::wants("network"))
    {
        test_network_manifest();
        test_network_whole_body();
        test_network_size_limit();
        test_network_stop();
    }
    std::printf("update check: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("update check", run_update_check_suite)
