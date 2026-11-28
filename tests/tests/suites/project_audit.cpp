/*
 * The project audit (editor/core/audit):
 *   - audit settings: written and read back alike, a newer version refused, ignores kept with their reasons, a
 *     threshold equal to the rule's own dropped from the file;
 *   - reports: written and read back alike, a newer version refused, CSV fields quoted, two reports compared;
 *   - asset rules: an uncompressed texture's size, a texture over the platform's side, a large decoded clip, a mesh
 *     without LODs, duplicate files, each only for the profiles that ship the asset and over their thresholds;
 *   - normal maps: a compiled map's green direction read from its texels or its source's name, and the issue and
 *     fix for one that points down;
 *   - settings rules: a phone or browser profile costlier than its platform's start, a high physics rate; one issue
 *     for profiles that find the same, profiles joined;
 *   - scene rules: missing references named by entity, components a profile's limits turn off;
 *   - code rules: the analysis tool on a compiled script finds every rule's pattern on the update method's path, a
 *     cached lambda is no allocation, and findings of one method and target make one issue;
 *   - deploy reports: files listed with their categories, two reports compared.
 */

#include "../tests.h"
#include "normal_map_fixture.h"

#include <asset_compiler/normal_map_green.h>
#include <asset_compiler/texture_target.h>
#include <audit/audit_catalog.h>
#include <audit/audit_code.h>
#include <audit/audit_config.h>
#include <audit/audit_facts.h>
#include <audit/audit_report_io.h>
#include <audit/audit_rules.h>
#include <audit/audit_run.h>
#include <deploy/deploy_report.h>
#include <deploy/profile.h>
#include <dotnetpp/dotnet_jit.h>
#include <engine/meta/assets/asset_database.hpp>
#include <engine/rendering/material_graph/material_graph_compiler.h>
#include <engine/meta/assets/asset_importer_meta.hpp>
#include <engine/scripting/ecs/systems/script_system.h>
#include <filesystem/filesystem.h>
#include <subprocess/subprocess.hpp>
#include <uuid/uuid.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace unravel::test;

