/*
 * Validation suite for the third-party notices compiled into the engine.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "third party notices"
 *
 * The LGPL asks every game to say which libraries it links and where their source is. This pins:
 *   - the text carries the libraries under the LGPL, with their license texts and source addresses;
 *   - it names the engine version of this binary instead of the file's placeholder;
 *   - it stays ASCII, as every text in the repository does.
 */

#include "../tests.h"

#include <engine/third_party_notices.h>
#include <version/version.h>

#include <algorithm>
#include <cstdio>
#include <string>

using namespace unravel;

namespace
{

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAILED: %s\n", what.c_str());
    }
}

auto has_text(const std::string& text, const std::string& part) -> bool
{
    return text.find(part) != std::string::npos;
}

void test_names_the_lgpl_libraries(const std::string& actual)
{
    const std::string expected_parts[] = {"OpenAL Soft 1.24.3",
                                          "https://github.com/kcat/openal-soft/tree/1.24.3",
                                          "GNU LIBRARY GENERAL PUBLIC LICENSE",
                                          "libdwarf-lite",
                                          "GNU LESSER GENERAL PUBLIC LICENSE",
                                          "https://github.com/unravel-dev/UnravelEngine",
                                          "three years"};
    for(const auto& expected : expected_parts)
    {
        check(has_text(actual, expected), "the notices contain: " + expected);
    }
}

void test_names_the_engine_version(const std::string& actual)
{
    check(!has_text(actual, "@ENGINE_VERSION@"), "the placeholder is filled in");
    check(has_text(actual, "Engine version of this player: " + version::get_full() + "\n"), "the notices name this binary's engine version");
}

void test_is_ascii(const std::string& actual)
{
    const auto is_ascii = [](char c) { return static_cast<unsigned char>(c) < 128; };
    check(std::all_of(actual.begin(), actual.end(), is_ascii), "the notices are ASCII");
}

auto run_third_party_notices_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    const auto actual = get_third_party_notices();
    test_names_the_lgpl_libraries(actual);
    test_names_the_engine_version(actual);
    test_is_ascii(actual);
    std::printf("third party notices: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("third party notices", run_third_party_notices_suite)
