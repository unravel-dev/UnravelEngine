#include "mcp_tools_common.h"

#include <deploy/content.h>
#include <deploy/deploy_settings.h>
#include <deploy/keys.h>
#include <editor/deploy/deploy_android.h>
#include <editor/deploy/deploy_pipeline.h>
#include <editor/deploy/deploy_preflight.h>
#include <editor/deploy/project_releases.h>
#include <engine/assets/asset_manager.h>
#include <editor/hub/hub.h>
#include <editor/hub/panels/deploy_panel/deploy_panel.h>
#include <editor/hub/panels/panel.h>
#include <editor/system/mcp_manager.h>
#include <editor/system/project_manager.h>
#include <filesystem/filesystem.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace unravel::mcp
{
namespace
{

constexpr int64_t k_default_deploy_timeout_ms = 180000;
constexpr int64_t k_max_deploy_timeout_ms = 900000;
constexpr auto k_deploy_poll_interval = std::chrono::milliseconds(250);

/// The Deploy window owns the editor's deploy state - the toolbar's Deploy and Run shows the same
/// progress - so a deploy started here is the one the user sees, and two cannot overlap.
auto get_deploy_panel(rtti::context& ctx) -> deploy_panel*
{
    if(!ctx.has<hub>())
    {
        return nullptr;
    }
    return &ctx.get_cached<hub>().get_panels().get_deploy_panel();
}

/// One step's state; a failed step is ready and has an error.
struct step_status
{
    std::string name;
    bool ready{false};
    std::string error;
};

/// Reads the panel's jobs. Main thread only: the panel replaces and clears its map there.
auto collect_step_status(rtti::context& ctx, std::vector<step_status>& out) -> size_t
{
    auto* panel = get_deploy_panel(ctx);
    if(panel == nullptr)
    {
        return 0;
    }
    size_t ready = 0;
    for(const auto& step : panel->get_jobs())
    {
        step_status status;
        status.name = step.name;
        status.ready = step.job.is_ready();
        status.error = deployment::get_step_error(step.job);
        ready += status.ready ? 1 : 0;
        out.push_back(std::move(status));
    }
    return ready;
}

auto steps_to_json(const std::vector<step_status>& steps) -> std::string
{
    std::string json = "[";
    bool first = true;
    for(const auto& step : steps)
    {
        if(!first)
        {
            json += ",";
        }
        first = false;
        json += fmt::format(R"({{"step":{},"done":{},"error":{}}})",
                            make_json_string(step.name),
                            bool_to_json(step.ready),
                            step.error.empty() ? "null" : make_json_string(step.error));
    }
    json += "]";
    return json;
}

struct deploy_snapshot
{
    std::vector<step_status> steps;
    size_t ready{0};
    std::string location;
};

auto take_snapshot(rtti::context& ctx) -> deploy_snapshot
{
    deploy_snapshot snapshot;
    snapshot.ready = collect_step_status(ctx, snapshot.steps);
    if(ctx.has<project_manager>())
    {
        snapshot.location = fs::to_utf8_string(get_output_location(ctx.get_cached<project_manager>().get_deploy_settings()));
    }
    return snapshot;
}

/// The progress of the current or last deploy the Deploy window tracks.
auto deploy_progress_json(const deploy_snapshot& snapshot) -> std::string
{
    if(snapshot.steps.empty())
    {
        return R"({"deploying":false,"steps":[],"note":"No deploy was started in this editor session."})";
    }
    return fmt::format(R"({{"deploying":{},"done":{},"total":{},"location":{},"steps":{}}})",
                       bool_to_json(snapshot.ready < snapshot.steps.size()),
                       snapshot.ready,
                       snapshot.steps.size(),
                       make_json_string(snapshot.location),
                       steps_to_json(snapshot.steps));
}

constexpr const char* PLATFORM_NAMES[] = {"desktop", "android"};
constexpr const char* ORIENTATION_NAMES[] = {"landscape", "portrait", "automatic"};

/// Sets `out` from the name at `key` when the arguments have one; false (with `error`) for an unknown name.
template<typename Enum, std::size_t Count>
auto read_enum_name(const simdjson::dom::object& args, const char* key, const char* const (&names)[Count], Enum& out, std::string& error)
    -> bool
{
    std::string name;
    if(!read_string(args, key, name))
    {
        return true;
    }
    for(std::size_t i = 0; i < Count; ++i)
    {
        if(name == names[i])
        {
            out = static_cast<Enum>(i);
            return true;
        }
    }
    error = fmt::format("Unknown {} '{}'", key, name);
    return false;
}

auto android_settings_to_json(const deploy_settings& params) -> std::string
{
    return fmt::format(
        R"({{"application_id":{},"version_code":{},"orientation":{},"release":{},"keystore":{},"key_alias":{},"device":{}}})",
        params.android_application_id.empty() ? "null" : make_json_string(params.android_application_id),
        params.android_version_code,
        make_json_string(ORIENTATION_NAMES[static_cast<int>(params.android_screen_orientation)]),
        bool_to_json(params.android_release),
        params.android_keystore.empty() ? "null" : make_json_string(fs::to_utf8_string(params.android_keystore)),
        params.android_key_alias.empty() ? "null" : make_json_string(params.android_key_alias),
        params.android_device.empty() ? "null" : make_json_string(params.android_device));
}

/// The Android side of deploy_get_state: the editor's player, this machine's tools, the devices from the
/// last listing (which this call starts again) and whether a game runs on one.
auto android_state_to_json(rtti::context& ctx, const deploy_settings& params) -> std::string
{
    deployment::refresh_android_devices(ctx);
    const auto& player = deployment::get_android_template();
    const auto tools = deployment::get_android_toolchain();
    std::string devices = "[";
    for(const auto& device : deployment::get_android_devices())
    {
        devices += fmt::format(R"({}{{"serial":{},"model":{},"state":{}}})",
                               devices.size() > 1 ? "," : "",
                               make_json_string(device.serial),
                               make_json_string(device.model),
                               make_json_string(device.state));
    }
    devices += "]";
    const auto conflict = deployment::get_android_install_conflict();
    return fmt::format(
        R"({{"player":{{"found":{},"error":{}}},"tools":{{"sdk":{},"java":{},"error":{}}},"application_id":{},"devices":{},"game_running":{},"install_conflict":{}}})",
        bool_to_json(player.is_found()),
        player.error.empty() ? "null" : make_json_string(player.error),
        tools.sdk.empty() ? "null" : make_json_string(fs::to_utf8_string(tools.sdk)),
        tools.java.empty() ? "null" : make_json_string(fs::to_utf8_string(tools.java)),
        tools.error.empty() ? "null" : make_json_string(tools.error),
        make_json_string(deployment::get_android_application_id(ctx, params)),
        devices,
        bool_to_json(deployment::is_android_game_running()),
        conflict.empty() ? "null" : make_json_string(conflict));
}

/// Reads the platform and Android settings deploy_set_settings takes.
auto read_android_settings(const simdjson::dom::object& args, deploy_settings& params, std::string& error) -> bool
{
    if(!read_enum_name(args, "platform", PLATFORM_NAMES, params.deploy_target, error) ||
       !read_enum_name(args, "android_orientation", ORIENTATION_NAMES, params.android_screen_orientation, error))
    {
        return false;
    }
    read_string(args, "android_application_id", params.android_application_id);
    const auto version_code = read_int64_or(args, "android_version_code", params.android_version_code);
    params.android_version_code = static_cast<std::uint32_t>(std::max<int64_t>(1, version_code));
    read_bool(args, "android_release", params.android_release);
    std::string keystore;
    if(read_string(args, "android_keystore", keystore))
    {
        params.android_keystore = fs::from_utf8_string(keystore);
    }
    read_string(args, "android_key_alias", params.android_key_alias);
    read_string(args, "android_device", params.android_device);
    return true;
}

auto deploy_settings_to_json(const deploy_settings& params) -> std::string
{
    return fmt::format(
        R"({{"platform":{},"location":{},"output":{},"deploy_dependencies":{},"run_after_deploy":{},"cook_assets":{},"include_debug_symbols":{},"pack_assets":{},"patch_release":{},"only_used_assets":{},"scenes":{},"include":{},"exclude":{},"android":{}}})",
        make_json_string(PLATFORM_NAMES[static_cast<int>(params.deploy_target)]),
        params.deploy_location.empty() ? "null" : make_json_string(fs::to_utf8_string(params.deploy_location)),
        params.deploy_location.empty() ? "null" : make_json_string(fs::to_utf8_string(get_output_location(params))),
        bool_to_json(params.deploy_dependencies),
        bool_to_json(params.deploy_and_run),
        bool_to_json(params.deploy_cooked_assets),
        bool_to_json(params.deploy_debug_symbols),
        bool_to_json(params.deploy_packed_assets),
        params.deploy_patch_release.empty() ? "null" : make_json_string(params.deploy_patch_release),
        bool_to_json(params.deploy_only_used_assets),
        strings_to_json_array(params.deploy_scenes),
        strings_to_json_array(params.deploy_included),
        strings_to_json_array(params.deploy_excluded),
        android_settings_to_json(params));
}

/// Replaces `out` with the string array at `key` when the arguments have one.
auto read_key_list(const simdjson::dom::object& args, const char* key, std::vector<std::string>& out, std::string& error)
    -> bool
{
    simdjson::dom::element element;
    if(args[key].get(element))
    {
        return true;
    }
    std::vector<std::string> values;
    if(!parse_string_array(element, values, error, key))
    {
        return false;
    }
    out = std::move(values);
    return true;
}

/// Whether an entry passes deploy_get_content's folder and show arguments.
auto is_content_listed(const deploy_settings& params,
                       const deployment::content_entry& entry,
                       const std::string& folder,
                       deployment::content_filter filter) -> bool
{
    return (folder.empty() || deployment::is_key_under(entry.key, folder)) && deployment::passes_filter(params, entry, filter);
}

auto named_folders_to_json(const std::vector<deployment::named_folder>& folders) -> std::string
{
    std::string json = "[";
    for(const auto& folder : folders)
    {
        json += fmt::format(R"({}{{"folder":{},"scripts":{}}})",
                            json.size() > 1 ? "," : "",
                            make_json_string(folder.key),
                            strings_to_json_array(folder.scripts));
    }
    return json + "]";
}

auto content_entry_to_json(const deploy_settings& params, const deployment::content_entry& entry) -> std::string
{
    return fmt::format(R"({{"key":{},"ships":{},"reason":"{}","used":{},"used_by":{},"size":{}}})",
                       make_json_string(entry.key),
                       bool_to_json(deployment::is_shipped(params, entry)),
                       deployment::get_reason_name(deployment::get_reason(params, entry)),
                       bool_to_json(entry.is_used),
                       entry.used_by.empty() ? "null" : make_json_string(entry.used_by),
                       entry.size);
}

/// Sets the destination, creating the folder when asked - but never inside the project, where
/// deployment::get_problems refuses it anyway.
auto apply_location(fs::path& destination, const std::string& location, bool create_location, std::string& error) -> bool
{
    const fs::path target = fs::from_utf8_string(location);
    if(!target.is_absolute())
    {
        error = "location must be absolute: " + location;
        return false;
    }
    fs::error_code ec;
    if(create_location && !fs::exists(target, ec) && !deployment::is_inside_project(target))
    {
        fs::create_directories(target, ec);
        if(ec)
        {
            error = "Failed to create deploy location: " + location + " (" + ec.message() + ")";
            return false;
        }
    }
    destination = target;
    return true;
}

/// Tidies the scene list and the rules as the Deploy window stores them; the keys that name nothing in
/// the project come back as warnings.
auto normalize_deploy_keys(rtti::context& ctx, deploy_settings& params) -> std::vector<std::string>
{
    const auto found = deployment::list_content(deployment::make_project_content_source(ctx.get_cached<asset_manager>()));
    std::vector<std::string> warnings;
    for(const auto& key : deployment::normalize_keys(params, found))
    {
        warnings.push_back(key + " names no asset or folder of the project; it matches nothing");
    }
    return warnings;
}

/// What deploy_get_content walks from, taken on the main thread.
struct content_request
{
    deployment::content_source source;
    deploy_settings params;
    std::string startup_scene;
};

} // namespace

auto require_not_deploying(rtti::context& ctx, std::string& error) -> bool
{
    const auto* panel = get_deploy_panel(ctx);
    if(deployment::is_deploy_running() || (panel != nullptr && panel->is_deploying()))
    {
        error = "A deploy is running; wait for it to finish (deploy_get_state) before changing the "
                "project or its settings";
        return false;
    }
    return true;
}

void register_deploy_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "deploy_get_state",
         .description =
             "Everything about deploying in one read: the settings (destination folder, runtime dependencies, run "
             "after deploy, cooked assets, debug symbols, packing, the release patched against); the kept releases "
             "a deploy can patch; what keeps the project from deploying, one sentence each (usually a missing "
             "deploy folder - deploy_set_settings - no startup scene - settings_set standalone.startup_scene - or "
             "assets still compiling); and the progress of the current or last deploy. With platform android it "
             "also reports android: whether the editor has its Android player, the Android SDK and Java found, the "
             "application id used, the devices adb listed (each call lists them again in the background, so call "
             "again for a fresh list) and whether a game started by Deploy and Run still runs; the game's log goes "
             "to the editor log ([Device] lines, logs_get_recent).",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
         {
             std::string error;
             auto* pm = get_open_project(ctx, error);
             if(pm == nullptr)
             {
                 return {.text = error, .is_error = true};
             }
             const auto& params = pm->get_deploy_settings();
             const auto problems = deployment::get_problems(ctx, params);
             const bool is_android = params.deploy_target == deploy_platform::android;
             return {.text = fmt::format(R"({{"settings":{},"releases":{},"can_deploy":{},"problems":{},"deploy":{},"android":{}}})",
                                         deploy_settings_to_json(params),
                                         strings_to_json_array(deployment::list_project_releases()),
                                         bool_to_json(problems.empty()),
                                         strings_to_json_array(problems),
                                         deploy_progress_json(take_snapshot(ctx)),
                                         is_android ? android_state_to_json(ctx, params) : std::string("null")),
                     .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "deploy_set_settings",
         .description =
             "Set the project's deploy settings and save them, then report what still keeps the "
             "project from deploying. location is an absolute folder outside the project, created "
             "when missing; it must be empty or an earlier deploy, which a deploy updates in place. "
             "include_debug_symbols ships the scripts' .pdb/.xml so managed exceptions name file and line. "
             "pack_assets (default true) writes the compiled assets into data/<engine|app>/content.pak; off "
             "ships them loose. patch_release names a kept release (deploy_keep_release): its packs ship "
             "unchanged and content_p1.pak holds every change since; an empty string makes full builds again. "
             "only_used_assets ships only the project assets the startup scene, the scenes list and the scripts' "
             "path literals reach (deploy_get_content shows the result); include / exclude are folder or asset "
             "keys that always / never ship, the most specific rule winning (exclude on a tie). scenes, include and "
             "exclude replace the whole list; keys are stored as the project spells them, and keys that name "
             "nothing come back as warnings. Each platform builds into a folder of its own under location: "
             "<location>/desktop, or <location>/android for platform android, which holds <project>.apk only (the "
             "data stays in the editor's build folder); deploy_get_state reports what an Android deploy still "
             "needs. android_device is an "
             "adb serial from deploy_get_state's device list, used by run_after_deploy and not saved. Omitted "
             "fields keep their value.",
         .input_schema_json =
             R"json({"type":"object","properties":{"platform":{"type":"string","enum":["desktop","android"]},"android_application_id":{"type":"string","description":"e.g. com.company.game; empty: com.<company>.<product>"},"android_version_code":{"type":"integer","minimum":1},"android_orientation":{"type":"string","enum":["landscape","portrait","automatic"]},"android_release":{"type":"boolean","description":"A release package signed with android_keystore (passwords from UNRAVEL_KEYSTORE_PASSWORD / UNRAVEL_KEY_PASSWORD); default false, a debug package"},"android_keystore":{"type":"string"},"android_key_alias":{"type":"string"},"android_device":{"type":"string","description":"adb serial of the device run_after_deploy installs on"},"location":{"type":"string","description":"Absolute destination folder, outside the project"},"deploy_dependencies":{"type":"boolean"},"run_after_deploy":{"type":"boolean"},"cook_assets":{"type":"boolean"},"include_debug_symbols":{"type":"boolean","description":"Ship the script libraries' .pdb and .xml files; default false"},"pack_assets":{"type":"boolean","description":"Pack the compiled assets into one compressed file per protocol; default true"},"patch_release":{"type":"string","description":"A kept release to patch; empty for a full build"},"only_used_assets":{"type":"boolean","description":"Ship only the project assets the scenes and scripts use; default false (everything)"},"scenes":{"type":"array","items":{"type":"string"},"description":"Scene keys the game loads besides the startup scene, e.g. app:/data/levels/two.spfb"},"include":{"type":"array","items":{"type":"string"},"description":"Folder or asset keys that always ship"},"exclude":{"type":"array","items":{"type":"string"},"description":"Folder or asset keys that never ship"},"create_location":{"type":"boolean","description":"Create the destination folder when it does not exist; default true"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             auto* pm = get_open_project(ctx, error);
             if(pm == nullptr || !require_not_deploying(ctx, error))
             {
                 return {.text = error, .is_error = true};
             }
             auto params = pm->get_deploy_settings();
             std::string location;
             if(read_string(args, "location", location) && !location.empty())
             {
                 bool create_location = true;
                 read_bool(args, "create_location", create_location);
                 if(!apply_location(params.deploy_location, location, create_location, error))
                 {
                     return {.text = error, .is_error = true};
                 }
             }
             read_bool(args, "deploy_dependencies", params.deploy_dependencies);
             read_bool(args, "run_after_deploy", params.deploy_and_run);
             read_bool(args, "cook_assets", params.deploy_cooked_assets);
             read_bool(args, "include_debug_symbols", params.deploy_debug_symbols);
             read_bool(args, "pack_assets", params.deploy_packed_assets);
             read_string(args, "patch_release", params.deploy_patch_release);
             read_bool(args, "only_used_assets", params.deploy_only_used_assets);
             if(!read_android_settings(args, params, error) ||
                !read_key_list(args, "scenes", params.deploy_scenes, error) ||
                !read_key_list(args, "include", params.deploy_included, error) ||
                !read_key_list(args, "exclude", params.deploy_excluded, error))
             {
                 return {.text = error, .is_error = true};
             }
             const auto warnings = normalize_deploy_keys(ctx, params);

             pm->get_deploy_settings() = params;
             pm->save_deploy_settings();

             const auto problems = deployment::get_problems(ctx, params);
             return {.text = fmt::format(R"({{"ok":true,"settings":{},"warnings":{},"problems":{}}})",
                                         deploy_settings_to_json(params),
                                         strings_to_json_array(warnings),
                                         strings_to_json_array(problems)),
                     .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::destructive,
         .idempotent = true});

    registry.add(
        {.name = "deploy_keep_release",
         .description =
             "Keep the packed build in the deploy folder as a release (in the project's releases/<name>), so later "
             "deploys with patch_release set to its name ship only a patch for it (content_p1.pak) beside its "
             "unchanged packs. name defaults to the product version (settings app.version). Needs a full packed "
             "deploy in the folder; refuses a name already kept.",
         .input_schema_json =
             R"json({"type":"object","properties":{"name":{"type":"string","description":"Release name; default the product version"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             auto* pm = get_open_project(ctx, error);
             if(pm == nullptr || !require_not_deploying(ctx, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::string name;
             if(!read_string(args, "name", name) || name.empty())
             {
                 name = deployment::get_default_release_name(ctx);
             }
             const auto result = deployment::keep_project_release(pm->get_deploy_settings(), name);
             if(!result.is_ok())
             {
                 return {.text = result.error, .is_error = true};
             }
             return {.text = fmt::format(R"({{"ok":true,"release":{},"folder":{},"releases":{}}})",
                                         make_json_string(name),
                                         make_json_string(fs::to_utf8_string(deployment::get_project_release_folder(name))),
                                         strings_to_json_array(deployment::list_project_releases())),
                     .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::destructive});

    registry.add(
        {.name = "deploy_get_content",
         .description =
             "What a deploy ships of the project's assets, as the Deploy window's Content tab shows it: finds what "
             "the startup scene, the scenes list (deploy_set_settings scenes) and the scripts' path literals use, "
             "directly or through other assets, then applies the include / exclude rules and only_used_assets. "
             "Returns counts and sizes, deploy scenes no longer in the project, named_folders: folders scripts build "
             "asset paths in at run time, each with those scripts (worth an include rule), and one entry per asset: ships, reason (scene, referenced, named_in_script, "
             "included, excluded, unused), used_by (what led to it) and compiled size, plus orphan_rules: rules on "
             "keys that name nothing in the project (deploy_set_settings drops them when given the lists without "
             "them). Engine assets always ship and are not listed. The walk runs off the main thread.",
         .input_schema_json =
             R"json({"type":"object","properties":{"folder":{"type":"string","description":"Only assets in this folder (or this one asset), e.g. app:/data/levels"},"show":{"type":"string","enum":["all","shipped","left_out","changed"],"description":"Default all; changed = assets under an include or exclude rule"},"offset":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1,"maximum":2000,"description":"Default 200"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             constexpr int64_t default_limit = 200;
             constexpr int64_t max_limit = 2000;
             std::string folder;
             if(read_string(args, "folder", folder) && !folder.empty())
             {
                 folder = deployment::normalize_key(folder);
             }
             std::string show = "all";
             read_string(args, "show", show);
             deployment::content_filter filter{};
             if(!deployment::parse_filter(show, filter))
             {
                 return {.text = "show must be all, shipped, left_out or changed, not '" + show + "'", .is_error = true};
             }
             const auto offset = static_cast<size_t>(read_clamped_int64(args, "offset", 0, 0));
             const auto limit = static_cast<size_t>(std::min(read_clamped_int64(args, "limit", default_limit, 1), max_limit));
             std::string error;
             auto request = ctx.get_cached<mcp_manager>().invoke_on_main(
                 [&ctx, &error]() -> hpp::optional<content_request>
                 {
                     auto* pm = get_open_project(ctx, error);
                     if(pm == nullptr)
                     {
                         return hpp::nullopt;
                     }
                     return content_request{deployment::make_project_content_source(ctx.get_cached<asset_manager>()),
                                            pm->get_deploy_settings(),
                                            pm->get_settings().standalone.startup_scene.id()};
                 });
             if(!request || !*request)
             {
                 return {.text = error.empty() ? "Timed out reading the project on the main thread" : error, .is_error = true};
             }
             const auto& params = (*request)->params;
             const auto content = deployment::detect_content((*request)->source, params, (*request)->startup_scene);
             if(!folder.empty() && !content.has_folder(folder) && content.find(folder) == nullptr)
             {
                 return {.text = "folder " + folder + " names no folder or asset of the project", .is_error = true};
             }
             size_t shipped = 0;
             std::uint64_t size = 0;
             std::uint64_t shipped_size = 0;
             size_t listed = 0;
             std::string entries = "[";
             for(const auto& entry : content.entries)
             {
                 const bool is_shipped = deployment::is_shipped(params, entry);
                 shipped += is_shipped ? 1 : 0;
                 size += entry.size;
                 shipped_size += is_shipped ? entry.size : 0;
                 if(!is_content_listed(params, entry, folder, filter))
                 {
                     continue;
                 }
                 if(listed >= offset && listed < offset + limit)
                 {
                     entries += (entries.size() > 1 ? "," : "") + content_entry_to_json(params, entry);
                 }
                 ++listed;
             }
             entries += "]";
             return {.text = fmt::format(
                         R"({{"only_used_assets":{},"assets":{},"shipped":{},"left_out":{},"size":{},"shipped_size":{},"missing_scenes":{},"named_folders":{},"orphan_rules":{},"listed":{},"offset":{},"entries":{}}})",
                         bool_to_json(params.deploy_only_used_assets),
                         content.entries.size(),
                         shipped,
                         content.entries.size() - shipped,
                         size,
                         shipped_size,
                         strings_to_json_array(content.missing_scenes),
                         named_folders_to_json(content.named_folders),
                         strings_to_json_array(deployment::find_orphan_rules(params, content)),
                         listed,
                         offset,
                         entries),
                     .is_error = false};
         },
         .mutates_scene = false,
         .requires_main_thread = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "deploy_remove_release",
         .description =
             "Delete a kept release's packs (deploy_get_state lists the releases). Patches can no longer be made "
             "for it; when patch_release names it, patch_release is cleared and deploys make full builds again.",
         .input_schema_json =
             R"json({"type":"object","properties":{"name":{"type":"string","description":"The release to delete"}},"required":["name"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             auto* pm = get_open_project(ctx, error);
             if(pm == nullptr || !require_not_deploying(ctx, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::string name;
             read_string(args, "name", name);
             const auto result = deployment::remove_project_release(ctx, name);
             if(!result.is_ok())
             {
                 return {.text = result.error, .is_error = true};
             }
             return {.text = fmt::format(R"({{"ok":true,"removed":{},"releases":{}}})",
                                         make_json_string(name),
                                         strings_to_json_array(deployment::list_project_releases())),
                     .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::destructive});

    registry.add(
        {.name = "project_deploy",
         .description =
             "Build the game into the deploy folder, as the editor's Deploy does. The folder must be empty or an "
             "earlier deploy, which is updated in place: unchanged files and pack entries are kept, files the "
             "build no longer ships are removed. Needs a deploy location and a startup scene: call "
             "deploy_get_state first. Waits for running asset and script "
             "compiles first, then for the deploy, up to timeout_ms in all (default 180000); on timeout the deploy "
             "keeps running and deploy_get_state reports it. Assets without a compiled file fail the deploy before "
             "the folder is touched, named in failed_steps. Refuses play mode. With platform android the last step "
             "builds <project>.apk with Gradle from the editor's Android player; run_after_deploy then installs and "
             "starts it on android_device, and the game's log goes to the editor log ([Device] lines).",
         .input_schema_json =
             R"json({"type":"object","properties":{"location":{"type":"string","description":"Deploy folder; the platform builds into <location>/desktop or <location>/android. Also saved into the deploy settings. Optional when already set"},"run_after_deploy":{"type":"boolean"},"platform":{"type":"string","enum":["desktop","android"],"description":"Also saved into the deploy settings. android builds an .apk; with run_after_deploy it is installed and started on android_device"},"android_device":{"type":"string","description":"adb serial from deploy_get_state's android.devices"},"android_replace_installed":{"type":"boolean","description":"When the game on the device was signed with another key (debug vs release), uninstall it first, which removes its saved data; default false reports android.install_conflict instead"},"timeout_ms":{"type":"integer","minimum":1000,"maximum":900000}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();

             std::string location;
             read_string(args, "location", location);
             deploy_platform platform{};
             std::string argument_error;
             std::string platform_name;
             const bool has_platform = read_string(args, "platform", platform_name);
             if(!read_enum_name(args, "platform", PLATFORM_NAMES, platform, argument_error))
             {
                 return {.text = argument_error, .is_error = true};
             }
             std::string android_device;
             const bool has_device = read_string(args, "android_device", android_device);
             bool replace_installed = false;
             read_bool(args, "android_replace_installed", replace_installed);
             // The device check below reads the last listing; this call may be the first of the session.
             if(has_device || (has_platform && platform == deploy_platform::android))
             {
                 deployment::update_android_devices();
             }
             bool run_after_deploy = false;
             const bool has_run_flag = read_bool(args, "run_after_deploy", run_after_deploy);
             const auto timeout =
                 read_timeout_ms(args, k_default_deploy_timeout_ms, k_max_deploy_timeout_ms, "timeout_ms");
             const auto deadline = std::chrono::steady_clock::now() + timeout;

             // A deploy needs the compiler idle - it is one of the deploy problems - so wait here as a
             // user would for the Deploy button to enable. On timeout the start below reports it.
             while(std::chrono::steady_clock::now() < deadline)
             {
                 auto pending = mcp.invoke_on_main(
                     [&ctx]() -> std::string
                     {
                         return deployment::get_pending_compile_reason(ctx);
                     });
                 if(!pending || pending->empty())
                 {
                     break;
                 }
                 std::this_thread::sleep_for(k_deploy_poll_interval);
             }

             auto start = mcp.invoke_on_main(
                 [&ctx, location, run_after_deploy, has_run_flag, platform, has_platform, android_device, has_device, replace_installed]() -> tool_result
                 {
                     std::string error;
                     if(!require_not_play_mode(ctx, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     auto* pm = get_open_project(ctx, error);
                     auto* panel = get_deploy_panel(ctx);
                     if(pm == nullptr || panel == nullptr)
                     {
                         return {.text = pm == nullptr ? error : "Deploy window unavailable", .is_error = true};
                     }
                     if(panel->is_deploying())
                     {
                         return {.text = "A deploy is already running; poll deploy_get_state", .is_error = true};
                     }

                     auto params = pm->get_deploy_settings();
                     if(has_platform)
                     {
                         params.deploy_target = platform;
                     }
                     if(!location.empty() && !apply_location(params.deploy_location, location, true, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     if(has_run_flag)
                     {
                         params.deploy_and_run = run_after_deploy;
                     }
                     if(has_device)
                     {
                         params.android_device = android_device;
                     }
                     pm->get_deploy_settings() = params;
                     pm->save_deploy_settings();
                     // For this deploy only: it may uninstall the game from the device.
                     params.android_replace_installed = replace_installed;

                     const auto problems = deployment::get_problems(ctx, params);
                     if(!problems.empty())
                     {
                         return {.text = fmt::format(R"({{"ok":false,"problems":{}}})",
                                                     strings_to_json_array(problems)),
                                 .is_error = true};
                     }
                     if(!panel->start_deploy(ctx, params))
                     {
                         return {.text = "The deploy did not start; see logs_get_recent", .is_error = true};
                     }
                     return {.text = fmt::format(R"({{"started":true,"steps":{}}})", panel->get_jobs().size()),
                             .is_error = false};
                 },
                 std::chrono::milliseconds(30000));

             if(!start)
             {
                 return {.text = "Timed out starting the deploy on the main thread", .is_error = true};
             }
             if(start->is_error)
             {
                 return *start;
             }

             // Wait on the HTTP worker so frames keep pumping; each look at the jobs hops to the
             // main thread, where the panel owns them.
             deploy_snapshot snapshot;
             while(true)
             {
                 auto taken = mcp.invoke_on_main(
                     [&ctx]() -> deploy_snapshot
                     {
                         return take_snapshot(ctx);
                     });
                 if(taken)
                 {
                     snapshot = std::move(*taken);
                 }
                 const bool finished = !snapshot.steps.empty() && snapshot.ready == snapshot.steps.size();
                 const bool is_untracked = taken && snapshot.steps.empty();
                 if(finished || is_untracked || std::chrono::steady_clock::now() >= deadline)
                 {
                     break;
                 }
                 std::this_thread::sleep_for(k_deploy_poll_interval);
             }
             if(snapshot.steps.empty())
             {
                 return {.text = "The deploy's steps are no longer tracked; check the deploy folder and logs_get_recent",
                         .is_error = true};
             }

             const bool finished = !snapshot.steps.empty() && snapshot.ready == snapshot.steps.size();
             if(!finished)
             {
                 return {.text = fmt::format(
                             R"({{"ok":false,"timed_out":true,"done":{},"total":{},"location":{},)"
                             R"("steps":{},"note":"The deploy is still running; poll deploy_get_state."}})",
                             snapshot.ready,
                             snapshot.steps.size(),
                             make_json_string(snapshot.location),
                             steps_to_json(snapshot.steps)),
                         .is_error = true};
             }

             std::vector<std::string> failed;
             for(const auto& step : snapshot.steps)
             {
                 if(!step.error.empty())
                 {
                     failed.push_back(step.name + ": " + step.error);
                 }
             }
             if(!failed.empty())
             {
                 return {.text = fmt::format(R"({{"ok":false,"location":{},"failed_steps":{},"steps":{}}})",
                                             make_json_string(snapshot.location),
                                             strings_to_json_array(failed),
                                             steps_to_json(snapshot.steps)),
                         .is_error = true};
             }
             return {.text = fmt::format(R"({{"ok":true,"location":{},"steps":{}}})",
                                         make_json_string(snapshot.location),
                                         steps_to_json(snapshot.steps)),
                     .is_error = false};
         },
         .mutates_scene = false,
         .requires_main_thread = false,
         .effect = tool_effect::destructive});
}

} // namespace unravel::mcp