namespace
{
namespace audit = unravel::audit;
namespace deployment = unravel::deployment;
using unravel::target_platform;

constexpr std::uint64_t MIB = 1024 * 1024;

auto count_rule(const std::vector<audit::issue>& issues, const std::string& rule) -> std::size_t
{
    return static_cast<std::size_t>(std::count_if(issues.begin(),
                                                  issues.end(),
                                                  [&rule](const audit::issue& item)
                                                  {
                                                      return item.rule == rule;
                                                  }));
}

auto find_rule_issue(const std::vector<audit::issue>& issues, const std::string& rule) -> const audit::issue*
{
    const auto found = std::find_if(issues.begin(),
                                    issues.end(),
                                    [&rule](const audit::issue& item)
                                    {
                                        return item.rule == rule;
                                    });
    return found != issues.end() ? &*found : nullptr;
}

auto contains(const std::string& text, const std::string& part) -> bool
{
    return text.find(part) != std::string::npos;
}

/// A profile that ships every asset the content lists, as used.
auto make_profile(const std::string& name, target_platform platform, const std::vector<std::string>& shipped)
    -> audit::profile_record
{
    audit::profile_record profile;
    profile.name = name;
    profile.platform = platform;
    profile.deploy.deploy_target = platform;
    profile.deploy.profile = name;
    profile.start = profile.resolved;
    for(const auto& key : shipped)
    {
        deployment::content_entry entry;
        entry.key = key;
        entry.is_used = true;
        entry.use = deployment::content_reason::scene;
        profile.content.entries.push_back(entry);
    }
    profile.content.build_index();
    return profile;
}

auto make_texture(const std::string& key, std::uint32_t side, bool is_compressed) -> audit::asset_record
{
    auto importer = std::make_shared<unravel::texture_importer_meta>();
    importer->quality.max_size = unravel::texture_importer_meta::texture_size::size_4096;
    importer->quality.compression = is_compressed ? unravel::texture_importer_meta::compression_quality::normal_quality
                                                  : unravel::texture_importer_meta::compression_quality::none;
    audit::asset_record asset;
    asset.key = key;
    asset.importer = importer;
    audit::texture_facts facts;
    facts.width = side;
    facts.height = side;
    facts.mips = 1;
    facts.format = is_compressed ? "BC7" : "RGBA8";
    facts.is_compressed = is_compressed;
    facts.bits_per_pixel = is_compressed ? 8 : 32;
    asset.facts.texture = facts;
    return asset;
}

/// Runs one family of rules over the input with default settings.
template<typename Check>
auto check_with(const audit::input& in, Check check, const audit::config& settings = {}) -> std::vector<audit::issue>
{
    std::vector<audit::issue> issues;
    audit::detail::rule_context context(in, settings, issues);
    check(context);
    return issues;
}

void test_config_round_trip()
{
    audit::config value;
    value.ignore("texture.uncompressed|asset:app:/data/a.png", "Pixel art: compression blurs it.");
    value.set_rule_enabled(audit::rules::CODE_OBJECT, false);
    const auto* oversized = audit::find_rule(audit::rules::TEXTURE_OVERSIZED);
    require(oversized != nullptr, "the catalog has the oversized texture rule");
    value.set_threshold(*oversized, audit::thresholds::MAX_SIDE, target_platform::android, 1024);

    audit::config read;
    std::string error;
    check(audit::read_config(audit::write_config(value), read, error), "a written config reads back: " + error);
    check(read == value, "the config reads back alike");
    check(read.find_ignore("texture.uncompressed|asset:app:/data/a.png") != nullptr &&
              read.find_ignore("texture.uncompressed|asset:app:/data/a.png")->reason == "Pixel art: compression blurs it.",
          "an ignore keeps its reason");
    check(!read.is_rule_enabled(audit::rules::CODE_OBJECT) && read.is_rule_enabled(audit::rules::CODE_LINQ),
          "a disabled rule stays disabled and the others run");
    check(read.get_threshold(*oversized, audit::thresholds::MAX_SIDE, target_platform::android) == 1024 &&
              read.get_threshold(*oversized, audit::thresholds::MAX_SIDE, target_platform::desktop) == 4096,
          "a threshold set for one platform leaves the others at the rule's own");

    read.set_threshold(*oversized, audit::thresholds::MAX_SIDE, target_platform::android, 2048);
    check(read.thresholds.empty(), "a threshold set back to the rule's own leaves the file");
    check(read.unignore("texture.uncompressed|asset:app:/data/a.png") && read.ignores.empty(), "an issue can stop being ignored");
    check(!read.unignore("missing"), "stopping to ignore an issue that is not ignored changes nothing");

    std::string newer = audit::write_config(value);
    const auto version = newer.find("\"version\": 1");
    require(version != std::string::npos, "the config names its version");
    newer.replace(version, std::string("\"version\": 1").size(), "\"version\": 99");
    audit::config refused;
    check(!audit::read_config(newer, refused, error) && contains(error, "newer engine"), "a newer config is refused");
}

auto make_sample_report() -> audit::report
{
    audit::report value;
    value.engine_version = "1.2.0-320";
    value.project = "Sample";
    value.finished_at = 1763460000;
    value.seconds = 2.5;
    value.profiles = {"Desktop", "Android"};
    audit::issue item;
    item.rule = audit::rules::MISSING_ASSET;
    item.level = audit::severity::error;
    item.areas = audit::area::correctness | audit::area::quality;
    item.where = {audit::subject_kind::entity, "app:/data/Main.spfb", "b90b71f4-1e48-394d-be48-957f76567566", "Root/Player", 0};
    item.discriminator = "uid";
    item.message = "A material field refers to the asset x, which is not in the project.";
    item.details = {"Android: a new string."};
    item.lines = {4, 9};
    item.call_path = {"A.OnUpdate", "A.Move"};
    item.profiles = {"Android"};
    item.fix = audit::fixes::COMPRESS_TEXTURE;
    item.is_ignored = true;
    item.ignore_reason = "Comma, \"quotes\" and a line\nbreak.";
    value.issues.push_back(item);
    audit::insight_table table{"textures", "Textures", "Every texture.", {"Texture", "Width"}, {}};
    audit::insight_cell number;
    number.text = "2048";
    number.number = 2048;
    number.is_number = true;
    table.rows.push_back({{"app:/data/a.png", 0, false, "app:/data/a.png"}, number});
    value.insights.push_back(table);
    value.notes = {"The script analysis was not asked for."};
    return value;
}

void test_report_round_trip()
{
    const auto value = make_sample_report();
    audit::report read;
    std::string error;
    require(audit::read_report(audit::write_report(value), read, error), "a written report reads back: " + error);
    require(read.issues.size() == 1 && read.insights.size() == 1, "the report keeps its issues and insights");
    const auto& item = read.issues.front();
    check(item.get_id() == value.issues.front().get_id(), "an issue keeps its id");
    check(item.level == audit::severity::error && item.areas == value.issues.front().areas, "an issue keeps its severity and areas");
    check(item.where == value.issues.front().where, "an issue keeps its subject, the entity's label included");
    check(item.details == value.issues.front().details && item.profiles == value.issues.front().profiles &&
              item.fix == value.issues.front().fix,
          "an issue keeps its details, profiles and fix");
    check(item.lines == value.issues.front().lines && item.call_path == value.issues.front().call_path,
          "an issue keeps its lines and its call path");
    check(item.is_ignored && item.ignore_reason == value.issues.front().ignore_reason, "an issue keeps its ignore");
    check(read.insights.front().rows.front()[1].is_number && read.insights.front().rows.front()[1].number == 2048,
          "an insight keeps its numbers");
    check(read.notes == value.notes && read.engine_version == "1.2.0-320", "a report keeps its notes and engine version");
    check(read.count(audit::severity::error) == 0, "an ignored issue leaves the counts");

    const auto csv = audit::write_issues_csv(value);
    check(contains(csv, "\"Comma, \"\"quotes\"\" and a line\nbreak.\""), "a CSV field with separators is quoted, its quotes doubled");
    check(csv.rfind("Severity,Rule,Title", 0) == 0, "the CSV starts with its header");
    check(contains(csv, ",\"4, 9\",") && contains(csv, "A.OnUpdate > A.Move"), "the CSV lists the lines and the call path");

    auto newer = value;
    newer.issues.front().where.key = "app:/data/Other.spfb";
    const auto change = audit::compare_reports(value, newer);
    check(change.added.size() == 1 && change.fixed.size() == 1, "a moved subject is a new issue and a fixed one");

    // A rule a run did not check (turned off, or its input not read) neither finds nor fixes anything.
    auto checked = value;
    checked.checked_rules = {audit::rules::MISSING_ASSET, audit::rules::DEPLOY_PROBLEM};
    audit::report read_checked;
    require(audit::read_report(audit::write_report(checked), read_checked, error), "a report with its checked rules reads back");
    check(read_checked.checked_rules == checked.checked_rules, "a report keeps the rules it checked");
    auto without_rule = checked;
    without_rule.issues.clear();
    without_rule.checked_rules = {audit::rules::DEPLOY_PROBLEM};
    check(audit::compare_reports(checked, without_rule).fixed.empty(),
          "an issue of a rule the newer run did not check is not counted as gone");
    check(audit::compare_reports(without_rule, checked).added.empty(),
          "an issue of a rule the older run did not check is not counted as new");
    without_rule.checked_rules = {audit::rules::MISSING_ASSET};
    check(audit::compare_reports(checked, without_rule).fixed.size() == 1, "an issue both runs checked counts as gone when it is gone");
}

void test_texture_facts_from_compiled_file()
{
    // A compiled KTX2 texture larger than the header read: its levels run past the first bytes, so the first parse
    // fails and the whole file is read. Neither attempt may assert.
    // The engine's compiled files sit beside its script library.
    const auto file = fs::resolve_protocol(unravel::script_system::get_lib_compiled_key("engine")).parent_path() / "logos" /
                      "made_with.png.asset";
    fs::error_code ec;
    if(!fs::exists(file, ec))
    {
        skip("the engine's compiled logo texture is not in this build's data");
    }
    audit::asset_record asset;
    asset.key = "engine:/data/logos/made_with.png";
    asset.importer = std::make_shared<unravel::texture_importer_meta>();
    asset.compiled_files = {file};
    const auto facts = audit::read_asset_facts(asset, nullptr);
    require(facts.texture.has_value(), "the compiled texture's facts are read");
    check(facts.texture->width > 0 && facts.texture->height > 0 && !facts.texture->format.empty(),
          "the texture's size and format come from its container");
}

void test_texture_rules()
{
    audit::input in;
    in.assets.push_back(make_texture("app:/data/big.png", 4096, false));
    in.assets.push_back(make_texture("app:/data/small.png", 128, false));
    in.assets.push_back(make_texture("app:/data/compressed.png", 4096, true));
    in.assets.push_back(make_texture("app:/data/unshipped.png", 4096, false));
    in.profiles.push_back(make_profile("Desktop", target_platform::desktop, {"app:/data/big.png", "app:/data/small.png", "app:/data/compressed.png"}));
    in.profiles.push_back(make_profile("Android", target_platform::android, {"app:/data/big.png", "app:/data/small.png", "app:/data/compressed.png"}));
    in.editor_texture_target = {};
    const auto issues = check_with(in, &audit::detail::check_assets);

    std::size_t uncompressed = 0;
    for(const auto& item : issues)
    {
        if(item.rule == audit::rules::TEXTURE_UNCOMPRESSED)
        {
            ++uncompressed;
            check(item.where.key == "app:/data/big.png", "only the large uncompressed texture is reported: " + item.where.key);
            // 4096 x 4096 RGBA8 with its mipmaps is 85.3 MB.
            check(contains(item.message, "4096 x 4096 RGBA8 with mipmaps") && contains(item.message, "about 85.3 MB"),
                  "the message gives the size and the memory: " + item.message);
            check(item.fix == audit::fixes::COMPRESS_TEXTURE, "the issue offers compression");
        }
        check(item.where.key != "app:/data/unshipped.png", "a texture no profile ships is not judged");
    }
    check_eq(uncompressed, 2, "one uncompressed issue per shipping profile before merging");

    std::vector<std::string> oversized_profiles;
    for(const auto& item : issues)
    {
        if(item.rule == audit::rules::TEXTURE_OVERSIZED)
        {
            oversized_profiles.insert(oversized_profiles.end(), item.profiles.begin(), item.profiles.end());
            check(contains(item.message, "2048-pixel guideline"), "the message names the platform's guideline: " + item.message);
        }
    }
    // Desktop's guideline is 4096, which a 4096 texture does not exceed; Android's is 2048.
    check(std::count(oversized_profiles.begin(), oversized_profiles.end(), "Android") == 2 &&
              std::count(oversized_profiles.begin(), oversized_profiles.end(), "Desktop") == 0,
          "a 4096 texture is over Android's guideline and within desktop's");

    audit::config settings;
    settings.set_threshold(*audit::find_rule(audit::rules::TEXTURE_OVERSIZED), audit::thresholds::MAX_SIDE, target_platform::android, 8192);
    const auto relaxed = check_with(in, &audit::detail::check_assets, settings);
    check(count_rule(relaxed, audit::rules::TEXTURE_OVERSIZED) == 0, "a project threshold changes what is reported");
    settings.set_rule_enabled(audit::rules::TEXTURE_UNCOMPRESSED, false);
    check(count_rule(check_with(in, &audit::detail::check_assets, settings), audit::rules::TEXTURE_UNCOMPRESSED) == 0,
          "a disabled rule reports nothing");
}

/// A texture imported as a normal map, with the green direction the run read from its compiled file.
auto make_normal_map(const std::string& key,
                     unravel::asset_compiler::normal_green green,
                     unravel::texture_importer_meta::normal_direction normal_y) -> audit::asset_record
{
    auto asset = make_texture(key, 1024, true);
    auto importer = std::make_shared<unravel::texture_importer_meta>();
    importer->type = unravel::texture_importer_meta::texture_type::normal_map;
    importer->normal_y = normal_y;
    asset.importer = importer;
    asset.facts.texture->normal_green = green;
    return asset;
}

void test_normal_map_green_rule()
{
    using unravel::asset_compiler::normal_green;
    using normal_direction = unravel::texture_importer_meta::normal_direction;
    audit::input in;
    in.assets.push_back(make_normal_map("app:/data/untold.png", normal_green::down, normal_direction::automatic));
    in.assets.push_back(make_normal_map("app:/data/turned_down.png", normal_green::down, normal_direction::down));
    in.assets.push_back(make_normal_map("app:/data/kept_down.png", normal_green::down, normal_direction::up));
    in.assets.push_back(make_normal_map("app:/data/up.png", normal_green::up, normal_direction::automatic));
    in.assets.push_back(make_normal_map("app:/data/flat.png", normal_green::unknown, normal_direction::automatic));
    // A color texture is not judged, whatever was read of it.
    in.assets.push_back(make_texture("app:/data/color.png", 1024, true));
    in.assets.back().facts.texture->normal_green = normal_green::down;
    const auto issues = check_with(in, &audit::detail::check_assets);

    check_eq(count_rule(issues, audit::rules::NORMAL_MAP_GREEN_DOWN), 3, "only normal maps that read green down once compiled are reported");
    for(const auto& item : issues)
    {
        if(item.rule != audit::rules::NORMAL_MAP_GREEN_DOWN)
        {
            continue;
        }
        if(item.where.key == "app:/data/untold.png")
        {
            check(item.fix == audit::fixes::SET_NORMAL_Y_DOWN && contains(item.message, "Auto could not tell"),
                  "a map Auto could not read offers Down: " + item.message);
        }
        else if(item.where.key == "app:/data/kept_down.png")
        {
            check(item.fix == audit::fixes::SET_NORMAL_Y_DOWN && contains(item.message, "set to Up"),
                  "a green-down map set to Up offers Down: " + item.message);
        }
        else
        {
            check(item.where.key == "app:/data/turned_down.png" && item.fix == audit::fixes::SET_NORMAL_Y_UP,
                  "a green-up map that Down turns over offers Up: " + item.where.key);
        }
    }
    check(std::string(audit::get_fix_label(audit::fixes::SET_NORMAL_Y_DOWN)) != audit::get_fix_label(audit::fixes::SET_NORMAL_Y_UP) &&
              audit::get_fix_label(audit::fixes::SET_NORMAL_Y_UP)[0] != '\0',
          "each of the two fixes has its own label");

    audit::config settings;
    settings.set_rule_enabled(audit::rules::NORMAL_MAP_GREEN_DOWN, false);
    check(count_rule(check_with(in, &audit::detail::check_assets, settings), audit::rules::NORMAL_MAP_GREEN_DOWN) == 0,
          "the rule can be turned off");
}

void test_normal_map_green_facts(const fs::path& folder)
{
    using unravel::asset_compiler::normal_green;
    using normal_direction = unravel::texture_importer_meta::normal_direction;
    require_tool("texturec");
    const std::string source_folder_key = "app:/data/audit_green";
    const fs::path source_folder = fs::resolve_protocol(source_folder_key);
    const fs::path source = source_folder / "bumps.png";
    fs::error_code ec;
    fs::remove_all(source_folder, ec);
    constexpr std::uint32_t SIDE = 512;
    require(write_rgba_png(source, make_surface_normal_map({.is_green_up = false}), SIDE, SIDE), "the green-down map is written");

    const auto write_settings = [&](normal_direction normal_y)
    {
        unravel::asset_meta meta;
        meta.uid = hpp::uuid::from_string("70000000-0000-4000-8000-000000000002").value();
        meta.type = ".png";
        auto importer = std::make_shared<unravel::texture_importer_meta>();
        importer->type = unravel::texture_importer_meta::texture_type::normal_map;
        importer->normal_y = normal_y;
        meta.importer = importer;
        unravel::save_to_file(source.string() + ".meta", meta);
        return importer;
    };
    const fs::path compiled = folder / "bumps.png.asset";
    const auto read_compiled = [&](normal_direction normal_y) -> hpp::optional<audit::texture_facts>
    {
        audit::asset_record asset;
        asset.key = source_folder_key + "/bumps.png";
        asset.source_file = source;
        asset.importer = write_settings(normal_y);
        asset.compiled_files = {compiled};
        if(!unravel::asset_compiler::compile_texture_for_target(asset.key, compiled, {}))
        {
            return hpp::nullopt;
        }
        return audit::read_asset_facts(asset, nullptr).texture;
    };

    const auto on_auto = read_compiled(normal_direction::automatic);
    require(on_auto.has_value(), "the normal map compiles and its facts are read");
    check(on_auto->normal_green == normal_green::up, "a green-down map on Auto compiles turned over and reads green up: " + on_auto->format);
    const auto on_up = read_compiled(normal_direction::up);
    require(on_up.has_value(), "the normal map compiles with Normal Y Up");
    check(on_up->normal_green == normal_green::down, "set to Up it compiles as it is and reads green down: " + on_up->format);
    const auto on_down = read_compiled(normal_direction::down);
    require(on_down.has_value(), "the normal map compiles with Normal Y Down");
    check(on_down->normal_green == normal_green::up, "set to Down it compiles turned over and reads green up: " + on_down->format);

    // A source whose name states the convention is not read: the compiled file here still holds green-up texels.
    audit::asset_record named;
    named.key = source_folder_key + "/wall_nor_gl.png";
    named.source_file = source_folder / "wall_nor_gl.png";
    auto named_settings = std::make_shared<unravel::texture_importer_meta>();
    named_settings->type = unravel::texture_importer_meta::texture_type::normal_map;
    named_settings->normal_y = normal_direction::down;
    named.importer = named_settings;
    named.compiled_files = {compiled};
    const auto named_facts = audit::read_asset_facts(named, nullptr).texture;
    check(named_facts.has_value() && named_facts->normal_green == normal_green::down,
          "a map named green up that Down turns over reads green down");
    named_settings->normal_y = normal_direction::automatic;
    const auto named_auto_facts = audit::read_asset_facts(named, nullptr).texture;
    check(named_auto_facts.has_value() && named_auto_facts->normal_green == normal_green::up,
          "and on Auto, which goes by its name, it reads green up");

    // A texture that is no normal map is not read at all.
    audit::asset_record color = named;
    color.importer = std::make_shared<unravel::texture_importer_meta>();
    const auto color_facts = audit::read_asset_facts(color, nullptr).texture;
    check(color_facts.has_value() && color_facts->normal_green == normal_green::unknown, "a color texture has no green direction");
    fs::remove_all(source_folder, ec);
}

void test_audio_and_mesh_rules()
{
    audit::input in;
    audit::asset_record clip;
    clip.key = "app:/data/music.ogg";
    clip.importer = std::make_shared<unravel::audio_importer_meta>();
    clip.facts.audio = audit::audio_facts{180.0, 44100, 2, 180ull * 44100 * 2 * 2};
    in.assets.push_back(clip);
    audit::asset_record model;
    model.key = "app:/data/statue.fbx";
    auto mesh_importer = std::make_shared<unravel::mesh_importer_meta>();
    mesh_importer->model.generate_lods = false;
    model.importer = mesh_importer;
    model.facts.mesh = audit::mesh_facts{30000, 50000, 2, 0};
    in.assets.push_back(model);
    in.profiles.push_back(make_profile("Desktop", target_platform::desktop, {clip.key, model.key}));
    in.profiles.push_back(make_profile("Android", target_platform::android, {clip.key, model.key}));
    const auto issues = check_with(in, &audit::detail::check_assets);

    const auto* decoded = find_rule_issue(issues, audit::rules::AUDIO_LARGE_DECODED);
    require(decoded != nullptr, "a 3-minute stereo clip is large on Android");
    check(decoded->profiles == std::vector<std::string>{"Android"}, "desktop's larger guideline keeps it off desktop");
    check(contains(decoded->message, "30.3 MB") && contains(decoded->message, "stereo") && contains(decoded->message, "44100 Hz"),
          "the message gives the decoded size and the sound: " + decoded->message);
    check(decoded->fix == audit::fixes::FORCE_MONO, "a stereo clip offers Force To Mono");

    const auto* lods = find_rule_issue(issues, audit::rules::MESH_NO_LODS);
    require(lods != nullptr, "a 50000-triangle mesh without LODs is reported on Android");
    check(count_rule(issues, audit::rules::MESH_NO_LODS) == 1 && lods->profiles == std::vector<std::string>{"Android"},
          "desktop's 100000-triangle guideline keeps it off desktop");
    check(lods->fix == audit::fixes::GENERATE_LODS, "the mesh issue offers Generate LODs");
}

void test_heavy_morph_rule()
{
    audit::input in;
    audit::asset_record face;
    face.key = "app:/data/face.glb";
    face.importer = std::make_shared<unravel::mesh_importer_meta>();
    audit::mesh_facts facts{20000, 30000, 1, 3};
    facts.morph_targets = 52;
    facts.morphed_vertices = 18000;
    facts.morph_bytes = 6ull * 1024 * 1024;
    face.facts.mesh = facts;
    in.assets.push_back(face);
    in.profiles.push_back(make_profile("Desktop", target_platform::desktop, {face.key}));
    in.profiles.push_back(make_profile("Android", target_platform::android, {face.key}));
    in.profiles.push_back(make_profile("Web", target_platform::web, {face.key}));
    const auto issues = check_with(in, &audit::detail::check_assets);
    check(count_rule(issues, audit::rules::MESH_HEAVY_MORPHS) == 2, "6 MB of morph deltas are heavy on Android and the web only");
    const auto* heavy = find_rule_issue(issues, audit::rules::MESH_HEAVY_MORPHS);
    require(heavy != nullptr, "a heavy face is reported");
    check(contains(heavy->message, "52 morph targets") && contains(heavy->message, "6.0 MB"),
          "the message gives the targets and their size: " + heavy->message);

    audit::input light = in;
    light.assets[0].facts.mesh->morph_bytes = 1024 * 1024;
    const auto web_only = check_with(light, &audit::detail::check_assets);
    const auto* web = find_rule_issue(web_only, audit::rules::MESH_HEAVY_MORPHS);
    check(count_rule(web_only, audit::rules::MESH_HEAVY_MORPHS) == 1 && web != nullptr &&
              web->profiles == std::vector<std::string>{"Web"} && contains(web->message, "18000 vertices"),
          "a light face moving many vertices is reported on the web alone");
}

void test_duplicate_files(const fs::path& folder)
{
    fs::error_code ec;
    fs::create_directories(folder, ec);
    const std::string bytes(128 * 1024, 'x');
    const auto write = [&](const char* name, const std::string& content)
    {
        std::ofstream(folder / name, std::ios::binary) << content;
    };
    write("a.png", bytes);
    write("b.png", bytes);
    write("c.png", std::string(bytes.size(), 'y'));
    write("tiny_a.png", "same");
    write("tiny_b.png", "same");
    audit::input in;
    for(const char* name : {"a.png", "b.png", "c.png", "tiny_a.png", "tiny_b.png"})
    {
        audit::asset_record asset;
        asset.key = std::string("app:/data/") + name;
        asset.source_file = folder / name;
        asset.importer = std::make_shared<unravel::texture_importer_meta>();
        in.assets.push_back(asset);
    }
    const auto issues = check_with(in, &audit::detail::check_assets);
    require(count_rule(issues, audit::rules::DUPLICATE_FILES) == 1, "one group of duplicates over the size threshold");
    const auto* duplicate = find_rule_issue(issues, audit::rules::DUPLICATE_FILES);
    check(duplicate->where.key == "app:/data/a.png" && duplicate->details == std::vector<std::string>{"The same file: app:/data/b.png."} &&
              contains(duplicate->message, "1 other file holds"),
          "the group is named by its first key and lists the others");
}

void test_settings_rules()
{
    unravel::settings base;
    audit::input in;
    for(const char* name : {"Android", "Phone"})
    {
        auto profile = make_profile(name, target_platform::android, {});
        profile.start = deployment::resolve_settings(base, deployment::make_profile(name, target_platform::android, base));
        profile.resolved = profile.start;
        profile.resolved.graphics.limits.global_illumination = true;
        profile.resolved.physics.fixed_timestep = 0.004f;
        profile.resolved.graphics.eviction.keep_mip_tails = false;
        profile.resolved.graphics.eviction.max_upload_mb = profile.start.graphics.eviction.max_upload_mb * 8;
        profile.resolved.graphics.reflection_probe_faces_per_frame = profile.start.graphics.reflection_probe_faces_per_frame + 4;
        in.profiles.push_back(profile);
    }
    auto web = make_profile("Web", target_platform::web, {});
    web.start = deployment::resolve_settings(base, deployment::make_profile("Web", target_platform::web, base));
    web.resolved = web.start;
    web.resolved.graphics.material_quality = unravel::material_graph_quality::high;
    web.resolved.physics.fixed_timestep = 1.0f / 120.0f;
    in.profiles.push_back(web);
    const auto issues = check_with(in, &audit::detail::check_settings);

    std::size_t gi = 0;
    for(const auto& item : issues)
    {
        if(item.rule == audit::rules::COSTLIER_THAN_START && item.where.key == "graphics/limits/global_illumination")
        {
            ++gi;
            check(item.level == audit::severity::warning, "a phone profile's costlier setting is a warning");
            check(contains(item.message, "Global Illumination is on, where a new profile of its platform starts with off"),
                  "the message compares the value with the start: " + item.message);
            check(item.fix == audit::fixes::USE_PLATFORM_START, "the issue offers the starting value");
        }
        if(item.rule == audit::rules::COSTLIER_THAN_START && item.where.key == "graphics/material_quality")
        {
            check(item.level == audit::severity::info && item.profiles == std::vector<std::string>{"Web"},
                  "a browser profile's costlier setting is info");
        }
    }
    check_eq(gi, 2, "each phone profile reports its GI");
    std::size_t paging = 0;
    for(const auto& item : issues)
    {
        if(item.rule == audit::rules::COSTLIER_THAN_START &&
           (item.where.key == "graphics/eviction/keep_mip_tails" || item.where.key == "graphics/eviction/max_upload_mb"))
        {
            ++paging;
        }
        check(!(item.rule == audit::rules::COSTLIER_THAN_START && item.where.key == "graphics/eviction/reduce_textures_in_use"),
              "a paging setting left at its start is not reported");
    }
    check_eq(paging, 4, "each phone profile reports mip tails turned off and a raised upload limit");
    std::size_t probe_faces = 0;
    for(const auto& item : issues)
    {
        if(item.rule == audit::rules::COSTLIER_THAN_START && item.where.key == "graphics/reflection_probe_faces_per_frame")
        {
            ++probe_faces;
            check(item.level == audit::severity::warning && item.fix == audit::fixes::USE_PLATFORM_START,
                  "more probe faces a frame than a phone starts with is a warning that offers the start");
        }
    }
    check_eq(probe_faces, 2, "each phone profile reports its raised reflection probe faces");
    std::size_t rates = 0;
    for(const auto& item : issues)
    {
        if(item.rule == audit::rules::PHYSICS_RATE)
        {
            ++rates;
            check(contains(item.message, "250 times a second"), "the rate is named: " + item.message);
        }
    }
    check_eq(rates, 2, "a 250 Hz rate is reported and a 120 Hz one is not");

    // Issues of one id merge in a run, their profiles joined.
    in.source = {};
    const auto report = audit::run_audit(in, {}, {});
    std::size_t merged = 0;
    for(const auto& item : report.issues)
    {
        if(item.rule == audit::rules::COSTLIER_THAN_START && item.where.key == "graphics/limits/global_illumination")
        {
            ++merged;
            check(item.profiles == std::vector<std::string>{"Android", "Phone"}, "the merged issue names both profiles");
        }
    }
    check_eq(merged, 1, "two profiles with the same finding make one issue");
    check(report.profiles.size() == 3, "the report names the profiles it judged");
}

void test_scene_rules()
{
    audit::input in;
    in.scan.ran = true;
    in.scan.missing.push_back({"app:/data/Main.spfb", "uid-1", "Root/Lamp", "material", unravel::generate_uuid()});
    audit::feature_use decals;
    decals.document = "app:/data/Main.spfb";
    decals.component = "Decal";
    decals.feature = audit::limited_feature::decals;
    decals.entities = {"Root/Crack", "Root/Stain"};
    in.scan.features.push_back(decals);
    auto desktop = make_profile("Desktop", target_platform::desktop, {"app:/data/Main.spfb"});
    auto android = make_profile("Android", target_platform::android, {"app:/data/Main.spfb"});
    android.resolved.graphics.limits.decals = false;
    in.profiles = {desktop, android};
    const auto issues = check_with(in, &audit::detail::check_scenes);

    const auto* missing = find_rule_issue(issues, audit::rules::MISSING_ASSET);
    require(missing != nullptr, "a missing reference is reported");
    check(missing->where.kind == audit::subject_kind::entity && missing->where.detail == "uid-1" &&
              missing->where.label == "Root/Lamp",
          "the issue names the entity by uid, labelled by its path");
    check(missing->level == audit::severity::error && contains(missing->message, "A material field"),
          "the missing reference is an error naming the field's type");
    const auto* feature = find_rule_issue(issues, audit::rules::FEATURE_OFF);
    require(feature != nullptr, "decals the Android profile turns off are reported");
    check(feature->profiles == std::vector<std::string>{"Android"} && count_rule(issues, audit::rules::FEATURE_OFF) == 1,
          "only the profile that turns decals off reports them");
    check(contains(feature->message, "draw nothing") && feature->details.size() == 2, "the issue says what happens and lists the entities");

    in.scan.scene_color_materials = {"app:/data/Main.spfb"};
    in.profiles[1].resolved.graphics.limits.translucent_scene_color = false;
    const auto glass_issues = check_with(in, &audit::detail::check_scenes);
    const auto* glass = find_rule_issue(glass_issues, audit::rules::SCENE_COLOR_OFF);
    require(glass != nullptr, "a material reading the scene where the Android profile turns the copy off is reported");
    check(glass->profiles == std::vector<std::string>{"Android"} && count_rule(glass_issues, audit::rules::SCENE_COLOR_OFF) == 1 &&
              contains(glass->message, "refracts nothing"),
          "only the profile without the scene copy reports it, saying what happens");

    in.scan.emitter_materials.push_back({"app:/data/Main.spfb", "uid-fire", "Root/Fire", "app:/data/Materials/Rock.mat"});
    const auto emitter_issues = check_with(in, &audit::detail::check_scenes);
    const auto* emitter = find_rule_issue(emitter_issues, audit::rules::EMITTER_MATERIAL);
    require(emitter != nullptr, "an emitter whose material cannot draw particles is reported");
    check(emitter->where.detail == "uid-fire" && emitter->where.label == "Root/Fire" && emitter->level == audit::severity::warning &&
              contains(emitter->message, "app:/data/Materials/Rock.mat") && contains(emitter->message, "plain particle material"),
          "the issue names the entity and the material, and says what draws in its place");

    audit::feature_use splats;
    splats.document = "app:/data/Main.spfb";
    splats.component = "Particle Emitter";
    splats.feature = audit::limited_feature::collision_decals;
    splats.entities = {"Root/Paint"};
    in.scan.features.push_back(splats);
    const auto splat_issues = check_with(in, &audit::detail::check_scenes);
    bool reports_splats = false;
    for(const auto& item : splat_issues)
    {
        reports_splats = reports_splats ||
                         (item.rule == audit::rules::FEATURE_OFF && item.profiles == std::vector<std::string>{"Android"} &&
                          contains(item.message, "1 Particle Emitter component leaves no decals where its particles land"));
    }
    check(reports_splats, "an emitter whose contacts leave decals is reported where the profile turns decals off");

    in.scan.local_space_collisions.push_back({"app:/data/Main.spfb", "uid-rain", "Root/Rain", {}});
    in.scan.collider_emitters.push_back({"app:/data/Main.spfb", {"Root/Sparks", "Root/Debris"}, 60.0 * 1500.0});
    in.profiles[0].resolved.graphics.particles.max_collision_casts = 2048;
    in.profiles[1].resolved.graphics.particles.max_collision_casts = 512;
    const auto collision_issues = check_with(in, &audit::detail::check_scenes);
    const auto* local_collision = find_rule_issue(collision_issues, audit::rules::LOCAL_SPACE_COLLISION);
    require(local_collision != nullptr, "an emitter that collides in Local space is reported");
    check(local_collision->where.detail == "uid-rain" && local_collision->level == audit::severity::warning &&
              contains(local_collision->message, "Collision does nothing") && contains(local_collision->message, "Local space"),
          "the issue names the entity and says why nothing collides");
    const auto* casts = find_rule_issue(collision_issues, audit::rules::COLLISION_CASTS);
    require(casts != nullptr, "emitters that need more casts than a profile allows are reported");
    check(count_rule(collision_issues, audit::rules::COLLISION_CASTS) == 1 && casts->profiles == std::vector<std::string>{"Android"} &&
              contains(casts->message, "2 Particle Emitters that collide with physics colliders need about 1500 casts a frame") &&
              contains(casts->message, "allows 512") && casts->details.size() == 2,
          "only the profile whose limit is lower reports it, with the casts, the limit and the emitters");

    // What an emitter on Scene Surfaces asks for that no such contact gives: every loss is named, a mesh emitter's
    // alone, and an emitter that asks for none of it is not reported.
    audit::scene_contact_needs hail_needs;
    check(audit::make_scene_contact_message(hail_needs).empty(), "an emitter that waits for nothing of its contacts is not reported");
    hail_needs.fires_on_death = true;
    hail_needs.lights_scene = true;
    const std::string hail_message = audit::make_scene_contact_message(hail_needs);
    check(contains(hail_message, "collides with Scene Surfaces") && contains(hail_message, "sub-emitter on Death fires where") &&
              contains(hail_message, "particle lights follow") && !contains(hail_message, "on Collision") &&
              !contains(hail_message, "death events"),
          "the message names each thing the emitter asks for, and nothing it does not");
    audit::scene_contact_needs every_need = hail_needs;
    every_need.fires_on_collision = true;
    every_need.sends_death_events = true;
    const std::string every_message = audit::make_scene_contact_message(every_need);
    check(contains(every_message, "on Collision never fires") && contains(every_message, "death events give") &&
              contains(every_message, "on Death fires where") && contains(every_message, "particle lights follow"),
          "an emitter that asks for all of it reads of all of it in one issue");
    audit::scene_contact_needs mesh_needs = every_need;
    mesh_needs.draws_meshes = true;
    const std::string mesh_message = audit::make_scene_contact_message(mesh_needs);
    check(contains(mesh_message, "meshes, which do not touch scene surfaces") && contains(mesh_message, "Collision does nothing") &&
              !contains(mesh_message, "sub-emitter"),
          "a mesh emitter reads that nothing collides, not of contacts it never has");
    in.scan.unreported_scene_contacts.push_back({"app:/data/Main.spfb", "uid-hail", "Root/Hail", hail_message});
    in.scan.scene_surface_emitters.push_back({"app:/data/Main.spfb", {"Root/Hail", "Root/Embers"}});
    in.profiles[1].resolved.graphics.particles.scene_collision = unravel::ps_soa::scene_collision::off;
    const auto scene_collision_issues = check_with(in, &audit::detail::check_scenes);
    const auto* unreported = find_rule_issue(scene_collision_issues, audit::rules::SCENE_CONTACT_UNREPORTED);
    require(unreported != nullptr, "an emitter that waits for contacts with scene surfaces is reported");
    check(unreported->where.detail == "uid-hail" && unreported->level == audit::severity::warning &&
              unreported->message == hail_message,
          "the issue names the entity and carries the message");
    const auto* collision_off = find_rule_issue(scene_collision_issues, audit::rules::SCENE_COLLISION_OFF);
    require(collision_off != nullptr, "emitters that collide with scene surfaces are reported where a profile turns that off");
    check(count_rule(scene_collision_issues, audit::rules::SCENE_COLLISION_OFF) == 1 &&
              collision_off->profiles == std::vector<std::string>{"Android"} &&
              collision_off->level == audit::severity::info &&
              contains(collision_off->message, "2 Particle Emitters collide with Scene Surfaces, and the profile's") &&
              contains(collision_off->message, "Scene Collision is Off. In its builds") && collision_off->details.size() == 2,
          "only the profile that turns it off reports it, with the emitters");
    in.profiles[1].resolved.graphics.particles.scene_collision = unravel::ps_soa::scene_collision::depth_buffer;

    // An emitter that asks for the Global Illumination's light with a material that leaves it no place: the message
    // names what the material reads, and a material that fits is not reported.
    const std::string smoke_material = "app:/data/Materials/Smoke.mat";
    check(audit::make_gi_ambient_message(smoke_material, 2, false, 2).empty() && audit::make_gi_ambient_message(smoke_material, 0, false, 2).empty(),
          "a material of two textures or fewer that reads no Scene Color is not reported");
    const std::string heavy_message = audit::make_gi_ambient_message(smoke_material, 3, false, 2);
    check(contains(heavy_message, "Material app:/data/Materials/Smoke.mat reads 3 textures.") &&
              contains(heavy_message, "at most 2 textures") && contains(heavy_message, "keep the sky's light"),
          "a material of three textures reads how many it has and how many fit");
    check(contains(audit::make_gi_ambient_message(smoke_material, 1, true, 2), "reads the Scene Color.") &&
              contains(audit::make_gi_ambient_message(smoke_material, 4, true, 2), "reads 4 textures and the Scene Color."),
          "a read of the Scene Color is named alone and beside the textures");
    // The audit reports exactly the materials the renderer refuses.
    bool agrees_with_renderer = true;
    for(std::uint32_t textures = 0; textures <= unravel::MATERIAL_GRAPH_MAX_PARTICLE_GI_TEXTURES + 2; ++textures)
    {
        for(const bool reads_scene_color : {false, true})
        {
            const bool is_reported = !audit::make_gi_ambient_message(smoke_material,
                                                                      textures,
                                                                      reads_scene_color,
                                                                      unravel::MATERIAL_GRAPH_MAX_PARTICLE_GI_TEXTURES)
                                          .empty();
            const bool fits = unravel::fits_material_graph_particle_gi(textures,
                                                                       reads_scene_color ? unravel::material_scene_texture_color : 0u);
            agrees_with_renderer = agrees_with_renderer && is_reported != fits;
        }
    }
    check(agrees_with_renderer, "the audit reports a material exactly when a particle draw cannot give it the Global Illumination's light");
    in.scan.gi_ambient_misfits.push_back({"app:/data/Main.spfb", "uid-smoke", "Root/Smoke", heavy_message});
    const auto ambient_issues = check_with(in, &audit::detail::check_scenes);
    const auto* misfit = find_rule_issue(ambient_issues, audit::rules::GI_AMBIENT_NOT_TAKEN);
    require(misfit != nullptr, "an emitter whose material cannot take the Global Illumination's light is reported");
    check(count_rule(ambient_issues, audit::rules::GI_AMBIENT_NOT_TAKEN) == 1 && misfit->where.detail == "uid-smoke" &&
              misfit->level == audit::severity::warning && misfit->message == heavy_message,
          "the issue names the entity and carries the message");

    in.scan.unshown_colors.push_back({"app:/data/Main.spfb", "uid-rope", "Root/Rope", "Line", "app:/data/Materials/Rock.mat"});
    in.scan.unshown_colors.push_back({"app:/data/Main.spfb", "uid-arc", "Root/Arc", "Trail", "app:/data/Materials/Rock.mat"});
    const auto color_issues = check_with(in, &audit::detail::check_scenes);
    const auto* unshown = find_rule_issue(color_issues, audit::rules::COLOR_NOT_SHOWN);
    require(unshown != nullptr, "a line whose colour its material never shows is reported");
    check(count_rule(color_issues, audit::rules::COLOR_NOT_SHOWN) == 2 && unshown->where.detail == "uid-rope" &&
              unshown->level == audit::severity::info && contains(unshown->message, "Line's Color Over Length") &&
              contains(unshown->message, "app:/data/Materials/Rock.mat") && contains(unshown->message, "Vertex Color"),
          "each one is an issue that names the component's setting, the material and the node it lacks");

    in.scan.unsuited_strip_materials.push_back({"app:/data/Main.spfb", "uid-rope", "Root/Rope", "Line", "app:/data/Materials/Stain.mat"});
    const auto strip_issues = check_with(in, &audit::detail::check_scenes);
    const auto* unsuited = find_rule_issue(strip_issues, audit::rules::STRIP_MATERIAL);
    require(unsuited != nullptr, "a line whose material is no Surface material is reported");
    check(unsuited->where.detail == "uid-rope" && unsuited->level == audit::severity::warning &&
              contains(unsuited->message, "The Line's Material app:/data/Materials/Stain.mat") &&
              contains(unsuited->message, "engine's strip material"),
          "the issue names the component and the material, and says what draws in its place");

    in.scan.cameras.push_back({"app:/data/Main.spfb", "uid-cam", "Root/Close Camera", 0.001f, 1000.0f});
    in.scan.cameras.push_back({"app:/data/Main.spfb", "uid-wide", "Root/Wide Camera", 0.1f, 1000.0f});
    auto web_gpu = make_profile("WebGPU", target_platform::web, {"app:/data/Main.spfb"});
    web_gpu.deploy.web.graphics = unravel::web_graphics::webgpu;
    auto web_gl = make_profile("WebGL", target_platform::web, {"app:/data/Main.spfb"});
    web_gl.deploy.web.graphics = unravel::web_graphics::webgpu_then_webgl2;
    // Desktop OpenGL clips depth 0..1 where the driver has clip control: a desktop profile is not reported.
    const auto desktop_gl = make_profile("Desktop", target_platform::desktop, {"app:/data/Main.spfb"});
    in.profiles = {desktop_gl, web_gpu, web_gl};
    const auto camera_issues = check_with(in, &audit::detail::check_scenes);
    const auto* camera = find_rule_issue(camera_issues, audit::rules::CAMERA_DEPTH_RANGE);
    require(camera != nullptr, "a camera whose far clip is a million times its near clip is reported where WebGL 2 ships");
    const auto* no_compute = find_rule_issue(camera_issues, audit::rules::SCENE_COLLISION_OFF);
    check(count_rule(camera_issues, audit::rules::SCENE_COLLISION_OFF) == 1 && no_compute != nullptr &&
              no_compute->profiles == std::vector<std::string>{"WebGL"} &&
              contains(no_compute->message, "can run on WebGL 2, which has no compute shaders. Where a browser runs WebGL 2"),
          "emitters that collide with scene surfaces are reported where WebGL 2 ships, which has no compute shaders");
    check(count_rule(camera_issues, audit::rules::CAMERA_DEPTH_RANGE) == 1 && camera->profiles == std::vector<std::string>{"WebGL"},
          "only the profile shipping WebGL 2 reports it, and a 10000 to 1 camera is not reported");
    check(camera->where.detail == "uid-cam" && contains(camera->message, "1000000 times"),
          "the issue names the camera and the ratio");
    audit::config no_limit;
    no_limit.set_threshold(*audit::find_rule(audit::rules::CAMERA_DEPTH_RANGE), audit::thresholds::MAX_FAR_TO_NEAR, target_platform::web, 0);
    check(count_rule(check_with(in, &audit::detail::check_scenes, no_limit), audit::rules::CAMERA_DEPTH_RANGE) == 0,
          "a far to near threshold of 0 turns the check off");

    in.scan.far_entities.push_back({"app:/data/Main.spfb", "uid-far", "Root/Outpost", 25000.0});
    in.scan.far_entities.push_back({"app:/data/Main.spfb", "uid-near", "Root/Camp", 900.0});
    const auto far_issues = check_with(in, &audit::detail::check_scenes);
    const auto* outpost = find_rule_issue(far_issues, audit::rules::FAR_FROM_ORIGIN);
    require(outpost != nullptr, "an entity 25 km from the origin along an axis is reported");
    check(count_rule(far_issues, audit::rules::FAR_FROM_ORIGIN) == 1 && outpost->where.detail == "uid-far" &&
              outpost->profiles.size() == 3 && contains(outpost->message, "25.0 km") &&
              contains(outpost->message, "2.0 mm"),
          "the far entity alone is reported, once for every profile shipping it, with its coordinate and the float "
          "step");
    in.scan.far_entities.clear();

    in.scan.ran = false;
    check(check_with(in, &audit::detail::check_scenes).empty(), "without a scan the scene rules report nothing");
}

void test_code_grouping()
{
    audit::input in;
    in.code.ran = true;
    const auto finding = [](const char* rule, std::uint32_t line, const char* method, const char* target, const char* detail,
                            std::vector<std::string> path)
    {
        return audit::code_finding{rule, "app:/data/Player.cs", line, method, target, detail, std::move(path)};
    };
    in.code.findings.push_back(finding(audit::rules::CODE_LOG, 30, "Player.OnUpdate", "Log.Info", "", {"Player.OnUpdate"}));
    in.code.findings.push_back(finding(audit::rules::CODE_LOG, 12, "Player.OnUpdate", "Log.Info", "", {"Player.OnUpdate"}));
    in.code.findings.push_back(finding(audit::rules::CODE_LOG, 40, "Player.Move", "Log.Info", "", {"Player.OnUpdate", "Player.Move"}));
    in.code.findings.push_back(finding("code.unknown", 1, "Player.OnUpdate", "x", "", {}));
    in.code.findings.push_back(finding(audit::rules::CODE_ARRAY, 50, "Player.Move", "Object[]", "String.Format", {"Player.OnUpdate", "Player.Move"}));
    in.code.findings.push_back(finding(audit::rules::CODE_ARRAY, 51, "Player.Move", "Object[]", "", {"Player.OnUpdate", "Player.Move"}));
    in.code.findings.push_back(finding(audit::rules::CODE_ALLOCATING_CALL, 52, "Player.Move", "Entity.name", "a new string on every call", {"Player.OnUpdate", "Player.Move"}));
    in.code.findings.push_back(finding(audit::rules::CODE_STRING, 60, "Player.Move", "Int32.ToString", "", {"Player.OnUpdate", "Player.Move"}));
    in.code.findings.push_back(finding(audit::rules::CODE_STRING, 60, "Player.Move", "String.Concat", "", {"Player.OnUpdate", "Player.Move"}));
    in.code.findings.push_back(finding(audit::rules::CODE_OBJECT, 70, "Player.Spawn (iterator)", "WaitForSeconds", "", {"Player.Spawn (iterator)"}));
    const auto issues = check_with(in, &audit::detail::check_code);
    check_eq(issues.size(), 7, "findings of one rule, method and target make one issue; a params array is its own; unknown rules are dropped");
    const auto by_method = [&issues](const char* rule, const char* method) -> const audit::issue*
    {
        const auto found = std::find_if(issues.begin(),
                                        issues.end(),
                                        [&](const audit::issue& item)
                                        {
                                            return item.rule == rule && item.where.detail == method;
                                        });
        return found != issues.end() ? &*found : nullptr;
    };
    const auto* first = by_method(audit::rules::CODE_LOG, "Player.OnUpdate");
    require(first != nullptr, "the log finding is reported");
    check(first->where.line == 12 && first->lines == std::vector<std::uint32_t>{12, 30} && first->details.empty(),
          "the issue lists its lines in order, points at the first, and keeps them out of the details");
    check(!contains(first->message, "12") && !contains(first->message, "Line"), "the message does not repeat the lines");
    const auto* moved = by_method(audit::rules::CODE_LOG, "Player.Move");
    require(moved != nullptr, "the finding off the update method is reported");
    check(moved->call_path == std::vector<std::string>{"Player.OnUpdate", "Player.Move"},
          "a finding off the update method keeps how it is reached");
    const auto* call = by_method(audit::rules::CODE_ALLOCATING_CALL, "Player.Move");
    require(call != nullptr, "the allocating engine call is reported");
    check(contains(call->message, "Entity.name") && contains(call->message, "a new string"),
          "the allocating call says what it allocates");
    const auto params = std::count_if(issues.begin(),
                                      issues.end(),
                                      [](const audit::issue& item)
                                      {
                                          return item.rule == audit::rules::CODE_ARRAY && contains(item.message, "params argument of String.Format");
                                      });
    check_eq(params, 1, "a params array names the call it is passed to");
    const auto* strings = by_method(audit::rules::CODE_STRING, "Player.Move");
    require(strings != nullptr, "the string building is reported");
    check(contains(strings->message, "Int32.ToString and String.Concat") && strings->lines == std::vector<std::uint32_t>{60},
          "a concatenation and the ToString of its parts make one issue that names both");
    const auto* coroutine = by_method(audit::rules::CODE_OBJECT, "Player.Spawn (iterator)");
    require(coroutine != nullptr, "the coroutine's allocation is reported");
    check(contains(coroutine->message, "each time the coroutine resumes") && !contains(coroutine->message, "each time it runs"),
          "code in a coroutine runs each time the coroutine resumes, not every frame");
}

const char* const AUDIT_PROBE_SOURCE = R"cs(
using System.Linq;
using Unravel.Core;

public class AuditProbe : ScriptComponent
{
    private int[] values = new int[8];

