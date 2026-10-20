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
 *   - the content tree: counts, folders first, rows of open folders only, search and filter rows,
 *     left out but used assets counted;
 *   - files: classification, compiled paths, a target's shader variants, pack inputs with cooked
 *     variants, stale binaries, older runtime versions;
 *   - platform folders under the deploy folder; deploy locations relative to the project, and the
 *     project folders the editor manages;
 *   - Android: default and invalid application ids, adb device lists and one entry per phone, logcat
 *     lines, which are the game's or the device system's and the engine lines' text, the player template's
 *     stamp checks, Gradle's command line, the start command with the game's options;
 *   - run arguments: split on whitespace, quoted for a device shell;
 *   - subprocess::call hands over output lines and adds environment variables; subprocess::stream stops
 *     its process at once;
 *   - a protocol shipped packed, loose, packed again, as a patch over a kept release and full again,
 *     with left out assets;
 *   - releases: names, keep / list / remove, and their refusals;
 *   - dependency scanner output parsed; a failed scan throws every time;
 *   - folder checks: earlier deploys, Android package folders, inside / outside, an executable in use;
 *   - unravel::process::spawn_detached runs a program and reports one that does not start.
 * Everything lives in a temp folder.
 */

#include "../tests.h"

#include <deploy/android.h>
#include <deploy/android_icon.h>
#include <deploy/content.h>
#include <deploy/content_tree.h>
#include <deploy/dependencies.h>
#include <deploy/files.h>
#include <deploy/folders.h>
#include <deploy/keys.h>
#include <deploy/protocol_data.h>
#include <deploy/releases.h>
#include <deploy/run_arguments.h>
#include <engine/assets/impl/asset_extensions.h>
#include <filesystem/filesystem.h>
#include <filesystem/pack.h>
#include <process/process.h>
#include <subprocess/subprocess.hpp>

#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/file.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace
{
namespace deployment = unravel::deployment;
using unravel::asset_database;
using unravel::deploy_settings;

constexpr const char* TEST_PROTOCOL = "unravel-tests-deploy";
constexpr std::size_t COMPILED_WALL_SIZE = 123;
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
    check(deployment::is_script_key("app:/data/scripts/game.cs") && !deployment::is_script_key("app:/data/a.png"),
          "scripts are told by their extension");
    const auto compiled = deployment::get_compiled_paths("app:/data/a/b.png", root);
    check(compiled == std::vector<fs::path>{root / "compiled/a/b.png.asset"}, "an asset compiles to one file");
    check(deployment::get_compiled_paths("app:/settings/settings.cfg", root).empty(), "a key outside the data folder has none");
    check(deployment::get_compiled_paths("app:/data/scripts/game.cs", root).empty(), "a script has none");
    check(!deployment::get_compiled_paths("app:/data/shaders/fs_a.sc", root).empty(), "a shader compiles once per renderer");
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
    const auto paths = deployment::get_compiled_paths("app:/data/shaders/fs_a.sc", root, vulkan);
    check(paths.size() == 1 && paths.front().extension() == ".spirv", "a target's shader has its renderer's variant only");
    check(deployment::is_other_shader_variant(root / "compiled/shaders/fs_a.sc.asset.dxbc", vulkan),
          "another renderer's variant is told apart");
    check(!deployment::is_other_shader_variant(root / "compiled/shaders/fs_a.sc.asset.spirv", vulkan), "the target's variant ships");
    check(!deployment::is_other_shader_variant(root / "compiled/a.png.asset", vulkan), "other compiled files are no variant");
    check(!deployment::is_other_shader_variant(root / "compiled/shaders/fs_a.sc.asset.dxbc", {}), "no target ships every variant");
}

//-----------------------------------------------------------------------------
// Android
//-----------------------------------------------------------------------------

