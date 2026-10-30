/*
 * Validation suite for settings profiles: a named target (a platform, what it overrides of the project
 * settings and of the shared deploy settings) and the texture variants its deploys ship.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "settings profiles"
 *
 * This pins:
 *   - document paths: within / related, adding and removing paths, leaf paths, changed paths;
 *   - sparse overrides: only the overridden properties are stated, they apply over any base and the
 *     rest follows the base, reverting removes them, a document's unknown paths and the settings every
 *     profile shares (layers, input, the physics backend, the deploy location) are left out;
 *   - profile files: a new Android profile's starting values (feature limits included), save / load round
 *     trip, rename, delete;
 *   - a project from before profiles: Desktop and Android made from its deploy.cfg, its releases and its
 *     patch release moved to Desktop, its deploy.cfg kept; the next load reads the profiles; an old
 *     deploy.cfg arriving beside profiles is reported and backed up, never overwriting an earlier backup;
 *   - a profile file that does not read: reported, overriding nothing, never written back, its overrides
 *     locked until it is mended; a file not edited stored again byte for byte, unknown entries included;
 *   - releases from before profiles adopted by Desktop whenever they turn up, with the release patched;
 *   - project profiles: create (copies too), names refused in any letter case, rename moving releases and
 *     the texture cache, the last profile kept, overrides of shared settings (the Game view resolutions
 *     included) refused with their dotted paths, an edit merged over overrides changed meanwhile, a revert
 *     of nothing writing nothing, caches of profiles that no longer exist deleted;
 *   - ASTC blocks: the block each quality takes keeps a texture's size, capped and non-square sizes included;
 *   - texture parameters: a platform's own quality, defaults, fallbacks, one family for uncompressed textures;
 *   - sRGB textures: a texture tagged sRGB never compiles to a format without an sRGB view (one and two
 *     channel formats), for any format family and uncompressed;
 *   - texture variants, compiled with texturec: ASTC, ETC2 and BC formats (blocks that keep a texture's size),
 *     a variant reused while current, compiled again in place when a setting changes (never a second
 *     file), stray and unneeded files swept, a texture the profile compiles as the editor does shipping the
 *     editor's file once it is current and a variant until then, left-out textures kept.
 * Everything lives in a temp folder and the tests' app protocol.
 */

#include "../tests.h"

#include <asset_compiler/texture_target.h>
#include <deploy/profile.h>
#include <deploy/profile_cache.h>
#include <deploy/project_profiles.h>
#include <deploy/releases.h>
#include <deploy/texture_variants.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/meta/assets/asset_database.hpp>
#include <engine/meta/assets/asset_importer_meta.hpp>
#include <filesystem/filesystem.h>
#include <graphics/utils/bgfx_utils.h>
#include <serialization/document_paths.h>
#include <serialization/sparse_document.h>

#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/file.h>
#include <bx/hash.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
namespace deployment = unravel::deployment;
using unravel::deploy_settings;
using unravel::settings;
using unravel::settings_profile;
using unravel::target_platform;
using unravel::texture_importer_meta;
namespace ac = unravel::asset_compiler;

/// The folder under the tests' app protocol the texture sources are written to.
constexpr const char* TEXTURE_FOLDER = "data/profile_tests";
/// The low quality max size the wall texture is capped to in the variants test.
constexpr std::uint32_t LOW_QUALITY_SIDE = 64;
/// A side ASTC 6x6 blocks divide.
constexpr std::uint32_t WALL_SIDE = 96;
/// The grayscale texture the sRGB tests compile.
constexpr std::uint32_t SRGB_TEXTURE_SIDE = 64;
/// A power of two, which ASTC 6x6 blocks do not divide.
constexpr std::uint32_t FLOOR_SIDE = 64;

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
    return fs::temp_directory_path() / "unravel-tests-settings-profiles";
}