    public override void OnUpdate()
    {
        var found = Scene.FindEntityByName("Player");
        var hits = Physics.RaycastAll(new Vector3(0, 0, 0), new Vector3(0, 0, 1));
        int total = values.Where(v => v > 0).Count();
        int threshold = total;
        int above = values.Count(v => v > threshold);
        object boxed = total;
        string text = "Total: " + total.ToString();
        var list = new System.Collections.Generic.List<int>();
        var array = new float[4];
        Log.Info(text);
        Use(found, hits, above, boxed, list, array);
        Helper();
    }

    private void Use(Entity a, RaycastHit[] b, int c, object d, System.Collections.Generic.List<int> e, float[] f)
    {
    }

    private void Keep(object value)
    {
    }

    private void Helper()
    {
        var method = typeof(AuditProbe).GetMethod("Helper");
        Keep(method);
    }
}

public class EmptyProbe : ScriptComponent
{
    public override void OnUpdate()
    {
    }
}

public class CacheProbe : ScriptComponent
{
    private System.Collections.Generic.Dictionary<int, System.Collections.Generic.List<int>> cache =
        new System.Collections.Generic.Dictionary<int, System.Collections.Generic.List<int>>();
    private System.Collections.Generic.List<int>[] slots = new System.Collections.Generic.List<int>[4];
    private int frame;

