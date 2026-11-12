/*
 * Validation suite for the deploy library (editor/core/deploy), which decides what a deploy ships and
 * writes it.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "deploy library"
 *
 * This pins:
 *   - keys: separators and trailing slashes normalized, "a" does not cover "ab", display paths;
 *   - rules: the most specific rule wins, exclude wins a tie whatever the trailing slash, set_shipped
 *     keeps a rule only where use alone would decide otherwise, keys are respelled and deduped, rules
 *     on nothing are reported, filter names round-trip;
 *   - the walk over a project folder: scenes, uids, quoted paths, UI links, generated assets, the
 *     project settings, script literals (with and without an extension, interpolated, folders),
 *     binary files not read, missing scenes reported, compiled sizes;
 *   - the walk over the engine's assets: those outside the folders it decides and those the engine loads
 *     always ship; engine fonts and UI files ship when a scene, a style sheet or a script uses them;
 *     relative links stay in the engine, and one that leaves it is not followed; rules reach engine assets;
 *     scripts building paths in engine folders that always ship raise no note; the content tree lists the
 *     engine's assets under a last top row; every key the engine loads by path is a file of engine_data;
 *   - the content tree: counts, folders first, rows of open folders only, search and filter rows,
 *     left out but used assets counted;
 *   - files: classification, compiled paths, a target's shader variants, pack inputs with cooked
 *     variants from several folders (the first wins) and cooked files without a compiled file, stale
 *     binaries, older runtime versions;
 *   - cooked shaders: the renderers a web build ships and cooks, the plan of what a deploy cooks;
 *   - web: the browser player template's stamp checks, the player copied beside the deployed data, and
 *     the list of the data's files the page loads; a trimmed build's runtime files left out of the copy, not
 *     missed in the site and listed for the page; a public link's cloudflared command, the tunnel address
 *     its output announces, and the requests that count as the tunnel's;
 *   - profile folders under the deploy folder; deploy locations relative to the project, and the
 *     project folders the editor manages;
 *   - Android: default and invalid application ids, adb device lists and one entry per phone, logcat
 *     lines, which are the game's or the device system's and the engine lines' text, the player template's
 *     stamp checks, Gradle's command line, the start command with the game's options; a build folder's owner,
 *     and the build folders no deploy needs any more;
 *   - the desktop player's stamp checks; text files written byte for byte, or a throw;
 *   - Windows executables: version numbers from version text, the icon group and version resources byte by
 *     byte, icon images per size, and on Windows a copied executable's icon and version replaced and read
 *     back as Windows reads them; the window icon file written beside executables that carry no icon;
 *   - run arguments: split on whitespace, quoted for a device shell;
 *   - subprocess::call hands over output lines, adds environment variables and ends its process when its stop
 *     check says so; subprocess::stream stops its process at once, and reports a program that does not exist as
 *     not started;
 *   - a cancelled deploy's cooks: nothing more is cooked and the cook throws;
 *   - a protocol shipped packed, loose, packed again, as a patch over a kept release and full again,
 *     with left out assets; a script library the deploy built shipped in place of the editor's; a protocol
 *     without a script library shipped packed;
 *   - the content walk over a profile's shipped settings instead of the settings file;
 *   - releases: names, keep / list / remove, and their refusals; a profile's releases folder, and the
 *     releases of a project from before profiles moved into one; a build's debug files kept, and kept with
 *     a release; only a build its deploy marked as finished is kept;
 *   - Steam: the settings an upload needs, the app build script byte by byte, steamcmd's command line, the
 *     build id and login failures read from its output;
 *   - dependency scanner output parsed; a failed scan throws every time;
 *   - folder checks: earlier deploys, a build's stamp (started, finished), Android package folders, inside /
 *     outside, an executable in use;
 *   - unravel::process::spawn_detached runs a program and reports one that does not start.
 * Everything lives in a temp folder.
 */

#include "../tests.h"

#include <deploy/android.h>
#include <deploy/android_icon.h>
#include <deploy/cancellation.h>
#include <deploy/content.h>
#include <deploy/content_tree.h>
#include <deploy/dependencies.h>
#include <deploy/extras.h>
#include <deploy/files.h>
#include <deploy/folders.h>
#include <deploy/icon_image.h>
#include <deploy/keys.h>
#include <deploy/profile.h>
#include <deploy/profile_cache.h>
#include <deploy/protocol_data.h>
#include <deploy/releases.h>
#include <deploy/run_arguments.h>
#include <deploy/shader_cook.h>
#include <deploy/steam.h>
#include <deploy/variant_cook.h>
#include <deploy/web.h>
#include <deploy/windows_resources.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/defaults/engine_asset_keys.h>
#include <filesystem/file_fingerprint.h>
#include <filesystem/filesystem.h>
#include <filesystem/pack.h>
#include <graphics/graphics.h>
#include <process/process.h>
#include <string_utils/utils.h>
#include <subprocess/subprocess.hpp>

#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/file.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace
{
namespace deployment = unravel::deployment;
using unravel::asset_database;
using unravel::deploy_settings;

constexpr const char* TEST_PROTOCOL = "unravel-tests-deploy";
constexpr std::size_t COMPILED_WALL_SIZE = 123;
constexpr std::size_t COMPILED_FONT_SIZE = 77;
constexpr auto SPAWN_WAIT = std::chrono::seconds(10);
constexpr auto SPAWN_POLL = std::chrono::milliseconds(50);

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
    return fs::temp_directory_path() / "unravel-tests-deploy-library";
}

