/*
 * Validation suite for the editor's sample catalog (editor/core/sample_catalog).
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "sample catalog"
 *   <build-dir>/bin/unravel-tests --suite "sample catalog" --network
 *
 * The samples workflow writes samples.json from the sample repositories (.github/scripts/sample_catalog.py) and
 * attaches it to the latest release; the editor ships a copy and reads the latest release's copy. This pins:
 *   - samples.json is read only in the layout the script writes; anything else is no answer, and an entry with any
 *     field the editor would not use (a name an address cannot carry, a path out of the repository, text too long)
 *     is skipped, as is a repository listed twice;
 *   - the copy the editor ships reads, and the picture of each of its samples ships beside it;
 *   - addresses are made from the names in the file, never read from it;
 *   - of two files the later one wins, and a file with no sample never replaces one; the automatic fetch asks once
 *     a day, 1 hour after an attempt without an answer, and its state reads back as it was written;
 *   - fetched pictures are named after their commit, and only those the file no longer names are removed;
 *   - a project is a copy of a sample by the repository's name, or by that name with a suffix and the sample's
 *     project_guid, never by the project_guid alone;
 *   - a sample's version comes from samples.json and a copy's from its own sample.json, 1 when either names none.
 * With --network it also fetches from GitHub: the latest release's samples.json, when there is one, and a sample's
 * picture at its commit.
 */

#include "../tests.h"

#include <https/fetch_schedule.h>
#include <https/https_get.h>
#include <sample_catalog/sample_cache.h>
#include <sample_catalog/sample_fetch.h>
#include <sample_catalog/sample_manifest.h>
#include <sample_catalog/sample_state.h>

#include <chrono>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>

using namespace unravel::test;

namespace
{

using namespace unravel::sample_catalog;

constexpr const char* GOOD_COMMIT = "0123456789abcdef0123456789abcdef01234567";
constexpr const char* GOOD_GUID = "0ea8296e-ec53-be46-bdb2-ee0d0615a730";
constexpr const char* GOOD_GENERATED = "2026-11-16T12:00:00Z";
/// A picture that is on GitHub: the 2048 sample's screenshot, at the commit that added it.
constexpr const char* NETWORK_REPOSITORY = "UnravelEngine-Sample-2048";
constexpr const char* NETWORK_PICTURE_PATH = "docs/screenshot.jpg";
constexpr const char* NETWORK_PICTURE_COMMIT = "886aba88a451b7174e9ccc47bc795134e09ff747";
/// A time well after the epoch, so that every offset the cases use stays positive.
constexpr std::int64_t NOW_SECONDS = 1'800'000'000;
constexpr std::int64_t HOUR_SECONDS = 60 * 60;

auto at_seconds(std::int64_t seconds) -> std::chrono::system_clock::time_point
{
    return std::chrono::system_clock::time_point(std::chrono::seconds(seconds));
}

/// An entry with every field. `override_fields` go first: of two equal keys the reader takes the first, so they
/// replace the defaults after them.
auto make_entry(const std::string& repository, const std::string& override_fields = {}) -> std::string
{
    return "{" + override_fields + R"("repository":")" + repository +
           R"(","branch":"main","title":"2048","topic":"Casual game","description":"The sliding tile puzzle.",)"
           R"("tags":["Puzzle","Mobile","C# Scripts"],"icon":"numeric-2-box","added":"2026-11-03",)"
           R"("min_engine":"1.2.0-400","project_guid":")" +
           GOOD_GUID + R"(","thumbnail":{"path":"thumbnail.jpg","commit":")" + GOOD_COMMIT + R"("}})";
}

auto make_manifest(const std::string& entries, const std::string& schema = "1", const std::string& generated = GOOD_GENERATED)
    -> std::string
{
    return R"({"schema":)" + schema + R"(,"generated":")" + generated + R"(","samples":[)" + entries + "]}";
}

/// How many samples a manifest of a valid entry and one with `override_fields` keeps.
auto count_with(const std::string& override_fields) -> std::size_t
{
    const auto manifest = read_sample_manifest(make_manifest(make_entry("UnravelEngine-Sample-Good") + "," +
                                                             make_entry("UnravelEngine-Sample-Other", override_fields)));
    return manifest ? manifest->samples.size() : 0;
}