    public override void OnUpdate()
    {
        frame++;
        int key = frame % 4;
        if (!cache.TryGetValue(key, out var list))
        {
            list = new System.Collections.Generic.List<int>();
            cache[key] = list;
        }
        slots[key] ??= new System.Collections.Generic.List<int>();
        ModelComponent model = owner.GetComponent<ModelComponent>();
        Keep(list, slots[key], model != null ? model.GetMaterialInstance(0) : null);
        AudioSourceComponent audio = owner.GetComponent<AudioSourceComponent>();
        UIDocumentComponent ui = owner.GetComponent<UIDocumentComponent>();
        UIDocument document = ui != null ? ui.document : null;
        Keep(audio != null ? audio.clip : null, document != null ? document.root : null, Describe(key));
        MaterialInstance instance = model != null ? model.GetMaterialInstance(0) : null;
        Keep(instance != null ? instance.sharedMaterial : null, null, null);
    }

    private Label label;

    // Remade only when the key changes: a cache with a stale check, filled through an object initializer.
    private Label Describe(int key)
    {
        if (label == null || label.Key != key)
        {
            label = new Label { Key = key };
        }
        return label;
    }

    private void Keep(object a, object b, object c)
    {
    }
}

public class Label
{
    public int Key;
}

public class JoinProbe : ScriptComponent
{
    private string label;