/// Whether paths in another letter case name the same file here, as the game's lookups see it.
auto is_case_folded() -> bool
{
    return fs::make_path_key("A") == fs::make_path_key("a");
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

auto make_uid(const char* text) -> hpp::uuid
{
    return hpp::uuid::from_string(std::string(text)).value();
}

auto quote(const hpp::uuid& uid) -> std::string
{
    return "\"" + hpp::to_string(uid) + "\"";
}

void add_asset(asset_database::database_t& database, const hpp::uuid& uid, const std::string& key, const hpp::uuid& source = {})
{
    asset_database::meta entry;
    entry.location = key;
    entry.meta.uid = uid;
    entry.meta.generated.source_uid = source;
    database.emplace(uid, entry);
}

auto get_entry(const deployment::content& found, const std::string& key) -> const deployment::content_entry*
{
    return found.find(key);
}

auto is_used_by(const deployment::content& found, const std::string& key, deployment::content_reason use, const std::string& used_by)
    -> bool
{
    const auto* entry = get_entry(found, key);
    return entry != nullptr && entry->is_used && entry->use == use && entry->used_by == used_by;
}

auto is_unused(const deployment::content& found, const std::string& key) -> bool
{
    const auto* entry = get_entry(found, key);
    return entry != nullptr && !entry->is_used;
}

auto contains(const std::vector<std::string>& values, const std::string& value) -> bool
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

/// Whether the walk names `folder` as one `script` builds asset paths in.
auto is_named_by(const deployment::content& found, const std::string& folder, const std::string& script) -> bool
{
    return std::any_of(found.named_folders.begin(),
                       found.named_folders.end(),
                       [&](const deployment::named_folder& named)
                       {
                           return named.key == folder && named.scripts == std::vector<std::string>{script};
                       });
}

//-----------------------------------------------------------------------------
// keys
//-----------------------------------------------------------------------------

void test_keys()
{
    std::printf("test_keys\n");
    check(deployment::normalize_key("app:/data/levels/") == "app:/data/levels", "a trailing slash is dropped");
    check(deployment::normalize_key("app:/data\\levels\\one.spfb") == "app:/data/levels/one.spfb", "backslashes become slashes");
    check(deployment::normalize_key("app:/") == "app:/", "a protocol's root keeps its slash");
    check(deployment::is_key_under("app:/data/ab/x.png", "app:/data/ab"), "a key lies under its folder");
    check(deployment::is_key_under("app:/data/ab", "app:/data/ab/"), "a folder covers itself, with or without a slash");
    check(!deployment::is_key_under("app:/data/ab", "app:/data/a"), "app:/data/a does not cover app:/data/ab");
    check(deployment::is_key_under("app:/data/a.png", "app:/"), "the protocol's root covers everything in it");
    check(deployment::is_key_under("app:/Data/Levels/One.spfb", "app:/data/levels") == is_case_folded(),
          "letter case counts only where the file system counts it");
    check(deployment::get_key_without_extension("app:/data/levels/one.spfb") == "app:/data/levels/one", "the extension is cut");
    check(deployment::get_key_without_extension("app:/data/a.b/c") == "app:/data/a.b/c", "a dot in a folder is no extension");
    check(deployment::get_key_name("app:/data/levels/") == "levels", "a folder's name");
    check(deployment::get_display_path("app:/data/levels/one.spfb") == "levels/one.spfb", "display paths drop the data folder");
    check(deployment::get_display_path("engine:/data/x.png") == "engine:/data/x.png", "other protocols keep their key");
    const deployment::left_out_assets left_out{"app:/data/Textures/Floor.png"};
    check(left_out.contains("app:/data\\Textures\\Floor.png") && !left_out.contains("app:/data/textures/wall.png"),
          "left-out keys compare whatever their separators");
    check(left_out.contains("app:/data/textures/floor.png") == is_case_folded(),
          "and their letter case only where the file system counts it");
    check(deployment::is_asset_key<gfx::texture>("app:/data/a.png") && !deployment::is_asset_key<gfx::texture>("app:/data/a.ogg"),
          "an asset's kind is told by its extension");
}

//-----------------------------------------------------------------------------
// rules
//-----------------------------------------------------------------------------

/// A content of six assets with no files behind them: what the rules work on.
auto make_listed_content(const fs::path& project_root) -> deployment::content
{
    deployment::content_source source;
    source.project_root = project_root;
    add_asset(source.database, make_uid("10000000-0000-4000-8000-000000000001"), "app:/data/levels/one.spfb");
    add_asset(source.database, make_uid("10000000-0000-4000-8000-000000000002"), "app:/data/levels/two.spfb");
    add_asset(source.database, make_uid("10000000-0000-4000-8000-000000000003"), "app:/data/textures/wall.png");
    add_asset(source.database, make_uid("10000000-0000-4000-8000-000000000004"), "app:/data/textures/floor.png");
    add_asset(source.database, make_uid("10000000-0000-4000-8000-000000000005"), "app:/data/scripts/game.cs");
    add_asset(source.database, make_uid("10000000-0000-4000-8000-000000000006"), "app:/data/readme.txt");
    return deployment::list_content(source);
}

void test_rule_precedence(const deployment::content& found)
{
    std::printf("test_rule_precedence\n");
    check(found.entries.size() == 5, "scripts are not content: they ship as their library");
    check(found.find("app:/data/levels/one.spfb") != nullptr, "an asset is found by key");
    check(found.has_folder("app:/data/levels") && !found.has_folder("app:/data/level"), "folders are found whole");
    deploy_settings params;
    params.deploy_included = {"app:/data/levels/"};
    params.deploy_excluded = {"app:/data/levels"};
    check(deployment::find_rule(params, "app:/data/levels/one.spfb") == deployment::content_rule::exclude,
          "exclude wins a tie, whatever the trailing slash");
    params.deploy_included = {"app:/data/textures/wall.png"};
    params.deploy_excluded = {"app:/data/textures"};
    check(deployment::find_rule(params, "app:/data/textures/wall.png") == deployment::content_rule::include,
          "the most specific rule wins");
    check(deployment::find_rule(params, "app:/data/textures/floor.png") == deployment::content_rule::exclude,
          "the folder's rule covers the rest");
    check(deployment::find_rule(params, "app:/data/levels/one.spfb") == deployment::content_rule::none, "no rule elsewhere");
    check(deployment::has_own_rule(params, "app:/data/textures/") && !deployment::has_own_rule(params, "app:/data"),
          "has_own_rule names the key itself");
    params.deploy_only_used_assets = true;
    const auto* wall = found.find("app:/data/textures/wall.png");
    const auto* one = found.find("app:/data/levels/one.spfb");
    check(deployment::is_shipped(params, *wall), "an included asset ships though nothing uses it");
    check(!deployment::is_shipped(params, *one), "an unused asset is left out when only used assets ship");
    check(deployment::get_reason(params, *wall) == deployment::content_reason::included, "its reason is the rule");
    check(deployment::passes_filter(params, *wall, deployment::content_filter::changed) &&
              !deployment::passes_filter(params, *one, deployment::content_filter::changed),
          "the changed filter shows assets under a rule");
    check(deployment::is_filtered(params) && deployment::needs_walk(params), "only used assets need the walk");
    params.deploy_only_used_assets = false;
    check(deployment::is_filtered(params) && !deployment::needs_walk(params), "exclude rules alone filter without the walk");
}

void test_set_shipped(const deployment::content& found)
{
    std::printf("test_set_shipped\n");
    deploy_settings params;
    deployment::set_shipped(params, found, "app:/data/textures/", false);
    check(params.deploy_excluded == std::vector<std::string>{"app:/data/textures"}, "leaving a folder out adds one exclude rule");
    deployment::set_shipped(params, found, "app:/data/textures/wall.png", true);
    check(params.deploy_included == std::vector<std::string>{"app:/data/textures/wall.png"}, "shipping one asset back includes it");
    deployment::set_shipped(params, found, "app:/data/textures", true);
    check(params.deploy_included.empty() && params.deploy_excluded.empty(),
          "shipping the folder again drops every rule in it: everything ships anyway");
    params.deploy_only_used_assets = true;
    deployment::set_shipped(params, found, "app:/data/levels", false);
    check(params.deploy_excluded.empty(), "no rule where use alone already leaves the folder out");
    deployment::set_shipped(params, found, "app:/data/levels", true);
    check(params.deploy_included == std::vector<std::string>{"app:/data/levels"}, "an include rule where use would leave it out");
    deployment::reset_rules(params, "app:/data");
    check(params.deploy_included.empty(), "reset_rules drops the rules under the key");
    check(deployment::get_left_out_keys(params, found).size() == found.entries.size(),
          "with nothing used and no rule, nothing ships");
}

void test_normalize_and_orphans(const deployment::content& found)
{
    std::printf("test_normalize_and_orphans\n");
    deploy_settings params;
    params.deploy_scenes = {"app:/data\\levels\\one.spfb", "app:/data/levels/one.spfb/", "app:/data/levels/gone.spfb"};
    params.deploy_included = {"app:/data/textures/", "app:/data/textures"};
    params.deploy_excluded = {"app:/data/nothing"};
    if(is_case_folded())
    {
        params.deploy_scenes.push_back("app:/data/LEVELS/Two.spfb");
    }
    const auto unknown = deployment::normalize_keys(params, found);
    auto expected_scenes = std::vector<std::string>{"app:/data/levels/one.spfb", "app:/data/levels/gone.spfb"};
    if(is_case_folded())
    {
        expected_scenes.push_back("app:/data/levels/two.spfb");
    }
    check(params.deploy_scenes == expected_scenes, "scene keys are normalized, respelled as the project spells them, deduped");
    check(params.deploy_included == std::vector<std::string>{"app:/data/textures"}, "rules are deduped");
    check(unknown == std::vector<std::string>{"app:/data/levels/gone.spfb", "app:/data/nothing"}, "keys on nothing are reported");
    const auto orphans = deployment::find_orphan_rules(params, found);
    check(orphans == std::vector<std::string>{"app:/data/nothing"}, "rules on nothing are orphans");
    deployment::content_filter filter{};
    check(deployment::parse_filter("left_out", filter) && filter == deployment::content_filter::left_out, "filter names parse");
    check(!deployment::parse_filter("bogus", filter), "an unknown filter name is refused");
    check(std::string(deployment::get_filter_name(deployment::content_filter::changed)) == "changed", "filter names round-trip");
}

//-----------------------------------------------------------------------------
// the walk
//-----------------------------------------------------------------------------

struct walk_project
{
    deployment::content_source source;
    std::string startup_scene = "app:/data/levels/one.spfb";
};

/// A project folder whose files reference one another in every way the walk follows.
auto make_walk_project(const fs::path& root) -> walk_project
{
    walk_project project;
    project.source.project_root = root;
    auto& database = project.source.database;
    const auto one = make_uid("20000000-0000-4000-8000-000000000001");
    const auto wall = make_uid("20000000-0000-4000-8000-000000000002");
    const auto hero = make_uid("20000000-0000-4000-8000-000000000003");
    const auto logo = make_uid("20000000-0000-4000-8000-000000000004");
    add_asset(database, one, "app:/data/levels/one.spfb");
    add_asset(database, make_uid("20000000-0000-4000-8000-000000000005"), "app:/data/levels/level_1.spfb");
    add_asset(database, make_uid("20000000-0000-4000-8000-000000000006"), "app:/data/levels/level_2.spfb");
    add_asset(database, wall, "app:/data/textures/wall.png");
    add_asset(database, make_uid("20000000-0000-4000-8000-000000000007"), "app:/data/textures/floor.png");
    add_asset(database, make_uid("20000000-0000-4000-8000-000000000008"), "app:/data/textures/secret.png");
    add_asset(database, make_uid("20000000-0000-4000-8000-000000000009"), "app:/data/textures/icon.png");
    add_asset(database, make_uid("20000000-0000-4000-8000-00000000000a"), "app:/data/textures/frame.png");
    add_asset(database, make_uid("20000000-0000-4000-8000-00000000000b"), "app:/data/textures/unused.png");
    add_asset(database, logo, "app:/data/textures/logo.png");
    add_asset(database, hero, "app:/data/models/hero.fbx");
    add_asset(database, make_uid("20000000-0000-4000-8000-00000000000c"), "app:/data/models/hero_material.mat", hero);
    add_asset(database, make_uid("20000000-0000-4000-8000-00000000000d"), "app:/data/ui/hud.rhtml");
    add_asset(database, make_uid("20000000-0000-4000-8000-00000000000e"), "app:/data/ui/hud.rcss");
    add_asset(database, make_uid("20000000-0000-4000-8000-00000000000f"), "app:/data/music/theme.ogg");
    add_asset(database, make_uid("20000000-0000-4000-8000-000000000010"), "app:/data/scripts/game.cs");
    write_text(root / "data/levels/one.spfb",
               "{\"mesh\": {\"uid\": " + quote(wall) + "}, \"model\": " + quote(hero) +
                   ", \"document\": \"app:/data/ui/hud.rhtml\"}");
    write_text(root / "data/ui/hud.rhtml",
               "<rml><head><link type=\"text/rcss\" href=\"hud.rcss\"/></head>"
               "<body><img src=\"../textures/icon.png\"/></body></rml>");
    write_text(root / "data/ui/hud.rcss", "body { decorator: image(\"app:/data/textures/frame.png\"); }");
    // Binary: the path after the zero byte must not be followed.
    write_text(root / "data/textures/wall.png", std::string("PNG\0\0 \"app:/data/textures/secret.png\"", 37));
    write_text(root / "settings/settings.cfg", "{\"splash\": " + quote(logo) + "}");
    write_text(root / "data/scripts/game.cs",
               "class Game {\n"
               "  void Load(int n, string name) {\n"
               "    Scene.Load($\"app:/data/levels/level_{n}.spfb\");\n"
               "    var floor = \"app:/data/Textures/floor\";\n"
               "    var music = \"app:/data/music/\" + name;\n"
               "  }\n"
               "}\n");
    write_text(root / "compiled/textures/wall.png.asset", std::string(COMPILED_WALL_SIZE, 'w'));
    return project;
}

void test_walk(const deployment::content& found)
{
    std::printf("test_walk\n");
    using reason = deployment::content_reason;
    check(is_used_by(found, "app:/data/levels/one.spfb", reason::scene, ""), "the startup scene is reached as a scene");
    check(is_used_by(found, "app:/data/textures/wall.png", reason::referenced, "app:/data/levels/one.spfb"),
          "a quoted uid is followed");
    check(is_used_by(found, "app:/data/ui/hud.rhtml", reason::referenced, "app:/data/levels/one.spfb"),
          "a quoted path is followed");
    check(is_used_by(found, "app:/data/ui/hud.rcss", reason::referenced, "app:/data/ui/hud.rhtml"),
          "a UI document's link is followed, relative to it");
    check(is_used_by(found, "app:/data/textures/icon.png", reason::referenced, "app:/data/ui/hud.rhtml"),
          "a link out of the document's folder is followed");
    check(is_used_by(found, "app:/data/textures/frame.png", reason::referenced, "app:/data/ui/hud.rcss"),
          "a style sheet's quoted path is followed");
    check(is_used_by(found, "app:/data/models/hero_material.mat", reason::referenced, "app:/data/models/hero.fbx"),
          "what an import generates beside its source ships with it");
    check(is_used_by(found, "app:/data/textures/logo.png", reason::referenced, "Project Settings"),
          "the project settings' handles are followed");
    check(is_unused(found, "app:/data/textures/secret.png"), "a binary file is not read for references");
    check(is_unused(found, "app:/data/textures/unused.png"), "what nothing uses is unused");
    check(is_unused(found, "app:/data/levels/level_1.spfb"), "an interpolated path names no asset");
    check(is_named_by(found, "app:/data/levels", "app:/data/scripts/game.cs"),
          "an interpolated path names the folder before it, and the script");
    check(is_named_by(found, "app:/data/music", "app:/data/scripts/game.cs"), "a path built from a folder names that folder");
    if(is_case_folded())
    {
        check(is_used_by(found, "app:/data/textures/floor.png", reason::named_in_script, "app:/data/scripts/game.cs"),
              "a script path without its extension, in another case, finds the asset");
    }
    check(found.missing_scenes == std::vector<std::string>{"app:/data/levels/gone.spfb"}, "a scene no longer there is reported");
    const auto* wall = found.find("app:/data/textures/wall.png");
    check(wall != nullptr && wall->size == COMPILED_WALL_SIZE, "an asset's size is its compiled file's");
}

/// A profile's deploy walks the settings it ships, not the project's file.
void test_walk_shipped_settings(walk_project project)
{
    std::printf("test_walk_shipped_settings\n");
    using reason = deployment::content_reason;
    project.source.settings_document = R"({"settings":{"splash":{"uid":"20000000-0000-4000-8000-00000000000b"}}})";
    const auto found = deployment::detect_content(project.source, {}, project.startup_scene);
    check(is_used_by(found, "app:/data/textures/unused.png", reason::referenced, "Project Settings") &&
              is_unused(found, "app:/data/textures/logo.png"),
          "the shipped settings' handles are followed instead of the settings file's");
}

/// A project whose scene, style sheet and script use engine fonts, beside engine fonts and UI files nothing
/// uses.
auto make_engine_walk_project(const fs::path& root) -> walk_project
{
    walk_project project;
    const auto project_root = root / "project";
    const auto engine_root = root / "engine";
    project.source.project_root = project_root;
    project.source.engine_root = engine_root;
    auto& database = project.source.database;
    auto& engine = project.source.engine_database;
    const auto bold = make_uid("30000000-0000-4000-8000-000000000001");
    add_asset(database, make_uid("30000000-0000-4000-8000-000000000002"), "app:/data/levels/one.spfb");
    add_asset(database, make_uid("30000000-0000-4000-8000-000000000003"), "app:/data/ui/hud.rcss");
    add_asset(database, make_uid("30000000-0000-4000-8000-000000000004"), "app:/data/scripts/game.cs");
    add_asset(engine, bold, "engine:/data/fonts/Inter/static/Inter-Bold.ttf");
    add_asset(engine, make_uid("30000000-0000-4000-8000-000000000005"), "engine:/data/fonts/Inter/static/Inter-Black.ttf");
    add_asset(engine, make_uid("30000000-0000-4000-8000-000000000006"), "engine:/data/fonts/Inter/static/Inter-Light.ttf");
    add_asset(engine, make_uid("30000000-0000-4000-8000-000000000007"), "engine:/data/fonts/Inter/static/Inter-Thin.ttf");
    add_asset(engine, make_uid("30000000-0000-4000-8000-000000000008"), unravel::UI_FONT_KEYS[0]);
    add_asset(engine, make_uid("30000000-0000-4000-8000-000000000009"), unravel::TEXT_DEFAULT_FONT_KEY);
    add_asset(engine, make_uid("30000000-0000-4000-8000-00000000000a"), unravel::SPLASH_DOCUMENT_KEY);
    add_asset(engine, make_uid("30000000-0000-4000-8000-00000000000b"), "engine:/data/ui/splash.rcss");
    add_asset(engine, make_uid("30000000-0000-4000-8000-00000000000c"), "engine:/data/ui/template.rhtml");
    add_asset(engine, make_uid("30000000-0000-4000-8000-00000000000d"), "engine:/data/ui/sample_hud.rhtml");
    add_asset(engine, make_uid("30000000-0000-4000-8000-00000000000e"), "engine:/data/scripts/SampleHud.cs");
    add_asset(engine, make_uid("30000000-0000-4000-8000-00000000000f"), "engine:/data/textures/default_color.dds");
    write_text(project_root / "data/levels/one.spfb",
               "{\"font\": {\"uid\": " + quote(bold) + "}, \"style\": \"app:/data/ui/hud.rcss\"}");
    write_text(project_root / "data/ui/hud.rcss", "@font-face { src: \"engine:/data/fonts/Inter/static/Inter-Black.ttf\"; }");
    write_text(project_root / "data/scripts/game.cs", "class Game { string font = \"engine:/data/fonts/Inter/static/Inter-Light\"; }");
    write_text(engine_root / "data/ui/splash.rhtml", "<rml><head><link type=\"text/rcss\" href=\"splash.rcss\"/></head></rml>");
    write_text(engine_root / "data/scripts/SampleHud.cs",
               "class SampleHud { const string Layout = \"engine:/data/ui/sample_hud.rhtml\"; }");
    write_text(engine_root / "compiled/fonts/Inter/static/Inter-Thin.ttf.asset", std::string(COMPILED_FONT_SIZE, 'f'));
    return project;
}

void test_walk_engine(const fs::path& root)
{
    std::printf("test_walk_engine\n");
    using reason = deployment::content_reason;
    const auto project = make_engine_walk_project(root);
    const auto found = deployment::detect_content(project.source, {}, project.startup_scene);
    check(is_used_by(found, "engine:/data/textures/default_color.dds", reason::engine, ""),
          "an engine asset outside the folders the walk decides always ships");
    check(is_used_by(found, unravel::UI_FONT_KEYS[0], reason::engine, "") &&
              is_used_by(found, unravel::TEXT_DEFAULT_FONT_KEY, reason::engine, "") &&
              is_used_by(found, unravel::SPLASH_DOCUMENT_KEY, reason::engine, ""),
          "the fonts and documents the engine loads always ship");
    check(is_used_by(found, "engine:/data/ui/splash.rcss", reason::referenced, unravel::SPLASH_DOCUMENT_KEY),
          "an engine document's relative link stays in the engine");
    check(is_used_by(found, "engine:/data/fonts/Inter/static/Inter-Bold.ttf", reason::referenced, "app:/data/levels/one.spfb"),
          "an engine font a scene holds by uid ships");
    check(is_used_by(found, "engine:/data/fonts/Inter/static/Inter-Black.ttf", reason::referenced, "app:/data/ui/hud.rcss"),
          "an engine font a project style sheet names ships");
    check(is_used_by(found, "engine:/data/fonts/Inter/static/Inter-Light.ttf", reason::named_in_script, "app:/data/scripts/game.cs"),
          "an engine font a project script names ships");
    check(is_used_by(found, "engine:/data/ui/sample_hud.rhtml", reason::named_in_script, "engine:/data/scripts/SampleHud.cs"),
          "an engine document an engine script names ships");
    check(is_unused(found, "engine:/data/fonts/Inter/static/Inter-Thin.ttf") && is_unused(found, "engine:/data/ui/template.rhtml"),
          "engine fonts and UI files nothing uses are unused");
    const auto* thin = found.find("engine:/data/fonts/Inter/static/Inter-Thin.ttf");
    check(thin != nullptr && thin->size == COMPILED_FONT_SIZE, "an engine asset's size is its compiled file's in the engine folder");
    deploy_settings params;
    params.deploy_only_used_assets = true;
    const auto left_out = deployment::get_left_out_keys(params, found);
    check(left_out.contains(fs::make_path_key("engine:/data/fonts/Inter/static/Inter-Thin.ttf")) &&
              !left_out.contains(fs::make_path_key("engine:/data/fonts/Inter/static/Inter-Black.ttf")) &&
              !left_out.contains(fs::make_path_key("engine:/data/textures/default_color.dds")),
          "only used assets leave the unused engine fonts out");
    deployment::content_tree tree;
    tree.build(found);
    tree.update(params, {});
    std::vector<std::size_t> rows;
    tree.collect_rows([](const deployment::tree_node&) { return false; }, rows);
    const auto engine_count = std::size_t(std::count_if(found.entries.begin(),
                                                        found.entries.end(),
                                                        [](const deployment::content_entry& entry)
                                                        {
                                                            return entry.key.rfind("engine:/data/", 0) == 0;
                                                        }));
    const auto& engine_row = tree.get_nodes()[rows.back()];
    check(engine_row.name == "Engine" && engine_row.key == "engine:/data" && engine_row.asset_count == engine_count &&
              engine_row.shipped_count + 2 == engine_count && tree.get_root().asset_count == 2 + engine_count,
          "the content tree lists the engine's assets under a last top row, the unused fonts and UI files left out");
}

/// What the engine walk does at its edges: links out of the engine, rules on engine keys, paths scripts build.
void test_walk_engine_edges(const fs::path& root)
{
    std::printf("test_walk_engine_edges\n");
    using reason = deployment::content_reason;
    auto project = make_engine_walk_project(root);
    const auto engine_root = root / "engine";
    add_asset(project.source.database, make_uid("30000000-0000-4000-8000-000000000010"), "app:/data/ui/lost.rcss");
    write_text(engine_root / "data/ui/splash.rhtml",
               "<rml><head><link type=\"text/rcss\" href=\"splash.rcss\"/>"
               "<link type=\"text/rcss\" href=\"../../../project/data/ui/lost.rcss\"/></head></rml>");
    write_text(engine_root / "data/scripts/SampleHud.cs",
               "class SampleHud { const string Layout = \"engine:/data/ui/sample_hud.rhtml\";\n"
               "string Texture(string name) => $\"engine:/data/textures/{name}.dds\";\n"
               "string Font(string style) => $\"engine:/data/fonts/Inter/static/{style}.ttf\"; }");
    const auto found = deployment::detect_content(project.source, {}, project.startup_scene);
    check(is_unused(found, "app:/data/ui/lost.rcss") &&
              is_used_by(found, "engine:/data/ui/splash.rcss", reason::referenced, unravel::SPLASH_DOCUMENT_KEY),
          "an engine document's relative link that leaves the engine is not followed");
    const bool names_textures = std::any_of(found.named_folders.begin(),
                                            found.named_folders.end(),
                                            [](const deployment::named_folder& named)
                                            {
                                                return named.key == "engine:/data/textures";
                                            });
    check(!names_textures, "a script building paths in an engine folder that always ships raises no note");
    check(is_named_by(found, "engine:/data/fonts/Inter/static", "engine:/data/scripts/SampleHud.cs"),
          "in one the walk decides, it does");
    deploy_settings params;
    params.deploy_only_used_assets = true;
    params.deploy_excluded = {"engine:/data/textures/default_color.dds"};
    params.deploy_included = {"engine:/data/fonts/Inter/static/Inter-Thin.ttf"};
    const auto left_out = deployment::get_left_out_keys(params, found);
    check(left_out.contains(fs::make_path_key("engine:/data/textures/default_color.dds")) &&
              !left_out.contains(fs::make_path_key("engine:/data/fonts/Inter/static/Inter-Thin.ttf")),
          "rules reach engine assets: an exclude leaves out one that always ships, an include ships an unused font");
}

/// The keys the engine loads by path are files of engine_data, in the folders the walk decides.
void test_engine_loaded_keys()
{
    std::printf("test_engine_loaded_keys\n");
#if !defined(ENGINE_DATA_DIR)
    std::printf("  SKIP: the engine_data location is not configured\n");
#else
    const fs::path engine_data = ENGINE_DATA_DIR;
    const std::string engine_root = "engine:/";
    for(const char* key : unravel::ENGINE_LOADED_KEYS)
    {
        const std::string text = key;
        check(text.compare(0, engine_root.size(), engine_root) == 0 &&
                  fs::exists(engine_data / fs::from_utf8_string(text.substr(engine_root.size()))),
              text + " is a file of engine_data");
        const bool is_decided = std::any_of(unravel::ENGINE_FOLDERS_SHIPPED_WHEN_USED.begin(),
                                            unravel::ENGINE_FOLDERS_SHIPPED_WHEN_USED.end(),
                                            [&text](const char* folder)
                                            {
                                                return deployment::is_key_under(text, folder);
                                            });
        check(is_decided, text + " lies in a folder the walk decides, which is why it is listed");
    }
#endif
}

void test_tree(const deployment::content& found)
{
    std::printf("test_tree\n");
    deploy_settings params;
    params.deploy_only_used_assets = true;
    deployment::content_tree tree;
    tree.build(found);
    deployment::tree_view view;
    tree.update(params, view);
    const auto& root = tree.get_root();
    const auto shipped = found.entries.size() - deployment::get_left_out_keys(params, found).size();
    check(root.asset_count == found.entries.size() && root.shipped_count == shipped, "the root counts every asset");
    std::vector<std::size_t> rows;
    const auto get_names = [&]()
    {
        std::vector<std::string> names;
        for(const auto row : rows)
        {
            names.push_back(tree.get_nodes()[row].name);
        }
        return names;
    };
    tree.collect_rows([](const deployment::tree_node&) { return false; }, rows);
    check(get_names() == std::vector<std::string>{"levels", "models", "music", "textures", "ui"},
          "closed, only the top folders show, by name");
    tree.collect_rows([](const deployment::tree_node& node) { return node.key == "app:/data/ui"; }, rows);
    check(get_names() == std::vector<std::string>{"levels", "models", "music", "textures", "ui", "hud.rcss", "hud.rhtml"},
          "an open folder's assets follow it");
    view.search = "WALL";
    tree.update(params, view);
    tree.collect_rows([](const deployment::tree_node&) { return false; }, rows);
    check(get_names() == std::vector<std::string>{"textures", "wall.png"}, "a search shows the matches inside their folders");
    view.search.clear();
    view.filter = deployment::content_filter::left_out;
    tree.update(params, view);
    tree.collect_rows([](const deployment::tree_node&) { return false; }, rows);
    check(!contains(get_names(), "ui") && contains(get_names(), "textures"), "the left out filter hides folders that all ship");
    check(tree.get_left_out_used_count() == 0, "nothing used is left out");
    params.deploy_excluded = {"app:/data/textures/wall.png"};
    tree.update(params, view);
    check(tree.get_left_out_used_count() == 1, "an excluded asset the scenes use is counted");
    check(tree.get_root().has_rule_inside, "a rule inside marks its folders");
}

//-----------------------------------------------------------------------------
// files
//-----------------------------------------------------------------------------

void test_file_classification(const fs::path& root)
{
    std::printf("test_file_classification\n");
    check(deployment::is_asset_key<unravel::script>("app:/data/scripts/game.cs") &&
              !deployment::is_asset_key<unravel::script>("app:/data/a.png"),
          "scripts are told by their extension");
    const auto compiled = deployment::get_compiled_paths("app:/data/a/b.png", root);
    check(compiled == std::vector<fs::path>{root / "compiled/a/b.png.asset"}, "an asset compiles to one file");
    check(deployment::get_compiled_paths("app:/settings/settings.cfg", root).empty(), "a key outside the data folder has none");
    check(deployment::get_compiled_paths("app:/data/scripts/game.cs", root).empty(), "a script has none");
    check(!deployment::get_compiled_paths("app:/data/shaders/fs_a.eshader", root).empty(), "a shader compiles once per renderer");
    check(deployment::get_source_key("app", "a/b.png.asset") == "app:/data/a/b.png", "a compiled file names its source");
    write_text(root / "libs/game.dll", "dll");
    write_text(root / "libs/game.xml", "docs");
    write_text(root / "libs/other.xml", "data");
    check(deployment::is_debug_companion_file(root / "libs/game.pdb"), "symbols are debug companions");
    check(deployment::is_debug_companion_file(root / "libs/game.xml"), "docs beside a dll are debug companions");
    check(!deployment::is_debug_companion_file(root / "libs/other.xml"), "other xml files are not");
    check(deployment::is_loose_only_file(root / "libs/game.dll"), "libraries stay loose");
    check(!deployment::is_shipped_compiled_file(root / "x.manifest", true), "manifests never ship");
    check(!deployment::is_shipped_compiled_file(root / "game.cs.asset", true), "compiled scripts never ship");
    check(!deployment::is_shipped_compiled_file(root / "libs/game.pdb", false) &&
              deployment::is_shipped_compiled_file(root / "libs/game.pdb", true),
          "symbols ship only when asked for");
}

void test_shader_variants(const fs::path& root)
{
    std::printf("test_shader_variants\n");
    const std::set<std::string> vulkan{".spirv"};
    const auto paths = deployment::get_compiled_paths("app:/data/shaders/fs_a.eshader", root, vulkan);
    check(paths.size() == 1 && paths.front().extension() == ".spirv", "a target's shader has its renderer's variant only");
    check(deployment::is_other_shader_variant(root / "compiled/shaders/fs_a.eshader.asset.dxbc", vulkan),
          "another renderer's variant is told apart");
    check(!deployment::is_other_shader_variant(root / "compiled/shaders/fs_a.eshader.asset.spirv", vulkan), "the target's variant ships");
    check(!deployment::is_other_shader_variant(root / "compiled/a.png.asset", vulkan), "other compiled files are no variant");
    check(!deployment::is_other_shader_variant(root / "compiled/shaders/fs_a.eshader.asset.dxbc", {}), "no target ships every variant");
}

void test_cooked_shader_variants()
{
    std::printf("test_cooked_shader_variants\n");
    deploy_settings params;
    params.deploy_target = unravel::deploy_platform::web;
    const std::set<std::string> webgpu{".wgsl"};
    const std::set<std::string> webgl2{".essl"};
    const std::set<std::string> both{".wgsl", ".essl"};
    check(deployment::get_shader_variants(params) == webgpu, "a web build ships WGSL shaders only by default");
    params.web.graphics = unravel::web_graphics::webgpu_then_webgl2;
    check(deployment::get_shader_variants(params) == both, "WebGPU, then WebGL 2 ships WGSL and GLSL ES");
    params.web.graphics = unravel::web_graphics::webgl2;
    check(deployment::get_shader_variants(params) == webgl2, "WebGL 2 only ships GLSL ES only");
    check(deployment::get_cooked_shader_variants(both) == both,
          "the editor compiles neither WGSL nor GLSL ES for itself, so a deploy cooks both");
    check(deployment::get_cooked_shader_variants(gfx::get_renderer_platform_supported_filename_extensions()).empty(),
          "the renderers the editor compiles for are never cooked");
    params.deploy_target = unravel::deploy_platform::desktop;
    check(deployment::get_shader_variants(params).empty(), "a desktop build ships every variant the editor compiled");
}

void test_web_shader_sets()
{
    std::printf("test_web_shader_sets\n");
    deploy_settings params;
    params.deploy_target = unravel::deploy_platform::web;
    params.deploy_packed_assets = true;
    check(deployment::get_web_shader_sets(params).empty(), "a WebGPU only build ships its shaders in the protocols' packs");
    params.web.graphics = unravel::web_graphics::webgpu_then_webgl2;
    const auto sets = deployment::get_web_shader_sets(params);
    check(sets.size() == 2 && sets[0].name == "webgpu" && sets[0].renderer_extension == ".wgsl" && sets[1].name == "webgl2" &&
              sets[1].renderer_extension == ".essl",
          "a packed build for both APIs ships each API's shaders in a set of its own, named as the page names the API");
    params.deploy_packed_assets = false;
    check(deployment::get_web_shader_sets(params).empty(), "a loose build ships both, as it ships every texture set");
    params.deploy_packed_assets = true;
    params.web.graphics = unravel::web_graphics::webgl2;
    check(deployment::get_web_shader_sets(params).empty(), "a WebGL 2 only build has one set");
    params.deploy_target = unravel::deploy_platform::desktop;
    params.web.graphics = unravel::web_graphics::webgpu_then_webgl2;
    check(deployment::get_web_shader_sets(params).empty(), "other platforms have none");
}

void test_shader_cook_plan(const fs::path& root)
{
    std::printf("test_shader_cook_plan\n");
    asset_database::database_t engine;
    add_asset(engine, make_uid("50000000-0000-4000-8000-000000000001"), "engine:/data/shaders/fs_a.eshader");
    add_asset(engine, make_uid("50000000-0000-4000-8000-000000000002"), "engine:/data/textures/noise.png");
    asset_database::database_t app;
    add_asset(app, make_uid("50000000-0000-4000-8000-000000000003"), "app:/data/shaders/fs_b.eshader");
    add_asset(app, make_uid("50000000-0000-4000-8000-000000000004"), "app:/data/shaders/fs_c.eshader");
    const deployment::left_out_assets left_out{"app:/data/shaders/fs_c.eshader"};
    deploy_settings params;
    params.profile = "Web";
    params.deploy_target = unravel::deploy_platform::web;
    const auto plan = deployment::make_shader_cook_plan(root, params, engine, app, left_out);
    check(plan.has_work() && plan.renderers == std::set<std::string>{".wgsl"} && plan.protocols.size() == 2,
          "a web deploy cooks WGSL for the engine and the project");
    check(plan.protocols.size() == 2 && plan.protocols[0].variants == deployment::get_profile_shaders_folder(root, "Web", "engine") &&
              plan.protocols[0].shaders.shipped == std::vector<std::string>{"engine:/data/shaders/fs_a.eshader"} &&
              plan.protocols[0].shaders.left_out.empty(),
          "the engine's shaders ship whole into the profile's shader cache");
    check(plan.protocols.size() == 2 && plan.protocols[1].shaders.shipped == std::vector<std::string>{"app:/data/shaders/fs_b.eshader"} &&
              plan.protocols[1].shaders.left_out == std::vector<std::string>{"app:/data/shaders/fs_c.eshader"},
          "the project's shaders the content selection leaves out are not cooked");
    params.deploy_target = unravel::deploy_platform::desktop;
    const auto desktop = deployment::make_shader_cook_plan(root, params, engine, app, left_out);
    check(!desktop.has_work() && desktop.protocols.empty(), "a desktop deploy cooks no shader");
}

//-----------------------------------------------------------------------------
// Android
//-----------------------------------------------------------------------------

void test_profile_locations()
{
    std::printf("test_profile_locations\n");
    deploy_settings params;
    check(params.deploy_location == fs::path(unravel::DEFAULT_DEPLOY_LOCATION), "a project starts with .deploy");
    params.profile = "Desktop";
    params.deploy_location.clear();
    check(unravel::get_output_location(params).empty(), "without a deploy folder there is no profile folder");
    params.deploy_location = fs::path("/builds/game");
    check(unravel::get_output_location(params) == fs::path("/builds/game/Desktop"), "a profile builds into its own folder");
    params.profile = "Android Low";
    params.deploy_target = unravel::deploy_platform::android;
    check(unravel::get_output_location(params) == fs::path("/builds/game/Android Low"), "whatever its platform");
    params.profile.clear();
    check(unravel::get_output_location(params).empty(), "without a profile a deploy has no folder, never the deploy folder itself");
    check(unravel::get_profile_location(params, "Demo") == fs::path("/builds/game/Demo"), "any profile's folder is named after it");
}

void test_deploy_locations(const fs::path& root)
{
    std::printf("test_deploy_locations\n");
    const auto project = root / "project";
    fs::error_code ec;
    fs::create_directories(project, ec);
    check(fs::resolve_relative_to(".deploy", project) == (project / ".deploy").lexically_normal(),
          "a relative location lies in the project");
    check(fs::resolve_relative_to("../builds", project) == (root / "builds").lexically_normal(),
          "a relative location may lead out of the project");
    check(fs::resolve_relative_to(root / "out", project) == root / "out", "an absolute location stays as it is");
    check(fs::make_relative_if_inside(project / "builds", project) == fs::path("builds"), "a folder in the project is kept relative");
    check(fs::make_relative_if_inside(root / "out", project) == root / "out", "a folder outside the project stays absolute");
    check(deployment::get_managed_project_folder(project / "data" / "out", project) == "data" &&
              deployment::get_managed_project_folder(project / "releases", project) == "releases" &&
              deployment::get_managed_project_folder(project / "profiles" / "Android", project) == "profiles" &&
              deployment::get_managed_project_folder(project / "cache" / "profiles", project) == "cache" &&
              deployment::get_managed_project_folder(project / "temp" / "bin", project) == "temp",
          "the folders the editor manages are named");
    check(deployment::get_managed_project_folder(project / ".deploy", project).empty() &&
              deployment::get_managed_project_folder(project / "builds" / "data", project).empty(),
          "other folders of the project are free");
    check(deployment::get_managed_project_folder(root / "data", project).empty() &&
              deployment::get_managed_project_folder(project, project).empty(),
          "neither a folder outside the project nor the project itself is a managed folder");
}

void test_application_ids()
{
    std::printf("test_application_ids\n");
    check(deployment::make_default_application_id("Unravel", "Sample 2048") == "com.unravel.sample_2048",
          "company and product become lowercase parts");
    check(deployment::make_default_application_id("", "") == "com.unravel.game", "empty names fall back");
    check(deployment::make_default_application_id("My Co.", "2048!") == "com.my_co.app2048",
          "a part starting with a digit gets letters in front, and no '_' at the edges");
    check(deployment::check_application_id("com.company.game").empty(), "a valid id passes");
    check(!deployment::check_application_id("").empty(), "an empty id is refused");
    check(!deployment::check_application_id("game").empty(), "one part is refused");
    check(!deployment::check_application_id("com.2game").empty(), "a part starting with a digit is refused");
    check(!deployment::check_application_id("com..game").empty(), "an empty part is refused");
    check(!deployment::check_application_id("com.game.").empty(), "a trailing '.' is refused");
    check(!deployment::check_application_id("com.ga-me").empty(), "other characters are refused");
}

void test_android_devices()
{
    std::printf("test_android_devices\n");
    const std::string output = "* daemon not running; starting now at tcp:5037\n"
                               "* daemon started successfully\n"
                               "List of devices attached\n"
                               "R3GL508T4SY            device usb:1-1 product:e3q model:SM_S948B device:e3q transport_id:1\r\n"
                               "adb-R3GL-x._adb-tls-connect._tcp device product:e3q model:SM_S948B device:e3q transport_id:2\n"
                               "emulator-5554          unauthorized transport_id:3\n"
                               "\n";
    const auto devices = deployment::parse_android_devices(output);
    check(devices.size() == 3, "every device line is read, the server's messages are not");
    check(devices.size() == 3 && devices[0].serial == "R3GL508T4SY" && devices[0].is_ready() && devices[0].model == "SM S948B",
          "a USB device with its model");
    check(devices.size() == 3 && devices[1].serial == "adb-R3GL-x._adb-tls-connect._tcp", "a wireless debugging device");
    check(devices.size() == 3 && devices[2].state == "unauthorized" && !devices[2].is_ready() && devices[2].model.empty(),
          "a device that does not allow this computer yet");
    check(devices.size() == 3 && devices[0].get_label() == "SM S948B - USB" && devices[1].get_label() == "SM S948B - Wi-Fi" &&
              devices[2].get_label() == "emulator-5554 - Emulator, unauthorized",
          "before a device reports its name, the label names its model, else its serial, then its connection and state");
    const std::string spaced = "adb-R5CR11EH5XE-jIeKII (2)._adb-tls-connect._tcp device product:p3sxeea model:SM_G998B "
                               "device:p3s transport_id:4\n"
                               "usb-0001 no permissions (missing udev rules?); see [http://developer.android.com]\n";
    const auto more = deployment::parse_android_devices(spaced);
    check(more.size() == 2 && more[0].serial == "adb-R5CR11EH5XE-jIeKII (2)._adb-tls-connect._tcp" && more[0].is_ready() &&
              more[0].model == "SM G998B",
          "a serial with a space ends at the state word");
    check(more.size() == 2 && more[1].serial == "usb-0001" && more[1].state == "no permissions", "the two-word state is read");
}

void test_android_device_properties()
{
    std::printf("test_android_device_properties\n");
    check(deployment::get_android_device_properties_command().find("settings get global device_name") != std::string::npos,
          "the properties command asks for the name the owner gave the phone");
    deployment::android_device device;
    device.serial = "adb-R3GL508T4SY-pTACbH._adb-tls-connect._tcp";
    device.state = "device";
    device.model = "SM S948B";
    deployment::read_android_device_properties("serial=R3GL508T4SY\r\nmodel=SM-S948B\r\nandroid=17\r\nname=Nikolay's S26 Ultra\r\n", device);
    check(device.hardware_serial == "R3GL508T4SY" && device.model == "SM-S948B" && device.android_version == "17" &&
              device.device_name == "Nikolay's S26 Ultra",
          "each name=value line fills its property, line ends and an apostrophe in the name included");
    check(device.get_name() == "Nikolay's S26 Ultra" && device.get_details() == "SM-S948B, Android 17, Wi-Fi",
          "the name comes first, the model, Android version and connection after it");
    check(device.get_label() == "Nikolay's S26 Ultra - SM-S948B, Android 17, Wi-Fi", "the label joins them");

    deployment::android_device unnamed;
    unnamed.serial = "R5CR11EH5XE";
    unnamed.state = "device";
    unnamed.model = "SM G998B";
    deployment::read_android_device_properties("serial=R5CR11EH5XE\nmodel=SM-G998B\nandroid=\nname=null\nsh: weird line\n", unnamed);
    check(unnamed.device_name.empty() && unnamed.android_version.empty() && unnamed.model == "SM-G998B",
          "an unset name ('null'), an empty value and a line without '=' leave the property as it was");
    check(unnamed.get_label() == "SM-G998B - USB", "without a name the model leads, and it is not repeated in the details");

    deployment::android_device named_hash;
    named_hash.serial = "192.168.0.7:5555";
    named_hash.state = "device";
    deployment::read_android_device_properties("name=Lab ## phone = 2\n", named_hash);
    check(named_hash.device_name == "Lab ## phone = 2", "a name keeps every character after the first '=', '#' included");
}

void test_logcat_lines()
{
    std::printf("test_logcat_lines\n");
    deployment::logcat_line line;
    check(deployment::parse_logcat_line("10-18 20:44:29.426 25848 25903 I spdlog  : [info] ready: yes", line) &&
              line.pid == "25848" && line.level == 'I' && line.tag == "spdlog" && line.message == "[info] ready: yes",
          "a threadtime line gives its process, level, tag and message");
    check(deployment::parse_logcat_line("10-18 20:44:30.001 30000 30000 F DEBUG   :   #00 pc 0000 /lib/libmain.so", line) &&
              line.tag == "DEBUG" && line.message == "  #00 pc 0000 /lib/libmain.so",
          "a crash report line keeps its indentation");
    check(!deployment::parse_logcat_line("--------- beginning of main", line), "markers are no log lines");
}

void test_game_log_lines()
{
    std::printf("test_game_log_lines\n");
    const auto is_game_line = [](const std::string& text)
    {
        deployment::logcat_line line;
        return deployment::parse_logcat_line(text, line) && deployment::is_game_log_line(line);
    };
    check(is_game_line("10-20 22:16:20.700 23862 23899 I spdlog  : [info] ready"), "the engine's log is the game's");
    check(is_game_line("10-20 22:16:20.700 23862 23899 V SDL     : nativeResume()") &&
              is_game_line("10-20 22:16:20.700 23862 23899 E SDL/APP : failed") &&
              is_game_line("10-20 22:16:20.700 23862 23899 W SDLAudio: no device"),
          "every SDL tag is the game's");
    check(is_game_line("10-20 22:16:20.766 23862 23917 W openal  : [ALSOFT] (WW) D-Bus not supported") &&
              is_game_line("10-20 22:16:20.599 23862 23899 D vulkan  : searching for layers"),
          "the audio and Vulkan libraries are the game's");
    check(is_game_line("10-20 22:16:21.000 23862 23862 E AndroidRuntime: FATAL EXCEPTION: main") &&
              is_game_line("10-20 22:16:21.000 23862 23899 F libc    : Fatal signal 11 (SIGSEGV)"),
          "a Java exception and any fatal line count as the game's");
    check(!is_game_line("10-20 22:06:26.203 23862 23880 E gralloc4: ERROR: Format allocation info not found for format: 38") &&
              !is_game_line("10-20 22:06:26.203 23862 23880 E AHardwareBuffer: GraphicBuffer(w=4, h=4, lc=1) failed") &&
              !is_game_line("10-20 22:06:26.205 23862 23862 I VRI[UnravelActivity]@f4e723e: onWindowFocusChanged(): true"),
          "the system's own messages are the device system's");
}

void test_device_log_messages()
{
    std::printf("test_device_log_messages\n");
    const auto get_message = [](const std::string& text)
    {
        deployment::logcat_line line;
        return deployment::parse_logcat_line(text, line) ? deployment::get_device_log_message(line) : std::string("?");
    };
    check(get_message("10-20 23:02:48.100 23862 23899 I spdlog  : [2026-10-20 23:02:48.099] [info] [Log] Loaded font face.") ==
              "Loaded font face.",
          "an engine line loses the time, level and logger the Console shows anyway");
    check(get_message("10-20 23:02:48.100 23862 23899 I spdlog  : [2026-10-20 23:02:48.099] [info] [Log] [UI] Ready.") ==
              "[UI] Ready.",
          "brackets in the engine's own text stay");
    check(get_message("10-20 23:02:48.100 23862 23899 I spdlog  : plain text") == "plain text",
          "an engine line without the prefix stays as it is");
    check(get_message("10-20 22:16:20.766 23862 23917 W openal  : [ALSOFT] (WW) D-Bus not supported") ==
              "[ALSOFT] (WW) D-Bus not supported",
          "other libraries' lines stay as they are");
}

auto make_device(const std::string& serial, const std::string& hardware_serial) -> deployment::android_device
{
    deployment::android_device device;
    device.serial = serial;
    device.state = hardware_serial.empty() ? "unauthorized" : "device";
    device.hardware_serial = hardware_serial;
    return device;
}

void test_merged_devices()
{
    std::printf("test_merged_devices\n");
    const std::string repeated = "adb-R5CR11EH5XE-jIeKII (2)._adb-tls-connect._tcp";
    const std::string wireless = "adb-R5CR11EH5XE-jIeKII._adb-tls-connect._tcp";
    auto merged = deployment::merge_android_devices({make_device(repeated, "R5CR11EH5XE"), make_device(wireless, "R5CR11EH5XE")});
    check(merged.size() == 1 && merged[0].serial == wireless && merged[0].is_named(repeated) && merged[0].is_wireless(),
          "a phone announced twice over wireless debugging is one entry, under its plain name");
    merged = deployment::merge_android_devices(
        {make_device(wireless, "R5CR11EH5XE"), make_device("R5CR11EH5XE", "R5CR11EH5XE"), make_device("192.168.1.7:5555", "R5CR11EH5XE")});
    check(merged.size() == 1 && merged[0].serial == "R5CR11EH5XE" && !merged[0].is_wireless() && merged[0].aliases.size() == 2,
          "USB wins over wireless, and the other connections become aliases");
    merged = deployment::merge_android_devices(
        {make_device("R3GL508T4SY", "R3GL508T4SY"), make_device(wireless, "R5CR11EH5XE"), make_device("emulator-5554", "")});
    check(merged.size() == 3 && !merged[1].is_named("R3GL508T4SY") && merged[2].aliases.empty(),
          "other phones and devices without a hardware serial stay apart");
}

void test_wireless_debugging_services()
{
    std::printf("test_wireless_debugging_services\n");
    const std::string output = "List of discovered mdns services\r\n"
                               "adb-R5CR11EH5XE-jIeKII (2)\t_adb-tls-connect._tcp\t192.168.0.119:44619\r\n"
                               "adb-R5CR11EH5XE-jIeKII\t_adb-tls-connect._tcp\t192.168.0.119:35351\r\n"
                               "adb-R5CR11EH5XE-jIeKII\t_adb-tls-pairing._tcp\t192.168.0.119:40001\r\n"
                               "\r\n";
    const auto services = deployment::parse_wireless_debugging_services(output);
    check(services.size() == 2, "the connect services are read, the header and the pairing service are not");
    check(services.size() == 2 && services[0].name == "adb-R5CR11EH5XE-jIeKII (2)" && services[0].address == "192.168.0.119:44619",
          "a name announced again keeps its space, the address loses the line end");
    check(services.size() == 2 && services[1].name == "adb-R5CR11EH5XE-jIeKII" && services[1].address == "192.168.0.119:35351",
          "the plain name with its own address");
    check(deployment::parse_wireless_debugging_services("List of discovered mdns services\n\n").empty(), "no service, none read");
}

void write_android_template(const fs::path& root, const std::string& version, const std::string& configuration)
{
    write_text(root / "template.stamp",
               "engine_version=" + version + "\nconfiguration=" + configuration +
                   "\nabi=arm64-v8a\ndotnet_runtime=10.0.13\nsymbols=/build/bin\n");
    write_text(root / "lib/arm64-v8a/libmain.so", "elf");
    write_text(root / "symbols/arm64-v8a/libmain.so.sym", "elf");
    write_text(root / "project/gradle/wrapper/gradle-wrapper.jar", "jar");
    write_text(root / "sdl/java/org/libsdl/app/SDLActivity.java", "java");
    write_text(root / "dotnet/native/libcoreclr.so", "elf");
}

void test_android_template(const fs::path& root)
{
    std::printf("test_android_template\n");
    check(!deployment::find_android_template(root / "none", "v1", "Release").is_found(), "a missing template is reported");
    write_android_template(root / "template", "v1", "Release");
    const auto found = deployment::find_android_template(root / "template", "v1", "Release");
    check(found.is_found() && found.found.abi == "arm64-v8a" && found.found.dotnet_runtime == "10.0.13" &&
              found.found.symbols_dir == fs::path("/build/bin"),
          "a complete template of this build is found with its stamp");
    check(found.found.get_player_dir() == root / "template/lib/arm64-v8a", "the player sits under lib/<abi>");
    check(!deployment::find_android_template(root / "template", "v2", "Release").is_found(), "another engine version is refused");
    check(!deployment::find_android_template(root / "template", "v1", "Debug").is_found(), "another configuration is refused");
    fs::error_code ec;
    fs::remove(root / "template/symbols/arm64-v8a/libmain.so.sym", ec);
    check(!deployment::find_android_template(root / "template", "v1", "Release").is_found(), "a template without its symbol table is refused");
    write_text(root / "template/symbols/arm64-v8a/libmain.so.sym", "elf");
    fs::remove(root / "template/lib/arm64-v8a/libmain.so", ec);
    check(!deployment::find_android_template(root / "template", "v1", "Release").is_found(), "an incomplete template is refused");
}

void test_player_version(const fs::path& root)
{
    std::printf("test_player_version\n");
    const auto player = root / "unravel-player.exe";
    check(!deployment::get_player_version_problem(player, "v1").empty(), "a player without a stamp is refused");
    write_text(root / "unravel-player.exe.stamp", "engine_version=v1\n");
    check(deployment::get_player_version_problem(player, "v1").empty(), "a player of the editor's version ships");
    check(deployment::get_player_version_problem(player, "v2").find("v1") != std::string::npos,
          "a player of another version is refused, naming its version");
    const deployment::player_wording wording{"The test player", "Mend it."};
    check(deployment::get_version_problem("", "v1", wording) == "The test player does not say which engine version built it. Mend it.",
          "a stamp without a version is named so");
    check(deployment::get_version_problem("v1", "v2", wording) ==
              "The test player comes from engine version v1, and the editor is v2. Mend it.",
          "another version names both");
}

//-----------------------------------------------------------------------------
// Web
//-----------------------------------------------------------------------------

void write_web_template(const fs::path& root, const std::string& engine_version)
{
    write_text(root / "template.stamp", "engine_version=" + engine_version + "\n");
    write_text(root / "wwwroot/index.html", "page");
    write_text(root / "wwwroot/main.js", "start");
    write_text(root / "wwwroot/_framework/dotnet.js", "loader");
}

void test_web_template(const fs::path& root)
{
    std::printf("test_web_template\n");
    check(!deployment::find_web_template(root / "none", "v1").is_found(), "a missing browser player is reported");
    write_web_template(root / "template", "v1");
    const auto found = deployment::find_web_template(root / "template", "v1");
    check(found.is_found() && found.found.engine_version == "v1" && found.found.get_site_dir() == root / "template/wwwroot",
          "a complete player of this version is found with its site");
    const auto other = deployment::find_web_template(root / "template", "v2");
    check(!other.is_found() && other.error.find("v1") != std::string::npos, "a player of another version is refused, naming its version");
    fs::error_code ec;
    fs::remove(root / "template/wwwroot/_framework/dotnet.js", ec);
    check(!deployment::find_web_template(root / "template", "v1").is_found(), "a player without its runtime is refused");
}

/// A chunk as a manifest lists it: ["<name>", size, check].
auto make_chunk_json(const std::string& bytes) -> std::string
{
    return "[\"" + fs::hash_bytes_fingerprint(bytes.data(), bytes.size()) + deployment::WEB_CHUNK_EXTENSION + "\", " +
           std::to_string(bytes.size()) + ", " + std::to_string(fs::hash_bytes_check(bytes.data(), bytes.size())) + "]";
}

void test_web_chunk_cuts()
{
    std::printf("test_web_chunk_cuts\n");
    constexpr std::uint64_t mib = 1024 * 1024;
    constexpr std::uint64_t min_size = deployment::WEB_CHUNK_MIN_SIZE;
    constexpr std::uint64_t max_size = deployment::WEB_CHUNK_MAX_SIZE;
    using cuts = std::vector<std::uint64_t>;
    check(deployment::find_web_chunk_cuts(0, {}).empty() && deployment::find_web_chunk_cuts(max_size, {}).empty(),
          "a file no larger than a chunk is one chunk");
    check(deployment::find_web_chunk_cuts(2 * max_size + 1, {}) == cuts{max_size, 2 * max_size},
          "a file without units is cut in even steps, the rest last");
    // Keys: an even one lets a chunk start at its unit, an odd one never does.
    constexpr std::uint64_t picked = 0;
    constexpr std::uint64_t passed = 1;
    const std::vector<deployment::web_chunk_unit> few_units{{0, mib, picked}, {mib, mib, picked}, {2 * mib, mib, picked},
                                                         {3 * mib, mib, passed}, {4 * mib, mib, picked}};
    check(deployment::find_web_chunk_cuts(5 * mib, few_units) == cuts{2 * mib, 4 * mib},
          "a chunk is closed before the first unit the rule picks once it holds the least a chunk may");
    check(min_size == 2 * mib, "(the test's units are sized for that least)");
    std::vector<deployment::web_chunk_unit> unpicked;
    for(std::uint64_t index = 0; index < 20; ++index)
    {
        unpicked.push_back({index * mib, mib, passed});
    }
    check(deployment::find_web_chunk_cuts(20 * mib, unpicked) == cuts{8 * mib, 16 * mib},
          "units the rule never picks fill a chunk to the most it may hold, whole");
    const std::vector<deployment::web_chunk_unit> large{{0, mib, passed}, {mib, 20 * mib, passed}, {21 * mib, mib, passed}};
    check(deployment::find_web_chunk_cuts(22 * mib, large) == cuts{mib, 9 * mib, 17 * mib, 21 * mib},
          "a unit larger than a chunk is cut on its own, in even steps from its start");
    const std::vector<deployment::web_chunk_unit> with_contents{{0, mib, passed}, {mib, mib / 2, passed, true}};
    check(deployment::find_web_chunk_cuts(mib + mib / 2, with_contents) == cuts{mib},
          "a unit that starts a chunk does, however little the chunk before it holds");
    // The same units with one of them grown: only the cuts beside it move.
    auto grown = few_units;
    grown[0].size += 100;
    for(std::size_t index = 1; index < grown.size(); ++index)
    {
        grown[index].offset += 100;
    }
    check(deployment::find_web_chunk_cuts(5 * mib + 100, grown) == cuts{2 * mib + 100, 4 * mib + 100},
          "a unit that grows moves the cuts after it by as much, so the chunks behind it hold the same bytes");
}

void test_web_site(const fs::path& root)
{
    std::printf("test_web_site\n");
    write_web_template(root / "template", "v1");
    deployment::web_template player;
    player.root = root / "template";
    const auto site = root / "site";
    const auto data_root = root / "data_root";
    const std::string pack_text = "engine pack";
    const std::string settings_text = "settings";
    write_text(data_root / "data/engine/content.pak", pack_text);
    write_text(data_root / "data/app/settings/settings.cfg", settings_text);
    write_text(data_root / "data/app/empty.txt", "");
    write_text(site / "_framework/stale.wasm", "older runtime");
    write_text(site / "data/engine/content.pak", "an older editor's whole file");
    write_text(site / deployment::WEB_CHUNKS_FOLDER / "older.bin", "an older build's chunk");
    check(!deployment::get_web_player_problem(player, site).empty(), "a folder without this editor's player cannot take its data");
    deployment::copy_web_player(player, site);
    check(read_text(site / "index.html") == "page" && read_text(site / "_framework/dotnet.js") == "loader",
          "the page and the runtime are copied into the site");
    check(!is_present(site / "_framework/stale.wasm"), "runtime files an older player had are removed");
    check(deployment::get_web_player_problem(player, site).empty(), "the copied player is this editor's");
    // A server's precompressed copy of a runtime file is no download of its own.
    write_text(site / "_framework/dotnet.js.br", "packed");
    const deployment::web_page_info page{"Game \"Two\"", deployment::get_web_user_folder("Studio", "Game")};
    deployment::write_web_data(data_root, site, page);
    const auto build = deployment::get_web_page_build(site);
    check(read_text(site / deployment::WEB_DATA_MANIFEST_NAME) ==
              "{\n  \"format\": 2,\n  \"build\": \"" + build +
                  "\",\n  \"title\": \"Game \\\"Two\\\"\",\n  \"user_folder\": \"/libsdl/Studio/Game\",\n  \"runtime_size\": 6,\n"
                  "  \"graphics\": {\"backends\": [\"webgpu\"], \"fallback_notice\": \"tell\", \"help_link\": \"\"},\n"
                  "  \"chunks\": \"chunks\",\n"
                  "  \"files\": [\n    {\"path\": \"data/app/empty.txt\", \"size\": 0, \"chunks\": []},\n"
                  "    {\"path\": \"data/app/settings/settings.cfg\", \"size\": 8, \"chunks\": [" + make_chunk_json(settings_text) +
                  "]},\n    {\"path\": \"data/engine/content.pak\", \"size\": 11, \"mapped\": true, \"chunks\": [" +
                  make_chunk_json(pack_text) + "]}\n  ]\n}\n",
          "the manifest names its format and the build's page code, the game and its folder, weighs the runtime without "
          "its precompressed copies, gives the graphics policy (WebGPU only by default) and lists the data's files by "
          "path with their sizes and chunks, a pack marked as mapped");
    const auto pack_chunk = fs::hash_bytes_fingerprint(pack_text.data(), pack_text.size()) + deployment::WEB_CHUNK_EXTENSION;
    check(read_text(site / deployment::WEB_CHUNKS_FOLDER / pack_chunk) == pack_text, "a chunk is a file named by its content");
    check(!is_present(site / deployment::WEB_CHUNKS_FOLDER / "older.bin"), "chunks no file of the build has are removed");
    check(is_present(site / "data/engine/content.pak"), "what else the site holds is not the data step's to remove");
    deployment::remove_legacy_web_data(site);
    check(!is_present(site / "data") && is_present(site / "index.html") && is_present(site / deployment::WEB_CHUNKS_FOLDER),
          "the whole files a build of an older editor left are removed on their own, before a project's files are copied");
    check(build.size() == 16 && deployment::get_web_page_build(site) == build, "a site's page code has one name");
    write_text(site / "main.js", "changed page code");
    check(deployment::get_web_page_build(site) != build, "changed page code has another name, so browsers load it anew");
    write_text(site / deployment::WEB_DATA_MANIFEST_NAME, "another manifest");
    write_text(site / "main.js.br", "packed");
    const auto changed_build = deployment::get_web_page_build(site);
    write_text(site / deployment::WEB_DATA_MANIFEST_NAME, "yet another manifest");
    check(deployment::get_web_page_build(site) == changed_build, "the manifest and precompressed copies are no page code");
    write_text(data_root / "data/engine/content.pak", "another engine pack");
    deployment::write_web_data(data_root, site, page);
    check(!is_present(site / deployment::WEB_CHUNKS_FOLDER / pack_chunk) &&
              read_text(site / deployment::WEB_DATA_MANIFEST_NAME).find(pack_chunk) == std::string::npos,
          "a changed file is other chunks, so the page fetches them and the old ones go");
    const std::string accented = "data/app/compiled/men\xc3\xbc.asset";
    write_text(data_root / fs::from_utf8_string(accented), "menu");
    deployment::write_web_data(data_root, site, page);
    check(read_text(site / deployment::WEB_DATA_MANIFEST_NAME).find("{\"path\": \"" + accented + "\", \"size\": 4, \"chunks\": [" +
                                                                    make_chunk_json("menu") + "]}") != std::string::npos,
          "a file named outside ASCII is listed in UTF-8, and its chunk's name is plain");
    deployment::web_manifest sets;
    sets.page = {"Game", "/libsdl/Game", {"bc", "astc"}};
    sets.files = {{"data/app/content_astc.pak", 14, {}, true}, {"data/app/content_bc.pak", 17, {}, true}};
    const auto sets_manifest = deployment::make_web_data_manifest(sets);
    check(sets_manifest.find("\"texture_sets\": [\"bc\", \"astc\"],") != std::string::npos &&
              sets_manifest.find("content_astc.pak\", \"size\": 14, \"mapped\": true, \"texture_set\": \"astc\", \"chunks\": []}") !=
                  std::string::npos &&
              sets_manifest.find("content_bc.pak\", \"size\": 17, \"mapped\": true, \"texture_set\": \"bc\", \"chunks\": []}") !=
                  std::string::npos,
          "with texture sets the manifest lists them, the computers' first, and names each set's pack");
    sets.page.texture_sets.clear();
    check(deployment::make_web_data_manifest(sets).find("texture_set") == std::string::npos, "a build with one set names none");
    unravel::web_deploy_settings web;
    web.graphics = unravel::web_graphics::webgpu_then_webgl2;
    web.fallback_notice = unravel::web_fallback_notice::ask;
    web.help_link = "https://example.com/webgpu";
    deployment::web_manifest shaders;
    shaders.page = {"Game", "/libsdl/Game", {}, {"webgpu", "webgl2"}};
    shaders.page.graphics = deployment::make_web_graphics_policy(web);
    shaders.files = {{"data/engine/content_webgl2.pak", 12, {}, true}, {"data/engine/content_webgpu.pak", 14, {}, true}};
    const auto shaders_manifest = deployment::make_web_data_manifest(shaders);
    check(shaders_manifest.find("  \"graphics\": {\"backends\": [\"webgpu\", \"webgl2\"], \"fallback_notice\": \"ask\", "
                                "\"help_link\": \"https://example.com/webgpu\"},\n") !=
              std::string::npos,
          "the graphics block carries the profile's APIs in the order the page tries them and what it tells the player");
    check(shaders_manifest.find("\"shader_sets\": [\"webgpu\", \"webgl2\"],") != std::string::npos &&
              shaders_manifest.find("content_webgl2.pak\", \"size\": 12, \"mapped\": true, \"shader_set\": \"webgl2\", \"chunks\": []}") !=
                  std::string::npos &&
              shaders_manifest.find("content_webgpu.pak\", \"size\": 14, \"mapped\": true, \"shader_set\": \"webgpu\", \"chunks\": []}") !=
                  std::string::npos,
          "with shader sets the manifest lists them and names each set's pack");
    web.graphics = unravel::web_graphics::webgl2;
    check(deployment::make_web_graphics_policy(web).backends == std::vector<std::string>{"webgl2"},
          "a WebGL 2 only build lists WebGL 2 only");
}

/// A trimmed web build: the runtime files it leaves out are neither copied nor missed, and the page is told of them.
void test_web_trim(const fs::path& root)
{
    std::printf("test_web_trim\n");
    write_web_template(root / "template", "v1");
    for(const auto* name : {"kept.wasm", "kept.wasm.br", "unused.wasm", "unused.wasm.br", "unused.wasm.gz"})
    {
        write_text(root / "template/wwwroot/_framework" / name, name);
    }
    deployment::web_template player;
    player.root = root / "template";
    const auto site = root / "site";
    const std::set<std::string> trimmed{"unused.wasm"};
    deployment::copy_web_player(player, site);
    check(is_present(site / "_framework/unused.wasm") && is_present(site / "_framework/unused.wasm.gz"),
          "a build that trims nothing copies the whole runtime");
    check(deployment::get_web_player_problem(player, site, {}, trimmed).empty(),
          "a site with more than the build downloads has no problem");
    deployment::copy_web_player(player, site, trimmed);
    check(!is_present(site / "_framework/unused.wasm") && !is_present(site / "_framework/unused.wasm.br") &&
              !is_present(site / "_framework/unused.wasm.gz"),
          "a trimmed build removes the files it leaves out, with their precompressed copies");
    check(is_present(site / "_framework/kept.wasm") && is_present(site / "_framework/kept.wasm.br") &&
              read_text(site / "_framework/dotnet.js") == "loader",
          "and keeps the rest of the runtime as the template has it");
    check(deployment::get_web_player_problem(player, site, {}, trimmed).empty(), "the trimmed site is whole for its own build");
    const auto untrimmed_problem = deployment::get_web_player_problem(player, site);
    check(untrimmed_problem.find("unused.wasm") != std::string::npos && untrimmed_problem.find("dependencies") != std::string::npos,
          "a build that downloads a file the site lacks names the file and what copies it");
    write_text(site / "index.html", "the project's own page");
    check(!deployment::get_web_player_problem(player, site, {}, trimmed).empty(), "a page that is not the template's is refused");
    check(deployment::get_web_player_problem(player, site, {"index.html", "styles/game.css"}, trimmed).empty(),
          "unless the project's extra files put it there");
    deployment::web_manifest manifest;
    manifest.page.trimmed_runtime_files = {"unused.wasm", "other.wasm"};
    check(deployment::make_web_data_manifest(manifest).find("  \"trimmed\": [\"unused.wasm\", \"other.wasm\"],\n") != std::string::npos,
          "the manifest lists the runtime files a trimmed build left out");
    check(deployment::make_web_data_manifest({}).find("trimmed") == std::string::npos, "a build that ships the whole runtime lists none");
    check(deployment::find_web_trimmed_files(player, {}).empty(), "a trim that is off leaves no runtime file out");
}

void test_web_chunks(const fs::path& root)
{
    std::printf("test_web_chunks\n");
    // The texts and values the page's test of its own check uses (game/web/tests/data.test.mjs).
    const auto get_check = [](const std::string& text)
    {
        return fs::hash_bytes_check(text.data(), text.size());
    };
    check(get_check("") == 0x02CC5D05u && get_check("a") == 0x550D7456u && get_check("abc") == 0x32D153FFu &&
              get_check("Nobody inspects the spammish repetition") == 0xE2293B2Fu,
          "a chunk's check is XXH32 with seed 0, which the page computes too");
    const auto chunks_folder = root / "chunks";
    // Three chunks: two of the most a chunk holds and the rest.
    const std::size_t rest = 1000;
    std::string content(2 * deployment::WEB_CHUNK_MAX_SIZE + rest, 'a');
    content[deployment::WEB_CHUNK_MAX_SIZE] = 'b';
    write_text(root / "large.bin", content);
    const auto chunks = deployment::write_web_chunks(root / "large.bin", chunks_folder);
    check(chunks.size() == 3 && chunks[0].size == deployment::WEB_CHUNK_MAX_SIZE && chunks[2].size == rest,
          "a file is cut into chunks of the most a chunk holds, the rest last");
    check(chunks[0].name != chunks[1].name && chunks[0].check != chunks[1].check, "chunks of other bytes have other names and checks");
    std::string joined;
    for(const auto& chunk : chunks)
    {
        const auto bytes = read_text(chunks_folder / chunk.name);
        check(bytes.size() == chunk.size && get_check(bytes) == chunk.check, "a chunk's file holds the bytes its size and check name");
        joined += bytes;
    }
    check(joined == content, "the chunks in order are the file");
    bool is_thrown = false;
    try
    {
        deployment::write_web_chunks(root / "empty.bin", chunks_folder);
    }
    catch(const std::exception&)
    {
        is_thrown = true;
    }
    check(is_thrown, "a file that cannot be read throws");
    write_text(root / "empty.bin", "");
    check(deployment::write_web_chunks(root / "empty.bin", chunks_folder).empty(), "an empty file has no chunk");
    // A pack is cut between its entries, and its table of contents starts a chunk.
    std::vector<fs::pack_input> inputs;
    for(const auto* name : {"compiled/a.asset", "compiled/b.asset", "compiled/c.asset"})
    {
        write_text(root / "pack_source" / name, std::string(1000, name[9]));
        inputs.push_back({name, root / "pack_source" / name});
    }
    fs::pack_write_options options;
    options.compress = false;
    std::string error;
    const auto pack = root / "content.pak";
    check(fs::write_pack(pack, inputs, options, error), "a pack is written: " + error);
    fs::pack_layout layout;
    check(fs::read_pack_layout(pack, layout, error), "its layout is read: " + error);
    const auto units = deployment::find_web_chunk_units(pack);
    check(units.size() == layout.entries.size() + 1 && units.back().starts_chunk && units.back().offset == layout.contents_offset &&
              units.back().offset + units.back().size == layout.file_size,
          "a pack's units are its entries and its table of contents, which starts a chunk");
    check(deployment::find_web_chunk_units(root / "large.bin").empty(), "any other file has no units");
    const auto pack_chunks = deployment::write_web_chunks(pack, chunks_folder);
    check(pack_chunks.size() == 2 && pack_chunks[0].size == layout.contents_offset,
          "a small pack is its entries in one chunk and its table of contents in another");
}

void test_web_addresses()
{
    std::printf("test_web_addresses\n");
    check(deployment::get_web_user_folder("Studio", "Game") == "/libsdl/Studio/Game" &&
              deployment::get_web_user_folder("", "Game") == "/libsdl/Game",
          "the game's folder is SDL's preferences folder in the browser");
    check(deployment::get_web_user_folder("Studio", "") == "/libsdl/Studio/app",
          "a project without a product takes the player's program name, as the deployed game does");
    check(deployment::encode_url_part("a-z_0.9~") == "a-z_0.9~" && deployment::encode_url_part("a b/\xc3\xbc") == "a%20b%2F%C3%BC",
          "unreserved characters stay; every other byte is %XX");
    const auto site = deployment::make_web_site_path("My Game", "Web");
    check(site == "/My Game/Web/", "a site path is the project and the profile");
    check(deployment::make_web_page_address("http://127.0.0.1:8742", site, {}) == "http://127.0.0.1:8742/My%20Game/Web/",
          "the page's address encodes each part of the path, slashes kept");
    check(deployment::make_web_page_address("http://127.0.0.1:8742", site, {"--log-level", "a&b c"}) ==
              "http://127.0.0.1:8742/My%20Game/Web/?arg=--log-level&arg=a%26b%20c",
          "run arguments go one per arg=, encoded");
    check(deployment::make_web_page_address("http://127.0.0.1:8742", site, {"--novsync"}, "webgl2") ==
              "http://127.0.0.1:8742/My%20Game/Web/?graphics=webgl2&arg=--novsync",
          "a graphics API to run with goes first, as graphics=");
    unravel::web_deploy_settings web;
    web.run_graphics = unravel::web_run_graphics::webgl2;
    check(deployment::get_web_run_graphics(web).empty(), "a WebGPU only build runs as it is");
    web.graphics = unravel::web_graphics::webgpu_then_webgl2;
    check(deployment::get_web_run_graphics(web) == "webgl2", "a build for both APIs runs with the chosen one");
    web.run_graphics = unravel::web_run_graphics::automatic;
    check(deployment::get_web_run_graphics(web).empty(), "Auto lets the page choose");
}

void test_web_tunnel()
{
    std::printf("test_web_tunnel\n");
    const std::vector<std::string> expected{"cloudflared", "tunnel", "--no-autoupdate", "--url", "http://127.0.0.1:8742"};
    check(deployment::make_web_tunnel_arguments({}, "http://127.0.0.1:8742") == expected,
          "without a set path the tunnel runs the cloudflared on PATH, to the local server");
    check(deployment::make_web_tunnel_arguments("C:/tools/cloudflared.exe", "http://127.0.0.1:8742").front() ==
              "C:/tools/cloudflared.exe",
          "a set path runs that cloudflared");
    const std::string announced = "2026-11-07T14:36:16Z INF |  https://address-rhythm-dean-attorneys.trycloudflare.com"
                                  "                                   |";
    check(deployment::find_web_tunnel_origin(announced) == "https://address-rhythm-dean-attorneys.trycloudflare.com",
          "the line that announces the tunnel gives its origin");
    check(deployment::find_web_tunnel_origin("ERR failed to request quick Tunnel: Post \"https://api.trycloudflare.com/"
                                             "tunnel\": dial tcp: no such host")
              .empty(),
          "the API address a failure names is not the tunnel's");
    check(deployment::find_web_tunnel_origin("INF Requesting new quick Tunnel on trycloudflare.com...").empty() &&
              deployment::find_web_tunnel_origin("https://www.cloudflare.com/website-terms/").empty() &&
              deployment::find_web_tunnel_origin("https://name.trycloudflare.com.example.org").empty(),
          "lines without a tunnel address, or with another domain, give none");
    const std::string tunnel = "https://address-rhythm-dean-attorneys.trycloudflare.com";
    const std::string host = "address-rhythm-dean-attorneys.trycloudflare.com";
    check(deployment::is_web_tunnel_request(host, "", tunnel) && deployment::is_web_tunnel_request(host, tunnel, tunnel),
          "the tunnel's requests come in, with or without the page as their Origin");
    check(deployment::is_web_tunnel_request("Address-Rhythm-Dean-Attorneys.trycloudflare.com:443", "", tunnel),
          "host names compare without case, and a port is not part of the name");
    check(!deployment::is_web_tunnel_request(host, "https://evil.example", tunnel),
          "a page elsewhere is refused through the tunnel too");
    check(!deployment::is_web_tunnel_request("evil.example", "", tunnel) &&
              !deployment::is_web_tunnel_request("127.0.0.1:8742", tunnel, tunnel),
          "another host name is not the tunnel's");
    check(!deployment::is_web_tunnel_request(host, "", "") && !deployment::is_web_tunnel_request("", "", "https://"),
          "without a tunnel nothing counts as its request");
}

void test_write_text(const fs::path& root)
{
    std::printf("test_write_text\n");
    fs::error_code ec;
    fs::create_directories(root, ec);
    const auto file = root / "text.txt";
    deployment::write_text_or_throw(file, "longer text\n");
    deployment::write_text_or_throw(file, "a\nb\n");
    check(read_text(file) == "a\nb\n", "the text replaces the file byte for byte");
    bool has_thrown = false;
    try
    {
        deployment::write_text_or_throw(root, "a folder cannot take text");
    }
    catch(const std::runtime_error&)
    {
        has_thrown = true;
    }
    check(has_thrown, "a file that cannot be written throws");
    const std::string bytes = "png bytes";
    check(deployment::write_file(file, bytes.data(), bytes.size()) && read_text(file) == bytes, "bytes replace the file");
    check(!deployment::write_file(root, bytes.data(), bytes.size()), "a file that cannot be written reports it");
}

/// The text helpers the deploy steps quote tool output and write JSON with.
void test_text_helpers()
{
    std::printf("test_text_helpers\n");
    check(string_utils::get_last_lines("a\nb\nc", 2) == "b\nc", "the last lines of output without a final line break");
    check(string_utils::get_last_lines("a\nb\nc\n", 1) == "c", "the line break that ends the output is no line of its own");
    check(string_utils::get_last_lines("a\r\nb\r\n", 1) == "b", "Windows line breaks end the output too");
    check(string_utils::get_last_lines("a\nb", 5) == "a\nb", "all of a shorter output");
    check(string_utils::get_last_lines("a\nb", 0).empty() && string_utils::get_last_lines("\n\n", 2).empty(),
          "no lines asked for or none there: empty");
    check(string_utils::make_json_string("say \"hi\"\\ok") == "\"say \\\"hi\\\"\\\\ok\"", "quotes and backslashes are escaped");
    check(string_utils::escape_json("a\nb\tc\x01") == "a\\nb\\tc\\u0001", "control characters are escaped");
    check(string_utils::escape_json("caf\xc3\xa9") == "caf\xc3\xa9", "UTF-8 passes as it is");
}

auto contains_argument(const std::vector<std::string>& arguments, const std::string& argument) -> bool
{
    return std::find(arguments.begin(), arguments.end(), argument) != arguments.end();
}

void test_gradle_arguments()
{
    std::printf("test_gradle_arguments\n");
    deployment::android_toolchain tools;
    tools.java = "/jdk/bin/java";
    deployment::android_package_params params;
    params.player.root = "/editor/players/android-arm64";
    params.player.abi = "arm64-v8a";
    params.player.dotnet_runtime = "10.0.13";
    params.project_dir = "/tmp/project";
    params.content_dir = "/deploy";
    params.application_id = "com.company.game";
    params.name = "Game";
    params.version_name = "1.2";
    params.version_code = 7;
    params.orientation = unravel::android_orientation::portrait;
    params.icon_dir = "/tmp/icon";
    params.permissions_manifest = "/tmp/permissions/AndroidManifest.xml";
    params.trimmed_framework_list = "/tmp/trimmed_framework.txt";
    auto arguments = deployment::make_gradle_arguments(tools, params);
    check(!arguments.empty() && arguments.front() == tools.java.string(), "Java runs Gradle's wrapper");
    check(contains_argument(arguments, "assembleDebug") && !contains_argument(arguments, "assembleRelease"), "a debug build");
    check(!contains_argument(arguments, "--no-daemon") && contains_argument(arguments, "-Dorg.gradle.daemon.idletimeout=600000"),
          "a debug build keeps Gradle's daemon for the next build, for ten minutes");
    check(!contains_argument(arguments, "bundleRelease") && !contains_argument(arguments, "packageNativeDebugSymbols"),
          "a debug build writes no store files");
    check(contains_argument(arguments, "-Punravel.symbols=/editor/players/android-arm64/symbols"), "the symbol table's folder is passed");
    check(contains_argument(arguments, "-Punravel.applicationId=com.company.game") &&
              contains_argument(arguments, "-Punravel.versionCode=7") &&
              contains_argument(arguments, "-Punravel.orientation=sensorPortrait") &&
              contains_argument(arguments, "-Punravel.player=/editor/players/android-arm64/lib/arm64-v8a") &&
              contains_argument(arguments, "-Punravel.content=/deploy") && contains_argument(arguments, "-Punravel.icon=/tmp/icon") &&
              contains_argument(arguments, "-Punravel.permissions=/tmp/permissions/AndroidManifest.xml"),
          "the game's identity, its icon and the template's folders are passed");
    check(contains_argument(arguments, "-Punravel.trimmedFramework=/tmp/trimmed_framework.txt"),
          "the list of the framework files the package leaves out is passed");
    check(params.player.get_framework_dir() == fs::path("/editor/players/android-arm64/dotnet/lib/net10.0"),
          "the runtime's managed framework lies in the folder named after its version's target framework");
    params.is_release = true;
    params.keystore = "/keys/game.jks";
    params.key_alias = "game";
    arguments = deployment::make_gradle_arguments(tools, params);
    check(contains_argument(arguments, "assembleRelease") && contains_argument(arguments, "-Punravel.keystore=/keys/game.jks") &&
              contains_argument(arguments, "-Punravel.keyAlias=game"),
          "a release build names its keystore and key");
    check(contains_argument(arguments, "bundleRelease") && contains_argument(arguments, "packageNativeDebugSymbols"),
          "a release build also writes the store's bundle and symbols");
    check(contains_argument(arguments, "--no-daemon") && !contains_argument(arguments, "-Dorg.gradle.daemon.idletimeout=600000"),
          "a release build leaves no daemon with the keystore's passwords behind");
    check(deployment::get_built_bundle("/p") == fs::path("/p/app/build/outputs/bundle/release/app-release.aab"), "the bundle's place");
    check(deployment::get_built_native_symbols("/p") == fs::path("/p/app/build/outputs/native-debug-symbols/native-debug-symbols.zip"),
          "the symbols' place");
    check(deployment::get_bundle_path("/out/My.Game.apk") == fs::path("/out/My.Game.aab"), "the bundle sits beside the package");
    check(deployment::get_native_symbols_path("/out/My.Game.apk") == fs::path("/out/My.Game-native-debug-symbols.zip"),
          "the symbols sit beside the package");
    check(std::none_of(arguments.begin(),
                       arguments.end(),
                       [](const std::string& argument)
                       {
                           return argument.find("assword") != std::string::npos;
                       }),
          "no password reaches the command line");
    check(deployment::make_signing_environment({}).empty(), "nothing typed leaves the process's environment as it is");
    using environment = std::vector<std::string>;
    check(deployment::make_signing_environment({"store", ""}) ==
              environment{"UNRAVEL_KEYSTORE_PASSWORD=store", "UNRAVEL_KEY_PASSWORD=store"},
          "a typed keystore password also replaces the key password");
    check(deployment::make_signing_environment({"store", "key"}) ==
              environment{"UNRAVEL_KEYSTORE_PASSWORD=store", "UNRAVEL_KEY_PASSWORD=key"},
          "both typed passwords are handed over");
    check(deployment::make_signing_environment({"", "key"}) == environment{"UNRAVEL_KEY_PASSWORD=key"},
          "a typed key password alone leaves the keystore's to the environment");
    check(deployment::make_gradle_local_properties("C:/Android/Sdk") == "sdk.dir=C\\:/Android/Sdk\n",
          "local.properties escapes the drive colon");
    check(deployment::get_built_package("/p", false) == fs::path("/p/app/build/outputs/apk/debug/app-debug.apk"),
          "the debug package's place");
}

/// A `width` x `height` PNG of one opaque color.
void write_solid_png(const fs::path& file, std::uint32_t width, std::uint32_t height)
{
    constexpr std::uint8_t OPAQUE_RED[] = {255, 0, 0, 255};
    std::vector<std::uint8_t> texels;
    for(std::uint32_t i = 0; i < width * height; ++i)
    {
        texels.insert(texels.end(), std::begin(OPAQUE_RED), std::end(OPAQUE_RED));
    }
    bx::FileWriter writer;
    bx::Error error;
    if(bx::open(&writer, bx::FilePath(file.string().c_str()), false, &error))
    {
        bimg::imageWritePng(&writer, width, height, width * 4, texels.data(), bimg::TextureFormat::RGBA8, false, &error);
        bx::close(&writer);
    }
}

/// The alpha of the texel at (x, y) of a PNG; -1 when it cannot be read.
auto read_png_alpha(const fs::path& file, std::uint32_t x, std::uint32_t y, std::uint32_t& width) -> int
{
    std::ifstream stream(file, std::ios::binary);
    const std::vector<char> bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    bx::DefaultAllocator allocator;
    auto* image = bimg::imageParse(&allocator, bytes.data(), static_cast<std::uint32_t>(bytes.size()), bimg::TextureFormat::RGBA8);
    if(image == nullptr)
    {
        return -1;
    }
    width = image->m_width;
    const auto* texels = static_cast<const std::uint8_t*>(image->m_data);
    const int alpha = x < image->m_width && y < image->m_height ? texels[(std::size_t(y) * image->m_width + x) * 4 + 3] : -1;
    bimg::imageFree(image);
    return alpha;
}

void test_android_java(const fs::path& root)
{
    std::printf("test_android_java\n");
    check(deployment::parse_java_version("openjdk version \"21.0.3\" 2024-04-16\nOpenJDK Runtime Environment") == 21,
          "a Java names its major version first");
    check(deployment::parse_java_version("java version \"1.8.0_281\"\nJava(TM) SE Runtime Environment") == 8,
          "Java 8 and older name it second, after 1");
    check(deployment::parse_java_version("openjdk version \"17\" 2021-09-14") == 17, "a version of one number reads");
    check(deployment::parse_jdk_release_version("IMPLEMENTOR=\"JetBrains s.r.o.\"\nJAVA_RUNTIME_VERSION=\"11.0.1+7\"\nJAVA_VERSION=\"21.0.3\"\n"
                                                "JAVA_VERSION_DATE=\"2024-04-16\"\n") == 21,
          "a JDK's release file gives its version on its own line, whatever other keys end the same way");
    check(deployment::parse_jdk_release_version("JAVA_VERSION=\"1.8.0_281\"\r\nOS_NAME=\"Windows\"\r\n") == 8 &&
              deployment::parse_jdk_release_version("") == 0 && deployment::parse_jdk_release_version("JAVA_RUNTIME_VERSION=\"17\"") == 0,
          "an old Java's release file reads too, and text without the line gives no version");
    check(deployment::parse_java_version("") == 0 && deployment::parse_java_version("'java' is not recognized") == 0 &&
              deployment::parse_java_version("version \"abc\"") == 0,
          "text that names no version gives none");
    // An SDK, an old Java where JAVA_HOME points, and a newer one in Android Studio's folder.
    const auto java_name = fs::path("java").replace_extension(fs::executable_extension());
    const auto adb_name = fs::path("adb").replace_extension(fs::executable_extension());
    write_text(root / "sdk" / "platform-tools" / adb_name, "adb");
    write_text(root / "old_jdk" / "bin" / java_name, "java 8");
    write_text(root / "studio_jdk" / "bin" / java_name, "java 21");
    const auto probe = [&root](const fs::path& java)
    {
        return java.parent_path().parent_path() == root / "old_jdk" ? 8 : 21;
    };
    deployment::android_toolchain_search search;
    search.sdk_candidates = {root / "no_sdk", root / "sdk"};
    search.jdk_candidates = {root / "no_jdk", root / "old_jdk", root / "studio_jdk"};
    auto tools = deployment::find_android_toolchain(search, probe);
    check(tools.is_complete() && tools.sdk == root / "sdk" && tools.adb == root / "sdk" / "platform-tools" / adb_name,
          "the first SDK that has its platform tools is taken");
    check(tools.java == root / "studio_jdk" / "bin" / java_name, "a Java too old gives way to the next one that is new enough");
    search.jdk_candidates = {root / "old_jdk"};
    tools = deployment::find_android_toolchain(search, probe);
    check(!tools.is_complete() && tools.java.empty() && tools.error.find("version 8") != std::string::npos &&
              tools.error.find("17") != std::string::npos,
          "with only a Java too old, the problem names its version and the one a build needs");
    search.jdk_candidates = {root / "no_jdk"};
    check(deployment::find_android_toolchain(search, probe).error.find("No Java") != std::string::npos, "no Java at all is said so");
    search.sdk_candidates = {root / "no_sdk"};
    check(deployment::find_android_toolchain(search, probe).error.find("No Android SDK") != std::string::npos,
          "a missing SDK is named first");
    const std::string properties = "distributionBase=GRADLE_USER_HOME\ndistributionPath=wrapper/dists\n"
                                   "distributionUrl=https\\://services.gradle.org/distributions/gradle-9.8.1-bin.zip\r\n"
                                   "zipStoreBase=GRADLE_USER_HOME\n";
    check(deployment::get_gradle_distribution_name(properties) == "gradle-9.8.1-bin", "a wrapper's properties name the Gradle it runs");
    check(deployment::get_gradle_distribution_name("distributionPath=wrapper/dists\n").empty() &&
              deployment::get_gradle_distribution_name("distributionUrl=gradle\n").empty(),
          "properties without a distribution name none");
    const auto gradle_home = root / "gradle_home";
    check(!deployment::has_gradle_distribution(gradle_home, "gradle-9.8.1-bin"), "a computer that never ran that Gradle has no copy");
    write_text(gradle_home / "wrapper" / "dists" / "gradle-9.8.1-bin" / "hash" / "gradle-9.8.1-bin.zip.ok", "");
    check(deployment::has_gradle_distribution(gradle_home, "gradle-9.8.1-bin") && !deployment::has_gradle_distribution(gradle_home, ""),
          "one that did has it under Gradle's folder");
    check(deployment::check_android_version_code(1).empty() &&
              deployment::check_android_version_code(deployment::MAX_ANDROID_VERSION_CODE).empty() &&
              !deployment::check_android_version_code(deployment::MAX_ANDROID_VERSION_CODE + 1).empty(),
          "a version code up to the store's largest is taken");
}

void test_android_device_answers()
{
    std::printf("test_android_device_answers\n");
    check(deployment::parse_android_process_id("12345\n", 0) == "12345", "the game's process id is read");
    check(deployment::parse_android_process_id("12345 678\r\n", 0) == "12345", "of several ids the first is taken");
    check(deployment::parse_android_process_id("", 1).empty(), "a game that does not run has none");
    check(deployment::parse_android_process_id("error: device offline\n", 1).empty() &&
              deployment::parse_android_process_id("adb.exe: device 'R5' not found", 1).empty() &&
              deployment::parse_android_process_id("error: closed", 0).empty() && deployment::parse_android_process_id("12345", 1).empty(),
          "adb's error text and a failed call are no process id");
    using lines = std::vector<std::string>;
    const auto feed = [](deployment::android_crash_report_filter& filter, const lines& messages)
    {
        lines released;
        for(const auto& message : messages)
        {
            for(const auto& line : filter.add({"raw " + message, message}))
            {
                released.push_back(line.message);
                check(line.text == "raw " + line.message, "a released line keeps its text as the log printed it");
            }
        }
        return released;
    };
    const lines other_report{"*** *** *** *** *** *** *** ***", "Build fingerprint: 'samsung'", "pid: 777, tid: 778, name: other  >>> com.other <<<",
                             "signal 11 (SIGSEGV)", "backtrace:"};
    const lines game_report{"*** *** *** *** *** *** *** ***", "Build fingerprint: 'samsung'", "ABI: 'arm64'",
                            "pid: 1234, tid: 1260, name: SDLThread  >>> com.company.game <<<", "signal 11 (SIGSEGV)",
                            "      #00 pc 0000000000012345  /data/app/lib/arm64/libmain.so"};
    deployment::android_crash_report_filter filter("1234");
    check(feed(filter, other_report).empty(), "the crash report of another process is dropped");
    check(feed(filter, game_report) == game_report, "the game's own report is released whole, from its first line");
    check(feed(filter, {"      #01 pc 0000000000023456  /data/app/lib/arm64/libmain.so"}).size() == 1, "and so is what follows it");
    check(feed(filter, other_report).empty(), "a later report of another process is dropped again");
    deployment::android_crash_report_filter other_filter("12");
    check(feed(other_filter, game_report).empty(), "a process whose id only starts the same is another process");
}

void test_desktop_graphics()
{
    std::printf("test_desktop_graphics\n");
    using unravel::preferred_renderer;
    using unravel::renderer_platform;
    using renderers = std::vector<preferred_renderer>;
    unravel::desktop_graphics_apis all;
    check(deployment::get_desktop_renderers(all, renderer_platform::windows) ==
              renderers{preferred_renderer::direct3d12, preferred_renderer::direct3d11, preferred_renderer::vulkan, preferred_renderer::opengl},
          "a Windows build's graphics APIs, in the order its game tries them");
    check(deployment::get_desktop_renderers(all, renderer_platform::linux) == renderers{preferred_renderer::vulkan, preferred_renderer::opengl},
          "a Linux build ships none of the APIs Linux does not have");
    check(!deployment::is_desktop_graphics_restricted(all, renderer_platform::windows) &&
              !deployment::is_desktop_graphics_restricted(all, renderer_platform::linux),
          "a profile with every API on restricts nothing");
    unravel::desktop_graphics_apis without_opengl;
    without_opengl.opengl = false;
    check(deployment::is_desktop_graphics_restricted(without_opengl, renderer_platform::windows) &&
              deployment::get_desktop_renderers(without_opengl, renderer_platform::linux) == renderers{preferred_renderer::vulkan},
          "an API switched off is left out");
    unravel::desktop_graphics_apis only_direct3d;
    only_direct3d.vulkan = false;
    only_direct3d.opengl = false;
    check(deployment::get_desktop_renderers(only_direct3d, renderer_platform::linux).empty(),
          "a profile with only APIs the platform lacks ships none, which the deploy refuses");
    unravel::desktop_graphics_apis without_direct3d11;
    without_direct3d11.direct3d11 = false;
    check(!deployment::is_desktop_graphics_restricted(without_direct3d11, renderer_platform::linux),
          "an API the platform lacks restricts nothing by being off");
    // What the game does with the list a restricted build ships.
    const renderers shipped{preferred_renderer::direct3d12, preferred_renderer::vulkan};
    check(unravel::get_renderer_candidates(preferred_renderer::auto_detect, {}) == renderers{preferred_renderer::auto_detect} &&
              unravel::get_renderer_candidates(preferred_renderer::vulkan, {}) == renderers{preferred_renderer::vulkan},
          "a build that ships every API starts the preferred one, or the platform's choice");
    check(unravel::get_renderer_candidates(preferred_renderer::auto_detect, shipped) == shipped,
          "a build that ships some tries them in its order");
    check(unravel::get_renderer_candidates(preferred_renderer::vulkan, shipped) == renderers{preferred_renderer::vulkan, preferred_renderer::direct3d12},
          "the preferred one first when the build ships it");
    check(unravel::get_renderer_candidates(preferred_renderer::opengl, shipped) == shipped,
          "and never an API it has no shaders for");
    // The shipped settings name the APIs of a restricted desktop build only.
    unravel::settings base;
    unravel::settings_profile profile;
    profile.name = "Desktop";
    profile.platform = unravel::target_platform::desktop;
    check(deployment::resolve_settings(base, profile).build.renderers.empty(), "an unrestricted build names no API in its settings");
    profile.desktop.graphics.opengl = false;
    const auto resolved = deployment::resolve_settings(base, profile);
    check(resolved.build.renderers == deployment::get_desktop_renderers(profile.desktop.graphics, unravel::get_current_renderer_platform()) &&
              !resolved.build.renderers.empty(),
          "a restricted one names the ones it ships, in order");
    profile.platform = unravel::target_platform::android;
    check(deployment::resolve_settings(base, profile).build.renderers.empty(), "an Android build names none: it has one API");
    unravel::deploy_settings params;
    params.deploy_target = unravel::deploy_platform::desktop;
    check(deployment::get_shader_variants(params).empty(), "a desktop deploy that leaves no API out ships every renderer's shaders");
    const auto shipped_apis = deployment::get_desktop_renderers(params.desktop.graphics, unravel::get_current_renderer_platform());
    if(shipped_apis.size() > 1)
    {
        // Leave the platform's last API out: the others' shader variants are what ships.
        const auto left_out = shipped_apis.back();
        params.desktop.graphics.opengl = left_out != preferred_renderer::opengl;
        params.desktop.graphics.vulkan = left_out != preferred_renderer::vulkan;
        const auto fewer = deployment::get_shader_variants(params);
        const auto left_out_extension = gfx::get_renderer_filename_extension(unravel::preferred_renderer_to_gfx_type(left_out));
        check(!fewer.empty() && !fewer.contains(left_out_extension),
              "a desktop deploy that leaves an API out ships the shader variants of the others");
    }
    check(deployment::get_desktop_renderers(all, renderer_platform::macos) == renderers{preferred_renderer::metal, preferred_renderer::vulkan},
          "a macOS build's graphics APIs, Metal first");
    unravel::desktop_graphics_apis without_vulkan;
    without_vulkan.vulkan = false;
    check(deployment::get_desktop_renderers(without_vulkan, renderer_platform::macos) == renderers{preferred_renderer::metal} &&
              deployment::is_desktop_graphics_restricted(without_vulkan, renderer_platform::macos),
          "a macOS build can ship Metal alone");
}

void test_extra_files(const fs::path& root)
{
    std::printf("test_extra_files\n");
    const auto folder = root / "extra";
    const auto build = root / "build";
    const auto record = root / "cache" / deployment::EXTRA_FILES_RECORD_NAME;
    write_text(build / "game.exe", "the game");
    write_text(build / "index.html", "the player's page");
    check(deployment::list_extra_files({}).empty() && deployment::list_extra_files(folder).empty(),
          "no folder and a folder that is not there list no extra file");
    check(deployment::deploy_extra_files({}, build, record).empty() && read_text(build / "game.exe") == "the game",
          "a deploy without extra files copies none and leaves the build alone");
    write_text(folder / "steam_api64.dll", "library");
    write_text(folder / "index.html", "the project's page");
    write_text(folder / "docs" / "manual" / "readme.txt", "manual");
    using names = std::vector<std::string>;
    const names expected{"docs/manual/readme.txt", "index.html", "steam_api64.dll"};
    check(deployment::list_extra_files(folder) == expected, "extra files are listed with their folders, in name order");
    check(deployment::deploy_extra_files(folder, build, record) == expected, "a deploy copies them and names them");
    check(read_text(build / "steam_api64.dll") == "library" && read_text(build / "docs/manual/readme.txt") == "manual",
          "each at the place it has in the folder");
    check(read_text(build / "index.html") == "the project's page", "an extra file named like a file of the build replaces it");
    check(read_text(build / "game.exe") == "the game", "the build's other files stay");
    fs::error_code ec;
    fs::remove(folder / "steam_api64.dll", ec);
    fs::remove_all(folder / "docs", ec);
    write_text(folder / "index.html", "the project's new page");
    deployment::remove_stale_extra_files(folder, build, record);
    check(!is_present(build / "steam_api64.dll") && !is_present(build / "docs"),
          "the next deploy first removes what an earlier one copied that the folder no longer has, with the folders it "
          "leaves empty");
    check(read_text(build / "index.html") == "the project's page" && read_text(build / "game.exe") == "the game",
          "and leaves the extra files the folder still has and the build's own files");
    check(deployment::deploy_extra_files(folder, build, record) == names{"index.html"}, "then copies what the folder holds now");
    check(read_text(build / "index.html") == "the project's new page", "a changed extra file is copied again");
    // The project takes its own page out of the folder: the next deploy removes it before its steps write the
    // build's page, so that page is the one that ships.
    fs::remove(folder / "index.html", ec);
    deployment::remove_stale_extra_files(folder, build, record);
    check(!is_present(build / "index.html"), "an extra file that had replaced a file of the build goes before the build is written");
    write_text(build / "index.html", "the player's page");
    check(deployment::deploy_extra_files(folder, build, record).empty() && read_text(build / "index.html") == "the player's page",
          "and the file the build's step then writes stays");
    deployment::remove_stale_extra_files(folder, build, record);
    check(read_text(build / "index.html") == "the player's page", "also through the deploys that follow");
    write_text(folder / "notes.txt", "notes");
    deployment::deploy_extra_files(folder, build, record);
    deployment::remove_stale_extra_files({}, build, record);
    check(!is_present(build / "notes.txt") && deployment::deploy_extra_files({}, build, record).empty(),
          "a profile that no longer names a folder gets its earlier extra files removed");
    // The web: the player's precompressed copies of a page file the project replaced.
    write_text(build / "main.js", "replaced");
    write_text(build / "main.js.br", "packed player code");
    write_text(build / "main.js.gz", "packed player code");
    write_text(build / "graphics.js.br", "another file's copy");
    deployment::remove_precompressed_copies(build, {"main.js", "styles/site.css"});
    check(!is_present(build / "main.js.br") && !is_present(build / "main.js.gz") && is_present(build / "graphics.js.br") &&
              is_present(build / "main.js"),
          "the precompressed copies of a replaced file go, the file and other files' copies stay");
}

void test_post_command(const fs::path& root)
{
    std::printf("test_post_command\n");
    const deployment::post_command_context context{root / "build folder", "Desktop", "desktop", "1.2"};
    const auto environment = deployment::make_post_command_environment(context);
    check(environment.size() == 4 && environment[1] == "UNRAVEL_DEPLOY_PROFILE=Desktop" && environment[2] == "UNRAVEL_DEPLOY_PLATFORM=desktop" &&
              environment[3] == "UNRAVEL_DEPLOY_VERSION=1.2" && environment[0].rfind("UNRAVEL_DEPLOY_FOLDER=", 0) == 0,
          "the command is told the build's folder, profile, platform and version");
    // The command runs as the user wrote it: quotes, a redirection, and a name with a space in it.
    fs::error_code ec;
    fs::create_directories(context.folder, ec);
    const auto script = root / deployment::get_post_command_script_name();
#ifdef _WIN32
    const std::string command = "echo %UNRAVEL_DEPLOY_PROFILE% \"quoted text\"> \"made by command.txt\"";
    const std::string failing = "exit /b 7";
#else
    const std::string command = "echo $UNRAVEL_DEPLOY_PROFILE \"quoted text\"> \"made by command.txt\"";
    const std::string failing = "exit 7";
#endif
    write_text(script, deployment::make_post_command_script(command));
    const auto result = subprocess::call(deployment::make_post_command_arguments(script), environment);
    const auto written = read_text(context.folder / "made by command.txt");
    check(result.started && result.retcode == 0, "a post-deploy command runs through the system's command interpreter: " + result.out_output);
    check(written.find("Desktop") != std::string::npos && written.find("quoted text") != std::string::npos,
          "in the build's folder, with its environment, and its quotes as written");
    write_text(script, deployment::make_post_command_script(failing));
    check(subprocess::call(deployment::make_post_command_arguments(script), environment).retcode == 7,
          "a command that fails ends the script with its exit code");
}

void test_android_additions()
{
    std::printf("test_android_additions\n");
    const std::vector<std::string> permissions{"android.permission.INTERNET", "android.permission.CAMERA"};
    const std::string lines = "    <uses-permission android:name=\"android.permission.INTERNET\" />\n"
                              "    <uses-permission android:name=\"android.permission.CAMERA\" />\n";
    check(deployment::make_android_build_manifest("", permissions) == deployment::make_android_permissions_manifest(permissions),
          "a project without a manifest of its own gets the permissions alone");
    const std::string head = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n"
                             "    xmlns:tools=\"http://schemas.android.com/tools\" android:note=\"a > inside a value\">";
    const std::string body = "\n    <application>\n        <meta-data android:name=\"com.vendor.KEY\" android:value=\"42\" />\n    </application>\n</manifest>\n";
    check(deployment::make_android_build_manifest(head + body, permissions) == head + "\n" + lines + body,
          "the permissions go right after the opening tag of the project's manifest, whose own text stays as written");
    check(deployment::make_android_build_manifest("<manifest xmlns:android=\"x\" />\n", permissions) ==
              "<manifest xmlns:android=\"x\" >\n" + lines + "</manifest>\n",
          "an empty manifest element is opened to take them");
    check(deployment::make_android_build_manifest("<application />", permissions).empty() &&
              deployment::make_android_build_manifest("<manifest xmlns:android=\"x\"", permissions).empty(),
          "text without a whole manifest tag gives no manifest");
    deployment::android_toolchain tools;
    tools.java = "/jdk/bin/java";
    deployment::android_package_params params;
    params.project_dir = "/tmp/project";
    const auto without_additions = deployment::make_gradle_arguments(tools, params);
    check(std::none_of(without_additions.begin(),
                       without_additions.end(),
                       [](const std::string& argument)
                       {
                           return argument.starts_with("-Punravel.additions=");
                       }),
          "a build without an additions folder passes none");
    params.additions = "/project/android";
    check(contains_argument(deployment::make_gradle_arguments(tools, params), "-Punravel.additions=/project/android"),
          "a build with one passes it to Gradle");
}

void test_android_permissions()
{
    std::printf("test_android_permissions\n");
    unravel::android_permissions permissions;
    check(deployment::get_android_permission_names(permissions) ==
              std::vector<std::string>{"android.permission.INTERNET", "android.permission.VIBRATE"},
          "a profile asks for the internet and vibration at first");
    permissions.internet = false;
    permissions.vibrate = false;
    permissions.location = true;
    permissions.other = {"WAKE_LOCK", "com.vendor.permission.SENSOR", "android.permission.ACCESS_FINE_LOCATION"};
    check(deployment::get_android_permission_names(permissions) ==
              std::vector<std::string>{"android.permission.ACCESS_COARSE_LOCATION",
                                       "android.permission.ACCESS_FINE_LOCATION",
                                       "android.permission.WAKE_LOCK",
                                       "com.vendor.permission.SENSOR"},
          "location asks for both locations, a short name gets android.permission. in front, and each name comes once");
    check(deployment::check_android_permission_name("WAKE_LOCK").empty() &&
              deployment::check_android_permission_name("com.vendor.permission.SENSOR").empty(),
          "short and full names are valid");
    check(!deployment::check_android_permission_name("").empty() && !deployment::check_android_permission_name("WAKE LOCK").empty() &&
              !deployment::check_android_permission_name("android..CAMERA").empty() &&
              !deployment::check_android_permission_name("android.permission.").empty() &&
              !deployment::check_android_permission_name("com.2vendor.X").empty(),
          "empty names, spaces, empty parts and parts starting with a digit are refused");
    const auto manifest = deployment::make_android_permissions_manifest({"android.permission.INTERNET"});
    check(manifest.find("<uses-permission android:name=\"android.permission.INTERNET\" />") != std::string::npos &&
              manifest.find("<manifest") != std::string::npos,
          "the permissions manifest holds a uses-permission element per name");
    check(deployment::make_android_permissions_manifest({}).find("uses-permission") == std::string::npos,
          "no permissions make an empty manifest");
}

void test_android_icon(const fs::path& root)
{
    std::printf("test_android_icon\n");
    fs::error_code ec;
    fs::create_directories(root, ec);
    // Twice as wide as tall: at xxxhdpi it fits the 288 px visible square as 288 x 144, centered in the
    // 432 px layer, so rows 144 to 287 hold it.
    write_solid_png(root / "icon.png", 64, 32);
    const auto res = root / "res";
    check(deployment::write_android_icon(root / "icon.png", math::color(0.0f, 0.5f, 1.0f, 1.0f), res).is_ok(),
          "an icon is made from an image");
    check(fs::exists(res / "mipmap-anydpi-v26/ic_launcher.xml", ec) && fs::exists(res / "mipmap-mdpi/ic_launcher_foreground.png", ec) &&
              fs::exists(res / "mipmap-xxxhdpi/ic_launcher_foreground.png", ec),
          "the adaptive icon and a foreground per density are written");
    check(read_text(res / "values/ic_launcher_background.xml").find(">#0080FF<") != std::string::npos,
          "the background is an opaque color resource");
    check(read_text(res / "mipmap-anydpi-v26/ic_launcher.xml").find("@mipmap/ic_launcher_foreground") != std::string::npos,
          "the adaptive icon names its foreground");
    std::uint32_t width = 0;
    const auto foreground = res / "mipmap-xxxhdpi/ic_launcher_foreground.png";
    check(read_png_alpha(foreground, 216, 216, width) == 255 && width == 432, "the image fills the middle of a 432 px layer");
    check(read_png_alpha(foreground, 216, 100, width) == 0 && read_png_alpha(foreground, 20, 216, width) == 0,
          "the layer is clear around the visible square and the image");
    check(!deployment::write_android_icon(root / "missing.png", math::color::white(), res).is_ok(), "a missing image is reported");
    check(deployment::make_android_color(math::color(1.0f, 0.0f, 0.0f, 0.2f)) == "#FF0000", "colors are written without alpha");
}

void test_windows_version_numbers()
{
    std::printf("test_windows_version_numbers\n");
    using numbers = std::array<std::uint16_t, 4>;
    check(deployment::parse_windows_version("1.2.3") == numbers{1, 2, 3, 0}, "three numbers fill the first three");
    check(deployment::parse_windows_version("v2.10") == numbers{2, 10, 0, 0}, "a leading v is skipped");
    check(deployment::parse_windows_version("1.2.3-beta") == numbers{1, 2, 3, 0} &&
              deployment::parse_windows_version("1.2.3.4.5") == numbers{1, 2, 3, 4},
          "text after the numbers and a fifth number are left out");
    check(deployment::parse_windows_version("") == numbers{} && deployment::parse_windows_version("beta") == numbers{},
          "a version without numbers is 0.0.0.0");
    check(deployment::parse_windows_version("70000.1") == numbers{65535, 1, 0, 0}, "a number past 65535 is 65535");
}

/// The little-endian 16-bit value at `offset`.
auto read_u16(const std::vector<std::uint8_t>& bytes, std::size_t offset) -> std::uint32_t
{
    return bytes[offset] | (std::uint32_t(bytes[offset + 1]) << 8);
}

/// The little-endian 32-bit value at `offset`.
auto read_u32(const std::vector<std::uint8_t>& bytes, std::size_t offset) -> std::uint32_t
{
    return read_u16(bytes, offset) | (read_u16(bytes, offset + 2) << 16);
}

void test_windows_icon_group()
{
    std::printf("test_windows_icon_group\n");
    // A 6-byte header, then a 14-byte entry per image: sizes, palette, planes, bits, bytes, id.
    constexpr std::size_t header = 6;
    constexpr std::size_t entry = 14;
    const std::vector<deployment::windows_icon_image> images{{16, std::vector<std::uint8_t>(10)}, {256, std::vector<std::uint8_t>(20)}};
    const auto group = deployment::make_icon_group_resource(images, 7);
    check(group.size() == header + 2 * entry && read_u16(group, 2) == 1 && read_u16(group, 4) == 2,
          "an icon group lists its images after its header");
    check(group[header] == 16 && group[header + entry] == 0, "a 256 pixel image is listed as size 0");
    check(read_u32(group, header + 8) == 10 && read_u16(group, header + 12) == 7 && read_u16(group, header + entry + 12) == 8,
          "the entries hold the images' byte counts and their ids from the first one on");
}

void test_windows_icon_images(const fs::path& root)
{
    std::printf("test_windows_icon_images\n");
    fs::error_code ec;
    fs::create_directories(root, ec);
    // Twice as wide as tall: in the 256 px image it fills rows 64 to 191.
    write_solid_png(root / "wide.png", 64, 32);
    const auto images = deployment::make_windows_icon_images(deployment::load_icon_image(root / "wide.png"));
    check(images.size() == deployment::WINDOWS_ICON_SIZES.size() && images.front().size == 16 && images.back().size == 256,
          "an icon has an image per size");
    std::ofstream(root / "256.png", std::ios::binary).write(reinterpret_cast<const char*>(images.back().png.data()),
                                                           std::streamsize(images.back().png.size()));
    std::uint32_t width = 0;
    check(read_png_alpha(root / "256.png", 128, 128, width) == 255 && width == 256, "the image is scaled to fit");
    check(read_png_alpha(root / "256.png", 128, 20, width) == 0, "it is centered on a clear square");
    check(deployment::load_icon_image(root / "missing.png").is_empty(), "a missing image loads empty");
}

void test_window_icon_file(const fs::path& root)
{
    std::printf("test_window_icon_file\n");
    fs::error_code ec;
    fs::create_directories(root, ec);
    write_solid_png(root / "wide.png", 64, 32);
    const auto file = root / "icon.png";
    check(deployment::write_window_icon(file, deployment::load_icon_image(root / "wide.png")), "the window icon is written");
    std::uint32_t width = 0;
    check(read_png_alpha(file, 128, 128, width) == 255 && width == deployment::WINDOW_ICON_SIZE,
          "the window icon is a square of the window icon size with the image inside");
    check(read_png_alpha(file, 128, 20, width) == 0, "the window icon keeps the image's proportions");
}

auto make_test_version_info() -> deployment::windows_version_info
{
    deployment::windows_version_info info;
    info.version = "1.2.3-beta";
    info.product_name = "Test Game";
    info.company_name = "Test Company";
    info.original_filename = "Test Game.exe";
    return info;
}

void test_windows_version_resource()
{
    std::printf("test_windows_version_resource\n");
    const auto bytes = deployment::make_version_resource(make_test_version_info());
    // The root block: its length, the length of its value, its type, "VS_VERSION_INFO" with its null in
    // UTF-16 (32 bytes), then padding to 40, where the fixed information starts.
    constexpr std::size_t fixed_info = 40;
    check(read_u16(bytes, 0) == bytes.size() && read_u16(bytes, 2) == 52 && read_u16(bytes, 4) == 0,
          "the root block covers the resource and holds a binary VS_FIXEDFILEINFO");
    check(read_u32(bytes, fixed_info) == 0xFEEF04BD, "the fixed information follows the key");
    check(read_u32(bytes, fixed_info + 8) == ((1u << 16) | 2) && read_u32(bytes, fixed_info + 12) == (3u << 16),
          "the file version is 1.2.3.0");
}

#ifdef _WIN32
/// A version string of `file` as Windows reads it, from the US English Unicode table.
auto read_version_string(const fs::path& file, const std::wstring& name) -> std::wstring
{
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(file.c_str(), &handle);
    std::vector<std::uint8_t> data(size);
    wchar_t* value = nullptr;
    UINT length = 0;
    const std::wstring path = L"\\StringFileInfo\\040904B0\\" + name;
    if(size == 0 || !GetFileVersionInfoW(file.c_str(), 0, size, data.data()) ||
       !VerQueryValueW(data.data(), path.c_str(), reinterpret_cast<void**>(&value), &length) || value == nullptr)
    {
        return {};
    }
    return value;
}

/// The file version of `file` as Windows reads it, its most significant half first.
auto read_file_version(const fs::path& file) -> std::pair<DWORD, DWORD>
{
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(file.c_str(), &handle);
    std::vector<std::uint8_t> data(size);
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if(size == 0 || !GetFileVersionInfoW(file.c_str(), 0, size, data.data()) ||
       !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) || info == nullptr)
    {
        return {};
    }
    return {info->dwFileVersionMS, info->dwFileVersionLS};
}