void write_text(const fs::path& path, const std::string& text)
{
    fs::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), std::streamsize(text.size()));
}

auto read_text(const fs::path& path) -> std::string
{
    std::ifstream stream(path, std::ios::binary);
    return fs::read_stream_str(stream);
}

void test_manifest_read()
{
    std::printf("test_manifest_read\n");
    const auto manifest = read_sample_manifest(make_manifest(make_entry("UnravelEngine-Sample-2048")));
    require(manifest && manifest->samples.size() == 1, "the file the script writes reads with its sample");
    check_eq(manifest->generated, GOOD_GENERATED, "the time it was written");
    const sample_entry& sample = manifest->samples.front();
    check_eq(sample.repository, "UnravelEngine-Sample-2048", "repository");
    check_eq(sample.branch, "main", "branch");
    check_eq(sample.title, "2048", "title");
    check_eq(sample.topic, "Casual game", "topic");
    check_eq(sample.description, "The sliding tile puzzle.", "description");
    check(sample.tags == std::vector<std::string>{"Puzzle", "Mobile", "C# Scripts"}, "tags in their order");
    check_eq(sample.icon, "numeric-2-box", "icon");
    check_eq(sample.added, "2026-11-03", "added");
    check(sample.min_engine && sample.min_engine->to_string() == "1.2.0-400", "min_engine");
    check_eq(sample.project_guid, GOOD_GUID, "project_guid");
    check_eq(sample.thumbnail.path, "thumbnail.jpg", "thumbnail path");
    check_eq(sample.thumbnail.commit, GOOD_COMMIT, "thumbnail commit");

    const std::string minimal = R"({"repository":"UnravelEngine-Sample-UI","branch":"main","title":"Game UI",)"
                                R"("topic":"User interface","description":"Menus.","unknown":[1,2],)"
                                R"("thumbnail":{"path":"docs/shot.png","commit":")" +
                                std::string(GOOD_COMMIT) + R"("}})";
    const auto with_minimal = read_sample_manifest(R"({"schema":1,"generated":")" + std::string(GOOD_GENERATED) +
                                                   R"(","extra":true,"samples":[)" + minimal + "]}");
    require(with_minimal && with_minimal->samples.size() == 1, "an entry without its optional fields reads");
    const sample_entry& plain = with_minimal->samples.front();
    check(plain.tags.empty() && plain.icon.empty() && plain.added.empty() && !plain.min_engine &&
              plain.project_guid.empty(),
          "optional fields left out stay empty, and unknown fields are ignored");
    check(read_sample_manifest(make_manifest("")).has_value(), "a manifest without samples is an answer");
}

void test_manifest_refused()
{
    std::printf("test_manifest_refused\n");
    const auto good = make_manifest(make_entry("UnravelEngine-Sample-2048"));
    check(!read_sample_manifest(""), "empty text");
    check(!read_sample_manifest("<!DOCTYPE html><html><body>Sign in to the network</body></html>"),
          "the HTML page of a portal or a proxy");
    check(!read_sample_manifest(good.substr(0, good.size() / 2)), "a file cut off");
    check(!read_sample_manifest(make_manifest(make_entry("UnravelEngine-Sample-2048"), "2")), "another schema");
    check(!read_sample_manifest(make_manifest(make_entry("UnravelEngine-Sample-2048"), R"("1")")),
          "a schema that is no number");
    check(!read_sample_manifest(make_manifest(make_entry("UnravelEngine-Sample-2048"), "1", "2026-11-16")),
          "a time without its clock");
    check(!read_sample_manifest(make_manifest(make_entry("UnravelEngine-Sample-2048"), "1", "2026-13-16T12:00:00Z")),
          "a time of a month that does not exist");
    check(!read_sample_manifest(R"({"schema":1,"samples":[]})"), "no time");
    check(!read_sample_manifest(R"({"schema":1,"generated":"2026-11-16T12:00:00Z","samples":{}})"),
          "samples that are no array");
    check(!read_sample_manifest("[" + good + "]"), "an array instead of an object");
    check(!read_sample_manifest(good + std::string(MAX_MANIFEST_SIZE, ' ')), "a valid file larger than the limit");
}