    public override void OnUpdate()
    {
        // An array of its own, kept in a local and then joined: not the params array of a concatenation.
        var parts = new string[2];
        parts[0] = "a";
        parts[1] = label;
        label = string.Join(",", parts);
    }
}

public class ReplaceProbe : ScriptComponent
{
    private System.Collections.Generic.List<int> live;
    private System.Collections.Generic.List<int> scratch;

    public override void OnUpdate()
    {
        // Replaced every frame while it is set, and made every frame: neither is a cache.
        if (live != null)
        {
            live = new System.Collections.Generic.List<int>();
        }
        scratch = new System.Collections.Generic.List<int>();
    }
}

public class CoroutineProbe : ScriptComponent
{
    public string Sequence = "a|b";
    private int seen;

    public override void OnStart()
    {
        StartCoroutine(Run());
    }

    private System.Collections.IEnumerator Run()
    {
        string[] parts = Sequence.Split('|');
        Prepare();
        while (true)
        {
            yield return new WaitForSeconds(1.0f);
            seen += parts.Length;
        }
    }

    private void Prepare()
    {
        var once = new System.Collections.Generic.List<int>();
        seen = once.Count;
    }
}

public class ColdPathProbe : ScriptComponent
{
    public Prefab Character;
    private System.Collections.Generic.List<int> cache;
    private float[] buffer;
    private System.Collections.Generic.IEnumerable<int> items = new int[4];
    private int count;