BOOL CALLBACK count_resource(HMODULE /*module*/, LPCWSTR /*type*/, LPWSTR /*name*/, LONG_PTR count)
{
    ++*reinterpret_cast<std::size_t*>(count);
    return TRUE;
}

/// How many resources of `type` (RT_ICON 3, RT_GROUP_ICON 14) `file` has.
auto count_resources(const fs::path& file, WORD type) -> std::size_t
{
    HMODULE module = LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if(module == nullptr)
    {
        return 0;
    }
    std::size_t count = 0;
    EnumResourceNamesW(module, MAKEINTRESOURCEW(type), count_resource, reinterpret_cast<LONG_PTR>(&count));
    FreeLibrary(module);
    return count;
}
#endif

void test_windows_executable_resources(const fs::path& root)
{
#ifdef _WIN32
    std::printf("test_windows_executable_resources\n");
    constexpr WORD icon_type = 3;
    constexpr WORD icon_group_type = 14;
    fs::error_code ec;
    fs::create_directories(root, ec);
    // The player the deploy copies, with the engine's icon, when it was built beside the runner; otherwise the
    // runner itself, without an icon. (A system executable will not do: Windows refuses to remove the icons
    // of one that has language files.)
    const auto player = fs::resolve_protocol("binary:/unravel-player.exe");
    const bool has_player = fs::exists(player, ec);
    const auto file = root / "game.exe";
    fs::copy_file(has_player ? player : fs::executable_path(nullptr), file, fs::copy_options::overwrite_existing, ec);
    check(!ec && (!has_player || count_resources(file, icon_group_type) > 0), "the copied executable has the engine's icon");
    write_solid_png(root / "icon.png", 48, 48);
    const auto icon = deployment::make_windows_icon_images(deployment::load_icon_image(root / "icon.png"));
    const auto info = make_test_version_info();
    const auto written = deployment::set_windows_executable_resources(file, icon, info);
    check(written.is_ok(), "an executable takes an icon and version information: " + written.error);
    check(count_resources(file, icon_group_type) == 1 && count_resources(file, icon_type) == icon.size(),
          "the icon replaces every earlier one");
    check(read_version_string(file, L"ProductName") == L"Test Game" && read_version_string(file, L"CompanyName") == L"Test Company" &&
              read_version_string(file, L"FileVersion") == L"1.2.3-beta" &&
              read_version_string(file, L"OriginalFilename") == L"Test Game.exe",
          "Windows reads the version strings");
    check(read_file_version(file) == std::pair<DWORD, DWORD>{(1u << 16) | 2, 3u << 16}, "and the file version 1.2.3.0");
    auto renamed = info;
    renamed.product_name = "Renamed";
    check(deployment::set_windows_executable_resources(file, {}, renamed).is_ok() && count_resources(file, icon_type) == icon.size() &&
              read_version_string(file, L"ProductName") == L"Renamed",
          "without images the icon stays and the version information is replaced");
    check(!deployment::set_windows_executable_resources(root / "missing.exe", icon, info).is_ok(), "a missing file is reported");
#else
    (void)root;
#endif
}