void test_manifest_skipped_entries()
{
    std::printf("test_manifest_skipped_entries\n");
    check_eq(count_with({}), 2, "two valid entries are both kept");
    const std::string long_text(MAX_DESCRIPTION_SIZE + 1, 'x');
    const struct
    {
        const char* fields;
        const char* what;
    } refused[] = {
        {R"("repository":"Sample-2048",)", "a repository without the sample prefix"},
        {R"("repository":"UnravelEngine-Sample-",)", "a repository that is only the prefix"},
        {R"("repository":"UnravelEngine-Sample-a/b",)", "a repository name with a slash"},
        {R"("repository":42,)", "a repository that is no text"},
        {R"("branch":"../main",)", "a branch that climbs out"},
        {R"("branch":"-main",)", "a branch that starts with a dash"},
        {R"("branch":"ma in",)", "a branch with a space"},
        {R"("title":"",)", "an empty title"},
        {R"("title":" 2048",)", "a title that starts with a space"},
        {"\"title\":\"20\\n48\",", "a title with a line break"},
        {R"("topic":"A topic far longer than any line over a title can hold, really",)", "a topic too long"},
        {R"("tags":["a","b","c","d"],)", "four tags"},
        {R"("tags":"Puzzle",)", "tags that are no array"},
        {R"("tags":[1],)", "a tag that is no text"},
        {R"("icon":"Sword_Cross",)", "an icon name with capitals"},
        {R"("added":"2026-02-30",)", "a day that does not exist"},
        {R"("min_engine":"v1.2.0",)", "a version with a v"},
        {R"("min_engine":"1.2.0-400-g98f4b223",)", "a version with a commit"},
        {R"("project_guid":"0EA8296E-EC53-BE46-BDB2-EE0D0615A730",)", "a guid in capitals"},
        {R"("thumbnail":{"path":"../secret.jpg","commit":"0123456789abcdef0123456789abcdef01234567"},)",
         "a picture out of the repository"},
        {R"("thumbnail":{"path":"/etc/shot.jpg","commit":"0123456789abcdef0123456789abcdef01234567"},)",
         "a picture at an absolute path"},
        {R"("thumbnail":{"path":"shot.gif","commit":"0123456789abcdef0123456789abcdef01234567"},)",
         "a picture that is no JPEG or PNG"},
        {R"("thumbnail":{"path":"shot.jpg","commit":"0123456"},)", "a short commit"},
        {R"("thumbnail":{"path":"shot.jpg","commit":"main"},)", "a branch for a commit"},
        {R"("thumbnail":"thumbnail.jpg",)", "a thumbnail that is no object"},
        {R"("version":0,)", "version 0"},
        {R"("version":-2,)", "a negative version"},
        {R"("version":1.5,)", "a version that is no whole number"},
        {R"("version":"2",)", "a version that is text"},
        {R"("version":1000001,)", "a version above the highest"},
    };
    for(const auto& entry : refused)
    {
        check_eq(count_with(entry.fields), 1, std::string("skipped: ") + entry.what);
    }
    check_eq(count_with(R"("description":")" + long_text + R"(",)"), 1, "skipped: a description too long");
    check_eq(count_with(R"("repository":"UnravelEngine-Sample-Good",)"), 1, "skipped: a repository listed twice");

    const std::string without_title = R"({"repository":"UnravelEngine-Sample-X","branch":"main","topic":"t",)"
                                      R"("description":"d","thumbnail":{"path":"a.jpg","commit":")" +
                                      std::string(GOOD_COMMIT) + R"("}})";
    const auto missing = read_sample_manifest(make_manifest(without_title));
    check(missing && missing->samples.empty(), "an entry without a title is skipped");

    std::string many;
    for(std::size_t index = 0; index < MAX_SAMPLES + 5; ++index)
    {
        many += (index == 0 ? "" : ",") + make_entry("UnravelEngine-Sample-" + std::to_string(index));
    }
    const auto capped = read_sample_manifest(make_manifest(many));
    check(capped && capped->samples.size() == MAX_SAMPLES, "at most MAX_SAMPLES are kept");
}

