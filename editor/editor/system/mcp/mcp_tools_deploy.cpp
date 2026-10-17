#include "mcp_tools_common.h"

#include <editor/deploy/deploy.h>
#include <editor/editing/editor_actions.h>
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

/// One step's state. A ready future rethrows its job's exception on get(), which is the only
/// way a failed step is told apart from a finished one.
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
    for(const auto& kvp : panel->get_jobs())
    {
        step_status status;
        status.name = kvp.first;
        status.ready = kvp.second.is_ready();
        if(status.ready)
        {
            ++ready;
            try
            {
                kvp.second.get();
            }
            catch(const std::exception& ex)
            {
                status.error = ex.what();
            }
            catch(...)
            {
                status.error = "unknown error";
            }
        }
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
        snapshot.location = ctx.get_cached<project_manager>().get_deploy_settings().deploy_location.generic_string();
    }
    return snapshot;
}

/// The progress of the current or last deploy the Deploy window tracks.
auto deploy_progress_json(const deploy_snapshot& snapshot) -> std::string
{
    if(snapshot.steps.empty())
    {
        return R"({"deploying":false,"steps":[],"note":"No deploy is tracked (none started, or the Deploy window was closed)."})";
    }
    return fmt::format(R"({{"deploying":{},"done":{},"total":{},"location":{},"steps":{}}})",
                       bool_to_json(snapshot.ready < snapshot.steps.size()),
                       snapshot.ready,
                       snapshot.steps.size(),
                       make_json_string(snapshot.location),
                       steps_to_json(snapshot.steps));
}

auto deploy_settings_to_json(const deploy_settings& params) -> std::string
{
    return fmt::format(
        R"({{"location":{},"deploy_dependencies":{},"run_after_deploy":{},"cook_assets":{},"include_debug_symbols":{},"pack_assets":{},"patch_release":{}}})",
        params.deploy_location.empty() ? "null" : make_json_string(params.deploy_location.generic_string()),
        bool_to_json(params.deploy_dependencies),
        bool_to_json(params.deploy_and_run),
        bool_to_json(params.deploy_cooked_assets),
        bool_to_json(params.deploy_debug_symbols),
        bool_to_json(params.deploy_packed_assets),
        params.deploy_patch_release.empty() ? "null" : make_json_string(params.deploy_patch_release));
}

/// Sets the destination, creating the folder when asked - but never inside the project, where
/// get_deploy_problems refuses it anyway.
auto apply_location(deploy_settings& params, const std::string& location, bool create_location, std::string& error)
    -> bool
{
    const fs::path target(location);
    if(!target.is_absolute())
    {
        error = "location must be absolute: " + location;
        return false;
    }
    fs::error_code ec;
    if(create_location && !fs::exists(target, ec) && !editor_actions::is_path_inside_project(target))
    {
        fs::create_directories(target, ec);
        if(ec)
        {
            error = "Failed to create deploy location: " + location + " (" + ec.message() + ")";
            return false;
        }
    }
    params.deploy_location = target;
    return true;
}

} // namespace

auto require_not_deploying(rtti::context& ctx, std::string& error) -> bool
{
    const auto* panel = get_deploy_panel(ctx);
    if(panel != nullptr && panel->is_deploying())
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
             "assets still compiling); and the progress of the current or last deploy.",
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
             const auto problems = editor_actions::get_deploy_problems(ctx, params);
             return {.text = fmt::format(R"({{"settings":{},"releases":{},"can_deploy":{},"problems":{},"deploy":{}}})",
                                         deploy_settings_to_json(params),
                                         strings_to_json_array(editor_actions::list_releases()),
                                         bool_to_json(problems.empty()),
                                         strings_to_json_array(problems),
                                         deploy_progress_json(take_snapshot(ctx))),
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
             "Omitted fields keep their value.",
         .input_schema_json =
             R"json({"type":"object","properties":{"location":{"type":"string","description":"Absolute destination folder, outside the project"},"deploy_dependencies":{"type":"boolean"},"run_after_deploy":{"type":"boolean"},"cook_assets":{"type":"boolean"},"include_debug_symbols":{"type":"boolean","description":"Ship the script libraries' .pdb and .xml files; default false"},"pack_assets":{"type":"boolean","description":"Pack the compiled assets into one compressed file per protocol; default true"},"patch_release":{"type":"string","description":"A kept release to patch; empty for a full build"},"create_location":{"type":"boolean","description":"Create the destination folder when it does not exist; default true"}}})json",
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
                 if(!apply_location(params, location, create_location, error))
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

             pm->get_deploy_settings() = params;
             pm->save_deploy_settings();

             const auto problems = editor_actions::get_deploy_problems(ctx, params);
             return {.text = fmt::format(R"({{"ok":true,"settings":{},"problems":{}}})",
                                         deploy_settings_to_json(params),
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
             read_string(args, "name", name);
             if(!editor_actions::keep_release(ctx, pm->get_deploy_settings(), name, error))
             {
                 return {.text = error, .is_error = true};
             }
             return {.text = fmt::format(R"({{"ok":true,"release":{},"folder":{},"releases":{}}})",
                                         make_json_string(name),
                                         make_json_string(editor_actions::get_release_folder(name).generic_string()),
                                         strings_to_json_array(editor_actions::list_releases())),
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
             "the folder is touched, named in failed_steps. Refuses play mode.",
         .input_schema_json =
             R"json({"type":"object","properties":{"location":{"type":"string","description":"Destination folder; also saved into the deploy settings. Optional when already set"},"run_after_deploy":{"type":"boolean"},"timeout_ms":{"type":"integer","minimum":1000,"maximum":900000}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();

             std::string location;
             read_string(args, "location", location);
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
                         return editor_actions::get_pending_compile_reason(ctx);
                     });
                 if(!pending || pending->empty())
                 {
                     break;
                 }
                 std::this_thread::sleep_for(k_deploy_poll_interval);
             }

             auto start = mcp.invoke_on_main(
                 [&ctx, location, run_after_deploy, has_run_flag]() -> tool_result
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
                     if(!location.empty() && !apply_location(params, location, true, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     if(has_run_flag)
                     {
                         params.deploy_and_run = run_after_deploy;
                     }
                     pm->get_deploy_settings() = params;
                     pm->save_deploy_settings();

                     const auto problems = editor_actions::get_deploy_problems(ctx, params);
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
                 // No steps after a start means the Deploy window was closed, which drops its progress.
                 const bool is_untracked = taken && snapshot.steps.empty();
                 if(finished || is_untracked || std::chrono::steady_clock::now() >= deadline)
                 {
                     break;
                 }
                 std::this_thread::sleep_for(k_deploy_poll_interval);
             }
             if(snapshot.steps.empty())
             {
                 return {.text = "The deploy's progress is no longer tracked (the Deploy window was closed); "
                                 "check the deploy folder and logs_get_recent",
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