void test_platform_locations()
{
    std::printf("test_platform_locations\n");
    deploy_settings params;
    check(params.deploy_location == fs::path(unravel::DEFAULT_DEPLOY_LOCATION), "a project starts with .deploy");
    params.deploy_location.clear();
    check(unravel::get_output_location(params).empty(), "without a deploy folder there is no platform folder");
    params.deploy_location = fs::path("/builds/game");
    check(unravel::get_output_location(params) == fs::path("/builds/game/desktop"), "desktop builds go to desktop/");
    params.deploy_target = unravel::deploy_platform::android;
    check(unravel::get_output_location(params) == fs::path("/builds/game/android"), "Android builds go to android/");
    check(unravel::get_platform_location(params, unravel::deploy_platform::desktop) == fs::path("/builds/game/desktop"),
          "releases read the desktop folder whatever the platform");
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
    check(devices.size() == 3 && devices[0].get_label() == "SM S948B (R3GL508T4SY)" && devices[2].get_label() == "emulator-5554",
          "labels name the model when there is one");
    const std::string spaced = "adb-R5CR11EH5XE-jIeKII (2)._adb-tls-connect._tcp device product:p3sxeea model:SM_G998B "
                               "device:p3s transport_id:4\n"
                               "usb-0001 no permissions (missing udev rules?); see [http://developer.android.com]\n";
    const auto more = deployment::parse_android_devices(spaced);
    check(more.size() == 2 && more[0].serial == "adb-R5CR11EH5XE-jIeKII (2)._adb-tls-connect._tcp" && more[0].is_ready() &&
              more[0].model == "SM G998B",
          "a serial with a space ends at the state word");
    check(more.size() == 2 && more[1].serial == "usb-0001" && more[1].state == "no permissions", "the two-word state is read");
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

void write_android_template(const fs::path& root, const std::string& version, const std::string& configuration)
{
    write_text(root / "template.stamp",
               "engine_version=" + version + "\nconfiguration=" + configuration +
                   "\nabi=arm64-v8a\ndotnet_runtime=10.0.13\nsymbols=/build/bin\n");
    write_text(root / "lib/arm64-v8a/libmain.so", "elf");
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
    fs::remove(root / "template/lib/arm64-v8a/libmain.so", ec);
    check(!deployment::find_android_template(root / "template", "v1", "Release").is_found(), "an incomplete template is refused");
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
    auto arguments = deployment::make_gradle_arguments(tools, params);
    check(!arguments.empty() && arguments.front() == tools.java.string(), "Java runs Gradle's wrapper");
    check(contains_argument(arguments, "assembleDebug") && !contains_argument(arguments, "assembleRelease"), "a debug build");
    check(contains_argument(arguments, "-Punravel.applicationId=com.company.game") &&
              contains_argument(arguments, "-Punravel.versionCode=7") &&
              contains_argument(arguments, "-Punravel.orientation=sensorPortrait") &&
              contains_argument(arguments, "-Punravel.player=/editor/players/android-arm64/lib/arm64-v8a") &&
              contains_argument(arguments, "-Punravel.content=/deploy") && contains_argument(arguments, "-Punravel.icon=/tmp/icon"),
          "the game's identity, its icon and the template's folders are passed");
    params.is_release = true;
    params.keystore = "/keys/game.jks";
    params.key_alias = "game";
    arguments = deployment::make_gradle_arguments(tools, params);
    check(contains_argument(arguments, "assembleRelease") && contains_argument(arguments, "-Punravel.keystore=/keys/game.jks") &&
              contains_argument(arguments, "-Punravel.keyAlias=game"),
          "a release build names its keystore and key");
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
    deployment::android_toolchain tools;
    tools.adb = "/sdk/platform-tools/adb";
    const auto plain = deployment::make_start_arguments(tools, "serial", "com.company.game", {});
    check(!plain.empty() && plain.back() == "com.company.game/com.unravel.player.UnravelActivity" &&
              !contains_argument(plain, "--es"),
          "a start without options sets no extra");
    const auto with_options =
        deployment::make_start_arguments(tools, "serial", "com.company.game", {"--gfx-stats", "--novsync"});
    check(with_options.size() == plain.size() + 3 && with_options[plain.size()] == "--es" &&
              with_options[plain.size() + 1] == "arguments" && with_options.back() == "'--gfx-stats --novsync'",
          "the options go in one quoted extra of the start intent");
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
    subprocess::stream stream;
    check(stream.start(waiting, {}), "a stream starts a process");
    check(stream.is_running(), "it runs");
    const auto stop_begin = std::chrono::steady_clock::now();
    stream.stop();
    check(!stream.is_running() && std::chrono::steady_clock::now() - stop_begin < std::chrono::seconds(20),
          "stopping ends it at once");
}

void test_pack_inputs(const fs::path& root)
{
    std::printf("test_pack_inputs\n");
    const auto compiled = root / "compiled";
    const auto cooked = root / "cooked";
    write_text(compiled / "a.png.asset", "a");
    write_text(compiled / "prefabs/b.pfb.asset", "plain");
    write_text(compiled / "scripts/game.dll", "dll");
    write_text(cooked / "prefabs/b.pfb.asset", "cooked");
    write_text(root / ex::get_asset_index_file_name(), "index");
    const auto inputs = deployment::collect_pack_inputs(compiled,
                                                        cooked,
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
    check(names == std::vector<std::string>{ex::get_asset_index_file_name(), "compiled/prefabs/b.pfb.asset"},
          "the pack takes the shipped compiled files and the index, by name, but no library");
    check(inputs.size() == 2 && read_text(inputs[1].source) == "cooked", "a cooked variant replaces its compiled file");
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
    job.cooked_root = root / "cooked/app";
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
    write_text(job.cooked_root / "prefabs/a.pfb.asset", "cooked prefab");
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
    const auto project = root / "project";
    const auto build = root / "build";
    check(!deployment::keep_release(project, build, "1.0").is_ok(), "a folder without a packed build cannot be kept");
    for(const auto* protocol : deployment::PACKED_PROTOCOLS)
    {
        write_text(build / deployment::DATA_FOLDER / protocol / ex::get_pack_file_name(), std::string("pack of ") + protocol);
    }
    check(!deployment::keep_release(project, build, "a/b").is_ok(), "an invalid name is refused");
    check(deployment::keep_release(project, build, "1.0").is_ok(), "a full packed build is kept");
    check(deployment::is_kept_release(project, "1.0"), "the release is kept");
    check(deployment::list_releases(project) == std::vector<std::string>{"1.0"}, "it is listed");
    check(read_text(deployment::get_release_pack(project, "1.0", "app")) == "pack of app", "its packs are copied");
    check(!deployment::keep_release(project, build, "1.0").is_ok(), "a name already kept is refused");
    write_text(build / deployment::DATA_FOLDER / "app" / ex::get_patch_pack_file_name(1), "patch");
    check(!deployment::keep_release(project, build, "1.1").is_ok(), "a patched build cannot be kept");
    check(!deployment::remove_release(project, "2.0").is_ok(), "removing a release that is not kept fails");
    check(deployment::remove_release(project, "1.0").is_ok() && deployment::list_releases(project).empty(),
          "a removed release is gone");
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
    const auto android = root / "android";
    fs::create_directories(android, ec);
    check(deployment::is_android_package_folder(android), "an empty folder takes an Android package");
    write_text(android / "Game.apk", "apk");
    write_text(android / "Other.APK", "apk");
    check(deployment::is_android_package_folder(android), "a folder of packages is an Android deploy's");
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
        test_tree(walked);
        test_file_classification(temp / "files");
        test_shader_variants(temp / "variants");
        test_platform_locations();
        test_deploy_locations(temp / "locations");
        test_application_ids();
        test_android_devices();
        test_logcat_lines();
        test_game_log_lines();
        test_device_log_messages();
        test_merged_devices();
        test_android_template(temp / "android");
        test_gradle_arguments();
        test_android_icon(temp / "icon");
        test_run_arguments();
        test_subprocess(temp / "subprocess");
        test_pack_inputs(temp / "inputs");
        test_stale_binaries(temp / "binaries");
        auto job = make_protocol_job(temp / "protocol");
        test_packed_protocol(job);
        test_loose_protocol(job);
        test_patch_over_release(job, temp / "protocol");
        test_release_names();
        test_keep_and_remove_release(temp / "releases");
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
