#pragma once

#include <base/basetypes.hpp>
#include <engine/assets/asset_handle.h>
#include <engine/settings/target_platform.h>

#include <filesystem/filesystem.h>
#include <graphics/texture.h>
#include <math/math.h>

#include <cstdint>
#include <string>
#include <vector>

namespace unravel
{

/// The platform a deploy builds the game for: the platform of the profile it deploys.
using deploy_platform = target_platform;

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

/// The block-compressed formats an Android build's textures are cooked to.
enum class android_texture_compression : std::uint8_t
{
    /// ASTC, the better image per byte. A device without it decodes the textures at load, into four
    /// times the memory.
    astc,
    /// ETC2 and EAC, which every OpenGL ES 3 and Vulkan device reads.
    etc2,
    /// BC, the desktop formats: the editor's own files ship. Some Android GPUs read them; on the others the
    /// textures are decoded at load, into four to eight times the memory.
    bc,
};

/// The permissions an Android build asks for in its manifest. The named ones are what a game commonly
/// uses (get_android_permission_entries maps them to manifest names); `other` holds any further one.
struct android_permissions
{
    /// INTERNET: networking, such as HttpClient and sockets. Granted at install, without a prompt.
    bool internet{true};
    /// ACCESS_NETWORK_STATE: whether and how the phone is online.
    bool network_state{};
    /// VIBRATE: the phone's vibration.
    bool vibrate{true};
    /// RECORD_AUDIO: the microphone. The player grants it at run time.
    bool microphone{};
    /// CAMERA: the cameras. The player grants it at run time.
    bool camera{};
    /// ACCESS_COARSE_LOCATION and ACCESS_FINE_LOCATION. The player grants them at run time.
    bool location{};
    /// POST_NOTIFICATIONS: notifications, which the player grants at run time from Android 13.
    bool notifications{};
    /// Further permissions by name: "android.permission.WAKE_LOCK", a vendor's, or a short name
    /// ("WAKE_LOCK") that gets "android.permission." in front.
    std::vector<std::string> other;
};

/// What an Android build is on the device: its identity, its icon, its signing and its texture
/// formats. An Android profile holds one (profiles/<name>/profile.cfg).
struct android_deploy_settings
{
    /// The game's id on Android devices and in stores. Empty: com.<company>.<product> from the
    /// project settings, which follows them. Changing it later installs a separate app.
    std::string application_id;
    android_permissions permissions;
    /// Android's version number of the game, which must grow with every release a store gets.
    std::uint32_t version_code{1};
    android_orientation screen_orientation{android_orientation::landscape};
    /// The image the game's launcher icon is made from, square at best. None: the engine's logo.
    asset_handle<gfx::texture> icon;
    /// The opaque color behind the icon's image, seen around it and through its transparent parts.
    math::color icon_background{math::color::white()};
    /// A release package signed with keystore; otherwise a debug package, signed with the Android
    /// SDK's debug keystore.
    bool release{};
    fs::path keystore;
    std::string key_alias;
    android_texture_compression texture_compression{android_texture_compression::astc};
};

/// Where a desktop build goes on Steam. The upload logs in with the login steamcmd keeps for `account`, so
/// no password is saved, and it never sets a build live on the default branch: Steam allows that only on
/// the Steamworks website.
struct steam_deploy_settings
{
    /// The game's app id in Steamworks; 0: the profile is not uploaded.
    std::uint32_t app_id{};
    /// The depot the build goes to: the app's depot of the operating system the profile builds for.
    std::uint32_t depot_id{};
    /// The Steam account that uploads, best one with build rights only.
    std::string account;
    /// The branch the build is set live on once uploaded; empty: none, it is set live on the website.
    std::string branch;
};

/// What a desktop build shows of itself besides the project's product, company and version, and where it
/// goes on Steam. A desktop profile holds one (profiles/<name>/profile.cfg).
struct desktop_deploy_settings
{
    /// The image the executable's icon is made from, square at best. None: the engine's logo.
    asset_handle<gfx::texture> icon;
    steam_deploy_settings steam;
};

/// The open project's folder, which a relative deploy location lies in.
constexpr const char* PROJECT_ROOT_KEY = "app:/";
/// The deploy location a project starts with: a folder of its own in the project.
constexpr const char* DEFAULT_DEPLOY_LOCATION = ".deploy";

/**
 * @brief How the project deploys one profile.
 *
 * deploy/deploy.cfg holds the settings every profile shares, and a profile's deploy.cfg overrides any of
 * them but the location. The profile's own fields come from the profile (resolve_deploy_settings) and are
 * saved with it; the device fields are not saved at all.
 */
struct deploy_settings
{
    /// The profile the deploy builds. From the profile.
    std::string profile;
    /// The profile's platform. From the profile.
    deploy_platform deploy_target{deploy_platform::desktop};
    /// The Android profile's identity, signing and texture formats. From the profile.
    android_deploy_settings android;
    /// The desktop profile's icon. From the profile.
    desktop_deploy_settings desktop;

    /// The folder builds go to, each profile into a folder of its own in it (get_output_location). A
    /// relative location lies in the project's folder. Shared: no profile overrides it.
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

    /// The kept release of the profile to patch (releases/<profile>/<release>). Set: the release's packs
    /// ship unchanged and content_p1.pak holds every change since it, so players who have the release
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

    /// The device Deploy and Run installs on (an adb serial). Not saved: devices belong to a computer.
    std::string android_device;
    /// Uninstall the game from the device when its installed copy was signed with another key, which
    /// removes its saved data. Not saved: asked for each time.
    bool android_replace_installed{};
};

/// The folder the deploy location names, a relative one in the open project's folder. Empty while no
/// deploy location is set.
inline auto get_deploy_folder(const deploy_settings& params) -> fs::path
{
    return fs::resolve_relative_to(params.deploy_location, fs::resolve_protocol(PROJECT_ROOT_KEY));
}

/// The folder a profile builds into under the deploy location: <deploy folder>/<profile>. Empty while no
/// deploy location or no profile is set, so a deploy never writes into the deploy folder itself.
inline auto get_profile_location(const deploy_settings& params, const std::string& profile) -> fs::path
{
    const auto folder = get_deploy_folder(params);
    return folder.empty() || profile.empty() ? fs::path{} : folder / fs::from_utf8_string(profile);
}

/// The folder a deploy with these settings builds into: its profile's under the deploy location.
inline auto get_output_location(const deploy_settings& params) -> fs::path
{
    return get_profile_location(params, params.profile);
}

} // namespace unravel