void test_names_and_dates()
{
    std::printf("test_names_and_dates\n");
    check(is_thumbnail_path("thumbnail.jpg") && is_thumbnail_path("docs/screenshot.jpeg") && is_thumbnail_path("a/b.png"),
          "pictures in the repository");
    check(!is_thumbnail_path("a//b.jpg") && !is_thumbnail_path("./a.jpg") && !is_thumbnail_path("a/../b.jpg") &&
              !is_thumbnail_path("a.JPG") && !is_thumbnail_path("a b.jpg") && !is_thumbnail_path("a.jpg/"),
          "no empty part, no dot part, lower-case extension, no space, no trailing slash");
    check(is_branch_name("main") && is_branch_name("release/1.2"), "branch names");
    check(!is_branch_name("") && !is_branch_name("a//b") && !is_branch_name("a/.."), "no empty or climbing part");
    check(is_project_guid(GOOD_GUID) && !is_project_guid("0ea8296e-ec53-be46-bdb2-ee0d0615a73") &&
              !is_project_guid("0ea8296eec53be46bdb2ee0d0615a730") && !is_project_guid(std::string(GOOD_GUID) + "0"),
          "project guids are 8-4-4-4-12 hex digits");
    check(is_icon_name("view-dashboard-outline") && !is_icon_name("") && !is_icon_name("palette!"), "icon names");
    check(!is_repository_name("UnravelEngine-Sample-\xC3\xA9t\xC3\xA9") && !is_thumbnail_path("\xC3\xA9.jpg"),
          "names hold ASCII only, whatever the locale");

    check(parse_date("1970-01-01") == std::int64_t{0}, "the epoch is day 0");
    check(parse_date("2000-03-01") == std::int64_t{11017}, "2000-03-01 follows the leap day of 2000");
    check(parse_date("2000-02-29").has_value() && !parse_date("1900-02-29") && !parse_date("2001-02-29"),
          "leap years: 2000 is one, 1900 and 2001 are not");
    check(!parse_date("2026-1-01") && !parse_date("2026-00-10") && !parse_date("2026-04-31") && !parse_date("20261116xx"),
          "other spellings and days that do not exist");
    check(is_generated_time("2026-11-16T23:59:59Z") && !is_generated_time("2026-11-16T24:00:00Z") &&
              !is_generated_time("2026-11-16 12:00:00Z") && !is_generated_time("2026-11-16T12:00:00"),
          "generated times");
    check(parse_min_engine("1.2.0").has_value() && parse_min_engine("1.2.0-450").has_value(), "plain versions");
    check(!parse_min_engine("1.2") && !parse_min_engine("01.2.0") && !parse_min_engine("1.2.0-0"),
          "other spellings of a version");
}

void test_addresses()
{
    std::printf("test_addresses\n");
    const auto manifest = read_sample_manifest(make_manifest(make_entry("UnravelEngine-Sample-2048")));
    require(manifest && manifest->samples.size() == 1, "the sample reads");
    const sample_entry& sample = manifest->samples.front();
    check_eq(get_repository_url(sample), "https://github.com/unravel-dev/UnravelEngine-Sample-2048", "repository page");
    check_eq(get_download_url(sample),
             "https://github.com/unravel-dev/UnravelEngine-Sample-2048/archive/refs/heads/main.zip",
             "ZIP of the branch");
    check_eq(get_thumbnail_url(sample),
             "https://raw.githubusercontent.com/unravel-dev/UnravelEngine-Sample-2048/" + std::string(GOOD_COMMIT) +
                 "/thumbnail.jpg",
             "picture at its commit");
    check(unravel::https::is_https_url(MANIFEST_URL) && unravel::https::is_https_url(get_thumbnail_url(sample)),
          "everything is fetched over HTTPS");
    check_eq(get_thumbnail_file_name(sample), "UnravelEngine-Sample-2048-0123456789ab.jpg", "fetched picture's name");
    check_eq(get_shipped_thumbnail_file_name(sample), "UnravelEngine-Sample-2048.jpg", "shipped picture's name");
}

