/*
 * Validation suite for the engine version text (engine/core/version).
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "engine version"
 *
 * The version is what git describe prints for the sources a build was made from. Projects store it, the editor
 * compares it with a project's and with a release's, and the player stamps carry it. This pins:
 *   - the forms git describe gives: a tag alone, a tag with the commits since it and the commit, each with or
 *     without the leading v, and either one ending in -dirty for sources with uncommitted changes;
 *   - text that is no version is refused: missing numbers, other suffixes, numbers too long to be one;
 *   - the order counts the commits since the tag, so 1.2.0-320 is newer than 1.2.0-319; the commit and the dirty
 *     marker take no part in it;
 *   - a version written back reads as the same version;
 *   - the version this runner was built with is one.
 */

#include "../tests.h"

#include <version/version.h>

#include <cstdio>
#include <string>

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
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

auto is_version(const std::string& text, int major, int minor, int patch, int commit_count, const std::string& sha)
    -> bool
{
    const auto parsed = version::parse(text);
    return parsed && parsed->major == major && parsed->minor == minor && parsed->patch == patch &&
           parsed->commit_count == commit_count && parsed->sha == sha && parsed->original == text;
}

void test_forms()
{
    std::printf("test_forms\n");
    check(is_version("1.2.0", 1, 2, 0, 0, ""), "a release's numbers alone");
    check(is_version("v1.2.0", 1, 2, 0, 0, ""), "a tag with its v");
    check(is_version("v1.2.0-413-ge6891cbf", 1, 2, 0, 413, "ge6891cbf"), "a tag, the commits since it and the commit");
    check(is_version("1.2.0-413", 1, 2, 0, 413, ""), "a package's version: the commits without the commit");
    check(is_version("v1.2.0-413-ge6891cbf-dirty", 1, 2, 0, 413, "ge6891cbf"),
          "sources with uncommitted changes are their commit's version");
    check(is_version("v1.3.0-dirty", 1, 3, 0, 0, ""), "also when the commit is the tag's");
    const auto spaced = version::parse("  v2.10.3-7-gabc  \n");
    check(spaced && spaced->major == 2 && spaced->minor == 10 && spaced->patch == 3 && spaced->commit_count == 7,
          "space around the text is no part of it");
}

void test_refused()
{
    std::printf("test_refused\n");
    for(const auto* text : {"",
                            "v",
                            "1.2",
                            "1.2.x",
                            "1.2.0-",
                            "1.2.0-beta",
                            "1.2.0.4",
                            "NOTFOUND",
                            "-128-NOTFOUND",
                            "dirty",
                            "1.2.0-99999999999",
                            "99999999999.0.0"})
    {
        check(!version::parse(text), std::string("no version: \"") + text + "\"");
    }
}

void test_order()
{
    std::printf("test_order\n");
    const auto parse = [](const char* text)
    {
        return *version::parse(text);
    };
    check(parse("1.2.0-320-gaaaaaaaa") > parse("1.2.0-319-gbbbbbbbb"), "more commits since the tag is newer");
    check(version::compare(parse("1.2.0-320"), parse("1.2.0-319")) == 1 &&
              version::compare(parse("1.2.0-319"), parse("1.2.0-320")) == -1,
          "compare says the same, either way round");
    check(parse("1.2.0-1-gabc") > parse("1.2.0"), "a commit after a tag is newer than the tag");
    check(parse("1.2.1") > parse("1.2.0-999-gabc") && parse("1.3.0") > parse("1.2.9") &&
              parse("2.0.0") > parse("1.99.99"),
          "a later tag is newer than any commit after an earlier one");
    check(parse("1.2.0-5-gaaaaaaaa") == parse("1.2.0-5-gbbbbbbbb") &&
              version::compare(parse("v1.2.0-5-gabc"), parse("1.2.0-5")) == 0,
          "the commit takes no part in the order");
    check(parse("1.2.0-5-gabc-dirty") == parse("1.2.0-5-gabc") && parse("1.2.0-dirty") == parse("1.2.0"),
          "nor does the marker of uncommitted changes");
}

void test_written_back()
{
    std::printf("test_written_back\n");
    for(const auto* text : {"1.2.0", "1.2.0-413", "1.2.0-413-ge6891cbf"})
    {
        check(version::parse(text)->to_string() == text, std::string("written back as it was read: ") + text);
    }
    check(version::parse("v1.2.0-413-ge6891cbf-dirty")->to_string() == "1.2.0-413-ge6891cbf",
          "written without the v and without the marker, as the commit's version");
    const auto current = version::get_current();
    check(version::parse(version::get_full()).has_value() &&
              current.to_string() == version::parse(version::get_full())->to_string(),
          "this build's own version is one: " + version::get_full());
}

auto run_engine_version_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_forms();
    test_refused();
    test_order();
    test_written_back();
    std::printf("engine version: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("engine version", run_engine_version_suite)