void test_run_arguments()
{
    std::printf("test_run_arguments\n");
    check(deployment::split_run_arguments("").empty() && deployment::split_run_arguments(" \t ").empty(),
          "no options in blank text");
    const auto split = deployment::split_run_arguments("  --gfx-stats\t--window=0,0,640,480 \n-n ");
    check(split == std::vector<std::string>{"--gfx-stats", "--window=0,0,640,480", "-n"},
          "options split on any whitespace");
    check(deployment::quote_for_shell("--gfx-stats -n") == "'--gfx-stats -n'", "a shell word in single quotes");
    check(deployment::quote_for_shell("it's $HOME") == "'it'\\''s $HOME'", "a single quote is escaped outside the quotes");
    const auto plain = deployment::make_start_arguments("com.company.game", {});
    check(!plain.empty() && plain.back() == "com.company.game/com.unravel.player.UnravelActivity" &&
              !contains_argument(plain, "--es"),
          "a start without options sets no extra");
    const auto with_options = deployment::make_start_arguments("com.company.game", {"--gfx-stats", "--novsync"});
    check(with_options.size() == plain.size() + 3 && with_options[plain.size()] == "--es" &&
              with_options[plain.size() + 1] == "arguments" && with_options.back() == "'--gfx-stats --novsync'",
          "the options go in one quoted extra of the start intent");
    const auto adb_command = deployment::make_adb_command("/sdk/platform-tools/adb", "serial", {"logcat", "-c"});
    check(adb_command == std::vector<std::string>{fs::path("/sdk/platform-tools/adb").string(), "-s", "serial", "logcat", "-c"},
          "an adb command names its device before its arguments");
}