    public override void OnLateUpdate()
    {
        if (!Spawn())
        {
            throw new System.InvalidOperationException("The spawn failed after " + count + " entities.");
        }
        cache ??= new System.Collections.Generic.List<int>();
        if (buffer == null)
        {
            buffer = new float[16];
        }
        try
        {
            count++;
        }
        catch (System.Exception error)
        {
            string text = "Caught " + error.Message;
            Keep(text);
        }
        Keep(owner.name);
        Keep(owner.transform.children);
        Keep(Assets.GetAsset<Prefab>("Prefabs/Enemy.pfb"));
        foreach (var item in items)
        {
            count += item;
        }
        Keep(System.DateTime.Now);
        if (count > 1000)
        {
            System.GC.Collect();
            System.Threading.Thread.Sleep(1);
            Keep(System.IO.File.Exists("save.json"));
        }
        string name = owner.name;
        Keep(string.Format("{0}{1}{2}{3}", name, name, name, name));
        Many(1, 2, 3);
    }

    private void Many(params int[] values)
    {
    }

    private bool Spawn()
    {
        Entity entity = Scene.Instantiate(Character);
        if (!entity.IsValid())
        {
            Log.Error($"ColdPathProbe: the Character prefab did not instantiate after {count} entities.");
            return false;
        }
        if (count < 0)
        {
            Log.Warning("ColdPathProbe: the count is negative: " + count.ToString());
        }
        return true;
    }