/// A manifest written at `generated` with `count` samples.
auto make_dated_manifest(const char* generated, std::size_t count) -> sample_manifest
{
    sample_manifest manifest;
    manifest.generated = generated;
    for(std::size_t index = 0; index < count; ++index)
    {
        sample_entry sample;
        sample.repository = "UnravelEngine-Sample-" + std::to_string(index);
        manifest.samples.push_back(sample);
    }
    return manifest;
}

void test_replacement_and_schedule()
{
    std::printf("test_replacement_and_schedule\n");
    const sample_manifest older = make_dated_manifest("2026-11-16T12:00:00Z", 2);
    const sample_manifest newer = make_dated_manifest("2026-11-16T12:00:01Z", 1);
    const sample_manifest newer_empty = make_dated_manifest("2026-11-17T00:00:00Z", 0);
    check(is_replacement_manifest(newer, older) && !is_replacement_manifest(older, newer),
          "the later file replaces the earlier one, not the other way");
    check(is_replacement_manifest(older, older), "the same file replaces itself, so its fetched pictures are listed");
    check(!is_replacement_manifest(newer_empty, older), "a later file with no sample does not empty the list");
    check(is_replacement_manifest(older, newer_empty) && is_replacement_manifest(older, sample_manifest{}),
          "any file with samples replaces one without");

    check(choose_manifest(older, newer).generated == newer.generated, "the fetched file when it is later");
    check(choose_manifest(newer, older).generated == newer.generated, "the shipped file when it is later");
    check(choose_manifest(older, newer_empty).samples.size() == 2, "the shipped file over a fetched one without samples");
    check(choose_manifest(hpp::nullopt, older).samples.size() == 2, "the fetched file when none ships");
    check(choose_manifest(hpp::nullopt, hpp::nullopt).samples.empty(), "no file: no samples");

    fetch_state state;
    check(is_fetch_due(state, at_seconds(NOW_SECONDS)), "never asked: due");
    record_fetch(state, true, at_seconds(NOW_SECONDS));
    check(state.last_attempt == NOW_SECONDS && state.last_answer == NOW_SECONDS, "an answer records both times");
    check(!is_fetch_due(state, at_seconds(NOW_SECONDS + 23 * HOUR_SECONDS)), "answered: not due within a day");
    check(is_fetch_due(state, at_seconds(NOW_SECONDS + 24 * HOUR_SECONDS)), "answered: due after a day");
    record_fetch(state, false, at_seconds(NOW_SECONDS + 24 * HOUR_SECONDS));
    check(state.last_answer == NOW_SECONDS, "an attempt without an answer keeps the last answer");
    check(!is_fetch_due(state, at_seconds(NOW_SECONDS + 24 * HOUR_SECONDS + HOUR_SECONDS - 1)),
          "no answer: not due within an hour");
    check(is_fetch_due(state, at_seconds(NOW_SECONDS + 25 * HOUR_SECONDS)), "no answer: due after an hour");
    check(is_fetch_due(state, at_seconds(NOW_SECONDS)), "the clock set back: due");

    const fs::path path = temp_path("sample_catalog_state") / "fetch_state.cfg";
    check(save_fetch_state(path, state), "the state is written");
    check(load_fetch_state(path) == state, "the state reads back as written");
    check(read_fetch_state("not a state") == fetch_state{}, "unreadable text is the default state");
    check(load_fetch_state(path.parent_path() / "missing.cfg") == fetch_state{}, "a missing file is the default state");
}