//-----------------------------------------------------------------------------
// subprocess
//-----------------------------------------------------------------------------

void test_subprocess(const fs::path& root)
{
    std::printf("test_subprocess\n");
#ifdef _WIN32
    const char* shell = std::getenv("ComSpec");
    const std::vector<std::string> echo = {shell == nullptr ? "cmd.exe" : shell, "/c", "echo first&& echo %UNRAVEL_TEST_VALUE%"};
    const std::vector<std::string> waiting = {"ping", "-n", "60", "127.0.0.1"};
#else
    const std::vector<std::string> echo = {"/bin/sh", "-c", "echo first; echo $UNRAVEL_TEST_VALUE"};
    const std::vector<std::string> waiting = {"sleep", "60"};
#endif
    std::vector<std::string> lines;
    const auto result = subprocess::call(echo,
                                         {"UNRAVEL_TEST_VALUE=second"},
                                         [&lines](const std::string& line)
                                         {
                                             lines.push_back(line);
                                         });
    check(result.started && result.retcode == 0, "a process runs with a changed environment");
    check(lines.size() >= 2 && lines[0].rfind("first", 0) == 0 && lines[1].rfind("second", 0) == 0,
          "its output arrives line by line, without line breaks, and sees the added variable");
    check(result.out_output.find("second") != std::string::npos, "its whole output is kept too");
    check(!subprocess::call({(root / "no_such_program.exe").string()}).started, "a program that does not exist does not start");
    const auto call_begin = std::chrono::steady_clock::now();
    const auto stopped = subprocess::call(waiting, {}, {}, []() { return true; });
    check(stopped.started && std::chrono::steady_clock::now() - call_begin < std::chrono::seconds(20),
          "a call whose stop check says so ends its process");
    subprocess::stream stream;
    check(stream.start(waiting, {}), "a stream starts a process");
    check(stream.is_running(), "it runs");
    const auto stop_begin = std::chrono::steady_clock::now();
    stream.stop();
    check(!stream.is_running() && std::chrono::steady_clock::now() - stop_begin < std::chrono::seconds(20),
          "stopping ends it at once");
    check(!stream.start({(root / "no_such_program.exe").string()}, {}) && !stream.is_running(),
          "a stream of a program that does not exist does not start, so the next program can be tried");
}