    private void Keep(System.DateTime value)
    {
    }

    private void Keep(bool value)
    {
    }

    private void Keep(object value)
    {
    }
}
)cs";

auto find_findings(const audit::code_analysis& code, const std::string& rule, const std::string& method)
    -> std::vector<const audit::code_finding*>
{
    std::vector<const audit::code_finding*> found;
    for(const auto& finding : code.findings)
    {
        if(finding.rule == rule && finding.method == method)
        {
            found.push_back(&finding);
        }
    }
    return found;
}

auto has_finding(const audit::code_analysis& code, const std::string& rule, const std::string& method, const std::string& target)
    -> bool
{
    const auto found = find_findings(code, rule, method);
    return std::any_of(found.begin(),
                       found.end(),
                       [&target](const audit::code_finding* finding)
                       {
                           return finding->target == target;
                       });
}

auto count_findings(const audit::code_analysis& code, const std::string& rule) -> std::size_t
{
    return static_cast<std::size_t>(std::count_if(code.findings.begin(),
                                                  code.findings.end(),
                                                  [&rule](const audit::code_finding& finding)
                                                  {
                                                      return finding.rule == rule;
                                                  }));
}

void test_code_tool(const fs::path& folder)
{
    const auto tool = fs::resolve_protocol("binary:/code_audit/Unravel.CodeAudit.dll");
    const auto engine_library = fs::resolve_protocol(unravel::script_system::get_lib_compiled_key("engine"));
    fs::error_code ec;
    if(!fs::exists(tool, ec))
    {
        skip("the script analysis tool is not built beside the runner (build the code_audit target)");
    }
    if(!fs::exists(engine_library, ec))
    {
        skip("the engine's script library is not compiled yet: start the editor of this build once");
    }
    fs::create_directories(folder, ec);
    const auto source = folder / "AuditProbe.cs";
    std::ofstream(source, std::ios::binary) << AUDIT_PROBE_SOURCE;

    dotnet::compiler_params params;
    params.files = {source.string()};
    params.output_name = (folder / "audit-probe.dll").string();
    params.references = {engine_library.filename().string()};
    params.references_locations = {engine_library.parent_path().string()};
    // The runner does not start the script bridge, which names the framework's reference assemblies to the
    // compiler: the ones it keeps beside itself are passed here.
    for(const auto& framework : fs::directory_iterator(fs::resolve_protocol("binary:/clrpp/ref"), ec))
    {
        for(const auto& file : fs::directory_iterator(framework.path(), ec))
        {
            if(file.path().extension() == ".dll")
            {
                params.references.push_back(file.path().string());
            }
        }
    }
    params.debug = true;
    params.debug_symbols = true;
    // The compiler alone: dotnet::compile also weaves the library, which needs the script bridge running.
    const auto command = dotnet::create_compile_command_detailed_rsp(params, (folder / "audit-probe.rsp").string());
    std::vector<std::string> arguments{command.cmd};
    arguments.insert(arguments.end(), command.args.begin(), command.args.end());
    const auto compiled = subprocess::call(arguments);
    require(compiled.retcode == 0 && fs::exists(params.output_name, ec),
            "the probe script compiles against the engine's library: " + compiled.out_output);

    audit::code_tool_paths paths;
    paths.dotnet = dotnet::create_compile_command_detailed(dotnet::compiler_params{}).cmd;
    paths.tool = tool;
    paths.assembly = params.output_name;
    paths.reference_folders = {engine_library.parent_path()};
    for(const auto& entry : fs::directory_iterator(fs::resolve_protocol("binary:/clrpp/ref"), ec))
    {
        paths.reference_folders.push_back(entry.path());
    }
    paths.project_root = folder;
    const auto code = audit::run_code_analysis(paths, {});
    require(code.ran, "the analysis runs: " + code.error);
    check_eq(code.entry_points.size(), 7, "the six update methods and the coroutine are entry points");

    const std::string probe = "AuditProbe.OnUpdate";
    check_eq(count_findings(code, audit::rules::CODE_FIND_ENTITY), 1, "the entity search is found");
    check_eq(count_findings(code, audit::rules::CODE_CAST_ALL), 1, "the allocating raycast is found");
    check(count_findings(code, audit::rules::CODE_LINQ) >= 2, "both LINQ calls are found");
    check_eq(find_findings(code, audit::rules::CODE_BOXING, probe).size(), 1, "the boxed int is found");
    check(find_findings(code, audit::rules::CODE_STRING, probe).size() >= 2, "the concatenation and the ToString are found");
    check(has_finding(code, audit::rules::CODE_ARRAY, probe, "Single[]"), "the float array is found");
    check(has_finding(code, audit::rules::CODE_OBJECT, probe, "List<Int32>"), "the list is found");
    check_eq(count_findings(code, audit::rules::CODE_LOG), 1, "the routine log line is found, the warning and error are not");
    check_eq(count_findings(code, audit::rules::CODE_EMPTY_UPDATE), 1, "the empty update method is found");
    // The capturing lambda allocates its closure and its delegate; the one that captures nothing is cached.
    std::size_t delegates = 0;
    std::size_t closures = 0;
    for(const auto& finding : code.findings)
    {
        if(finding.rule == audit::rules::CODE_CLOSURE)
        {
            delegates += finding.detail == "delegate" ? 1 : 0;
            closures += finding.detail == "closure" ? 1 : 0;
        }
    }
    check_eq(closures, 1, "the capturing lambda's closure is found");
    check_eq(delegates, 1, "only the capturing lambda's delegate is found, not the cached one");

    const auto reflection = std::find_if(code.findings.begin(),
                                         code.findings.end(),
                                         [](const audit::code_finding& finding)
                                         {
                                             return finding.rule == audit::rules::CODE_REFLECTION;
                                         });
    require(reflection != code.findings.end(), "the reflection in a called method is found");
    check(reflection->method == "AuditProbe.Helper" &&
              reflection->path == std::vector<std::string>{"AuditProbe.OnUpdate", "AuditProbe.Helper"},
          "a finding off the update method keeps its path");
    check(reflection->line > 0 && contains(reflection->file, "AuditProbe.cs"), "findings carry the script's file and line");

    // Error paths and one-time setup are not per-frame costs.
    const std::string late = "ColdPathProbe.OnLateUpdate";
    const std::string spawn = "ColdPathProbe.Spawn";
    check(find_findings(code, audit::rules::CODE_STRING, spawn).empty() && find_findings(code, audit::rules::CODE_LOG, spawn).empty(),
          "the strings built for Log.Error and Log.Warning on an error branch are not reported");
    check(find_findings(code, audit::rules::CODE_OBJECT, late).empty(),
          "neither the exception thrown on an error branch nor the list made once is reported");
    check(!has_finding(code, audit::rules::CODE_ARRAY, late, "Single[]"), "the buffer made once is not reported");
    check(find_findings(code, audit::rules::CODE_STRING, late).size() == 1 && has_finding(code, audit::rules::CODE_STRING, late, "String.Format"),
          "only the string built every run is reported, not the ones in the throw and the catch");
    check(has_finding(code, audit::rules::CODE_INSTANTIATE, spawn, "Scene.Instantiate"), "the instantiation is found");

    // A coroutine's code before its loop runs once each time it starts; its loop runs again and again.
    const std::string coroutine = "CoroutineProbe.Run (iterator)";
    check(find_findings(code, audit::rules::CODE_STRING, coroutine).empty(), "the split before the coroutine's loop is not reported");
    check(find_findings(code, audit::rules::CODE_OBJECT, "CoroutineProbe.Prepare").empty(),
          "a method the coroutine calls before its loop is not followed");
    check(has_finding(code, audit::rules::CODE_OBJECT, coroutine, "WaitForSeconds"), "the wait made in the coroutine's loop is reported");

    // What a cache makes once is not a per-frame cost: an entry a lookup missed, a slot left empty until first use, and
    // the engine's own cached wrappers.
    const std::string cached = "CacheProbe.OnUpdate";
    check(find_findings(code, audit::rules::CODE_OBJECT, cached).empty(), "the list a cache makes on a miss and the one a ??= makes are not reported");
    check(!has_finding(code, audit::rules::CODE_ALLOCATING_CALL, cached, "ModelComponent.GetMaterialInstance") &&
              !has_finding(code, audit::rules::CODE_ALLOCATING_CALL, cached, "AudioSourceComponent.clip") &&
              !has_finding(code, audit::rules::CODE_ALLOCATING_CALL, cached, "UIDocumentComponent.document") &&
              !has_finding(code, audit::rules::CODE_ALLOCATING_CALL, cached, "UIDocument.root"),
          "engine calls that make their object once and keep it are not reported");
    const auto shared = find_findings(code, audit::rules::CODE_ALLOCATING_CALL, cached);
    const auto shared_material = std::find_if(shared.begin(),
                                              shared.end(),
                                              [](const audit::code_finding* finding)
                                              {
                                                  return finding->target == "MaterialInstance.sharedMaterial";
                                              });
    require(shared_material != shared.end(), "an engine getter that allocates on some of its paths is reported");
    check(contains((*shared_material)->detail, "on some calls"), "it says only some calls allocate");
    check(find_findings(code, audit::rules::CODE_OBJECT, "CacheProbe.Describe").empty(),
          "an object remade only when its key changes is not reported");
    check(has_finding(code, audit::rules::CODE_ARRAY, "JoinProbe.OnUpdate", "String[]"),
          "an array kept in a local and passed to a call later is reported as an array of its own");
    const auto replaced = find_findings(code, audit::rules::CODE_OBJECT, "ReplaceProbe.OnUpdate");
    check_eq(replaced.size(), 2, "an object replaced while it is set, and one made every frame, are reported");

    // The engine calls that allocate, and the other per-frame costs.
    const auto names = find_findings(code, audit::rules::CODE_ALLOCATING_CALL, late);
    const auto name = std::find_if(names.begin(),
                                   names.end(),
                                   [](const audit::code_finding* finding)
                                   {
                                       return finding->target == "Entity.name";
                                   });
    require(name != names.end(), "reading an entity's name is found as an allocating call");
    check((*name)->detail == "a new string on every call", "the name allocates a new string on every call");
    const auto children = std::find_if(names.begin(),
                                       names.end(),
                                       [](const audit::code_finding* finding)
                                       {
                                           return finding->target == "TransformComponent.children";
                                       });
    require(children != names.end(), "reading a transform's children is found as an allocating call");
    check((*children)->detail == "a new Entity[] on every call", "the children allocate a new Entity[] on every call");
    check(!has_finding(code, audit::rules::CODE_ALLOCATING_CALL, late, "Entity.transform") &&
              !has_finding(code, audit::rules::CODE_ALLOCATING_CALL, late, "Component.owner"),
          "engine getters that allocate nothing are not reported");
    check(has_finding(code, audit::rules::CODE_ASSET_LOOKUP, late, "Assets.GetAsset"), "the asset lookup by key is found");
    check(has_finding(code, audit::rules::CODE_INTERFACE_ENUMERATION, late, "IEnumerable<Int32>"),
          "the loop through an interface is found");
    check(has_finding(code, audit::rules::CODE_LOCAL_TIME, late, "DateTime.Now"), "the local time read is found");
    check(has_finding(code, audit::rules::CODE_GC_COLLECT, late, "GC.Collect"), "the forced collection is found");
    check(has_finding(code, audit::rules::CODE_BLOCKING_WAIT, late, "Thread.Sleep"), "the blocking wait is found");
    check(has_finding(code, audit::rules::CODE_FILE_ACCESS, late, "File.Exists"), "the file access is found");
    const auto arrays = find_findings(code, audit::rules::CODE_ARRAY, late);
    // String.Format takes its arguments in a span on the stack (params ReadOnlySpan), so only Many allocates one.
    check(arrays.size() == 1 && arrays.front()->target == "Int32[]" && arrays.front()->detail == "ColdPathProbe.Many",
          "the params array names the call it is passed to");
}

