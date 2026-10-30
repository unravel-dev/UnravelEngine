/*
 * Validation suite for the player's display options.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "display options"
 *
 * The player keeps the window mode, size, display, vsync and frame cap the game chose in the user's folder, and the
 * command line changes them for one run. These pin, without a window:
 *   - window mode names both ways, and unknown names left alone;
 *   - resolutions as WIDTHxHEIGHT and WIDTHxHEIGHT@HZ, and malformed ones left alone;
 *   - options saved and loaded back; a file with only some fields keeps the rest; a missing or broken
 *     file loads nothing;
 *   - display modes listed once each, from the smallest; the closest mode by size, then by refresh rate;
 *   - a window's size: the default when none was chosen, at most the display's free area.
 * Everything lives in a temp folder.
 */

#include "../tests.h"

#include <engine/rendering/display_manager.h>
#include <filesystem/filesystem.h>

#include <cstdio>
#include <fstream>
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
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

auto get_temp_folder() -> fs::path
{
    return fs::temp_directory_path() / "unravel-tests-display-options";
}

auto make_mode(std::uint32_t width, std::uint32_t height, std::uint32_t refresh_rate) -> os::display::mode
{
    os::display::mode mode;
    mode.w = width;
    mode.h = height;
    mode.refresh_rate = refresh_rate;
    return mode;
}

auto is_mode(const os::display::mode& mode, std::uint32_t width, std::uint32_t height, std::uint32_t refresh_rate)
    -> bool
{
    return mode.w == width && mode.h == height && mode.refresh_rate == refresh_rate;
}

void test_window_mode_names()
{
    std::printf("test_window_mode_names\n");
    for(const auto mode : {window_mode::windowed, window_mode::borderless_fullscreen, window_mode::exclusive_fullscreen})
    {
        window_mode parsed = mode == window_mode::windowed ? window_mode::exclusive_fullscreen : window_mode::windowed;
        check(parse_window_mode(get_window_mode_name(mode), parsed) && parsed == mode,
              std::string("the name ") + get_window_mode_name(mode) + " reads back as its mode");
    }
    window_mode unchanged = window_mode::borderless_fullscreen;
    check(!parse_window_mode("fullscreen", unchanged) && unchanged == window_mode::borderless_fullscreen,
          "an unknown name is refused and leaves the mode");
    check(!parse_window_mode("Windowed", unchanged), "names are lower case");
}

void test_resolutions()
{
    std::printf("test_resolutions\n");
    usize32_t size{};
    std::uint32_t rate = 99;
    check(parse_resolution("1280x720", size, rate) && size == usize32_t{1280, 720} && rate == 0,
          "WIDTHxHEIGHT reads without a refresh rate");
    check(parse_resolution("1920X1080@144", size, rate) && size == usize32_t{1920, 1080} && rate == 144,
          "WIDTHxHEIGHT@HZ reads the refresh rate");
    for(const char* text : {"", "1280", "1280x", "x720", "0x720", "1280x0", "1280x720@", "1280x720junk", "-1x5",
                            "1280x720@60hz", "1280 x 720"})
    {
        usize32_t kept{800, 600};
        std::uint32_t kept_rate = 75;
        check(!parse_resolution(text, kept, kept_rate) && kept == usize32_t{800, 600} && kept_rate == 75,
              std::string("'") + text + "' is refused and leaves the values");
    }
}

void test_saved_options(const fs::path& root)
{
    std::printf("test_saved_options\n");
    display_options saved;
    saved.mode = window_mode::exclusive_fullscreen;
    saved.display = 1;
    saved.size = {2560, 1440};
    saved.refresh_rate = 165;
    saved.vsync = false;
    saved.max_fps = 144;
    const auto file = root / "settings" / "display.cfg";
    check(save_display_options(file, saved), "options are saved, with their folder");
    display_options loaded;
    check(load_display_options(file, loaded), "saved options load");
    check(loaded.mode == saved.mode && loaded.display == saved.display && loaded.size == saved.size &&
              loaded.refresh_rate == saved.refresh_rate && loaded.vsync == saved.vsync &&
              loaded.max_fps == saved.max_fps,
          "every field loads back");

    const auto partial = root / "partial.cfg";
    std::ofstream(partial) << R"({"display_options":{"mode":"windowed"}})";
    display_options defaults;
    defaults.size = {1024, 768};
    defaults.max_fps = 90;
    check(load_display_options(partial, defaults) && defaults.mode == window_mode::windowed &&
              defaults.size == usize32_t{1024, 768} && defaults.vsync && defaults.max_fps == 90,
          "a file with some fields keeps the others");

    const auto broken = root / "broken.cfg";
    std::ofstream(broken) << R"({"display_options":{"mode":"exclus)";
    display_options kept;
    check(!load_display_options(broken, kept) && kept.mode == window_mode::borderless_fullscreen,
          "a broken file loads nothing");
    check(!load_display_options(root / "missing.cfg", kept), "a missing file loads nothing");
}

void test_display_modes()
{
    std::printf("test_display_modes\n");
    const auto sorted = sort_display_modes({make_mode(1920, 1080, 60),
                                            make_mode(1280, 720, 60),
                                            make_mode(1920, 1080, 144),
                                            make_mode(1920, 1080, 60),
                                            make_mode(1280, 720, 60)});
    check(sorted.size() == 3, "each size and refresh rate is listed once");
    check(sorted.size() == 3 && is_mode(sorted[0], 1280, 720, 60) && is_mode(sorted[1], 1920, 1080, 60) &&
              is_mode(sorted[2], 1920, 1080, 144),
          "modes run from the smallest size and the lowest rate");

    check(is_mode(find_closest_display_mode(sorted, {1920, 1080}, 144, 60), 1920, 1080, 144), "an exact mode is found");
    check(is_mode(find_closest_display_mode(sorted, {1920, 1080}, 0, 60), 1920, 1080, 60),
          "no refresh rate picks the desktop's");
    check(is_mode(find_closest_display_mode(sorted, {1920, 1080}, 120, 60), 1920, 1080, 144),
          "the nearest refresh rate wins");
    check(is_mode(find_closest_display_mode(sorted, {1366, 768}, 0, 60), 1280, 720, 60),
          "a size the display lacks picks the nearest size");
    check(is_mode(find_closest_display_mode(sorted, {3840, 2160}, 0, 144), 1920, 1080, 144),
          "a size larger than any picks the largest");
    check(find_closest_display_mode({}, {1920, 1080}, 0, 60).w == 0, "no modes give an empty mode");
}

void test_windowed_sizes()
{
    std::printf("test_windowed_sizes\n");
    check(fit_windowed_size({}, {2560, 1400}) == usize32_t{DEFAULT_WINDOWED_WIDTH, DEFAULT_WINDOWED_HEIGHT},
          "no size picks the default");
    check(fit_windowed_size({800, 600}, {2560, 1400}) == usize32_t{800, 600}, "a size that fits is kept");
    check(fit_windowed_size({1920, 1080}, {1280, 760}) == usize32_t{1280, 760}, "each side is at most the free area's");
    check(fit_windowed_size({1920, 1080}, {}) == usize32_t{1920, 1080}, "an unknown free area keeps the size");
}

} // namespace

auto run_display_options_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    const auto temp = get_temp_folder();
    fs::error_code ec;
    fs::remove_all(temp, ec);
    fs::create_directories(temp, ec);
    test_window_mode_names();
    test_resolutions();
    test_saved_options(temp);
    test_display_modes();
    test_windowed_sizes();
    fs::remove_all(temp, ec);
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

REGISTER_TEST_SUITE("display options", run_display_options_suite)
