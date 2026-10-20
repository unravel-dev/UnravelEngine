#pragma once

#include <base/basetypes.hpp>
#include <engine/assets/asset_handle.h>

#include <filesystem/filesystem.h>
#include <graphics/texture.h>
#include <math/math.h>

#include <cstdint>
#include <string>
#include <vector>

namespace unravel
{

/// The platform a deploy builds the game for.
enum class deploy_platform : std::uint8_t
{
    /// This computer's platform: the player executable beside the data.
    desktop,
    /// An Android package (.apk) built from the editor's Android player.
    android,
};

/// How an Android game turns with the phone.
enum class android_orientation : std::uint8_t
{
    /// Both landscape directions.
    landscape,
    /// Both portrait directions.
    portrait,
    /// Every direction, following the phone.
    automatic,
};

/// The open project's folder, which a relative deploy location lies in.
constexpr const char* PROJECT_ROOT_KEY = "app:/";
/// The deploy location a project starts with: a folder of its own in the project.
constexpr const char* DEFAULT_DEPLOY_LOCATION = ".deploy";

/// How the project is deployed; saved with the project (app:/deploy/deploy.cfg).
struct deploy_settings
{
    deploy_platform deploy_target{deploy_platform::desktop};
    /// The folder builds go to, each platform into a folder of its own in it (get_output_location). A
    /// relative location lies in the project's folder.
    fs::path deploy_location{DEFAULT_DEPLOY_LOCATION};
    bool deploy_dependencies{true};
    bool deploy_and_run{};
    /// The command line options Deploy and Run starts the game with, separated by whitespace
    /// (split_run_arguments), such as --gfx-stats.
    std::string deploy_run_arguments;

    /**
     * @brief Resolve nested prefab instances into the deployed assets.
     *
     * A prefab that instances another stores a snapshot of it, taken when that prefab was last saved,
     * so it is stale as soon as the inner asset changes. The runtime normally hides that by refreshing
     * every nested instance on load - one extra asset load per instance, every time one is created.
     *
     * On: the deploy resolves them once, into cooked copies in a staging folder that replace the plain
     * compiled ones in the build, and marks them so the runtime skips the refresh. The marker is only
     * trustworthy because the deploy rewrites it on every run.
     *
     * Off: the compiled files ship unmarked and the runtime refreshes on load, as the editor does.
     * Slower to run, but it cannot serve stale content, and it is the safe choice if a bake ever looks
     * wrong.
     */
    bool deploy_cooked_assets{true};

    /// Ship the script libraries' debug companions (.pdb symbols, .xml docs). The game runs without
    /// them; with them, managed exceptions name the file and line they came from.
    bool deploy_debug_symbols{false};

    /// Write each protocol's compiled assets and asset index into one compressed pack
    /// (data/<protocol>/content.pak) instead of loose files. Off ships them loose, which is easier to
    /// inspect; the game reads either.
    bool deploy_packed_assets{true};

    /// The kept release to patch (a folder under the project's releases/). Set: the release's packs ship
    /// unchanged and content_p1.pak holds every change since it, so players who have the release
    /// download only the patch. Empty: full packs.
    std::string deploy_patch_release;

    /// Ship only the project assets the game uses: what the startup scene and deploy_scenes reference,
    /// directly or through other assets, with deploy_included added and deploy_excluded taken out.
    /// Off: every project asset ships but the excluded ones. Engine assets always ship whole, since
    /// engine code loads them by path.
    bool deploy_only_used_assets{false};
    /// Scenes the game loads besides the startup scene, by asset key ("app:/data/levels/one.spfb").
    std::vector<std::string> deploy_scenes;
    /// Folders and assets that ship whatever the scenes use, by key: what scripts load by a path built
    /// at run time. The most specific rule for a path wins over its parents' rules.
    std::vector<std::string> deploy_included;
    /// Folders and assets that never ship, by key, even when the scenes use them.
    std::vector<std::string> deploy_excluded;

    /// The game's id on Android devices and in stores. Empty: com.<company>.<product> from the project
    /// settings. Changing it later installs a separate app.
    std::string android_application_id;
    /// Android's version number of the game, which must grow with every release a store gets.
    std::uint32_t android_version_code{1};
    android_orientation android_screen_orientation{android_orientation::landscape};
    /// The image the game's launcher icon is made from, square at best. None: the engine's logo.
    asset_handle<gfx::texture> android_icon;
    /// The opaque color behind the icon's image, seen around it and through its transparent parts.
    math::color android_icon_background{math::color::white()};
    /// A release package signed with android_keystore; otherwise a debug package, signed with the
    /// Android SDK's debug keystore.
    bool android_release{};
    fs::path android_keystore;
    std::string android_key_alias;
    /// The device Deploy and Run installs on (an adb serial). Not saved: devices belong to a computer.
    std::string android_device;
    /// Uninstall the game from the device when its installed copy was signed with another key, which
    /// removes its saved data. Not saved: asked for each time.
    bool android_replace_installed{};
};

/// The folder of a platform's builds under the deploy location: "desktop" or "android".
inline auto get_platform_folder_name(deploy_platform platform) -> const char*
{
    return platform == deploy_platform::android ? "android" : "desktop";
}

/// The folder the deploy location names, a relative one in the open project's folder. Empty while no
/// deploy location is set.
inline auto get_deploy_folder(const deploy_settings& params) -> fs::path
{
    return fs::resolve_relative_to(params.deploy_location, fs::resolve_protocol(PROJECT_ROOT_KEY));
}

/// The folder `platform` builds into: <deploy folder>/<platform folder>. Empty while no deploy location is set.
inline auto get_platform_location(const deploy_settings& params, deploy_platform platform) -> fs::path
{
    const auto folder = get_deploy_folder(params);
    return folder.empty() ? fs::path{} : folder / get_platform_folder_name(platform);
}

/// The folder a deploy with these settings builds into: the chosen platform's under the deploy location.
inline auto get_output_location(const deploy_settings& params) -> fs::path
{
    return get_platform_location(params, params.deploy_target);
}

} // namespace unravel
