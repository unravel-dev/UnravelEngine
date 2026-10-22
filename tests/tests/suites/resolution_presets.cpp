/*
 * Validation suite for the resolution presets of the editor's views.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite resolution
 *
 * Presets have a device (desktop, phone, tablet) and the settings an orientation. A project saved
 * before that holds the old preset list verbatim, so the settings loader brings it up to date once:
 *   - the old default list becomes today's defaults, and the pick keeps its shape under its new name;
 *   - a list of the project's own keeps its presets and gains the default phones and tablets;
 *   - settings saved with an orientation load as they are, so a preset the user removed stays removed;
 *   - the device and the orientation survive the binary archive a deploy ships.
 */

#include "../tests.h"

#include <engine/meta/settings/settings.hpp>
#include <engine/settings/settings.h>

#include <filesystem/filesystem.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

using namespace unravel;

namespace
{
using resolution_settings = settings::resolution_settings;
using preset = resolution_settings::resolution;
using device_type = resolution_settings::device_type;
using screen_orientation = resolution_settings::screen_orientation;

/// Digits that write a float through a double and read it back unchanged.
constexpr int EXACT_DIGITS = 17;

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

/// The default presets of a project created before presets had a device.
auto make_presets_before_devices() -> std::vector<preset>
{
    return {{"Free Aspect", 0, 0, 0.0f},
            {"16:9 Aspect", 0, 0, 16.0f / 9.0f},
            {"16:10 Aspect", 0, 0, 16.0f / 10.0f},
            {"Full HD (1920x1080)", 1920, 1080, 16.0f / 9.0f},
            {"WXGA (1366x768)", 1366, 768, 16.0f / 9.0f},
            {"QHD (2560x1440)", 2560, 1440, 16.0f / 9.0f},
            {"4K UHD (3840x2160)", 3840, 2160, 16.0f / 9.0f}};
}

void write_preset(std::ostringstream& out, const preset& entry)
{
    out << R"({"name":")" << entry.name << R"(","width":)" << entry.width << R"(,"height":)" << entry.height
        << R"(,"aspect":)" << std::setprecision(EXACT_DIGITS) << static_cast<double>(entry.aspect) << "}";
}

/// A settings document as an editor before devices saved it: no device per preset, no orientation.
auto make_document_before_devices(const std::vector<preset>& presets, const preset& current) -> std::string
{
    std::ostringstream out;
    out << R"({"settings":{"resolution":{"resolutions":[)";
    for(std::size_t i = 0; i < presets.size(); ++i)
    {
        out << (i == 0 ? "" : ",");
        write_preset(out, presets[i]);
    }
    out << R"(],"current_resolution":)";
    write_preset(out, current);
    out << "}}}";
    return out.str();
}

auto load_document(const fs::path& path, const std::string& text, settings& loaded) -> bool
{
    {
        std::ofstream stream(path);
        stream << text;
    }
    return load_from_file(path.string(), loaded);
}

auto count_device_defaults() -> std::size_t
{
    const resolution_settings defaults{};
    return static_cast<std::size_t>(std::count_if(defaults.resolutions.begin(),
                                                  defaults.resolutions.end(),
                                                  [](const preset& entry)
                                                  {
                                                      return entry.device != device_type::desktop;
                                                  }));
}

void test_old_defaults_become_todays(const fs::path& root)
{
    const std::vector<preset> old_defaults = make_presets_before_devices();
    settings loaded{};
    check(load_document(root / "old_defaults.cfg", make_document_before_devices(old_defaults, old_defaults[3]), loaded),
          "a document from before devices loads");
    const resolution_settings defaults{};
    check(loaded.resolution.resolutions == defaults.resolutions, "the old default presets become today's defaults");
    check(loaded.resolution.current_resolution.name == "Full HD", "the pick keeps its shape under its new name");
    check(loaded.resolution.orientation == screen_orientation::portrait, "the orientation starts upright");
}

void test_own_presets_gain_devices(const fs::path& root)
{
    const std::vector<preset> own{{"Free Aspect", 0, 0, 0.0f}, {"Ultrawide", 3440, 1440, 3440.0f / 1440.0f}};
    settings loaded{};
    check(load_document(root / "own_presets.cfg", make_document_before_devices(own, own[1]), loaded),
          "a document with presets of the project's own loads");
    const auto& presets = loaded.resolution.resolutions;
    check(presets.size() == own.size() + count_device_defaults(), "the default phones and tablets are added");
    check(std::equal(own.begin(), own.end(), presets.begin()), "the project's own presets stay first and unchanged");
    check(std::all_of(presets.begin() + static_cast<std::ptrdiff_t>(own.size()),
                      presets.end(),
                      [](const preset& entry)
                      {
                          return entry.device != device_type::desktop;
                      }),
          "no desktop default is added to a list of the project's own");
    check(loaded.resolution.current_resolution == own[1], "the pick stays the project's own preset");
}

void test_saved_presets_load_as_they_are(const fs::path& root)
{
    settings authored{};
    auto& presets = authored.resolution.resolutions;
    presets.erase(std::remove_if(presets.begin(),
                                 presets.end(),
                                 [](const preset& entry)
                                 {
                                     return entry.device == device_type::tablet;
                                 }),
                  presets.end());
    const auto phone = std::find_if(presets.begin(),
                                    presets.end(),
                                    [](const preset& entry)
                                    {
                                        return entry.device == device_type::phone;
                                    });
    authored.resolution.set_current_resolution_index(static_cast<std::size_t>(std::distance(presets.begin(), phone)));
    authored.resolution.orientation = screen_orientation::landscape;
    const fs::path path = root / "saved.cfg";
    save_to_file(path.string(), authored);
    settings loaded{};
    check(load_from_file(path.string(), loaded), "settings saved with an orientation load");
    check(loaded.resolution == authored.resolution, "they load as saved: the removed tablets stay removed");
}

void test_binary_round_trip(const fs::path& root)
{
    settings authored{};
    const auto& presets = authored.resolution.resolutions;
    const auto tablet = std::find_if(presets.begin(),
                                     presets.end(),
                                     [](const preset& entry)
                                     {
                                         return entry.device == device_type::tablet;
                                     });
    authored.resolution.set_current_resolution_index(static_cast<std::size_t>(std::distance(presets.begin(), tablet)));
    authored.resolution.orientation = screen_orientation::landscape;
    const fs::path path = root / "deployed.bin";
    save_to_file_bin(path.string(), authored);
    settings loaded{};
    check(load_from_file_bin(path.string(), loaded), "binary settings load");
    check(loaded.resolution == authored.resolution, "the binary archive keeps every device and the orientation");
}

} // namespace

auto run_resolution_presets_suite(rtti::context& ctx) -> int
{
    (void)ctx;
    g_checks = 0;
    g_failures = 0;
    const fs::path root = fs::temp_directory_path() / "unravel_resolution_presets";
    fs::error_code err;
    fs::remove_all(root, err);
    fs::create_directories(root, err);
    test_old_defaults_become_todays(root);
    test_own_presets_gain_devices(root);
    test_saved_presets_load_as_they_are(root);
    test_binary_round_trip(root);
    fs::remove_all(root, err);
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

REGISTER_TEST_SUITE("resolution presets", run_resolution_presets_suite)
