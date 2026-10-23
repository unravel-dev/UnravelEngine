#include "mcp_asset_lookup.h"
#include "mcp_profile_utils.h"
#include "mcp_project_paths.h"
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
        snapshot.location = fs::to_utf8_string(get_output_location(ctx.get_cached<project_manager>().get_deploy_request()));
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

constexpr const char* ORIENTATION_NAMES[] = {"landscape", "portrait", "automatic"};
constexpr const char* TEXTURE_COMPRESSION_NAMES[] = {"astc", "etc2", "bc"};

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

auto android_permissions_to_json(const android_permissions& permissions) -> std::string
{
    std::string json = "{";
    for(const auto& entry : deployment::get_android_permission_entries())
    {
        json += fmt::format(R"("{}":{},)", entry.key, bool_to_json(permissions.*entry.member));
    }
    return json + R"("other":)" + strings_to_json_array(permissions.other) + "}";
}

auto android_settings_to_json(const android_deploy_settings& android, const std::string& device) -> std::string
{
    return fmt::format(
        R"({{"application_id":{},"permissions":{},"version_code":{},"orientation":{},"texture_compression":{},"icon":{},"icon_background":{},"release":{},"keystore":{},"key_alias":{},"device":{}}})",
        android.application_id.empty() ? "null" : make_json_string(android.application_id),
        android_permissions_to_json(android.permissions),
        android.version_code,
        make_json_string(ORIENTATION_NAMES[static_cast<int>(android.screen_orientation)]),
        make_json_string(TEXTURE_COMPRESSION_NAMES[static_cast<int>(android.texture_compression)]),
        asset_handle_key_json(android.icon),
        color_to_json(android.icon_background),
        bool_to_json(android.release),
        android.keystore.empty() ? "null" : make_json_string(fs::to_utf8_string(android.keystore)),
        android.key_alias.empty() ? "null" : make_json_string(android.key_alias),
        device.empty() ? "null" : make_json_string(device));
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
        devices += fmt::format(R"({}{{"serial":{},"name":{},"model":{},"android_version":{},"state":{},"aliases":{}}})",
                               devices.size() > 1 ? "," : "",
                               make_json_string(device.serial),
                               make_json_string(device.device_name),
                               make_json_string(device.model),
                               make_json_string(device.android_version),
                               make_json_string(device.state),
                               strings_to_json_array(device.aliases));
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

/// Reads android_icon, a texture's key or uid; null or an empty string choose the engine's logo.
auto read_android_icon(rtti::context& ctx, const simdjson::dom::object& args, android_deploy_settings& android, std::string& error) -> bool
{
    simdjson::dom::element value;
    if(args["android_icon"].get(value) != simdjson::SUCCESS)
    {
        return true;
    }
    std::string key;
    if(!value.is_null() && !parse_string(value, key, error))
    {
        return false;
    }
    if(key.empty())
    {
        android.icon = {};
        return true;
    }
    auto icon = find_existing_asset<gfx::texture>(ctx, to_asset_key(key), error);
    if(!icon)
    {
        return false;
    }
    android.icon = icon;
    return true;
}

/// android_permissions: the named permissions as booleans and `other` as names. A partial object changes
/// only what it names.
auto read_android_permissions(const simdjson::dom::object& args, android_permissions& permissions, std::string& error) -> bool
{
    simdjson::dom::element value;
    if(args["android_permissions"].get(value) != simdjson::SUCCESS)
    {
        return true;
    }
    simdjson::dom::object object;
    if(value.get(object) != simdjson::SUCCESS)
    {
        error = "android_permissions must be an object";
        return false;
    }
    for(const auto& entry : deployment::get_android_permission_entries())
    {
        read_bool(object, entry.key, permissions.*entry.member);
    }
    simdjson::dom::element other;
    if(object["other"].get(other) != simdjson::SUCCESS)
    {
        return true;
    }
    // The list given replaces the profile's.
    std::vector<std::string> names;
    if(!parse_string_array(other, names, error, "other"))
    {
        return false;
    }
    permissions.other = std::move(names);
    return true;
}

/// The Android settings deploy_set_settings takes, read over the profile's; `is_given` says whether the
/// arguments had any.
auto read_android_settings(rtti::context& ctx, const simdjson::dom::object& args, android_deploy_settings& android, bool& is_given, std::string& error)
    -> bool
{
    static constexpr const char* ANDROID_KEYS[] = {"android_application_id",
                                                   "android_permissions",
                                                   "android_version_code",
                                                   "android_orientation",
                                                   "android_texture_compression",
                                                   "android_icon",
                                                   "android_icon_background",
                                                   "android_release",
                                                   "android_keystore",
                                                   "android_key_alias"};
    is_given = std::any_of(std::begin(ANDROID_KEYS),
                           std::end(ANDROID_KEYS),
                           [&args](const char* key)
                           {
                               simdjson::dom::element value;
                               return args[key].get(value) == simdjson::SUCCESS;
                           });
    if(!read_enum_name(args, "android_orientation", ORIENTATION_NAMES, android.screen_orientation, error) ||
       !read_enum_name(args, "android_texture_compression", TEXTURE_COMPRESSION_NAMES, android.texture_compression, error) ||
       !read_android_icon(ctx, args, android, error) || !read_android_permissions(args, android.permissions, error))
    {
        return false;
    }
    simdjson::dom::element background;
    if(args["android_icon_background"].get(background) == simdjson::SUCCESS && !parse_color(background, android.icon_background, error))
    {
        return false;
    }
    read_string(args, "android_application_id", android.application_id);
    const auto version_code = read_int64_or(args, "android_version_code", android.version_code);
    android.version_code = static_cast<std::uint32_t>(std::max<int64_t>(1, version_code));
    read_bool(args, "android_release", android.release);
    std::string keystore;
    if(read_string(args, "android_keystore", keystore))
    {
        android.keystore = fs::from_utf8_string(keystore);
    }
    read_string(args, "android_key_alias", android.key_alias);
    return true;
}

/// The profile the arguments name at `key`, checked, or the deploy profile when they name none.
auto read_target_profile(const project_manager& pm,
                         const simdjson::dom::object& args,
                         const char* key,
                         std::string& profile,
                         std::string& error) -> bool
{
    profile = pm.get_deploy_profile();
    return read_profile_argument(pm, args, key, profile, error);
}

/// The deploy settings a deploy_set_settings call names, besides location and patch_release, as the tool names
/// them: they go where its scope says.
auto get_scoped_settings_given(const simdjson::dom::object& args) -> std::vector<std::string>
{
    std::vector<std::string> given;
    for(const auto& name : get_deploy_argument_names())
    {
        simdjson::dom::element value;
        if(name != "location" && name != "patch_release" && args[name].get(value) == simdjson::SUCCESS)
        {
            given.push_back(name);
        }
    }
    return given;
}

/// A deploy override to drop: the setting's name as the tool takes it, and its stored path.
struct deploy_revert
{
    std::string name;
    std::string path;
};

/// Reads the revert argument: deploy settings, named as the tool's arguments, a profile stops overriding.
auto read_revert_argument(const simdjson::dom::object& args, std::vector<deploy_revert>& reverts, std::string& error) -> bool
{
    std::vector<std::string> names;
    if(!read_string_array(args, "revert", names, error, false))
    {
        return false;
    }
    for(const auto& name : names)
    {
        const auto path = find_deploy_setting_path(name);
        if(path.empty())
        {
            error = fmt::format("{} is no deploy setting. deploy_get_state's settings name them.", name);
            return false;
        }
        if(!deployment::is_overridable_deploy_path(path))
        {
            error = fmt::format("{} is shared by every profile, so no profile overrides it.", name);
            return false;
        }
        reverts.push_back({name, path});
    }
    return true;
}

/// The profiles as the deploy tools list them: each with its platform.
auto profile_names_to_json(const project_manager& pm) -> std::string
{
    std::string json = "[";
    for(const auto& profile : pm.get_profiles().get_all())
    {
        json += fmt::format(R"({}{{"name":{},"platform":{}}})",
                            json.size() > 1 ? "," : "",
                            make_json_string(profile.name),
                            make_json_string(get_platform_name(profile.platform)));
    }
    return json + "]";
}

/// The platform argument is the profile's now: name what to do instead.
auto refuse_platform_argument(const simdjson::dom::object& args, std::string& error) -> bool
{
    simdjson::dom::element value;
    if(args["platform"].get(value) != simdjson::SUCCESS)
    {
        return true;
    }
    error = "platform is no deploy setting: it belongs to the profile. Pass profile (settings_profiles_list lists them, "
            "and settings_profiles_create makes an Android one).";
    return false;
}

auto deploy_settings_to_json(const deploy_settings& params) -> std::string
{
    return fmt::format(
        R"({{"profile":{},"platform":{},"location":{},"output":{},"deploy_dependencies":{},"run_after_deploy":{},"run_arguments":{},"cook_assets":{},"include_debug_symbols":{},"pack_assets":{},"patch_release":{},"only_used_assets":{},"scenes":{},"include":{},"exclude":{},"android":{}}})",
        make_json_string(params.profile),
        make_json_string(get_platform_name(params.deploy_target)),
        params.deploy_location.empty() ? "null" : make_json_string(fs::to_utf8_string(params.deploy_location)),
        params.deploy_location.empty() ? "null" : make_json_string(fs::to_utf8_string(get_output_location(params))),
        bool_to_json(params.deploy_dependencies),
        bool_to_json(params.deploy_and_run),
        make_json_string(params.deploy_run_arguments),
        bool_to_json(params.deploy_cooked_assets),
        bool_to_json(params.deploy_debug_symbols),
        bool_to_json(params.deploy_packed_assets),
        params.deploy_patch_release.empty() ? "null" : make_json_string(params.deploy_patch_release),
        bool_to_json(params.deploy_only_used_assets),
        strings_to_json_array(params.deploy_scenes),
        strings_to_json_array(params.deploy_included),
        strings_to_json_array(params.deploy_excluded),
        params.deploy_target == deploy_platform::android ? android_settings_to_json(params.android, params.android_device)
                                                         : std::string("null"));
}

/// The deploy settings a profile overrides, as deploy_set_settings names them.
auto deploy_overrides_to_json(const settings_profile& profile) -> std::string
{
    return strings_to_json_array(to_deploy_argument_names(profile.deploy.get_paths()));
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

/// Sets the destination as the Deploy window stores it: relative to the project's folder when it lies
/// in the project. A missing folder outside the project is created when asked; one in the project is
/// left to the deploy, after the checks that keep it out of the folders the editor manages.
auto apply_location(fs::path& destination, const std::string& location, bool create_location, std::string& error) -> bool
{
    const auto project_root = deployment::get_project_root();
    const fs::path given = fs::from_utf8_string(location);
    const fs::path target = fs::resolve_relative_to(given, project_root);
    if(!target.is_absolute())
    {
        error = "location must be absolute or relative to the project folder: " + location;
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
    destination = fs::make_relative_if_inside(target, project_root);
    return true;
}

/// Tidies the scene list and the rules as the Deploy window stores them; the keys that name nothing in
/// the project come back as warnings.
auto normalize_deploy_keys(rtti::context& ctx, deploy_settings& params) -> std::vector<std::string>
{
    auto& pm = ctx.get_cached<project_manager>();
    const auto shipped_settings = pm.get_profile_settings(pm.get_deploy_profile());
    const auto found = deployment::list_content(deployment::make_project_content_source(ctx.get_cached<asset_manager>(), shipped_settings));
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
             "Everything about deploying a profile in one read; profile (default the deploy profile, the one deploys "
             "build) picks which, and deploy_profile names the selected one. profiles lists every profile with its "
             "platform. settings are the deploy settings the profile deploys with (destination folder, runtime "
             "dependencies, run after deploy, cooked assets, debug symbols, packing, the release patched against), "
             "named as deploy_set_settings takes them; overridden lists the ones the profile overrides of the shared "
             "settings. Also the profile's kept releases a deploy can patch, what keeps it from deploying, one "
             "sentence each (usually a missing deploy folder - deploy_set_settings - no startup scene - settings_set "
             "standalone.startup_scene - or assets still compiling), and the progress of the current or last deploy. "
             "For an Android profile it also reports android: whether the editor has its Android player, the Android "
             "SDK and Java found, the application id used, the devices adb listed (each call lists them again in the "
             "background, so call again for a fresh list) and whether a game started by Deploy and Run still runs. "
             "The game's log goes to the editor log ([Device] lines, logs_get_recent).",
         .input_schema_json =
             R"json({"type":"object","properties":{"profile":{"type":"string","description":"The profile to report on; default the deploy profile"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             auto* pm = get_open_project(ctx, error);
             std::string profile;
             if(pm == nullptr || !read_target_profile(*pm, args, "profile", profile, error))
             {
                 return {.text = error, .is_error = true};
             }
             const auto params = *pm->get_profile_deploy_settings(profile);
             const auto problems = deployment::get_problems(ctx, params);
             const bool is_android = params.deploy_target == deploy_platform::android;
             return {.text = fmt::format(
                         R"({{"deploy_profile":{},"profile":{},"profiles":{},"settings":{},"overridden":{},"releases":{},"can_deploy":{},"problems":{},"deploy":{},"android":{}}})",
                         make_json_string(pm->get_deploy_profile()),
                         make_json_string(profile),
                         profile_names_to_json(*pm),
                         deploy_settings_to_json(params),
                         deploy_overrides_to_json(*pm->get_profiles().find(profile)),
                         strings_to_json_array(deployment::list_project_releases(profile)),
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
             "Set deploy settings and save them, then report what still keeps the profile from deploying. "
             "Every argument is checked before anything is written: a refused call changes nothing. "
             "profile names the profile the call writes (default the deploy profile) and leaves the selection alone; "
             "deploy_profile selects the profile deploys build. "
             "scope says where the settings other than location, patch_release and android_* go: \"shared\" (default) "
             "writes them for every profile that does not override them, \"profile\" stores them as overrides of the "
             "profile only. location is always shared; patch_release and the android_* settings always belong to the "
             "profile. written lists where each value went; shadowed lists shared values the profile overrides, so it "
             "keeps deploying with its own (revert them, or pass scope profile). revert lists settings, named as this "
             "tool's arguments, the profile stops overriding. "
             "location is an absolute folder, or one relative to the project folder (default .deploy, stored relative "
             "when it is in the project, outside the folders the editor manages: data, compiled, settings, deploy, "
             "editor, releases, profiles, cache, temp); it is created when missing and must be empty or an earlier "
             "deploy, which a deploy updates in place. Each profile builds into a folder named after it in location; "
             "an Android profile's holds <project>.apk only (the data stays in the editor's build folder). "
             "include_debug_symbols ships the scripts' .pdb/.xml so managed exceptions name file and line. "
             "pack_assets (default true) writes the compiled assets into data/<engine|app>/content.pak; off ships them "
             "loose. patch_release names a kept release (deploy_keep_release): its packs ship unchanged and "
             "content_p1.pak holds every change since; an empty string makes full builds again. only_used_assets ships "
             "only the project assets the startup scene, the scenes list and the scripts' path literals reach "
             "(deploy_get_content shows the result); include / exclude are folder or asset keys that always / never "
             "ship, the most specific rule winning (exclude on a tie). scenes, include and exclude replace the whole "
             "list; keys are stored as the project spells them, and keys that name nothing come back as warnings. "
             "android_device is an adb serial from deploy_get_state's device list, used by run_after_deploy and not "
             "saved. android_texture_compression picks the formats the profile's textures are cooked to (astc "
             "default, etc2, or bc: the desktop formats, which ship the editor's own files and which only some "
             "Android GPUs read). Omitted fields keep their value.",
         .input_schema_json =
             R"json({"type":"object","properties":{"profile":{"type":"string","description":"The profile this call writes (its overrides, patch_release, android_*, revert); default the deploy profile. Does not change the deploy profile"},"deploy_profile":{"type":"string","description":"Select the profile deploys build, as the Deploy window's profile picker does"},"scope":{"type":"string","enum":["shared","profile"],"description":"Where the settings other than location, patch_release and android_* go; default shared"},"revert":{"type":"array","items":{"type":"string"},"description":"Settings, named as this tool's arguments (e.g. pack_assets), the profile stops overriding"},"android_texture_compression":{"type":"string","enum":["astc","etc2","bc"]},"android_application_id":{"type":"string","description":"e.g. com.company.game; empty: com.<company>.<product>, made from the product settings and following them"},"android_permissions":{"type":"object","properties":{"internet":{"type":"boolean"},"network_state":{"type":"boolean"},"vibrate":{"type":"boolean"},"microphone":{"type":"boolean"},"camera":{"type":"boolean"},"location":{"type":"boolean"},"notifications":{"type":"boolean"},"other":{"type":"array","items":{"type":"string"},"description":"Further permissions by name; a name without a dot gets android.permission. in front"}},"description":"The permissions the manifest asks for; a partial object changes only what it names. Defaults: internet and vibrate on"},"android_version_code":{"type":"integer","minimum":1},"android_orientation":{"type":"string","enum":["landscape","portrait","automatic"]},"android_icon":{"type":["string","null"],"description":"Key or uid of a texture the launcher icon is made from, square at best; null or empty for the engine's logo"},"android_icon_background":{"type":"array","items":{"type":"number"},"description":"[r,g,b] from 0 to 1: the opaque color behind the icon's image; default white"},"android_release":{"type":"boolean","description":"A release package signed with android_keystore (passwords from UNRAVEL_KEYSTORE_PASSWORD / UNRAVEL_KEY_PASSWORD); default false, a debug package"},"android_keystore":{"type":"string"},"android_key_alias":{"type":"string"},"android_device":{"type":"string","description":"adb serial of the device run_after_deploy installs on"},"location":{"type":"string","description":"Destination folder: absolute, or relative to the project folder such as .deploy"},"deploy_dependencies":{"type":"boolean"},"run_after_deploy":{"type":"boolean"},"run_arguments":{"type":"string","description":"Command line options run_after_deploy starts the game with, separated by spaces, e.g. --gfx-stats (graphics statistics overlay) or --novsync"},"cook_assets":{"type":"boolean"},"include_debug_symbols":{"type":"boolean","description":"Ship the script libraries' .pdb and .xml files; default false"},"pack_assets":{"type":"boolean","description":"Pack the compiled assets into one compressed file per protocol; default true"},"patch_release":{"type":"string","description":"A kept release to patch; empty for a full build"},"only_used_assets":{"type":"boolean","description":"Ship only the project assets the scenes and scripts use; default false (everything)"},"scenes":{"type":"array","items":{"type":"string"},"description":"Scene keys the game loads besides the startup scene, e.g. app:/data/levels/two.spfb"},"include":{"type":"array","items":{"type":"string"},"description":"Folder or asset keys that always ship"},"exclude":{"type":"array","items":{"type":"string"},"description":"Folder or asset keys that never ship"},"create_location":{"type":"boolean","description":"Create the destination folder when it does not exist; default true"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             // Every argument is checked before anything changes: a refused call leaves the settings as they were.
             std::string error;
             auto* pm = get_open_project(ctx, error);
             if(pm == nullptr || !require_not_deploying(ctx, error) || !refuse_platform_argument(args, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::string deploy_profile;
             if(!read_target_profile(*pm, args, "deploy_profile", deploy_profile, error))
             {
                 return {.text = error, .is_error = true};
             }
             // A call that selects a profile writes that one unless it names another.
             std::string profile = deploy_profile;
             if(!read_profile_argument(*pm, args, "profile", profile, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::string scope = "shared";
             read_string(args, "scope", scope);
             if(scope != "shared" && scope != "profile")
             {
                 return {.text = fmt::format("scope must be shared or profile, not '{}'.", scope), .is_error = true};
             }
             const bool is_profile_scope = scope == "profile";
             std::vector<deploy_revert> reverts;
             if(!read_revert_argument(args, reverts, error))
             {
                 return {.text = error, .is_error = true};
             }
             const auto* found = pm->get_profiles().find(profile);
             const auto scoped = get_scoped_settings_given(args);
             std::string patch_release;
             const bool has_patch_release = read_string(args, "patch_release", patch_release);
             const bool writes_overrides = (is_profile_scope && !scoped.empty()) || has_patch_release || !reverts.empty();
             if(writes_overrides && found->deploy.is_unreadable())
             {
                 return {.text = fmt::format("Profile {}'s deploy.cfg could not be read. Mend or delete the file, then open "
                                             "the project again to change what it overrides.",
                                             profile),
                         .is_error = true};
             }
             auto shared = pm->get_deploy_settings();
             auto params = is_profile_scope ? *pm->get_profile_deploy_settings(profile) : shared;
             read_bool(args, "deploy_dependencies", params.deploy_dependencies);
             read_bool(args, "run_after_deploy", params.deploy_and_run);
             read_string(args, "run_arguments", params.deploy_run_arguments);
             read_bool(args, "cook_assets", params.deploy_cooked_assets);
             read_bool(args, "include_debug_symbols", params.deploy_debug_symbols);
             read_bool(args, "pack_assets", params.deploy_packed_assets);
             read_bool(args, "only_used_assets", params.deploy_only_used_assets);
             auto android = found->android;
             bool has_android_settings = false;
             std::string device;
             const bool has_device = read_string(args, "android_device", device);
             if(!read_android_settings(ctx, args, android, has_android_settings, error) ||
                !read_key_list(args, "scenes", params.deploy_scenes, error) ||
                !read_key_list(args, "include", params.deploy_included, error) ||
                !read_key_list(args, "exclude", params.deploy_excluded, error))
             {
                 return {.text = error, .is_error = true};
             }
             if(has_android_settings && found->platform != deploy_platform::android)
             {
                 return {.text = fmt::format("Profile {} is no Android profile: android_* settings belong to an Android "
                                             "profile (pass profile).",
                                             profile),
                         .is_error = true};
             }
             std::string location;
             if(read_string(args, "location", location) && !location.empty())
             {
                 bool create_location = true;
                 read_bool(args, "create_location", create_location);
                 if(!apply_location(shared.deploy_location, location, create_location, error))
                 {
                     return {.text = error, .is_error = true};
                 }
             }
             params.deploy_location = shared.deploy_location;
             const auto warnings = normalize_deploy_keys(ctx, params);

             // The shared location and device, the scope's settings, the profile's own, then the selection.
             std::vector<std::string> written_shared;
             std::vector<std::string> written_profile;
             pm->get_deploy_settings().deploy_location = shared.deploy_location;
             if(!location.empty())
             {
                 written_shared.emplace_back("location");
             }
             if(has_device)
             {
                 pm->get_deploy_settings().android_device = device;
             }
             if(is_profile_scope)
             {
                 pm->save_deploy_settings();
                 if(auto overridden = pm->override_profile_deploy(ctx, profile, params); !overridden.is_ok())
                 {
                     return {.text = overridden.error, .is_error = true};
                 }
                 written_profile.insert(written_profile.end(), scoped.begin(), scoped.end());
             }
             else
             {
                 params.deploy_patch_release = pm->get_deploy_settings().deploy_patch_release;
                 params.android_device = pm->get_deploy_settings().android_device;
                 params.profile.clear();
                 params.android = {};
                 pm->get_deploy_settings() = params;
                 pm->save_deploy_settings();
                 written_shared.insert(written_shared.end(), scoped.begin(), scoped.end());
             }
             if(has_patch_release)
             {
                 auto resolved = *pm->get_profile_deploy_settings(profile);
                 resolved.deploy_patch_release = patch_release;
                 if(auto overridden = pm->override_profile_deploy(ctx, profile, resolved); !overridden.is_ok())
                 {
                     return {.text = overridden.error, .is_error = true};
                 }
                 written_profile.emplace_back("patch_release");
             }
             std::vector<std::string> reverted;
             for(const auto& revert : reverts)
             {
                 if(auto result = pm->revert_profile_deploy(ctx, profile, revert.path); !result.is_ok())
                 {
                     return {.text = result.error, .is_error = true};
                 }
                 reverted.push_back(revert.name);
             }
             if(has_android_settings)
             {
                 if(auto changed = pm->set_profile_android(ctx, profile, android); !changed.is_ok())
                 {
                     return {.text = changed.error, .is_error = true};
                 }
                 written_profile.emplace_back("android");
             }
             if(deploy_profile != pm->get_deploy_profile())
             {
                 if(auto selected = pm->set_deploy_profile(deploy_profile); !selected.is_ok())
                 {
                     return {.text = selected.error, .is_error = true};
                 }
             }

             const auto* written_to = pm->get_profiles().find(profile);
             std::vector<std::string> shadowed;
             std::copy_if(written_shared.begin(),
                          written_shared.end(),
                          std::back_inserter(shadowed),
                          [written_to](const std::string& name)
                          {
                              return written_to->deploy.is_overridden(find_deploy_setting_path(name));
                          });
             const auto request = *pm->get_profile_deploy_settings(profile);
             const auto problems = deployment::get_problems(ctx, request);
             return {.text = fmt::format(
                         R"({{"ok":true,"deploy_profile":{},"profile":{},"written":{{"shared":{},"profile":{}}},"shadowed":{},"reverted":{},"settings":{},"overridden":{},"warnings":{},"problems":{}}})",
                         make_json_string(pm->get_deploy_profile()),
                         make_json_string(profile),
                         strings_to_json_array(written_shared),
                         strings_to_json_array(written_profile),
                         strings_to_json_array(shadowed),
                         strings_to_json_array(reverted),
                         deploy_settings_to_json(request),
                         deploy_overrides_to_json(*written_to),
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
             "Keep a desktop profile's packed build in its deploy folder as a release (in the project's "
             "releases/<profile>/<name>), so later deploys of the profile with patch_release set to its name ship "
             "only a patch for it (content_p1.pak) beside its unchanged packs. profile defaults to the deploy profile. "
             "name defaults to the product version (settings app.version). Needs a full packed deploy of the profile "
             "in its folder; refuses a name already kept.",
         .input_schema_json =
             R"json({"type":"object","properties":{"name":{"type":"string","description":"Release name; default the product version"},"profile":{"type":"string","description":"Default the deploy profile"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             auto* pm = get_open_project(ctx, error);
             std::string profile;
             if(pm == nullptr || !require_not_deploying(ctx, error) || !read_target_profile(*pm, args, "profile", profile, error))
             {
                 return {.text = error, .is_error = true};
             }
             const auto request = *pm->get_profile_deploy_settings(profile);
             std::string name;
             if(!read_string(args, "name", name) || name.empty())
             {
                 name = deployment::get_default_release_name(ctx, request.profile);
             }
             const auto result = deployment::keep_project_release(request, name);
             if(!result.is_ok())
             {
                 return {.text = result.error, .is_error = true};
             }
             return {.text = fmt::format(R"({{"ok":true,"profile":{},"release":{},"folder":{},"releases":{}}})",
                                         make_json_string(request.profile),
                                         make_json_string(name),
                                         make_json_string(fs::to_utf8_string(deployment::get_project_release_folder(request.profile, name))),
                                         strings_to_json_array(deployment::list_project_releases(request.profile))),
                     .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::destructive});

    registry.add(
        {.name = "deploy_get_content",
         .description =
             "What a deploy of a profile (default the deploy profile) ships of the project's assets, as the Deploy "
             "window's Content tab shows it: finds what "
             "the startup scene, the scenes list (deploy_set_settings scenes) and the scripts' path literals use, "
             "directly or through other assets, then applies the include / exclude rules and only_used_assets. "
             "Returns counts and sizes, deploy scenes no longer in the project, named_folders: folders scripts build "
             "asset paths in at run time, each with those scripts (worth an include rule), and one entry per asset: ships, reason (scene, referenced, named_in_script, "
             "included, excluded, unused), used_by (what led to it) and compiled size, plus orphan_rules: rules on "
             "keys that name nothing in the project (deploy_set_settings drops them when given the lists without "
             "them). Engine assets always ship and are not listed. The walk runs off the main thread.",
         .input_schema_json =
             R"json({"type":"object","properties":{"profile":{"type":"string","description":"Default the deploy profile"},"folder":{"type":"string","description":"Only assets in this folder (or this one asset), e.g. app:/data/levels"},"show":{"type":"string","enum":["all","shipped","left_out","changed"],"description":"Default all; changed = assets under an include or exclude rule"},"offset":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1,"maximum":2000,"description":"Default 200"}}})json",
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
                 [&ctx, &error, &args]() -> hpp::optional<content_request>
                 {
                     auto* pm = get_open_project(ctx, error);
                     std::string profile;
                     if(pm == nullptr || !read_target_profile(*pm, args, "profile", profile, error))
                     {
                         return hpp::nullopt;
                     }
                     const auto request = *pm->get_profile_deploy_settings(profile);
                     const auto shipped_settings = pm->get_profile_settings(request.profile);
                     return content_request{deployment::make_project_content_source(ctx.get_cached<asset_manager>(), shipped_settings),
                                            request,
                                            shipped_settings.standalone.startup_scene.id()};
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
             "Delete a kept release's packs of a profile (default the deploy profile; deploy_get_state lists its "
             "releases). Patches can no longer be made for it; when the profile's patch_release names it, "
             "patch_release is cleared and its deploys make full builds again.",
         .input_schema_json =
             R"json({"type":"object","properties":{"name":{"type":"string","description":"The release to delete"},"profile":{"type":"string","description":"Default the deploy profile"}},"required":["name"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             auto* pm = get_open_project(ctx, error);
             std::string profile;
             if(pm == nullptr || !require_not_deploying(ctx, error) || !read_target_profile(*pm, args, "profile", profile, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::string name;
             read_string(args, "name", name);
             const auto result = deployment::remove_project_release(ctx, profile, name);
             if(!result.is_ok())
             {
                 return {.text = result.error, .is_error = true};
             }
             return {.text = fmt::format(R"({{"ok":true,"removed":{},"releases":{}}})",
                                         make_json_string(name),
                                         strings_to_json_array(deployment::list_project_releases(profile))),
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
             "the folder is touched, named in failed_steps. Refuses play mode. It deploys the deploy profile, or the "
             "profile given, which becomes the deploy profile. For an Android profile the last step builds "
             "<project>.apk with Gradle from the editor's Android player; run_after_deploy then installs and starts "
             "it on android_device, and the game's log goes to the editor log ([Device] lines). Textures the profile "
             "compiles differently from the editor (its formats, its defaults) are cooked first (Cooking Textures) "
             "and kept in the project's cache/profiles/<profile> for its next deploys.",
         .input_schema_json =
             R"json({"type":"object","properties":{"location":{"type":"string","description":"Deploy folder, absolute or relative to the project folder; the profile builds into <location>/<profile>. Also saved into the deploy settings. Optional when already set"},"run_after_deploy":{"type":"boolean","description":"For this deploy only"},"profile":{"type":"string","description":"The profile to deploy; it becomes the deploy profile. An Android profile builds an .apk; with run_after_deploy it is installed and started on android_device"},"android_device":{"type":"string","description":"adb serial from deploy_get_state's android.devices"},"android_replace_installed":{"type":"boolean","description":"When the game on the device was signed with another key (debug vs release), uninstall it first, which removes its saved data; default false reports android.install_conflict instead"},"timeout_ms":{"type":"integer","minimum":1000,"maximum":900000}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();

             std::string location;
             read_string(args, "location", location);
             std::string argument_error;
             if(!refuse_platform_argument(args, argument_error))
             {
                 return {.text = argument_error, .is_error = true};
             }
             std::string profile;
             read_string(args, "profile", profile);
             if(!profile.empty())
             {
                 const auto checked = mcp.invoke_on_main(
                     [&ctx, &args]() -> std::string
                     {
                         std::string error;
                         std::string named;
                         auto* pm = get_open_project(ctx, error);
                         if(pm != nullptr)
                         {
                             read_profile_argument(*pm, args, "profile", named, error);
                         }
                         return error;
                     });
                 if(checked && !checked->empty())
                 {
                     return {.text = *checked, .is_error = true};
                 }
             }
             std::string android_device;
             const bool has_device = read_string(args, "android_device", android_device);
             bool replace_installed = false;
             read_bool(args, "android_replace_installed", replace_installed);
             // The device check below reads the last listing; this call may be the first of the session.
             if(has_device || !deployment::has_listed_android_devices())
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
                 [&ctx, location, run_after_deploy, has_run_flag, profile, android_device, has_device, replace_installed]() -> tool_result
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

                     if(!profile.empty())
                     {
                         if(auto selected = pm->set_deploy_profile(profile); !selected.is_ok())
                         {
                             return {.text = selected.error, .is_error = true};
                         }
                     }
                     auto& shared = pm->get_deploy_settings();
                     if(!location.empty() && !apply_location(shared.deploy_location, location, true, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     if(has_device)
                     {
                         shared.android_device = android_device;
                     }
                     pm->save_deploy_settings();
                     auto params = pm->get_deploy_request();
                     // For this deploy only: run it or not, and whether it may uninstall the game from the device.
                     if(has_run_flag)
                     {
                         params.deploy_and_run = run_after_deploy;
                     }
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

    registry.add(
        {.name = "android_device_action",
         .description =
             "Act on the game of the deploy profile (an Android profile) on a device without a new deploy, as the "
             "Deploy window's On Device row does. run_last_build installs <location>/<profile>/<project>.apk (from "
             "an earlier project_deploy of the profile) and starts it; its steps show in deploy_get_state and the game's log goes to the "
             "editor log as [Device] lines. follow_log follows the log of the game already running. clear_data "
             "removes the game's saved data (pm clear), which stops it. uninstall removes the game with its saved "
             "data. clear_data and uninstall finish in the background; logs_get_recent shows the outcome. "
             "android_device is an adb serial from deploy_get_state's android.devices; default the deploy "
             "settings' device, or the only ready one.",
         .input_schema_json =
             R"json({"type":"object","properties":{"action":{"type":"string","enum":["run_last_build","follow_log","clear_data","uninstall"]},"profile":{"type":"string","description":"An Android profile; default the deploy profile"},"android_device":{"type":"string","description":"adb serial; default the deploy settings' device, or the only ready one"}},"required":["action"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             constexpr const char* actions[] = {"run_last_build", "follow_log", "clear_data", "uninstall"};
             std::string action;
             read_string(args, "action", action);
             if(std::find(std::begin(actions), std::end(actions), action) == std::end(actions))
             {
                 return {.text = fmt::format("action must be run_last_build, follow_log, clear_data or uninstall, not '{}'.", action),
                         .is_error = true};
             }
             std::string android_device;
             const bool has_device = read_string(args, "android_device", android_device);
             // The device check reads the last listing; this call may be the first of the session.
             if(has_device || !deployment::has_listed_android_devices())
             {
                 deployment::update_android_devices();
             }
             auto done = mcp.invoke_on_main(
                 [&ctx, &args, action, android_device, has_device]() -> tool_result
                 {
                     std::string error;
                     auto* pm = get_open_project(ctx, error);
                     auto* panel = get_deploy_panel(ctx);
                     if(pm == nullptr || panel == nullptr)
                     {
                         return {.text = pm == nullptr ? error : "Deploy window unavailable", .is_error = true};
                     }
                     std::string profile;
                     if(!read_target_profile(*pm, args, "profile", profile, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     auto params = *pm->get_profile_deploy_settings(profile);
                     if(params.deploy_target != deploy_platform::android)
                     {
                         return {.text = fmt::format("Profile {} is no Android profile. Pass profile, an Android one.", profile),
                                 .is_error = true};
                     }
                     if(has_device)
                     {
                         params.android_device = android_device;
                     }
                     const auto devices = deployment::get_android_devices();
                     // As in the Deploy window, the only ready device is the one to use.
                     const auto ready_count = std::count_if(devices.begin(),
                                                            devices.end(),
                                                            [](const deployment::android_device& device)
                                                            {
                                                                return device.is_ready();
                                                            });
                     if(params.android_device.empty() && ready_count == 1)
                     {
                         params.android_device = std::find_if(devices.begin(),
                                                              devices.end(),
                                                              [](const deployment::android_device& device)
                                                              {
                                                                  return device.is_ready();
                                                              })
                                                     ->serial;
                     }
                     const bool is_ready = std::any_of(devices.begin(),
                                                       devices.end(),
                                                       [&params](const deployment::android_device& device)
                                                       {
                                                           return device.is_named(params.android_device) && device.is_ready();
                                                       });
                     if(!is_ready)
                     {
                         const auto text = params.android_device.empty()
                                               ? fmt::format("{} devices are ready. Pass android_device (deploy_get_state lists them).",
                                                             ready_count == 0 ? std::string("No") : std::to_string(ready_count))
                                               : fmt::format("'{}' is no connected, ready device. deploy_get_state lists them.",
                                                             params.android_device);
                         return {.text = text, .is_error = true};
                     }
                     if(panel->is_deploying() || deployment::is_android_device_busy())
                     {
                         return {.text = "A deploy or a device command is running; poll deploy_get_state", .is_error = true};
                     }
                     if(action == "run_last_build")
                     {
                         if(!deployment::has_android_package(ctx, params))
                         {
                             return {.text = fmt::format("No package at {}; run project_deploy of the profile first",
                                                         fs::to_utf8_string(deployment::get_android_package_path(ctx, params))),
                                     .is_error = true};
                         }
                         panel->run_last_android_build(ctx, params);
                         return {.text = fmt::format(R"({{"started":true,"steps":{}}})", panel->get_jobs().size()), .is_error = false};
                     }
                     if(action == "follow_log")
                     {
                         if(deployment::is_android_game_running())
                         {
                             return {.text = "The editor follows a game already; it ends with the game or Stop Game", .is_error = true};
                         }
                         deployment::follow_android_game(ctx, params);
                         return {.text = R"({"started":true})", .is_error = false};
                     }
                     if(action == "clear_data")
                     {
                         deployment::clear_android_game_data(ctx, params);
                         return {.text = R"({"started":true})", .is_error = false};
                     }
                     if(action == "uninstall")
                     {
                         deployment::uninstall_android_game(ctx, params);
                         return {.text = R"({"started":true})", .is_error = false};
                     }
                     return {.text = "Unknown action: " + action, .is_error = true};
                 },
                 std::chrono::milliseconds(30000));
             if(!done)
             {
                 return {.text = "Timed out on the main thread", .is_error = true};
             }
             return *done;
         },
         .mutates_scene = false,
         .requires_main_thread = false,
         .effect = tool_effect::destructive});
}

} // namespace unravel::mcp