void test_cache_files()
{
    std::printf("test_cache_files\n");
    check(is_picture_data(std::string("\xFF\xD8\xFF\xE0", 4)) && is_picture_data(std::string("\x89PNG\r\n\x1A\n\0", 9)),
          "JPEG and PNG files");
    check(!is_picture_data("<html>") && !is_picture_data("") && !is_picture_data("\xFF\xD8"), "anything else");

    const auto manifest = read_sample_manifest(make_manifest(make_entry("UnravelEngine-Sample-2048")));
    require(manifest.has_value(), "the sample reads");
    const fs::path dir = temp_path("sample_catalog_thumbnails");
    const fs::path listed = dir / get_thumbnail_file_name(manifest->samples.front());
    const fs::path unlisted = dir / "UnravelEngine-Sample-2048-ffffffffffff.jpg";
    const fs::path other = dir / "notes.txt";
    write_text(listed, "listed");
    write_text(unlisted, "unlisted");
    write_text(other, "other");
    remove_unlisted_thumbnails(dir, *manifest);
    fs::error_code error;
    check(fs::exists(listed, error) && !fs::exists(unlisted, error) && fs::exists(other, error),
          "only the pictures the file no longer names are removed");

    const fs::path manifest_path = dir / "samples.json";
    check(unravel::https::write_file_atomically(manifest_path, make_manifest(make_entry("UnravelEngine-Sample-2048"))),
          "a manifest is written");
    const auto loaded = load_manifest_file(manifest_path);
    check(loaded && loaded->samples.size() == 1, "and loads");
    write_text(manifest_path, make_manifest(make_entry("UnravelEngine-Sample-2048")) + std::string(MAX_MANIFEST_SIZE, ' '));
    check(!load_manifest_file(manifest_path), "a file larger than the limit is not read");
    check(!load_manifest_file(dir / "missing.json"), "a missing file is no manifest");
}

void test_sample_copies()
{
    std::printf("test_sample_copies\n");
    const std::string repository = "UnravelEngine-Sample-UI";
    const fs::path clone = "C:/Projects/UnravelEngine-Sample-UI";
    const fs::path clone_other_case = "C:/Projects/unravelengine-sample-ui";
    const fs::path zip = "C:/Downloads/UnravelEngine-Sample-UI-main";
    const fs::path other_sample = "C:/Projects/UnravelEngine-Sample-UI-Advanced";
    const fs::path game = "C:/Projects/MyGame";
    const fs::path missing = "C:/Gone/UnravelEngine-Sample-UI";
    std::vector<fs::path> reads;
    const project_lookup lookup{[&](const fs::path& project) -> std::string
                                {
                                    reads.push_back(project);
                                    return project == other_sample ? std::string("11111111-1111-1111-1111-111111111111")
                                                                   : std::string(GOOD_GUID);
                                },
                                [&](const fs::path& project)
                                {
                                    return project != missing;
                                }};
    check(find_sample_copy(repository, GOOD_GUID, {game, clone}, lookup) == clone, "a clone, by its name");
    check(find_sample_copy(repository, GOOD_GUID, {clone_other_case}, lookup) == clone_other_case,
          "a clone, by its name in another case");
    check(find_sample_copy(repository, GOOD_GUID, {zip}, lookup) == zip, "a ZIP copy, by its name and project_guid");
    check(find_sample_copy(repository, "", {zip}, lookup).empty(), "a ZIP copy needs the sample's project_guid");
    check(find_sample_copy(repository, GOOD_GUID, {other_sample}, lookup).empty(),
          "another sample whose name starts with this one's is not a copy");
    check(find_sample_copy(repository, GOOD_GUID, {game}, lookup).empty(),
          "a game made from the sample, under its own name, is not a copy");
    check(find_sample_copy(repository, GOOD_GUID, {missing}, lookup).empty(), "a folder that is gone is not a copy");
    check(std::none_of(reads.begin(),
                       reads.end(),
                       [&](const fs::path& project)
                       {
                           return project == game || project == clone;
                       }),
          "only folders named as a suffixed copy are read");
}

void test_versions()
{
    std::printf("test_versions\n");
    const auto plain = read_sample_manifest(make_manifest(make_entry("UnravelEngine-Sample-2048")));
    check(plain && plain->samples.size() == 1 && plain->samples.front().version == FIRST_SAMPLE_VERSION,
          "an entry without a version is version 1");
    const auto third = read_sample_manifest(make_manifest(make_entry("UnravelEngine-Sample-2048", R"("version":3,)")));
    check(third && third->samples.size() == 1 && third->samples.front().version == 3u, "an entry's version is read");

    check(read_sample_version(R"({"schema":1,"title":"2048"})") == FIRST_SAMPLE_VERSION,
          "a sample.json without a version is version 1");
    check(read_sample_version(R"({"version":4})") == 4u, "a sample.json's version is read");
    check(!read_sample_version(R"({"version":0})") && !read_sample_version(R"({"version":"4"})") &&
              !read_sample_version("[1]") && !read_sample_version("not json") &&
              !read_sample_version(R"({"version":4})" + std::string(MAX_SAMPLE_JSON_SIZE, ' ')),
          "anything else names no version");

    const fs::path copy = temp_path("sample_catalog_copy");
    check(!load_copy_version(copy), "a copy without a sample.json has no version");
    write_text(get_copy_sample_json_path(copy), R"({"schema":1,"version":2})");
    check(load_copy_version(copy) == 2u, "a copy's version comes from its sample.json");
}