void test_pack_inputs(const fs::path& root)
{
    std::printf("test_pack_inputs\n");
    const auto compiled = root / "compiled";
    const auto cooked = root / "cooked";
    const auto variants = root / "variants";
    write_text(compiled / "a.png.asset", "a");
    write_text(compiled / "prefabs/b.pfb.asset", "plain");
    write_text(compiled / "scripts/game.dll", "dll");
    write_text(cooked / "prefabs/b.pfb.asset", "cooked");
    write_text(variants / "prefabs/b.pfb.asset", "variant");
    write_text(variants / "c.png.asset", "variant c");
    write_text(compiled / "c.png.asset", "plain c");
    write_text(root / ex::get_asset_index_file_name(), "index");
    const auto inputs = deployment::collect_pack_inputs(compiled,
                                                        {cooked, variants, root / "missing"},
                                                        root / ex::get_asset_index_file_name(),
                                                        [](const fs::path&, const fs::path& relative)
                                                        {
                                                            return relative.generic_string() != "a.png.asset";
                                                        });
    std::vector<std::string> names;
    for(const auto& input : inputs)
    {
        names.push_back(input.name);
    }
    check(names == std::vector<std::string>{ex::get_asset_index_file_name(), "compiled/c.png.asset", "compiled/prefabs/b.pfb.asset"},
          "the pack takes the shipped compiled files and the index, by name, but no library");
    check(inputs.size() == 3 && read_text(inputs[2].source) == "cooked", "a cooked variant replaces its compiled file, the first folder winning");
    check(inputs.size() == 3 && read_text(inputs[1].source) == "variant c", "a later folder's variant replaces a file the first has none for");
    check(deployment::get_shipped_variant({}, "c.png.asset", compiled / "c.png.asset") == compiled / "c.png.asset",
          "without cooked folders the compiled file ships");
}