void test_deploy_reports(const fs::path& folder)
{
    fs::error_code ec;
    fs::create_directories(folder / "data" / "app" / "compiled", ec);
    std::ofstream(folder / "data" / "app" / "compiled" / "wall.png.asset", std::ios::binary) << std::string(1000, 'a');
    std::ofstream(folder / "data" / "app" / "compiled" / "hit.wav.asset", std::ios::binary) << std::string(300, 'b');
    std::ofstream(folder / "data" / "app" / "compiled" / "app-script.dll", std::ios::binary) << std::string(50, 'c');
    const auto files = deployment::list_deploy_files(folder);
    require(files.size() == 3, "every file is listed");
    std::map<std::string, std::string> categories;
    for(const auto& file : files)
    {
        categories[file.path] = file.category;
    }
    check(categories["data/app/compiled/wall.png.asset"] == "Textures" && categories["data/app/compiled/hit.wav.asset"] == "Audio" &&
              categories["data/app/compiled/app-script.dll"] == "Scripts",
          "files fall in their categories");

    deployment::deploy_report older;
    older.build_bytes = 1350;
    older.files = files;
    auto newer = older;
    newer.files[2].bytes += 500;
    newer.files.push_back({"data/app/compiled/new.png.asset", "Textures", 2000});
    newer.build_bytes = 3850;
    const auto change = deployment::compare_deploy_reports(older, newer);
    check(change.build_bytes == 2500, "the build's growth is the difference");
    check(!change.categories.empty() && change.categories.front().first == "Textures" && change.categories.front().second == 2500,
          "the category that grew most comes first");
    check(change.files.size() == 2 && change.files.front().first == "data/app/compiled/new.png.asset",
          "new and grown files are listed, largest first");

    deployment::deploy_report read;
    std::string error;
    check(deployment::read_deploy_report(deployment::write_deploy_report(newer), read, error) && read.files.size() == 4 &&
              read.build_bytes == 3850,
          "a deploy report reads back: " + error);
}

void run_project_audit_suite(rtti::context& ctx)
{
    (void)ctx;
    const auto temp = temp_path("project_audit");
    fs::error_code ec;
    fs::remove_all(temp, ec);
    try
    {
        RUN_CASE(test_config_round_trip());
        RUN_CASE(test_report_round_trip());
        RUN_CASE(test_texture_facts_from_compiled_file());
        RUN_CASE(test_texture_rules());
        RUN_CASE(test_normal_map_green_rule());
        RUN_CASE(test_normal_map_green_facts(temp / "green"));
        RUN_CASE(test_audio_and_mesh_rules());
        RUN_CASE(test_heavy_morph_rule());
        RUN_CASE(test_duplicate_files(temp / "duplicates"));
        RUN_CASE(test_settings_rules());
        RUN_CASE(test_scene_rules());
        RUN_CASE(test_code_grouping());
        RUN_CASE(test_code_tool(temp / "code"));
        RUN_CASE(test_deploy_reports(temp / "deploy"));
    }
    catch(const std::exception& ex)
    {
        check(false, std::string("unexpected exception: ") + ex.what());
    }
    fs::remove_all(temp, ec);
}

} // namespace

REGISTER_TEST_SUITE("project audit", run_project_audit_suite, {.lane = ::unravel::suite_lane::tools})
