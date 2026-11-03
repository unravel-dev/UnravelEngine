/*
 * Validation suite for the third-party notices compiled into the engine.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "third party notices"
 *
 * The LGPL asks every game to say which libraries it links and where their source is. This pins:
 *   - the text carries the libraries under the LGPL, with their license texts and source addresses;
 *   - a build without them carries no word of the LGPL, and only their own lines and license texts go;
 *   - this build links no library under the LGPL, and its text names only the libraries it links;
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

/// The line above and below the title of each library's license text.
const std::string SECTION_RULE = "\n" + std::string(78, '=') + "\n";
/// The license texts of OpenAL Soft, its parts under BSD licenses, Oboe, libdwarf-lite and miniaudio.
constexpr int OPTIONAL_SECTION_COUNT = 5;

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

auto count_text(const std::string& text, const std::string& part) -> int
{
    int count = 0;
    for(auto found = text.find(part); found != std::string::npos; found = text.find(part, found + part.size()))
    {
        ++count;
    }
    return count;
}

auto make_libraries(bool has_openal_soft, bool has_libdwarf, bool has_miniaudio) -> linked_libraries
{
    linked_libraries libraries;
    libraries.has_openal_soft = has_openal_soft;
    libraries.has_libdwarf = has_libdwarf;
    libraries.has_miniaudio = has_miniaudio;
    return libraries;
}

void test_names_the_lgpl_libraries(const std::string& all)
{
    const std::string expected_parts[] = {"LIBRARIES UNDER THE GNU LESSER GENERAL PUBLIC LICENSE",
                                          "  OpenAL Soft 1.24.3: https://github.com/kcat/openal-soft/tree/1.24.3",
                                          "GNU LIBRARY GENERAL PUBLIC LICENSE",
                                          "  libdwarf-lite: https://github.com/jeremy-rifkin/libdwarf-lite",
                                          "Version 2.1, February 1999",
                                          "https://github.com/unravel-dev/UnravelEngine",
                                          "three years",
                                          "  Oboe 1.11.0: Apache 2.0.",
                                          "  miniaudio 0.11.26: Public domain, or MIT No Attribution.",
                                          "Copyright 2026 David Reid"};
    for(const auto& expected : expected_parts)
    {
        check(has_text(all, expected), "the notices of every library contain: " + expected);
    }
}

void test_leaves_out_the_lgpl_without_its_libraries(const std::string& all)
{
    const auto actual = get_third_party_notices(make_libraries(false, false, true));
    const std::string left_out_parts[] = {"GNU LESSER GENERAL PUBLIC LICENSE",
                                          "GNU LIBRARY GENERAL PUBLIC LICENSE",
                                          "OpenAL",
                                          "libdwarf",
                                          "Oboe",
                                          "three years",
                                          "Engine version of this player"};
    for(const auto& left_out : left_out_parts)
    {
        check(!has_text(actual, left_out), "a build without OpenAL Soft and libdwarf leaves out: " + left_out);
    }
    check(has_text(actual, "list.\n\n\nTHE SOFTWARE IN THIS GAME\n"), "the list of the software follows the preface");
    check(has_text(actual, "  miniaudio 0.11.26:") && has_text(actual, "  bgfx: BSD 2-Clause."),
          "the other libraries stay listed");
    const auto expected_sections = count_text(all, SECTION_RULE) / 2 - (OPTIONAL_SECTION_COUNT - 1);
    check(count_text(actual, SECTION_RULE) / 2 == expected_sections,
          "only the license texts of OpenAL Soft, Oboe and libdwarf go");
}

void test_keeps_the_lgpl_library_a_build_links()
{
    const auto with_libdwarf = get_third_party_notices(make_libraries(false, true, true));
    check(has_text(with_libdwarf, "  libdwarf-lite: https://") && has_text(with_libdwarf, "Version 2.1, February 1999"),
          "a build with libdwarf alone names it and its license");
    check(!has_text(with_libdwarf, "OpenAL") && !has_text(with_libdwarf, "GNU LIBRARY GENERAL PUBLIC LICENSE"),
          "a build with libdwarf alone does not name OpenAL Soft");
    check(has_text(with_libdwarf, "Engine version of this player: " + version::get_full() + "\n"),
          "a build with libdwarf alone says where the player's source is");
    const auto with_openal_soft = get_third_party_notices(make_libraries(true, false, true));
    check(has_text(with_openal_soft, "  OpenAL Soft 1.24.3: https://") && has_text(with_openal_soft, "  Oboe 1.11.0:"),
          "a build with OpenAL Soft alone names it and Oboe");
    check(!has_text(with_openal_soft, "libdwarf") && !has_text(with_openal_soft, "Version 2.1, February 1999"),
          "a build with OpenAL Soft alone does not name libdwarf");
}

void test_leaves_out_miniaudio_without_it()
{
    const auto actual = get_third_party_notices(make_libraries(true, true, false));
    check(!has_text(actual, "miniaudio") && !has_text(actual, "Copyright 2026 David Reid"),
          "a build without miniaudio leaves out its line and license");
    check(has_text(actual, "LIBRARIES UNDER THE GNU LESSER GENERAL PUBLIC LICENSE"),
          "a build without miniaudio keeps the LGPL part");
}

void test_follows_this_build()
{
    const auto libraries = get_linked_libraries();
    check(!libraries.has_libdwarf, "the build reads symbols with dladdr or dbghelp, not libdwarf");
    const auto actual = get_third_party_notices();
    check(!has_text(actual, "GNU LESSER GENERAL PUBLIC LICENSE"), "the build links no library under the LGPL");
    check(has_text(actual, "  libdwarf-lite:") == libraries.has_libdwarf,
          "this binary's notices name libdwarf only when it links it");
    check(has_text(actual, "  OpenAL Soft 1.24.3:") == libraries.has_openal_soft,
          "this binary's notices name OpenAL Soft only when it links it");
    check(has_text(actual, "  miniaudio 0.11.26:") == libraries.has_miniaudio,
          "this binary's notices name miniaudio only when it links it");
}

void test_names_the_engine_version(const std::string& all)
{
    check(!has_text(all, "@ENGINE_VERSION@"), "the placeholder is filled in");
    check(has_text(all, "Engine version of this player: " + version::get_full() + "\n"),
          "the notices name this binary's engine version");
}

void test_is_ascii(const std::string& all)
{
    const auto is_ascii = [](char c) { return static_cast<unsigned char>(c) < 128; };
    check(std::all_of(all.begin(), all.end(), is_ascii), "the notices are ASCII");
}

auto run_third_party_notices_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    const auto all = get_third_party_notices(make_libraries(true, true, true));
    test_names_the_lgpl_libraries(all);
    test_leaves_out_the_lgpl_without_its_libraries(all);
    test_keeps_the_lgpl_library_a_build_links();
    test_leaves_out_miniaudio_without_it();
    test_follows_this_build();
    test_names_the_engine_version(all);
    test_is_ascii(all);
    std::printf("third party notices: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("third party notices", run_third_party_notices_suite)