void test_cooked_only_pack_inputs(const fs::path& root)
{
    std::printf("test_cooked_only_pack_inputs\n");
    const auto compiled = root / "compiled";
    const auto prefabs = root / "prefabs";
    const auto shaders = root / "shaders";
    const std::set<std::string> webgpu{".wgsl"};
    write_text(compiled / "shaders/fs_a.eshader.asset.dxbc", "dxbc");
    write_text(shaders / "shaders/fs_a.eshader.asset.wgsl", "wgsl a");
    write_text(shaders / "shaders/fs_a.eshader.asset.wgsl.manifest", "manifest");
    write_text(prefabs / "shaders/fs_b.eshader.asset.wgsl", "first");
    write_text(shaders / "shaders/fs_b.eshader.asset.wgsl", "second");
    write_text(shaders / "shaders/fs_c.eshader.asset.wgsl", "left out");
    write_text(root / ex::get_asset_index_file_name(), "index");
    const auto inputs = deployment::collect_pack_inputs(compiled,
                                                        {prefabs, shaders},
                                                        root / ex::get_asset_index_file_name(),
                                                        [&webgpu](const fs::path& file, const fs::path& relative)
                                                        {
                                                            return deployment::is_shipped_compiled_file(file, false) &&
                                                                   !deployment::is_other_shader_variant(file, webgpu) &&
                                                                   relative.generic_string() != "shaders/fs_c.eshader.asset.wgsl";
                                                        });
    std::vector<std::string> names;
    for(const auto& input : inputs)
    {
        names.push_back(input.name);
    }
    check(names == std::vector<std::string>{ex::get_asset_index_file_name(),
                                            "compiled/shaders/fs_a.eshader.asset.wgsl",
                                            "compiled/shaders/fs_b.eshader.asset.wgsl"},
          "cooked files without a compiled file ship, as the filter keeps them, without their manifests");
    check(inputs.size() == 3 && read_text(inputs[1].source) == "wgsl a" && read_text(inputs[2].source) == "first",
          "each is read from its cooked folder, the first folder winning");
}

void test_android_build_dirs(const fs::path& root)
{
    std::printf("test_android_build_dirs\n");
    constexpr auto lifetime = std::chrono::hours(24);
    const auto long_ago = fs::file_time_type::clock::now() - std::chrono::hours(48);
    const auto staging = root / "staging";
    const auto project = root / "project";
    fs::error_code ec;
    fs::create_directories(project, ec);
    check(deployment::find_unused_android_build_dirs(staging, staging / "kept", lifetime).empty(), "no staging folder: nothing is unused");
    check(deployment::write_android_build_owner(staging / "kept", {project, "Android"}), "a deploy names its build folder's owner");
    const auto owner = deployment::read_android_build_owner(staging / "kept");
    check(owner.project == project && owner.profile == "Android", "and the owner is read back");
    check(deployment::read_android_build_owner(staging / "none").project.empty(), "a folder without an owner names none");
    deployment::write_android_build_owner(staging / "used", {project, "Other"});
    deployment::write_android_build_owner(staging / "old", {project, "Old"});
    fs::last_write_time(staging / "old" / "owner.stamp", long_ago, ec);
    deployment::write_android_build_owner(staging / "gone", {root / "no_such_project", "Android"});
    write_text(staging / "older_editor_used" / "project" / "local.properties", "sdk.dir=");
    write_text(staging / "older_editor_old" / "project" / "local.properties", "sdk.dir=");
    fs::last_write_time(staging / "older_editor_old" / "project" / "local.properties", long_ago, ec);
    deployment::write_android_build_owner(staging / "kept_old", {project, "Kept"});
    fs::last_write_time(staging / "kept_old" / "owner.stamp", long_ago, ec);
    auto unused = deployment::find_unused_android_build_dirs(staging, staging / "kept_old", lifetime);
    std::sort(unused.begin(), unused.end());
    const std::vector<fs::path> expected = {staging / "gone", staging / "old", staging / "older_editor_old"};
    check(unused == expected,
          "the folders of a project that is gone and the ones no deploy used for the lifetime are unused; the asking "
          "deploy's own folder never is");
}

void test_cancellation()
{
    std::printf("test_cancellation\n");
    const std::vector<std::string> keys = {"app:/data/textures/wall.png"};
    std::size_t cooked = 0;
    const auto cook = [&cooked](std::size_t)
    {
        ++cooked;
        return deployment::cook_outcome{};
    };
    deployment::reset_deploy_cancel();
    check(deployment::cook_all(keys, cook).size() == 1 && cooked == 1, "a cook runs while no cancel is asked for");
    deployment::request_deploy_cancel();
    std::string error;
    try
    {
        deployment::cook_all(keys, cook);
    }
    catch(const std::exception& ex)
    {
        error = ex.what();
    }
    check(error == deployment::CANCELLED_STEP_ERROR && cooked == 1, "a cancelled deploy cooks nothing more, and its cook throws");
    deployment::reset_deploy_cancel();
    check(!deployment::is_deploy_cancelled() && deployment::cook_all(keys, cook).size() == 1, "the next deploy cooks again");
}

void test_stale_binaries(const fs::path& root)
{
    std::printf("test_stale_binaries\n");
#ifdef _WIN32
    const std::vector<std::string> kept = {"Game.exe", "keep.dll"};
    const std::string stale = "old.dll";
#else
    const std::vector<std::string> kept = {"libkeep.so"};
    const std::string stale = "libold.so.1";
#endif
    std::set<std::string> shipped;
    for(const auto& name : kept)
    {
        write_text(root / name, "binary");
        shipped.insert(deployment::get_binary_key(root / name));
    }
    write_text(root / stale, "binary");
    write_text(root / "notes.txt", "text");
    deployment::remove_stale_binaries(root, shipped);
    check(!is_present(root / stale), "a binary this build no longer ships is removed");
    check(std::all_of(kept.begin(), kept.end(), [&](const std::string& name) { return is_present(root / name); }),
          "the shipped binaries stay");
    check(is_present(root / "notes.txt"), "other files stay");
    write_text(root / "runtime/1.0/a.txt", "old");
    write_text(root / "runtime/2.0/a.txt", "new");
    write_text(root / "runtime/stray.txt", "stray");
    deployment::remove_siblings(root / "runtime/2.0");
    check(is_present(root / "runtime/2.0/a.txt") && !is_present(root / "runtime/1.0") && !is_present(root / "runtime/stray.txt"),
          "remove_siblings keeps only the current version");
    deployment::prune_folder_or_throw(root / "missing",
                                      [](const fs::path&)
                                      {
                                          return false;
                                      });
    check(!is_present(root / "missing"), "a missing folder has nothing to prune");
    for(const auto* name : {"a.txt", "b.txt", "c.txt", "d.txt", "keep.txt"})
    {
        write_text(root / "pruned" / name, name);
    }
    deployment::prune_folder_or_throw(root / "pruned",
                                      [](const fs::path& name)
                                      {
                                          return name == "keep.txt";
                                      });
    fs::error_code ec;
    check(is_present(root / "pruned/keep.txt") &&
              std::distance(fs::directory_iterator(root / "pruned", ec), fs::directory_iterator()) == 1,
          "pruning removes every entry the filter refuses, and keeps the others");
#ifdef _WIN32
    // Windows refuses to delete a file a program holds open without sharing its deletion.
    write_text(root / "pruned/held.txt", "held");
    bool has_thrown = false;
    {
        std::ifstream held(root / "pruned/held.txt");
        try
        {
            deployment::prune_folder_or_throw(root / "pruned",
                                              [](const fs::path& name)
                                              {
                                                  return name == "keep.txt";
                                              });
        }
        catch(const std::runtime_error&)
        {
            has_thrown = true;
        }
    }
    check(has_thrown, "an entry that cannot be removed fails the prune");
#endif
}

//-----------------------------------------------------------------------------
// protocol data
//-----------------------------------------------------------------------------

auto read_packed_text(const std::string& name) -> std::string
{
    const auto packed = fs::find_packed_file(fs::path(std::string(TEST_PROTOCOL) + ":/" + name));
    return packed.is_found() ? std::string(packed.data, packed.data + packed.size) : std::string{};
}

auto is_packed(const std::string& name) -> bool
{
    return fs::find_packed_file(fs::path(std::string(TEST_PROTOCOL) + ":/" + name)).is_found();
}

auto make_protocol_job(const fs::path& root) -> deployment::protocol_job
{
    deployment::protocol_job job;
    job.protocol = "app";
    job.compiled_root = root / "project/compiled";
    job.cooked = {root / "cooked/app"};
    job.protocol_root = root / "deploy/data/app";
    add_asset(job.database, make_uid("30000000-0000-4000-8000-000000000001"), "app:/data/textures/wall.png");
    add_asset(job.database, make_uid("30000000-0000-4000-8000-000000000002"), "app:/data/textures/floor.png");
    add_asset(job.database, make_uid("30000000-0000-4000-8000-000000000003"), "app:/data/prefabs/a.pfb");
    add_asset(job.database, make_uid("30000000-0000-4000-8000-000000000004"), "app:/data/scripts/game.cs");
    job.left_out = {fs::make_path_key("app:/data/textures/floor.png")};
    write_text(job.compiled_root / "textures/wall.png.asset", "wall");
    write_text(job.compiled_root / "textures/floor.png.asset", "floor");
    write_text(job.compiled_root / "prefabs/a.pfb.asset", "plain prefab");
    write_text(job.compiled_root / "scripts/game.dll", "library");
    write_text(job.compiled_root / "scripts/game.pdb", "symbols");
    write_text(job.cooked.front() / "prefabs/a.pfb.asset", "cooked prefab");
    return job;
}

auto mount_deployed(const fs::path& protocol_root, bool with_patch) -> bool
{
    std::string error;
    fs::unmount_packs();
    bool is_mounted = fs::mount_pack(TEST_PROTOCOL, protocol_root / ex::get_pack_file_name(), error);
    if(with_patch)
    {
        is_mounted = is_mounted && fs::mount_pack(TEST_PROTOCOL, protocol_root / ex::get_patch_pack_file_name(1), error);
    }
    check(is_mounted, "the deployed packs mount: " + error);
    return is_mounted;
}

void test_packed_protocol(deployment::protocol_job& job)
{
    std::printf("test_packed_protocol\n");
    write_text(job.protocol_root / deployment::LEGACY_ASSET_INDEX_NAME, "index of an older build");
    deployment::deploy_protocol_data(job);
    const auto& protocol_root = job.protocol_root;
    check(!is_present(protocol_root / deployment::LEGACY_ASSET_INDEX_NAME), "the index under its older name is removed");
    check(is_present(protocol_root / ex::get_pack_file_name()), "a packed protocol writes its pack");
    check(!is_present(protocol_root / ex::get_asset_index_file_name()), "the index travels inside the pack");
    check(read_text(protocol_root / "compiled/scripts/game.dll") == "library", "libraries stay loose");
    check(!is_present(protocol_root / "compiled/scripts/game.pdb"), "symbols ship only when asked for");
    write_text(job.cooked.front() / "scripts/game.dll", "library the deploy built");
    write_text(job.cooked.front() / "scripts/game.pdb", "symbols the deploy built");
    deployment::deploy_protocol_data(job);
    check(read_text(protocol_root / "compiled/scripts/game.dll") == "library the deploy built" &&
              !is_present(protocol_root / "compiled/scripts/game.pdb"),
          "a library the deploy built ships in place of the editor's, its symbols only when asked for");
    fs::error_code ec;
    fs::remove(job.cooked.front() / "scripts/game.dll", ec);
    fs::remove(job.cooked.front() / "scripts/game.pdb", ec);
    deployment::deploy_protocol_data(job);
    check(read_text(protocol_root / "compiled/scripts/game.dll") == "library", "without one the editor's library ships again");
    fs::add_path_protocol(TEST_PROTOCOL, protocol_root);
    if(mount_deployed(protocol_root, false))
    {
        check(read_packed_text("compiled/textures/wall.png.asset") == "wall", "a shipped asset is packed");
        check(!is_packed("compiled/textures/floor.png.asset"), "a left out asset is not packed");
        check(read_packed_text("compiled/prefabs/a.pfb.asset") == "cooked prefab", "the cooked variant is packed");
        const auto index = read_packed_text(ex::get_asset_index_file_name());
        check(index.find("wall.png") != std::string::npos && index.find("floor.png") == std::string::npos &&
                  index.find("game.cs") == std::string::npos,
              "the index lists what ships, without left out assets and scripts");
    }
    fs::unmount_packs();
}

/// Mounts the protocol's pack and, over it, the shader pack of `set`.
auto mount_shader_set(const fs::path& protocol_root, const std::string& set) -> bool
{
    std::string error;
    fs::unmount_packs();
    const bool is_mounted = fs::mount_pack(TEST_PROTOCOL, protocol_root / ex::get_pack_file_name(), error) &&
                            fs::mount_pack(TEST_PROTOCOL, protocol_root / ex::get_set_pack_file_name(set), error);
    check(is_mounted, "the protocol's pack and the " + set + " shader pack mount: " + error);
    return is_mounted;
}

void test_shader_set_packs(const fs::path& root)
{
    std::printf("test_shader_set_packs\n");
    auto job = make_protocol_job(root);
    const auto cooked_shaders = root / "cooked/shaders";
    write_text(cooked_shaders / "shaders/fs_a.eshader.asset.wgsl", "wgsl a");
    write_text(cooked_shaders / "shaders/fs_a.eshader.asset.essl", "essl a");
    write_text(job.compiled_root / "shaders/fs_a.eshader.asset.dxbc", "dxbc a");
    job.cooked.push_back(cooked_shaders);
    job.shader_variants = {".wgsl", ".essl"};
    job.shader_sets = {{"webgpu", ".wgsl"}, {"webgl2", ".essl"}};
    deployment::deploy_protocol_data(job);
    const auto& protocol_root = job.protocol_root;
    fs::add_path_protocol(TEST_PROTOCOL, protocol_root);
    if(mount_deployed(protocol_root, false))
    {
        check(!is_packed("compiled/shaders/fs_a.eshader.asset.wgsl") && !is_packed("compiled/shaders/fs_a.eshader.asset.essl") &&
                  !is_packed("compiled/shaders/fs_a.eshader.asset.dxbc") &&
                  read_packed_text("compiled/textures/wall.png.asset") == "wall",
              "with shader sets the protocol's pack holds every shipped asset but the shaders");
    }
    if(mount_shader_set(protocol_root, "webgpu"))
    {
        check(read_packed_text("compiled/shaders/fs_a.eshader.asset.wgsl") == "wgsl a" &&
                  !is_packed("compiled/shaders/fs_a.eshader.asset.essl"),
              "the WebGPU set's pack holds the WGSL shaders only");
    }
    if(mount_shader_set(protocol_root, "webgl2"))
    {
        check(read_packed_text("compiled/shaders/fs_a.eshader.asset.essl") == "essl a" &&
                  !is_packed("compiled/shaders/fs_a.eshader.asset.wgsl"),
              "the WebGL 2 set's pack holds the GLSL ES shaders only");
    }
    fs::unmount_packs();
    std::string error;
    check(fs::mount_pack(TEST_PROTOCOL, protocol_root / ex::get_set_pack_file_name("webgl2"), error) &&
              !is_packed(ex::get_asset_index_file_name()) && !is_packed("compiled/textures/wall.png.asset"),
          "a shader pack holds shaders only, no index");
    fs::unmount_packs();
    job.shader_sets.clear();
    deployment::deploy_protocol_data(job);
    check(!is_present(protocol_root / ex::get_set_pack_file_name("webgpu")) &&
              !is_present(protocol_root / ex::get_set_pack_file_name("webgl2")),
          "a build without shader sets removes the shader packs an earlier build left");
    if(mount_deployed(protocol_root, false))
    {
        check(read_packed_text("compiled/shaders/fs_a.eshader.asset.wgsl") == "wgsl a" &&
                  read_packed_text("compiled/shaders/fs_a.eshader.asset.essl") == "essl a",
              "and its protocol's pack holds both APIs' shaders again");
    }
    fs::unmount_packs();
}

/// Mounts the protocol's pack and, over it, the texture pack of `set`.
auto mount_texture_set(const fs::path& protocol_root, const std::string& set) -> bool
{
    std::string error;
    fs::unmount_packs();
    const bool is_mounted = fs::mount_pack(TEST_PROTOCOL, protocol_root / ex::get_pack_file_name(), error) &&
                            fs::mount_pack(TEST_PROTOCOL, protocol_root / ex::get_set_pack_file_name(set), error);
    check(is_mounted, "the protocol's pack and the " + set + " texture pack mount: " + error);
    return is_mounted;
}

void test_texture_set_packs(const fs::path& root)
{
    std::printf("test_texture_set_packs\n");
    auto job = make_protocol_job(root);
    const auto astc = root / "cooked/textures_astc";
    write_text(astc / "textures/wall.png.asset", "astc wall");
    job.texture_sets = {{"bc", job.cooked}, {"astc", {astc}}};
    deployment::deploy_protocol_data(job);
    const auto& protocol_root = job.protocol_root;
    fs::add_path_protocol(TEST_PROTOCOL, protocol_root);
    if(mount_deployed(protocol_root, false))
    {
        check(!is_packed("compiled/textures/wall.png.asset") && read_packed_text("compiled/prefabs/a.pfb.asset") == "cooked prefab",
              "with texture sets the protocol's pack holds every shipped asset but the textures");
    }
    if(mount_texture_set(protocol_root, "bc"))
    {
        check(read_packed_text("compiled/textures/wall.png.asset") == "wall", "the first set's pack holds its textures");
    }
    if(mount_texture_set(protocol_root, "astc"))
    {
        check(read_packed_text("compiled/textures/wall.png.asset") == "astc wall" && !is_packed("compiled/textures/floor.png.asset"),
              "the second set's pack holds its own variants and no left out texture");
    }
    fs::unmount_packs();
    std::string error;
    check(fs::mount_pack(TEST_PROTOCOL, protocol_root / ex::get_set_pack_file_name("astc"), error) &&
              !is_packed(ex::get_asset_index_file_name()) && !is_packed("compiled/prefabs/a.pfb.asset"),
          "a texture pack holds textures only, no index");
    fs::unmount_packs();
    job.texture_sets.clear();
    deployment::deploy_protocol_data(job);
    check(!is_present(protocol_root / ex::get_set_pack_file_name("bc")) &&
              !is_present(protocol_root / ex::get_set_pack_file_name("astc")),
          "a build with one set removes the texture packs an earlier build left");
}

/// A project without scripts has no library: its packed build has nothing to keep loose.
void test_packed_protocol_without_library(const fs::path& root)
{
    std::printf("test_packed_protocol_without_library\n");
    auto job = make_protocol_job(root);
    fs::error_code ec;
    fs::remove(job.compiled_root / "scripts/game.dll", ec);
    fs::remove(job.compiled_root / "scripts/game.pdb", ec);
    bool is_deployed = true;
    try
    {
        deployment::deploy_protocol_data(job);
    }
    catch(const std::exception&)
    {
        is_deployed = false;
    }
    check(is_deployed && is_present(job.protocol_root / ex::get_pack_file_name()), "a protocol without a script library ships packed");
    check(!is_present(job.protocol_root / "compiled"), "and leaves no folder of loose files");
}

void test_loose_protocol(deployment::protocol_job& job)
{
    std::printf("test_loose_protocol\n");
    job.is_packed = false;
    deployment::deploy_protocol_data(job);
    const auto& protocol_root = job.protocol_root;
    check(!is_present(protocol_root / ex::get_pack_file_name()), "a loose build removes the pack an earlier build left");
    check(is_present(protocol_root / ex::get_asset_index_file_name()), "the index ships loose");
    check(read_text(protocol_root / "compiled/textures/wall.png.asset") == "wall", "shipped assets are copied");
    check(!is_present(protocol_root / "compiled/textures/floor.png.asset"), "left out assets are not");
    check(read_text(protocol_root / "compiled/prefabs/a.pfb.asset") == "cooked prefab", "cooked variants replace compiled files");
    job.is_packed = true;
}

void test_patch_over_release(deployment::protocol_job& job, const fs::path& root)
{
    std::printf("test_patch_over_release\n");
    deployment::deploy_protocol_data(job);
    check(!is_present(job.protocol_root / "compiled/textures/wall.png.asset") &&
              !is_present(job.protocol_root / ex::get_asset_index_file_name()),
          "packing again removes the loose files a loose build left");
    const auto release_pack = root / "release/app" / ex::get_pack_file_name();
    fs::error_code ec;
    fs::create_directories(release_pack.parent_path(), ec);
    fs::copy_file(job.protocol_root / ex::get_pack_file_name(), release_pack, fs::copy_options::overwrite_existing, ec);
    write_text(job.compiled_root / "textures/wall.png.asset", "wall, changed");
    job.patch_release_pack = release_pack;
    deployment::deploy_protocol_data(job);
    check(read_text(job.protocol_root / ex::get_pack_file_name()) == read_text(release_pack), "the release's pack ships unchanged");
    check(is_present(job.protocol_root / ex::get_patch_pack_file_name(1)), "the patch holds the changes");
    if(mount_deployed(job.protocol_root, true))
    {
        check(read_packed_text("compiled/textures/wall.png.asset") == "wall, changed", "the patch serves the changed asset");
        check(read_packed_text("compiled/prefabs/a.pfb.asset") == "cooked prefab", "the release serves the rest");
    }
    fs::unmount_packs();
    job.patch_release_pack.clear();
    deployment::deploy_protocol_data(job);
    check(!is_present(job.protocol_root / ex::get_patch_pack_file_name(1)), "a full build removes the patch");
}

//-----------------------------------------------------------------------------
// releases
//-----------------------------------------------------------------------------

void test_release_names()
{
    std::printf("test_release_names\n");
    check(deployment::is_valid_release_name("1.0.0") && deployment::is_valid_release_name("com10"), "plain names are valid");
    for(const std::string name : {"", ".", "..", "a/b", "a\\b", "a:b", "name.", "name ", "con", "CON.txt", "COM1", "lpt9"})
    {
        check(!deployment::is_valid_release_name(name), "'" + name + "' cannot name a release folder");
    }
    check(!deployment::is_valid_release_name(std::string("a\tb")), "control characters are refused");
}