void write_text(const fs::path& file, const std::string& text)
{
    fs::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

auto read_text(const fs::path& file) -> std::string
{
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

auto is_present(const fs::path& path) -> bool
{
    fs::error_code ec;
    return fs::exists(path, ec);
}

auto count_files(const fs::path& folder) -> std::size_t
{
    std::size_t count = 0;
    fs::error_code ec;
    for(fs::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
    {
        count += it->is_regular_file(ec) ? 1 : 0;
    }
    return count;
}

auto leaf_paths(const std::string& document) -> serialization::path_set
{
    serialization::path_set paths;
    serialization::get_leaf_paths(document, unravel::SETTINGS_DOCUMENT_ROOT, paths);
    return paths;
}

/// What a profile's settings override after `edit` changed the settings it resolves to.
template<typename Edit>
void edit_profile_settings(settings_profile& profile, const settings& base, Edit edit)
{
    auto resolved = deployment::resolve_settings(base, profile);
    const auto before = serialization::save_to_document(resolved, unravel::SETTINGS_DOCUMENT_ROOT);
    edit(resolved);
    serialization::path_set changed;
    serialization::get_changed_paths(before, serialization::save_to_document(resolved, unravel::SETTINGS_DOCUMENT_ROOT), unravel::SETTINGS_DOCUMENT_ROOT, changed);
    profile.settings.set_values(resolved, changed);
}

//-----------------------------------------------------------------------------
// document paths
//-----------------------------------------------------------------------------

void test_document_paths()
{
    std::printf("test_document_paths\n");
    check(serialization::is_path_within("a/b", "a") && serialization::is_path_within("a[0]", "a") &&
              serialization::is_path_within("a", "a") && !serialization::is_path_within("ab", "a") &&
              !serialization::is_path_within("a", "a/b"),
          "a path lies in its ancestors only, by whole names");
    serialization::path_set paths{"a/b", "c"};
    check(serialization::is_path_related("a", paths) && serialization::is_path_related("a/b/c", paths) &&
              !serialization::is_path_related("a/x", paths),
          "a path relates to the paths it holds or lies in");
    serialization::add_path(paths, "a");
    check(paths == serialization::path_set{"a", "c"}, "adding a path drops the ones it holds");
    serialization::add_path(paths, "a/z");
    check(paths == serialization::path_set{"a", "c"}, "a path inside a listed one adds nothing");
    serialization::remove_path(paths, "a/z");
    check(paths == serialization::path_set{"c"}, "removing a path inside one removes the one holding it");

    const std::string document = R"({"settings":{"a":{"b":1,"c":{"d":"x"}},"e":[1,2],"f":{}}})";
    check(leaf_paths(document) == serialization::path_set{"a/b", "a/c/d", "e", "f"},
          "leaf paths walk objects and stop at arrays, values and empty objects");
    serialization::path_set invalid;
    check(!serialization::get_leaf_paths("not json", "settings", invalid) && invalid.empty(), "a broken document has no paths");
    check(!serialization::get_leaf_paths(document, "other", invalid), "nor a document without the root");

    serialization::path_set changed;
    check(serialization::get_changed_paths(document, R"({"settings":{"a":{"b":2,"c":{"d":"x"}},"e":[1,3],"g":true}})", "settings", changed),
          "two documents compare");
    check(changed == serialization::path_set{"a/b", "e", "f", "g"},
          "changed values, arrays as a whole, and paths only one document has");
}

//-----------------------------------------------------------------------------
// sparse overrides
//-----------------------------------------------------------------------------

void test_sparse_overrides()
{
    std::printf("test_sparse_overrides\n");
    settings base;
    settings_profile profile;
    profile.name = "Phone";
    edit_profile_settings(profile,
                          base,
                          [](settings& s)
                          {
                              s.graphics.particles.spawn_scale = 0.5f;
                          });
    check(profile.settings.get_paths() == serialization::path_set{"graphics/particles/spawn_scale"},
          "only the edited property is overridden");
    check(leaf_paths(profile.settings.get_document()) == profile.settings.get_paths(), "and only it is stated");

    settings other_base;
    other_base.graphics.particles.spawn_scale = 0.9f;
    other_base.graphics.particles.sorting = false;
    other_base.physics.solver_iterations = 3;
    const auto resolved = deployment::resolve_settings(other_base, profile);
    check(resolved.graphics.particles.spawn_scale == 0.5f, "the override applies over any base");
    check(!resolved.graphics.particles.sorting && resolved.physics.solver_iterations == 3, "and the rest follows the base");
    check(resolved.build.profile == "Phone" && resolved.build.platform == target_platform::desktop, "the build names the profile");

    {
        settings_profile store;
        store.name = "Store";
        edit_profile_settings(store,
                              base,
                              [](settings& s)
                              {
                                  s.standalone.run_in_background = true;
                              });
        check(store.settings.get_paths() == serialization::path_set{"standalone/run_in_background"},
              "running in the background is a per-profile setting");
        check(!base.standalone.run_in_background && deployment::resolve_settings(base, store).standalone.run_in_background,
              "a minimized game is capped unless its profile runs it in the background");
    }

    edit_profile_settings(profile,
                          base,
                          [](settings& s)
                          {
                              s.standalone.startup_scene = {};
                              s.splash.logos.push_back({});
                          });
    check(profile.settings.is_overridden("graphics/particles/spawn_scale") && profile.settings.is_related("splash") &&
              profile.settings.is_overridden("splash/logos[0]"),
          "a list is overridden whole, with what was overridden before");
    check(deployment::resolve_settings(base, profile).splash.logos.size() == 1, "an overridden list replaces the base's");
    profile.settings.revert(deployment::resolve_settings(base, profile), "splash/logos[0]/duration_sec");
    check(!profile.settings.is_related("splash") && profile.settings.is_overridden("graphics/particles/spawn_scale"),
          "reverting inside a list reverts the list and nothing else");
    profile.settings.revert(deployment::resolve_settings(base, profile), "graphics");
    check(!profile.settings.is_related("graphics"), "reverting a group reverts what lies in it");

    {
        unravel::profile_overrides read;
        const std::string stated =
            R"({"settings":{"graphics":{"particles":{"sorting":false},"no_such":1},"layer":{"layers":{"value3":"X"}},"physics":{"backend":1,"solver_iterations":4}}})";
        const auto dropped = read.read(stated, base, &deployment::is_overridable_settings_path);
        check(read.get_paths() == serialization::path_set{"graphics/particles/sorting", "physics/solver_iterations"},
              "a document's known, per-profile paths are kept");
        check(dropped.size() == 3, "unknown paths and shared settings are left out and reported");
        settings applied;
        read.apply(applied);
        check(!applied.graphics.particles.sorting && applied.physics.solver_iterations == 4 &&
                  applied.layer.layers[3] == base.layer.layers[3] && applied.physics.backend == base.physics.backend,
              "only the kept overrides reach the settings");
        check(leaf_paths(read.get_document()) == read.get_paths(), "and the document is written again without the rest");
        check(!read.is_unreadable(), "a document that parses is readable");
        check(read.get_document_to_store() == stated, "an overrides document not edited is stored as it was read");
        check(read.read("{ broken", base, &deployment::is_overridable_settings_path).empty() && read.is_empty() &&
                  read.is_unreadable(),
              "a broken document overrides nothing and is marked unreadable");
    }

    {
        settings before;
        settings current = before;
        current.physics.solver_iterations = 9;
        settings edited = before;
        edited.graphics.particles.spawn_scale = 0.25f;
        const auto changed = serialization::apply_edit(before, edited, unravel::SETTINGS_DOCUMENT_ROOT, current);
        check(changed == serialization::path_set{"graphics/particles/spawn_scale"} && current.graphics.particles.spawn_scale == 0.25f &&
                  current.physics.solver_iterations == 9,
              "an edit carried over keeps what changed meanwhile");
    }

    deploy_settings deploy_base;
    unravel::profile_overrides deploy_overrides;
    const auto deploy_dropped = deploy_overrides.read(R"({"settings":{"deploy_location":"x","deploy_run_arguments":"--novsync"}})",
                                                      deploy_base,
                                                      &deployment::is_overridable_deploy_path);
    check(deploy_dropped == std::vector<std::string>{"deploy_location"} &&
              deploy_overrides.get_paths() == serialization::path_set{"deploy_run_arguments"},
          "a profile cannot move the deploy location");
}

//-----------------------------------------------------------------------------
// profile files
//-----------------------------------------------------------------------------

void test_profile_files(const fs::path& root)
{
    std::printf("test_profile_files\n");
    settings base;
    deploy_settings deploy_base;
    auto android = deployment::make_profile("Android", target_platform::android, base);
    check(android.settings.is_overridden("assets/texture/default_max_size") &&
              android.settings.is_overridden("global_illumination/surface_cache_atlas_size"),
          "a new Android profile starts with smaller textures and a smaller surface cache");
    check(android.settings.is_overridden("graphics/limits/global_illumination") &&
              android.settings.is_overridden("graphics/limits/max_anti_aliasing") &&
              android.settings.is_overridden("graphics/limits/volumetric_fog") &&
              android.settings.is_overridden("graphics/limits/subsurface") &&
              android.settings.is_overridden("graphics/limits/volumetric_clouds") &&
              !android.settings.is_overridden("graphics/render_scale"),
          "and with the effects a phone cannot afford limited, at the project's render scale");
    check(deployment::make_profile("Desktop", target_platform::desktop, base).settings.is_empty(), "a new Desktop profile overrides nothing");
    android.android.application_id = "com.test.game";
    android.android.texture_compression = unravel::android_texture_compression::etc2;
    edit_profile_settings(android,
                          base,
                          [](settings& s)
                          {
                              s.physics.fixed_timestep = 0.04f;
                          });
    auto desktop = deployment::make_profile("Desktop", target_platform::desktop, base);
    desktop.desktop.steam.app_id = 480;
    desktop.desktop.steam.depot_id = 481;
    desktop.desktop.steam.account = "builder";
    desktop.desktop.steam.branch = "beta";
    check(deployment::save_profile(root, android).is_ok() && deployment::save_profile(root, desktop).is_ok(), "profiles save");
    check(!is_present(deployment::get_profile_folder(root, "Desktop") / "settings.cfg"),
          "a profile that overrides nothing has no settings file");
    auto list = deployment::load_profiles(root, base, deploy_base);
    check(list.warnings.empty() && list.profiles.size() == 2, "and load: " + std::to_string(list.warnings.size()) + " warning(s)");
    const auto& loaded = list.profiles.front();
    check(loaded.name == "Android" && loaded.platform == target_platform::android && loaded.android.application_id == "com.test.game" &&
              loaded.android.texture_compression == unravel::android_texture_compression::etc2,
          "a profile's platform and Android settings round trip");
    const auto& loaded_steam = list.profiles.back().desktop.steam;
    check(list.profiles.back().name == "Desktop" && loaded_steam.app_id == 480 && loaded_steam.depot_id == 481 &&
              loaded_steam.account == "builder" && loaded_steam.branch == "beta",
          "a desktop profile's Steam settings round trip");
    check(loaded.settings.get_paths() == android.settings.get_paths() &&
              deployment::resolve_settings(base, loaded).physics.fixed_timestep == 0.04f,
          "and its overrides");
    write_text(deployment::get_profiles_folder(root) / "Broken" / "settings.cfg", "{}");
    list = deployment::load_profiles(root, base, deploy_base);
    check(list.profiles.size() == 2 && list.warnings.size() == 1, "a folder without profile.cfg is left out with a warning");
    check(deployment::rename_profile_files(root, "Android", "Phone").is_ok() && is_present(deployment::get_profile_folder(root, "Phone")) &&
              !is_present(deployment::get_profile_folder(root, "Android")),
          "a profile folder renames");
    check(!deployment::rename_profile_files(root, "Phone", "Desktop").is_ok(), "onto a name in use it does not");
    check(!deployment::rename_profile_files(root, "Phone", "a/b").is_ok(), "nor to a name that is no folder name");
    check(deployment::remove_profile_files(root, "Phone").is_ok() && !is_present(deployment::get_profile_folder(root, "Phone")),
          "a profile folder deletes");
}

//-----------------------------------------------------------------------------
// project profiles
//-----------------------------------------------------------------------------

void test_legacy_project(const fs::path& root)
{
    std::printf("test_legacy_project\n");
    write_text(root / "deploy" / "deploy.cfg",
               R"({"settings":{"deploy_target":1,"deploy_location":".deploy","deploy_patch_release":"1.0",)"
               R"("android_application_id":"com.old.game","android_version_code":7}})");
    for(const auto* protocol : deployment::PACKED_PROTOCOLS)
    {
        write_text(root / "releases" / "1.0" / protocol / ex::get_pack_file_name(), "pack");
    }
    settings base;
    deploy_settings deploy_base;
    unravel::load_from_file((root / "deploy" / "deploy.cfg").string(), deploy_base);
    check(deploy_base.deploy_patch_release == "1.0", "the shared settings read what is still theirs");
    deployment::project_profiles profiles;
    std::string deploy_profile;
    const auto warnings = profiles.load(root, base, deploy_base, deploy_profile);
    check(warnings.empty(), "a project from before profiles converts");
    check(profiles.get_names() == std::vector<std::string>{"Android", "Desktop"}, "into Desktop and Android");
    check(deploy_profile == "Android", "and deploys the profile of its old target");
    const auto* android = profiles.find("Android");
    check(android != nullptr && android->android.application_id == "com.old.game" && android->android.version_code == 7,
          "the Android fields move to the Android profile");
    check(deploy_base.deploy_patch_release.empty() &&
              profiles.resolve_deploy("Desktop", deploy_base)->deploy_patch_release == "1.0",
          "the patch release moves to Desktop, whose releases they were");
    check(deployment::list_releases(deployment::get_profile_releases_folder(root, "Desktop")) == std::vector<std::string>{"1.0"},
          "and so do the kept releases");
    check(is_present(root / "deploy" / "deploy.cfg.before-profiles"), "the old deploy settings are kept");
    deployment::project_profiles again;
    std::string again_profile = "Desktop";
    again.load(root, base, deploy_base, again_profile);
    check(again.get_names() == profiles.get_names() && again_profile == "Desktop", "the next load reads the profiles, no conversion");

    // A deploy.cfg from before profiles that arrives once the project has profiles (merged from elsewhere).
    write_text(root / "deploy" / "deploy.cfg", R"({"settings":{"deploy_target":1,"android_application_id":"com.other.game"}})");
    deployment::project_profiles merged;
    std::string merged_profile = "Desktop";
    const auto merged_warnings = merged.load(root, base, deploy_base, merged_profile);
    check(merged_warnings.size() == 1 && merged.find("Android")->android.application_id == "com.old.game",
          "settings from before profiles beside profiles are reported and not used");
    check(read_text(root / "deploy" / "deploy.cfg.before-profiles-2").find("com.other.game") != std::string::npos &&
              read_text(root / "deploy" / "deploy.cfg.before-profiles").find("com.old.game") != std::string::npos,
          "and kept in a backup of their own, the earlier backup untouched");

    // Releases kept before profiles that arrive once the project has profiles, with the release patched.
    for(const auto* protocol : deployment::PACKED_PROTOCOLS)
    {
        write_text(root / "releases" / "0.9" / protocol / ex::get_pack_file_name(), "old pack");
    }
    deploy_settings merged_deploy = deploy_base;
    merged_deploy.deploy_patch_release = "0.9";
    deployment::project_profiles adopted;
    std::string adopted_profile = "Desktop";
    adopted.load(root, base, merged_deploy, adopted_profile);
    check(deployment::list_releases(deployment::get_profile_releases_folder(root, "Desktop")) ==
              std::vector<std::string>{"0.9", "1.0"},
          "releases from before profiles become Desktop's whenever they turn up");
    check(merged_deploy.deploy_patch_release.empty() && adopted.resolve_deploy("Desktop", merged_deploy)->deploy_patch_release == "1.0",
          "and Desktop keeps the release it patches over the shared one from before profiles");
    check(adopted.revert_deploy("Desktop", merged_deploy, "deploy_patch_release").is_ok(), "Desktop stops patching");
    merged_deploy.deploy_patch_release = "0.9";
    adopted.load(root, base, merged_deploy, adopted_profile);
    check(merged_deploy.deploy_patch_release.empty() && adopted.resolve_deploy("Desktop", merged_deploy)->deploy_patch_release == "0.9",
          "a profile without a patch release of its own takes the shared one from before profiles");
}

void test_unreadable_profile(const fs::path& root)
{
    std::printf("test_unreadable_profile\n");
    settings base;
    deploy_settings deploy_base;
    {
        deployment::project_profiles profiles;
        std::string deploy_profile;
        profiles.load(root, base, deploy_base, deploy_profile);
        profiles.create("Android", target_platform::android, base);
    }
    const auto settings_file = deployment::get_profile_folder(root, "Android") / "settings.cfg";
    const std::string conflicted = "<<<<<<< ours\n{\"settings\":{}}\n>>>>>>> theirs\n";
    write_text(settings_file, conflicted);
    deployment::project_profiles profiles;
    std::string deploy_profile;
    const auto warnings = profiles.load(root, base, deploy_base, deploy_profile);
    const auto* android = profiles.find("Android");
    check(warnings.size() == 1 && android != nullptr && android->settings.is_unreadable() && android->settings.is_empty(),
          "a settings file that does not read is reported, and the profile overrides nothing");
    auto identity = android->android;
    identity.version_code = 5;
    check(profiles.set_android("Android", identity).is_ok() && read_text(settings_file) == conflicted,
          "saving the profile leaves that file as it is");

    // A file with entries this version does not know, from a newer editor, stays whole while it is not edited.
    const auto deploy_file = deployment::get_profile_folder(root, "Android") / "deploy.cfg";
    const std::string from_newer =
        R"({"settings":{"deploy_run_arguments":"--novsync","deploy_from_a_newer_editor":true}})";
    write_text(deploy_file, from_newer);
    profiles.load(root, base, deploy_base, deploy_profile);
    identity.version_code = 6;
    check(profiles.set_android("Android", identity).is_ok() && read_text(deploy_file) == from_newer,
          "saving a profile for another change keeps a file it did not edit byte for byte");
    auto edited = *profiles.resolve_settings("Android", base);
    edited.physics.solver_iterations = 2;
    check(!profiles.override_settings("Android", base, edited).is_ok() && read_text(settings_file) == conflicted,
          "and its overrides cannot change until it is mended");
}

void test_project_profiles(const fs::path& root)
{
    std::printf("test_project_profiles\n");
    settings base;
    deploy_settings deploy_base;
    deployment::project_profiles profiles;
    std::string deploy_profile;
    write_text(deployment::get_profile_cache_folder(root, "Gone") / "app" / "a.png.asset", "stale");
    profiles.load(root, base, deploy_base, deploy_profile);
    check(profiles.get_names() == std::vector<std::string>{"Desktop"}, "a new project gets Desktop");
    check(!is_present(deployment::get_profile_cache_folder(root, "Gone")), "the cache of a profile that does not exist is deleted");
    check(profiles.create("Android", target_platform::android, base).is_ok(), "a profile is created");
    check(!profiles.create("android", target_platform::android, base).is_ok(), "a name in use is refused in any letter case");
    check(profiles.create("Android Low", target_platform::android, base, "Android").is_ok() &&
              profiles.find("Android Low")->settings.get_paths() == profiles.find("Android")->settings.get_paths(),
          "a copy starts with the profile's overrides");
    auto edited = *profiles.resolve_settings("Android", base);
    edited.physics.solver_iterations = 2;
    check(profiles.override_settings("Android", base, edited).is_ok() &&
              profiles.resolve_settings("Android", base)->physics.solver_iterations == 2,
          "an edit of a profile's settings becomes its override");
    {
        // An edit started before a tool changed another override keeps the tool's change.
        const auto before = *profiles.resolve_settings("Android", base);
        auto by_tool = before;
        by_tool.physics.solver_iterations = 6;
        profiles.override_settings("Android", base, by_tool);
        auto by_editor = before;
        by_editor.graphics.particles.spawn_scale = 0.3f;
        check(profiles.override_settings("Android", base, before, by_editor).is_ok() &&
                  profiles.resolve_settings("Android", base)->physics.solver_iterations == 6 &&
                  profiles.resolve_settings("Android", base)->graphics.particles.spawn_scale == 0.3f,
              "an edit stores what it changed over the profile's current overrides");
    }
    auto resolution_edit = *profiles.resolve_settings("Android", base);
    resolution_edit.resolution.set_current_resolution_index(3);
    check(!profiles.override_settings("Android", base, resolution_edit).is_ok(), "the Game view's resolutions are shared");
    const auto before_revert = profiles.get_revision();
    check(profiles.revert_settings("Android", base, "app/company").is_ok() && profiles.get_revision() == before_revert,
          "reverting what a profile does not override writes nothing");
    edited.layer.layers[5] = "Mine";
    const auto refused = profiles.override_settings("Android", base, edited);
    check(!refused.is_ok() && profiles.resolve_settings("Android", base)->layer.layers[5] != "Mine",
          "an edit of a setting every profile shares is refused, and nothing changes");
    check(refused.error.find("layer.layers") != std::string::npos, "the refusal names the shared setting as the tools do");
    auto deploy_edit = *profiles.resolve_deploy("Android", deploy_base);
    deploy_edit.deploy_location = "elsewhere";
    check(!profiles.override_deploy("Android", deploy_base, deploy_edit).is_ok(), "so is moving a profile's deploy location");
    deploy_edit.deploy_location = deploy_base.deploy_location;
    deploy_edit.deploy_run_arguments = "--gfx-stats";
    check(profiles.override_deploy("Android", deploy_base, deploy_edit).is_ok() &&
              profiles.resolve_deploy("Android", deploy_base)->deploy_run_arguments == "--gfx-stats" &&
              profiles.resolve_deploy("Desktop", deploy_base)->deploy_run_arguments.empty(),
          "a deploy setting can differ per profile");
    check(profiles.revert_deploy("Android", deploy_base, "deploy_run_arguments").is_ok() &&
              profiles.resolve_deploy("Android", deploy_base)->deploy_run_arguments.empty(),
          "and goes back to the shared one");
    const auto resolved = *profiles.resolve_deploy("Android", deploy_base);
    check(resolved.profile == "Android" && resolved.deploy_target == unravel::deploy_platform::android, "a profile deploys for its platform");

    write_text(deployment::get_profile_cache_folder(root, "Android") / "app" / "a.png.asset", "variant");
    write_text(deployment::get_profile_releases_folder(root, "Android") / "1.0" / "app" / "content.pak", "pack");
    const auto revision = profiles.get_revision();
    check(profiles.rename("Android", "Phone").is_ok() && profiles.find("Phone") != nullptr && profiles.find("Android") == nullptr,
          "a profile is renamed");
    check(profiles.get_revision() != revision, "and the change is counted");
    check(is_present(deployment::get_profile_cache_folder(root, "Phone") / "app" / "a.png.asset") &&
              is_present(deployment::get_profile_releases_folder(root, "Phone") / "1.0") &&
              !is_present(deployment::get_profile_cache_folder(root, "Android")),
          "its texture cache and releases move with it");
    check(profiles.rename("Phone", "phone").is_ok() && profiles.find("phone") != nullptr, "a change of letter case renames too");
    check(profiles.remove("phone").is_ok() && !is_present(deployment::get_profile_cache_folder(root, "phone")) &&
              !is_present(deployment::get_profile_releases_folder(root, "phone")),
          "a deleted profile takes its cache and releases");
    check(profiles.remove("Android Low").is_ok() && !profiles.remove("Desktop").is_ok() && profiles.get_names().size() == 1,
          "the last profile stays");
}

//-----------------------------------------------------------------------------
// texture variants
//-----------------------------------------------------------------------------

void write_texture(const fs::path& file, std::uint32_t side)
{
    std::vector<std::uint8_t> texels;
    for(std::uint32_t i = 0; i < side * side; ++i)
    {
        const auto shade = static_cast<std::uint8_t>(i * 7);
        texels.insert(texels.end(), {shade, static_cast<std::uint8_t>(255 - shade), 64, 200});
    }
    fs::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    bx::FileWriter writer;
    bx::Error error;
    if(bx::open(&writer, bx::FilePath(file.string().c_str()), false, &error))
    {
        bimg::imageWritePng(&writer, side, side, side * 4, texels.data(), bimg::TextureFormat::RGBA8, false, &error);
        bx::close(&writer);
    }
}

void append_big_endian(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    for(int shift = 24; shift >= 0; shift -= 8)
    {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void append_png_chunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& data)
{
    append_big_endian(out, static_cast<std::uint32_t>(data.size()));
    bx::HashCrc32 crc;
    crc.begin();
    crc.add(type, 4);
    crc.add(data.data(), static_cast<std::int32_t>(data.size()));
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    append_big_endian(out, crc.end());
}

/// A grayscale PNG `side` texels wide: one channel, as texturec reads it. Written by hand, one stored deflate
/// block per row: bimg's PNG writer labels its single-channel images as RGBA.
void write_gray_texture(const fs::path& file, std::uint32_t side)
{
    std::vector<std::uint8_t> header;
    append_big_endian(header, side);
    append_big_endian(header, side);
    header.insert(header.end(), {8, 0, 0, 0, 0});
    std::vector<std::uint8_t> image_data = {0x78, 0x01};
    bx::HashAdler32 adler;
    adler.begin();
    const auto row_size = static_cast<std::uint16_t>(side + 1);
    for(std::uint32_t y = 0; y < side; ++y)
    {
        std::vector<std::uint8_t> row = {0};
        for(std::uint32_t x = 0; x < side; ++x)
        {
            row.push_back(static_cast<std::uint8_t>((x + y) * 2));
        }
        image_data.push_back(y + 1 == side ? 1 : 0);
        image_data.insert(image_data.end(),
                          {static_cast<std::uint8_t>(row_size), static_cast<std::uint8_t>(row_size >> 8),
                           static_cast<std::uint8_t>(~row_size), static_cast<std::uint8_t>(~row_size >> 8)});
        image_data.insert(image_data.end(), row.begin(), row.end());
        adler.add(row.data(), static_cast<std::int32_t>(row.size()));
    }
    append_big_endian(image_data, adler.end());
    std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    append_png_chunk(png, "IHDR", header);
    append_png_chunk(png, "IDAT", image_data);
    append_png_chunk(png, "IEND", {});
    fs::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
}

/// Whether a compiled texture's container is tagged sRGB.
auto is_texture_srgb(const fs::path& file) -> bool
{
    const auto bytes = read_text(file);
    if(bytes.empty())
    {
        return false;
    }
    bx::DefaultAllocator allocator;
    auto* image = bimg::imageParse(&allocator, bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    if(image == nullptr)
    {
        return false;
    }
    const bool is_srgb = image->m_srgb;
    bimg::imageFree(image);
    return is_srgb;
}

/// The format and width of a compiled texture as the engine reads it; Count when it cannot be read.
auto read_texture_format(const fs::path& file, std::uint32_t& width) -> bimg::TextureFormat::Enum
{
    const auto bytes = read_text(file);
    bimg::ImageContainer image;
    if(!imageParseInfo(bytes.data(), static_cast<std::uint32_t>(bytes.size()), image))
    {
        return bimg::TextureFormat::Count;
    }
    width = image.m_width;
    return image.m_format;
}

void test_astc_blocks()
{
    std::printf("test_astc_blocks\n");
    using quality = texture_importer_meta::compression_quality;
    struct astc_case
    {
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t max_side;
        quality compression;
        bgfx::TextureFormat::Enum expected;
        const char* what;
    };
    const astc_case cases[] = {
        {96, 96, 0, quality::normal_quality, bgfx::TextureFormat::ASTC6x6, "6x6 where it divides the size"},
        {64, 64, 0, quality::normal_quality, bgfx::TextureFormat::ASTC4x4, "4x4 for a power of two at normal quality"},
        {96, 64, 0, quality::normal_quality, bgfx::TextureFormat::ASTC4x4, "a block must divide both sides"},
        {30, 30, 0, quality::normal_quality, bgfx::TextureFormat::ASTC6x6, "a small size 6x6 divides"},
        {25, 25, 0, quality::normal_quality, bgfx::TextureFormat::ASTC5x5, "5x5 before 4x4"},
        {31, 31, 0, quality::normal_quality, bgfx::TextureFormat::ASTC4x4, "4x4 when no block divides"},
        {1024, 1024, 512, quality::low_quality, bgfx::TextureFormat::ASTC8x8, "low quality 8x8 under a cap"},
        {3000, 2000, 1024, quality::normal_quality, bgfx::TextureFormat::ASTC4x4, "a capped size no block keeps"},
        {3072, 1536, 1536, quality::normal_quality, bgfx::TextureFormat::ASTC6x6, "a capped size 6x6 keeps"},
        {96, 96, 0, quality::high_quality, bgfx::TextureFormat::ASTC4x4, "high quality is always 4x4"},
    };
    for(const auto& entry : cases)
    {
        const auto actual = ac::select_astc_ldr_format(entry.width, entry.height, entry.max_side, entry.compression);
        check(actual == entry.expected,
              fmt::format("{} ({}x{}, cap {}: {})", entry.what, entry.width, entry.height, entry.max_side, bimg::getName(bimg::TextureFormat::Enum(actual))));
    }
}

void test_texture_parameters()
{
    std::printf("test_texture_parameters\n");
    texture_importer_meta importer;
    ac::texture_target desktop;
    desktop.defaults.default_max_size = texture_importer_meta::texture_size::size_4096;
    ac::texture_target android;
    android.platform = target_platform::android;
    android.family = ac::texture_compression_family::astc;
    android.defaults.default_max_size = texture_importer_meta::texture_size::size_1024;
    check(ac::resolve_texture_parameters(importer, desktop).max_size == texture_importer_meta::texture_size::size_4096 &&
              ac::resolve_texture_parameters(importer, android).max_size == texture_importer_meta::texture_size::size_1024,
          "Project Default takes the target's defaults");
    android.defaults.default_compression = texture_importer_meta::compression_quality::project_default;
    check(ac::resolve_texture_parameters(importer, android).compression == texture_importer_meta::compression_quality::normal_quality,
          "and falls back to normal quality");
    importer.platforms.android.is_overridden = true;
    importer.platforms.android.quality.max_size = texture_importer_meta::texture_size::size_256;
    check(ac::resolve_texture_parameters(importer, android).max_size == texture_importer_meta::texture_size::size_256 &&
              ac::resolve_texture_parameters(importer, desktop).max_size == texture_importer_meta::texture_size::size_4096,
          "a platform's own quality applies on that platform only");
    importer.platforms.android.is_overridden = false;
    check(ac::resolve_texture_parameters(importer, android).max_size == texture_importer_meta::texture_size::size_1024,
          "and not while it is not overridden");
    check(ac::to_string(ac::resolve_texture_parameters(importer, android)) != ac::to_string(ac::resolve_texture_parameters(importer, desktop)),
          "the parameters a manifest records tell targets apart");
    importer.quality.compression = texture_importer_meta::compression_quality::none;
    android.defaults.default_max_size = desktop.defaults.default_max_size;
    check(ac::resolve_texture_parameters(importer, android) == ac::resolve_texture_parameters(importer, desktop),
          "an uncompressed texture compiles the same for every format family");
}

void test_srgb_textures(const fs::path& root)
{
    std::printf("test_srgb_textures\n");
    check(!ac::has_srgb_view(bgfx::TextureFormat::BC4) && !ac::has_srgb_view(bgfx::TextureFormat::EACRG11) &&
              !ac::has_srgb_view(bgfx::TextureFormat::R8) && ac::has_srgb_view(bgfx::TextureFormat::BC1) &&
              ac::has_srgb_view(bgfx::TextureFormat::ETC2A) && ac::has_srgb_view(bgfx::TextureFormat::ASTC6x6),
          "one and two channel formats have no sRGB view, color formats have one");
    check(ac::select_srgb_format(bgfx::TextureFormat::BC5, ac::texture_compression_family::bc) == bgfx::TextureFormat::BC1 &&
              ac::select_srgb_format(bgfx::TextureFormat::EACR11, ac::texture_compression_family::etc2) == bgfx::TextureFormat::ETC2 &&
              ac::select_srgb_format(bgfx::TextureFormat::RG8, ac::texture_compression_family::bc) == bgfx::TextureFormat::RGBA8 &&
              ac::select_srgb_format(bgfx::TextureFormat::BC3, ac::texture_compression_family::bc) == bgfx::TextureFormat::BC3,
          "an sRGB texture takes its family's color format, RGBA8 uncompressed, and keeps a format with a view");
    if(!is_present(fs::resolve_protocol(std::string("binary:/texturec") + fs::executable_extension())))
    {
        return;
    }
    fs::error_code ec;
    fs::create_directories(root, ec);
    const auto app_root = fs::resolve_protocol("app:/");
    const auto source = app_root / TEXTURE_FOLDER / "gray.png";
    const auto key = std::string("app:/") + TEXTURE_FOLDER + "/gray.png";
    write_gray_texture(source, SRGB_TEXTURE_SIDE);
    unravel::asset_meta meta;
    meta.uid = hpp::uuid::from_string("40000000-0000-4000-8000-000000000003").value();
    meta.type = ".png";
    auto importer = std::make_shared<texture_importer_meta>();
    importer->colorspace = texture_importer_meta::color_space::srgb;
    meta.importer = importer;
    unravel::save_to_file((app_root / TEXTURE_FOLDER / "gray.png.meta").string(), meta);
    struct family_case
    {
        ac::texture_compression_family family;
        target_platform platform;
    };
    for(const auto& item : {family_case{ac::texture_compression_family::bc, target_platform::desktop},
                            family_case{ac::texture_compression_family::etc2, target_platform::android},
                            family_case{ac::texture_compression_family::astc, target_platform::android}})
    {
        ac::texture_target target;
        target.platform = item.platform;
        target.family = item.family;
        target.defaults.default_compression = texture_importer_meta::compression_quality::normal_quality;
        const auto output = root / "gray.png.asset";
        std::uint32_t width = 0;
        const bool is_compiled = ac::compile_texture_for_target(key, output, target);
        const auto format = static_cast<bgfx::TextureFormat::Enum>(read_texture_format(output, width));
        const auto message = fmt::format("a grayscale texture tagged sRGB compiles for {} to a format with an sRGB view, "
                                         "tagged sRGB (compiled: {}, {})",
                                         ac::to_string(ac::texture_compile_parameters{item.family}),
                                         is_compiled ? bimg::getName(static_cast<bimg::TextureFormat::Enum>(format)) : "no",
                                         is_texture_srgb(output) ? "sRGB" : "linear");
        check(is_compiled && ac::has_srgb_view(format) && is_texture_srgb(output), message.c_str());
    }
    importer->quality.compression = texture_importer_meta::compression_quality::none;
    unravel::save_to_file((app_root / TEXTURE_FOLDER / "gray.png.meta").string(), meta);
    ac::texture_target uncompressed;
    const auto output = root / "gray_uncompressed.png.asset";
    std::uint32_t width = 0;
    check(ac::compile_texture_for_target(key, output, uncompressed) &&
              read_texture_format(output, width) == bimg::TextureFormat::RGBA8 && is_texture_srgb(output),
          "uncompressed, it is RGBA8 rather than R8");
    fs::remove(source, ec);
    fs::remove(app_root / TEXTURE_FOLDER / "gray.png.meta", ec);
}

/// A cook plan over the tests' app protocol for one profile.
struct cook_fixture
{
    fs::path project;
    std::vector<std::string> keys;
    unravel::asset_database::database_t database;
    deploy_settings params;
    settings runtime;
    ac::texture_target editor_target;

    auto make_plan(const std::unordered_set<std::string>& left_out = {}) const -> deployment::texture_cook_plan
    {
        return deployment::make_texture_cook_plan(project, params, runtime, database, left_out, editor_target);
    }
};

void test_texture_variants(const fs::path& root)
{
    std::printf("test_texture_variants\n");
    if(!is_present(fs::resolve_protocol(std::string("binary:/texturec") + fs::executable_extension())))
    {
        check(false, "texturec is beside the tests: the texture variants need it");
        return;
    }
    cook_fixture fixture;
    fixture.project = root;
    const auto app_root = fs::resolve_protocol("app:/");
    fs::error_code ec;
    fs::remove_all(app_root / TEXTURE_FOLDER, ec);
    // The editor's compiled files of the test textures, as the editor lays them out.
    const auto editor_compiled = fs::resolve_protocol(ex::get_compiled_directory("app")) / "profile_tests";
    const auto editor_wall = editor_compiled / "wall.png.asset";
    fs::remove_all(editor_compiled, ec);
    for(const auto* name : {"wall.png", "floor.png"})
    {
        write_texture(app_root / TEXTURE_FOLDER / name, fixture.keys.empty() ? WALL_SIDE : FLOOR_SIDE);
        const auto key = std::string("app:/") + TEXTURE_FOLDER + "/" + name;
        fixture.keys.push_back(key);
        unravel::asset_database::meta entry;
        entry.location = key;
        entry.meta.uid = hpp::uuid::from_string(fixture.keys.size() == 1 ? "40000000-0000-4000-8000-000000000001"
                                                                         : "40000000-0000-4000-8000-000000000002")
                             .value();
        fixture.database.emplace(entry.meta.uid, entry);
    }
    fixture.params.profile = "Phone";
    fixture.params.deploy_target = unravel::deploy_platform::android;
    fixture.runtime.assets.texture.default_max_size = texture_importer_meta::texture_size::size_1024;
    fixture.runtime.assets.texture.default_compression = texture_importer_meta::compression_quality::normal_quality;
    fixture.editor_target.defaults = fixture.runtime.assets.texture;
    const auto variants = deployment::get_profile_variants_folder(root, "Phone", "app");
    const auto wall = variants / "profile_tests" / "wall.png.asset";
    const auto floor = variants / "profile_tests" / "floor.png.asset";

    auto report = deployment::cook_texture_variants(fixture.make_plan());
    check(report.failed.empty() && report.variants == 2 && report.compiled == 2, "an Android profile cooks a variant per texture");
    std::uint32_t width = 0;
    check(read_texture_format(wall, width) == bimg::TextureFormat::ASTC6x6 && width == WALL_SIDE,
          "normal quality is ASTC 6x6, at the source's size under the cap");
    check(read_texture_format(floor, width) == bimg::TextureFormat::ASTC4x4 && width == FLOOR_SIDE,
          "a texture 6x6 blocks do not divide takes 4x4 ones and keeps its size: texturec would resample it");
    report = deployment::cook_texture_variants(fixture.make_plan());
    check(report.compiled == 0 && report.variants == 2 && report.removed == 0, "a current variant is reused");

    fixture.runtime.assets.texture.default_compression = texture_importer_meta::compression_quality::high_quality;
    report = deployment::cook_texture_variants(fixture.make_plan());
    check(report.compiled == 2 && read_texture_format(wall, width) == bimg::TextureFormat::ASTC4x4,
          "a changed default compiles the variant again: high quality is ASTC 4x4");
    fixture.runtime.assets.texture.default_max_size = texture_importer_meta::texture_size::size_32;
    report = deployment::cook_texture_variants(fixture.make_plan());
    check(report.compiled == 2 && read_texture_format(wall, width) == bimg::TextureFormat::ASTC4x4 && width == 32,
          "a smaller default size compiles it again, smaller");
    check(count_files(variants) == 4, "in place: one variant and its manifest per texture, nothing left of the earlier ones");

    fixture.params.android.texture_compression = unravel::android_texture_compression::etc2;
    report = deployment::cook_texture_variants(fixture.make_plan());
    check(report.compiled == 2 && read_texture_format(wall, width) == bimg::TextureFormat::ETC2A && count_files(variants) == 4,
          "ETC2 compiles in place too: ETC2A for a texture with alpha");
    fixture.params.android.texture_compression = unravel::android_texture_compression::bc;
    report = deployment::cook_texture_variants(fixture.make_plan());
    check(report.compiled == 2 && read_texture_format(wall, width) == bimg::TextureFormat::BC3 && count_files(variants) == 4,
          "BC compiles the desktop's formats in place: BC3 for a texture with alpha");
    fixture.params.android.texture_compression = unravel::android_texture_compression::etc2;
    report = deployment::cook_texture_variants(fixture.make_plan());
    check(report.compiled == 2 && read_texture_format(wall, width) == bimg::TextureFormat::ETC2A, "and back to ETC2, in place");

    write_text(variants / "profile_tests" / "stray.png.asset", "stray");
    write_text(variants / "elsewhere" / "old.png.asset.manifest", "stray");
    report = deployment::cook_texture_variants(fixture.make_plan());
    check(report.compiled == 0 && report.removed == 2 && count_files(variants) == 4 && !is_present(variants / "elsewhere"),
          "files nothing needs are swept, with their folders");

    unravel::asset_meta meta;
    meta.uid = hpp::uuid::from_string("40000000-0000-4000-8000-000000000001").value();
    meta.type = ".png";
    auto importer = std::make_shared<texture_importer_meta>();
    importer->platforms.android.is_overridden = true;
    importer->platforms.android.quality.max_size = texture_importer_meta::texture_size::size_64;
    importer->platforms.android.quality.compression = texture_importer_meta::compression_quality::low_quality;
    meta.importer = importer;
    unravel::save_to_file((app_root / TEXTURE_FOLDER / "wall.png.meta").string(), meta);
    report = deployment::cook_texture_variants(fixture.make_plan());
    check(report.compiled == 1, "a texture's own quality for the platform compiles its variant again");
    fixture.params.android.texture_compression = unravel::android_texture_compression::astc;
    report = deployment::cook_texture_variants(fixture.make_plan());
    check(read_texture_format(wall, width) == bimg::TextureFormat::ASTC8x8 && width == LOW_QUALITY_SIDE,
          "low quality is ASTC 8x8, at the texture's own max size");

    report = deployment::cook_texture_variants(fixture.make_plan({fs::make_path_key(fixture.keys[1])}));
    check(report.variants == 1 && is_present(variants / "profile_tests" / "floor.png.asset"),
          "a left-out texture is not cooked, and its variant is kept for a deploy that ships it");
    fixture.database.erase(hpp::uuid::from_string("40000000-0000-4000-8000-000000000002").value());
    report = deployment::cook_texture_variants(fixture.make_plan());
    check(report.removed == 2 && !is_present(variants / "profile_tests" / "floor.png.asset"),
          "the variant of a texture the project no longer has is deleted");

    // A desktop profile with the editor's defaults ships the editor's own files, once they are current.
    fixture.params.profile = "Desktop";
    fixture.params.deploy_target = unravel::deploy_platform::desktop;
    fixture.runtime.assets.texture = fixture.editor_target.defaults;
    const auto desktop_variants = deployment::get_profile_variants_folder(root, "Desktop", "app");
    write_text(desktop_variants / "profile_tests" / "wall.png.asset", "left from other settings");
    auto outdated_target = fixture.editor_target;
    outdated_target.defaults.default_max_size = texture_importer_meta::texture_size::size_32;
    check(ac::compile_texture_for_target(fixture.keys[0], editor_wall, outdated_target), "the editor's file compiles");
    report = deployment::cook_texture_variants(fixture.make_plan());
    check(report.variants == 1 && report.compiled == 1 && read_texture_format(desktop_variants / "profile_tests" / "wall.png.asset", width) != bimg::TextureFormat::Count,
          "a profile that compiles like the editor gets a variant while the editor's file is compiled with other defaults");
    check(ac::compile_texture_for_target(fixture.keys[0], editor_wall, fixture.editor_target), "the editor's file compiles again");
    report = deployment::cook_texture_variants(fixture.make_plan());
    check(report.variants == 0 && report.compiled == 0 && count_files(desktop_variants) == 0,
          "and ships the editor's current file, its variant deleted");
    fs::remove_all(app_root / TEXTURE_FOLDER, ec);
    fs::remove_all(editor_compiled, ec);
}

auto run_settings_profiles_suite(rtti::context&) -> int
{
    g_checks = 0;
    g_failures = 0;
    const auto temp = get_temp_folder();
    fs::error_code ec;
    fs::remove_all(temp, ec);
    try
    {
        test_document_paths();
        test_sparse_overrides();
        test_profile_files(temp / "files");
        test_legacy_project(temp / "legacy");
        test_project_profiles(temp / "project");
        test_unreadable_profile(temp / "unreadable");
        test_astc_blocks();
        test_texture_parameters();
        test_srgb_textures(temp / "srgb");
        test_texture_variants(temp / "variants");
    }
    catch(const std::exception& ex)
    {
        check(false, std::string("unexpected exception: ") + ex.what());
    }
    fs::remove_all(temp, ec);
    std::printf("settings profiles: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("settings profiles", run_settings_profiles_suite)