void test_shipped_catalog()
{
    std::printf("test_shipped_catalog\n");
    const fs::path dir = fs::path(EDITOR_DATA_DIR) / "samples";
    const auto manifest = load_manifest_file(dir / "samples.json");
    require(manifest && !manifest->samples.empty(), "the samples.json the editor ships reads with its samples");
    std::printf("  %zu samples, written %s\n", manifest->samples.size(), manifest->generated.c_str());
    for(const sample_entry& sample : manifest->samples)
    {
        const fs::path picture = dir / get_shipped_thumbnail_file_name(sample);
        check(is_picture_data(read_text(picture)), sample.repository + ": its picture ships as " + picture.filename().string());
    }
}

/// Whether the request reached a server that has no such file: WinHTTP's status, wget's or curl's exit code for an
/// HTTP error.
auto is_missing_file_answer(const std::string& error) -> bool
{
    return error.find("status 404") != std::string::npos || error.find("wget ended with exit code 8") != std::string::npos ||
           error.find("curl ended with exit code 22") != std::string::npos;
}

void test_network_manifest()
{
    std::printf("test_network_manifest\n");
    const auto response = unravel::https::https_get(MANIFEST_URL, {std::chrono::seconds(20), MAX_MANIFEST_SIZE, 5});
    if(!response.body && is_missing_file_answer(response.error))
    {
        skip("The latest release has no samples.json yet (" + response.error + ").");
    }
    const auto manifest = response.body ? read_sample_manifest(*response.body) : hpp::nullopt;
    check(manifest.has_value(), "the latest release's samples.json is fetched and read: " + response.error);
    if(manifest)
    {
        std::printf("  %zu samples, written %s\n", manifest->samples.size(), manifest->generated.c_str());
    }
}

void test_network_thumbnail()
{
    std::printf("test_network_thumbnail\n");
    sample_manifest manifest;
    manifest.generated = GOOD_GENERATED;
    sample_entry sample;
    sample.repository = NETWORK_REPOSITORY;
    sample.branch = "main";
    sample.thumbnail = {NETWORK_PICTURE_PATH, NETWORK_PICTURE_COMMIT};
    manifest.samples.push_back(sample);
    sample_entry missing = sample;
    missing.repository = "UnravelEngine-Sample-Missing";
    missing.thumbnail.commit = std::string(40, '0');
    manifest.samples.push_back(missing);

    const fs::path dir = temp_path("sample_catalog_network");
    const auto errors = fetch_missing_thumbnails(manifest, dir, {});
    const fs::path fetched = dir / get_thumbnail_file_name(sample);
    check(is_picture_data(read_text(fetched)), "a sample's picture is fetched at its commit");
    fs::error_code error;
    check(errors.size() == 1 && !fs::exists(dir / get_thumbnail_file_name(missing), error),
          "a picture that is not there is named among the errors and writes nothing");
    const auto again = fetch_missing_thumbnails(manifest, dir, {});
    check(again.size() == 1, "a picture already fetched is not fetched again");
}

void run_sample_catalog_suite(rtti::context& /*ctx*/)
{
    RUN_CASE(test_manifest_read());
    RUN_CASE(test_manifest_refused());
    RUN_CASE(test_manifest_skipped_entries());
    RUN_CASE(test_names_and_dates());
    RUN_CASE(test_addresses());
    RUN_CASE(test_replacement_and_schedule());
    RUN_CASE(test_cache_files());
    RUN_CASE(test_sample_copies());
    RUN_CASE(test_versions());
    RUN_CASE(test_shipped_catalog());
    if(unravel::tests::wants("network"))
    {
        RUN_CASE(test_network_manifest());
        RUN_CASE(test_network_thumbnail());
    }
}

} // namespace

REGISTER_TEST_SUITE("sample catalog", run_sample_catalog_suite)