void test_keep_and_remove_release(const fs::path& root)
{
    std::printf("test_keep_and_remove_release\n");
    const auto project = deployment::get_profile_releases_folder(root / "project", "Desktop");
    check(project == root / "project" / "releases" / "Desktop", "a profile's releases are in releases/<profile>");
    const auto build = root / "build";
    const auto symbols = root / "symbols";
    check(!deployment::keep_release(project, build, symbols, "1.0").is_ok(), "a folder without a packed build cannot be kept");
    for(const auto* protocol : deployment::PACKED_PROTOCOLS)
    {
        write_text(build / deployment::DATA_FOLDER / protocol / ex::get_pack_file_name(), std::string("pack of ") + protocol);
    }
    check(!deployment::keep_release(project, build, symbols, "0.9").is_ok(), "a build no deploy marked as finished cannot be kept");
    check(deployment::write_build_stamp(build, {"Desktop", "v1", false}) && !deployment::keep_release(project, build, symbols, "0.9").is_ok(),
          "nor can the build of a deploy that did not finish");
    check(deployment::write_build_stamp(build, {"Desktop", "v1", true}), "a finished deploy marks its build");
    check(!deployment::keep_release(project, build, symbols, "a/b").is_ok(), "an invalid name is refused");
    check(deployment::keep_release(project, build, symbols, "0.9").is_ok() &&
              !fs::exists(deployment::get_release_symbols_folder(project, "0.9")),
          "a build without debug files is kept without them");
    write_text(symbols / "game.exe", "game");
    write_text(symbols / "unravel-player.pdb", "symbols");
    check(!deployment::are_debug_files_of_build(symbols, build), "debug files with no file of the build are not known to be its own");
    write_text(build / "game.exe", "another game");
    check(!deployment::are_debug_files_of_build(symbols, build), "nor are the ones kept with another executable");
    check(deployment::keep_release(project, build, symbols, "0.95").is_ok() &&
              !fs::exists(deployment::get_release_symbols_folder(project, "0.95")),
          "and a release is kept without them");
    check(deployment::remove_release(project, "0.95").is_ok(), "that release is removed");
    write_text(build / "game.exe", "game");
    check(deployment::are_debug_files_of_build(symbols, build), "debug files kept with the build's executable are its own");
    check(deployment::keep_release(project, build, symbols, "1.0").is_ok(), "a full packed build is kept");
    check(deployment::is_kept_release(project, "1.0"), "the release is kept");
    check(deployment::list_releases(project) == std::vector<std::string>{"0.9", "1.0"}, "it is listed");
    check(read_text(deployment::get_release_pack(project, "1.0", "app")) == "pack of app", "its packs are copied");
    const auto kept_symbols = deployment::get_release_symbols_folder(project, "1.0");
    check(read_text(kept_symbols / "game.exe") == "game" && read_text(kept_symbols / "unravel-player.pdb") == "symbols",
          "its executable and symbols are kept with it");
    check(!deployment::keep_release(project, build, symbols, "1.0").is_ok(), "a name already kept is refused");
    write_text(build / deployment::DATA_FOLDER / "app" / ex::get_patch_pack_file_name(1), "patch");
    check(!deployment::keep_release(project, build, symbols, "1.1").is_ok(), "a patched build cannot be kept");
    check(deployment::remove_release(project, "0.9").is_ok(), "the release without debug files is removed");
    check(!deployment::remove_release(project, "2.0").is_ok(), "removing a release that is not kept fails");
    check(deployment::remove_release(project, "1.0").is_ok() && deployment::list_releases(project).empty(),
          "a removed release is gone");

    // A project from before profiles kept its releases in releases/<release>.
    const auto old_project = root / "old";
    for(const auto* protocol : deployment::PACKED_PROTOCOLS)
    {
        write_text(old_project / "releases" / "0.9" / protocol / ex::get_pack_file_name(), "old pack");
    }
    check(deployment::move_releases_into_profile(old_project, "Desktop").is_ok(), "the old releases move");
    const auto moved = deployment::get_profile_releases_folder(old_project, "Desktop");
    check(deployment::list_releases(moved) == std::vector<std::string>{"0.9"}, "into the profile's releases");
    check(deployment::list_releases(old_project / "releases").empty(), "and no release is left outside a profile");
    check(deployment::move_releases_into_profile(old_project, "Desktop").is_ok() &&
              deployment::list_releases(moved) == std::vector<std::string>{"0.9"},
          "moving again changes nothing: a profile's folder is no release");
}

void test_debug_files(const fs::path& root)
{
    std::printf("test_debug_files\n");
    const auto folder = root / "symbols";
    write_text(root / "build" / "game.exe", "game 1");
    write_text(root / "player" / "unravel-player.pdb", "symbols 1");
    write_text(folder / "old-game.exe", "an earlier build's executable");
    const std::vector<fs::path> files{root / "build" / "game.exe", root / "player" / "unravel-player.pdb"};
    check(deployment::keep_debug_files(files, folder).is_ok(), "a build's debug files are kept");
    check(read_text(folder / "game.exe") == "game 1" && read_text(folder / "unravel-player.pdb") == "symbols 1",
          "the executable and the symbols are copied");
    check(!fs::exists(folder / "old-game.exe"), "an earlier build's files are removed");
    // Another size: a copy is skipped by size and write time, and two writes within one clock tick share a time.
    write_text(root / "build" / "game.exe", "game two");
    check(deployment::keep_debug_files(files, folder).is_ok() && read_text(folder / "game.exe") == "game two",
          "a changed executable is copied again");
    check(!deployment::keep_debug_files({root / "missing.pdb"}, folder).is_ok(), "a missing file fails");
}

//-----------------------------------------------------------------------------
// Steam
//-----------------------------------------------------------------------------

auto make_steam_settings() -> unravel::steam_deploy_settings
{
    unravel::steam_deploy_settings steam;
    steam.app_id = 480;
    steam.depot_id = 481;
    steam.account = "builder";
    return steam;
}

void test_steam_settings()
{
    std::printf("test_steam_settings\n");
    check(deployment::check_steam_settings(make_steam_settings()).empty(), "an app, a depot and an account upload");
    auto steam = make_steam_settings();
    steam.branch = "beta";
    check(deployment::check_steam_settings(steam).empty(), "with a beta branch too");
    auto missing = make_steam_settings();
    missing.app_id = 0;
    check(!deployment::check_steam_settings(missing).empty(), "an app is needed");
    missing = make_steam_settings();
    missing.depot_id = 0;
    check(!deployment::check_steam_settings(missing).empty(), "a depot is needed");
    missing = make_steam_settings();
    missing.account.clear();
    check(!deployment::check_steam_settings(missing).empty(), "an account is needed");
    for(const char* branch : {"default", "Default", "two words", "quo\"te"})
    {
        steam.branch = branch;
        check(!deployment::check_steam_settings(steam).empty(),
              std::string("the branch '") + branch + "' is refused: no default branch, no spaces or quotes");
    }
    steam = make_steam_settings();
    steam.account = "build er";
    check(!deployment::check_steam_settings(steam).empty(), "an account with a space is refused");
}

void test_steam_build_script()
{
    std::printf("test_steam_build_script\n");
    deployment::steam_build build;
    build.app_id = 480;
    build.depot_id = 481;
    build.description = "Game \"Deluxe\" 1.0\n(Desktop)";
    build.content_root = fs::path("C:/projects/game/.deploy/Desktop");
    build.build_output = fs::path("C:/temp/steam/output/");
    const auto script = deployment::make_steam_app_build_script(build);
    const std::string expected = "\"AppBuild\"\n"
                                 "{\n"
                                 "\t\"AppID\" \"480\"\n"
                                 "\t\"Desc\" \"Game 'Deluxe' 1.0 (Desktop)\"\n"
                                 "\t\"ContentRoot\" \"C:/projects/game/.deploy/Desktop/\"\n"
                                 "\t\"BuildOutput\" \"C:/temp/steam/output/\"\n"
                                 "\t\"Depots\"\n"
                                 "\t{\n"
                                 "\t\t\"481\"\n"
                                 "\t\t{\n"
                                 "\t\t\t\"FileMapping\"\n"
                                 "\t\t\t{\n"
                                 "\t\t\t\t\"LocalPath\" \"*\"\n"
                                 "\t\t\t\t\"DepotPath\" \".\"\n"
                                 "\t\t\t\t\"recursive\" \"1\"\n"
                                 "\t\t\t}\n"
                                 "\t\t\t\"FileExclusion\" \"Log.txt\"\n"
                                 "\t\t\t\"FileExclusion\" \"CrashLog.txt\"\n"
                                 "\t\t\t\"FileExclusion\" \"CrashLog.dmp\"\n"
                                 "\t\t\t\"FileExclusion\" \"build.stamp\"\n"
                                 "\t\t}\n"
                                 "\t}\n"
                                 "}\n";
    check(script == expected,
          "the app build script maps the whole folder into the depot, logs and the build's stamp left out:\n" + script);
    check(script.find("SetLive") == std::string::npos, "no branch: the build is set live nowhere");
    build.branch = "beta";
    check(deployment::make_steam_app_build_script(build).find("\t\"SetLive\" \"beta\"\n") != std::string::npos,
          "a branch: the build is set live on it");
}

void test_steamcmd_output()
{
    std::printf("test_steamcmd_output\n");
    const std::vector<std::string> expected{"steamcmd",
                                            "+@ShutdownOnFailedCommand",
                                            "1",
                                            "+@NoPromptForPassword",
                                            "1",
                                            "+login",
                                            "builder",
                                            "+run_app_build",
                                            fs::to_utf8_string(fs::path("C:/temp/app_build_480.vdf")),
                                            "+quit"};
    check(deployment::make_steamcmd_arguments({}, "builder", "C:/temp/app_build_480.vdf") == expected,
          "steamcmd from PATH logs in without asking for a password, builds and quits");
    check(deployment::make_steamcmd_arguments("D:/sdk/steamcmd.exe", "builder", "C:/temp/app_build_480.vdf").front() ==
              fs::to_utf8_string(fs::path("D:/sdk/steamcmd.exe")),
          "a set steamcmd runs instead");
    const std::string finished = "Uploading content...\r\n"
                                 "Successfully finished AppID 480 build (BuildID 12345678).\r\n";
    check(deployment::find_steam_build_id(finished) == "12345678", "a finished build's id is read");
    check(deployment::find_steam_build_id("BuildID 12 without a finished build").empty(), "no finished build: no id");
    check(deployment::find_steam_build_id("").empty(), "no output: no id");
    check(deployment::is_steam_login_failure("Logging in user 'builder' to Steam Public...\nFAILED (Invalid Password)"),
          "a refused password is a login failure");
    check(deployment::is_steam_login_failure("Cached credentials not found."), "a missing kept login is one too");
    check(!deployment::is_steam_login_failure("ERROR! Failed to commit build for AppID 480 : Access Denied"),
          "a refused build is not");
}

//-----------------------------------------------------------------------------
// dependencies, folders, processes
//-----------------------------------------------------------------------------

void test_dependency_parse(const fs::path& root)
{
    std::printf("test_dependency_parse\n");
#ifdef _WIN32
    const auto absolute = (root / "sub/abs.dll").string();
    const std::string output = "[ApplicationDirectory] game.dll : game.dll\r\n"
                               "[ApplicationDirectory] abs.dll : " + absolute + "\r\n"
                               "[WindowsFolder] kernel32.dll : C:\\Windows\\system32\\kernel32.dll\r\n"
                               "[NotFound] missing.dll : \r\n";
    const auto found = deployment::parse_dependencies(output, root);
    check(found == std::vector<std::string>{(root / "game.dll").string(), absolute},
          "the libraries of the application's folder are listed, relative ones resolved against it");
#else
    write_text(root / "libgame.so", "library");
    const std::string output = "\tlinux-vdso.so.1 (0x00007ffd)\n"
                               "\tlibgame.so => " + (root / "libgame.so").string() + " (0x00007f00)\n"
                               "\tlibc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007f01)\n";
    const auto found = deployment::parse_dependencies(output, root);
    check(found == std::vector<std::string>{(root / "libgame.so").string()}, "only the libraries beside the player are listed");
#endif
}

void test_failed_scan_throws(const fs::path& root)
{
    std::printf("test_failed_scan_throws\n");
    for(int attempt = 0; attempt < 2; ++attempt)
    {
        bool is_thrown = false;
        try
        {
            deployment::find_dependencies(root / "no_such_player.exe", root / "no_such_scanner.exe");
        }
        catch(const std::exception&)
        {
            is_thrown = true;
        }
        check(is_thrown, "a scan that does not run throws, and is not kept for the next deploy");
    }
}

void test_folders(const fs::path& root)
{
    std::printf("test_folders\n");
    const auto build = root / "build";
    fs::error_code ec;
    fs::create_directories(build, ec);
    check(!deployment::is_previous_deploy(build), "an empty folder holds no earlier deploy");
    write_text(build / deployment::DATA_FOLDER / "app" / ex::get_pack_file_name(), "pack");
    check(deployment::is_previous_deploy(build), "a packed build is an earlier deploy");
    const auto loose_build = root / "loose_build";
    write_text(loose_build / deployment::DATA_FOLDER / "app" / ex::get_asset_index_file_name(), "index");
    check(deployment::is_previous_deploy(loose_build), "a loose build is an earlier deploy");
    const auto older_build = root / "older_build";
    write_text(older_build / deployment::DATA_FOLDER / "app" / deployment::LEGACY_ASSET_INDEX_NAME, "index");
    check(deployment::is_previous_deploy(older_build), "a loose build of an older engine version is an earlier deploy");
    const auto started_build = root / "started_build";
    fs::create_directories(started_build, ec);
    check(!deployment::is_finished_build(started_build), "a folder without a stamp holds no finished build");
    check(deployment::write_build_stamp(started_build, {"Desktop", "v1", false}) && deployment::is_previous_deploy(started_build) &&
              !deployment::is_finished_build(started_build),
          "a deploy that started marks the folder as its own, and its build as unfinished");
    check(deployment::write_build_stamp(started_build, {"Desktop", "v1", true}) && deployment::is_finished_build(started_build),
          "a deploy that finished marks its build as finished");
    check(deployment::get_other_platform_problem(started_build, unravel::deploy_platform::web).empty(),
          "a stamp that names no platform refuses none");
    check(deployment::write_build_stamp(started_build, {"Desktop", "v1", true, unravel::get_platform_name(unravel::deploy_platform::desktop)}) &&
              deployment::read_build_stamp(started_build).platform == "desktop",
          "a deploy's stamp names the platform it built for");
    check(deployment::get_other_platform_problem(started_build, unravel::deploy_platform::desktop).empty() &&
              !deployment::get_other_platform_problem(started_build, unravel::deploy_platform::web).empty(),
          "and the folder takes builds for that platform only");
    check(deployment::get_other_platform_problem(root / "no_such_folder", unravel::deploy_platform::web).empty(),
          "a folder without a stamp takes any");
    check(!deployment::get_executable_name_problem(root / deployment::DATA_FOLDER).empty(),
          "an executable named like the data folder cannot be written beside it");
    check(deployment::get_executable_name_problem(root / "data.exe").empty() &&
              deployment::get_executable_name_problem(root / "Game").empty(),
          "any other name can");
    const auto read_only = root / "no_such_folder" / "build";
    check(!deployment::write_build_stamp(read_only, {"Desktop", "v1", true}), "a stamp that cannot be written is reported");
    const auto android = root / "android";
    fs::create_directories(android, ec);
    check(deployment::is_android_package_folder(android), "an empty folder takes an Android package");
    write_text(android / "Game.apk", "apk");
    write_text(android / "Other.APK", "apk");
    check(deployment::is_android_package_folder(android), "a folder of packages is an Android deploy's");
    write_text(android / "Game.aab", "aab");
    write_text(android / "Game-native-debug-symbols.zip", "zip");
    check(deployment::is_android_package_folder(android), "the store's bundle and symbols belong to an Android deploy");
    check(deployment::write_build_stamp(android, {"Android", "v1", true}) && deployment::is_android_package_folder(android),
          "and so does the deploy's stamp");
    const auto zip_only = root / "zip_only";
    write_text(zip_only / "Backup.zip", "zip");
    check(!deployment::is_android_package_folder(zip_only), "any other archive keeps the folder from an Android deploy");
    write_text(android / deployment::DATA_FOLDER / "app" / ex::get_pack_file_name(), "pack");
    check(deployment::is_android_package_folder(android), "the data an earlier Android deploy left beside its package belongs to it");
    check(!deployment::is_android_package_folder(build), "data without a package is a desktop build without its player");
    write_text(android / "Game.exe", "exe");
    check(!deployment::is_android_package_folder(android), "any other file keeps the folder from an Android deploy");
    const auto project = root / "project";
    check(deployment::is_inside(project / "a/b", project), "a path below a folder is inside it");
    check(deployment::is_inside(project, project), "a folder is inside itself");
    check(!deployment::is_inside(root / "projectX", project), "a sibling with a longer name is not inside");
    check(!deployment::is_inside(project / "a/../..", project), "'..' cannot slip out");
    if(is_case_folded())
    {
        check(deployment::is_inside(root / "PROJECT/a", project), "letter case does not slip past");
    }
    check(!deployment::is_executable_in_use(root / "no_such_game.exe"), "a missing executable is not in use");
#ifdef _WIN32
    const auto runner = fs::resolve_protocol("binary:/unravel-tests" + fs::executable_extension());
    if(is_present(runner))
    {
        check(deployment::is_executable_in_use(runner), "the running test runner is in use");
    }
#endif
}

auto wait_for_file(const fs::path& file) -> bool
{
    const auto deadline = std::chrono::steady_clock::now() + SPAWN_WAIT;
    while(std::chrono::steady_clock::now() < deadline)
    {
        if(read_text(file).find("spawned") != std::string::npos)
        {
            return true;
        }
        std::this_thread::sleep_for(SPAWN_POLL);
    }
    return false;
}

void test_spawn_detached(const fs::path& root)
{
    std::printf("test_spawn_detached\n");
    fs::error_code ec;
    fs::create_directories(root, ec);
#ifdef _WIN32
    const char* shell = std::getenv("ComSpec");
    const std::vector<std::string> arguments = {shell == nullptr ? "cmd.exe" : shell, "/c", "echo spawned> marker.txt"};
#else
    const std::vector<std::string> arguments = {"/bin/sh", "-c", "echo spawned > marker.txt"};
#endif
    const auto error = unravel::process::spawn_detached(arguments, root.string());
    check(!error, "a program starts: " + error.message());
    check(!error && wait_for_file(root / "marker.txt"), "it runs in the working directory given");
    const auto failed = unravel::process::spawn_detached({(root / "no_such_program.exe").string()}, root.string());
    check(static_cast<bool>(failed), "a program that does not exist reports an error");
}

auto run_deploy_library_suite(rtti::context&) -> int
{
    g_checks = 0;
    g_failures = 0;
    const auto temp = get_temp_folder();
    fs::error_code ec;
    fs::remove_all(temp, ec);
    try
    {
        test_keys();
        const auto listed = make_listed_content(temp / "listed");
        test_rule_precedence(listed);
        test_set_shipped(listed);
        test_normalize_and_orphans(listed);
        const auto project = make_walk_project(temp / "walk");
        deploy_settings params;
        params.deploy_scenes = {"app:/data/levels/gone.spfb"};
        const auto walked = deployment::detect_content(project.source, params, project.startup_scene);
        test_walk(walked);
        test_walk_shipped_settings(project);
        test_walk_engine(temp / "walk_engine");
        test_walk_engine_edges(temp / "walk_engine_edges");
        test_engine_loaded_keys();
        test_tree(walked);
        test_file_classification(temp / "files");
        test_shader_variants(temp / "variants");
        test_cooked_shader_variants();
        test_web_shader_sets();
        test_shader_cook_plan(temp / "shader_cook");
        test_profile_locations();
        test_deploy_locations(temp / "locations");
        test_application_ids();
        test_android_devices();
        test_android_device_properties();
        test_logcat_lines();
        test_game_log_lines();
        test_device_log_messages();
        test_merged_devices();
        test_wireless_debugging_services();
        test_android_template(temp / "android");
        test_player_version(temp / "player");
        test_web_template(temp / "web_template");
        test_web_chunk_cuts();
        test_web_site(temp / "web_site");
        test_web_trim(temp / "web_trim");
        test_web_chunks(temp / "web_chunks");
        test_web_addresses();
        test_web_tunnel();
        test_write_text(temp / "write_text");
        test_text_helpers();
        test_gradle_arguments();
        test_desktop_graphics();
        test_extra_files(temp / "extra_files");
        test_post_command(temp / "post_command");
        test_android_additions();
        test_android_java(temp / "android_java");
        test_android_device_answers();
        test_android_permissions();
        test_android_icon(temp / "icon");
        test_windows_version_numbers();
        test_windows_icon_group();
        test_windows_icon_images(temp / "windows_icon");
        test_window_icon_file(temp / "window_icon");
        test_windows_version_resource();
        test_windows_executable_resources(temp / "windows_executable");
        test_run_arguments();
        test_subprocess(temp / "subprocess");
        test_pack_inputs(temp / "inputs");
        test_cooked_only_pack_inputs(temp / "cooked_only_inputs");
        test_stale_binaries(temp / "binaries");
        test_android_build_dirs(temp / "android_build_dirs");
        test_cancellation();
        auto job = make_protocol_job(temp / "protocol");
        test_packed_protocol(job);
        test_loose_protocol(job);
        test_patch_over_release(job, temp / "protocol");
        test_packed_protocol_without_library(temp / "scriptless");
        test_texture_set_packs(temp / "texture_sets");
        test_shader_set_packs(temp / "shader_sets");
        test_release_names();
        test_keep_and_remove_release(temp / "releases");
        test_debug_files(temp / "debug_files");
        test_steam_settings();
        test_steam_build_script();
        test_steamcmd_output();
        test_dependency_parse(temp / "dependencies");
        test_failed_scan_throws(temp / "dependencies");
        test_folders(temp / "folders");
        test_spawn_detached(temp / "spawn");
    }
    catch(const std::exception& ex)
    {
        check(false, std::string("unexpected exception: ") + ex.what());
    }
    fs::unmount_packs();
    fs::remove_all(temp, ec);
    std::printf("deploy library: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("deploy library", run_deploy_library_suite)
