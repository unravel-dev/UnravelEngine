#include "mcp_tools_common.h"

#include <editor/editing/create_scene_modal.h>
#include <editor/editing/editing_manager.h>
#include <editor/system/mcp_manager.h>
#include <editor/system/project_manager.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/ecs.h>
#include <filesystem/filesystem.h>

#include <chrono>

namespace unravel::mcp
{
namespace
{

auto compat_to_string(project_manager::project_compat status) -> const char*
{
    switch(status)
    {
        case project_manager::project_compat::no_info_file:
            return "no_info_file";
        case project_manager::project_compat::ok:
            return "ok";
        case project_manager::project_compat::engine_older:
            return "engine_older";
    }
    return "ok";
}

/// Why opening a folder needs `upgrade:true`, or empty when it opens as it is. The start page asks
/// the same two questions before opening such a folder.
auto compat_refusal(const fs::path& project_path, const project_manager::project_compat_report& report) -> std::string
{
    const auto path = project_path.generic_string();
    switch(report.status)
    {
        case project_manager::project_compat::ok:
            return {};
        case project_manager::project_compat::no_info_file:
            return path + " has no project.cfg: it is either not a project (make one with project_create) or a "
                          "legacy project. Pass upgrade:true only for a legacy project; opening writes project.cfg "
                          "and the project folders into it.";
        case project_manager::project_compat::engine_older:
            return path + " was last opened by engine " + report.on_disk.engine_version_opened.to_string() +
                   ", older than this one; opening may re-save its files in newer formats. Back it up, then pass "
                   "upgrade:true.";
    }
    return {};
}

} // namespace

void register_project_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "project_list_recent",
         .description = "List recent project directories with existence and compatibility status.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
         {
             if(!ctx.has<project_manager>())
             {
                 return {.text = "[]", .is_error = false};
             }

             auto& pm = ctx.get_cached<project_manager>();
             const auto& items = pm.get_editor_settings().projects.recent_projects;

             std::string json = "[";
             for(size_t i = 0; i < items.size(); ++i)
             {
                 if(i > 0)
                 {
                     json += ",";
                 }

                 fs::error_code ec;
                 const auto& path = items[i];
                 const bool exists = fs::exists(path, ec);
                 const auto report = pm.inspect_project(path);
                 json += fmt::format(
                     R"({{"index":{},"path":{},"exists":{},"compat":{},"guid":{},"engine_version_opened":{}}})",
                     i,
                     make_json_string(path.generic_string()),
                     exists ? "true" : "false",
                     make_json_string(compat_to_string(report.status)),
                     make_json_string(report.on_disk.project_guid),
                     make_json_string(report.on_disk.engine_version_opened.to_string()));
             }
             json += "]";
             return {.text = json, .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "project_open",
         .description =
             "Open an existing project by path, recent:true, or recent_index. Refuses a folder that is "
             "not a project or was saved by an older engine unless upgrade:true, and unsaved edits in "
             "the open project unless force:true. Seeds a scene from preset when the project has none.",
         .input_schema_json =
             R"json({"type":"object","properties":{"path":{"type":"string","description":"Absolute project folder"},"recent":{"type":"boolean","description":"Open the most recent project"},"recent_index":{"type":"integer","minimum":0},"preset":{"type":"string","enum":["low","medium","high","showcase"],"description":"Scene seeded when the project has none; default medium"},"upgrade":{"type":"boolean","default":false,"description":"Open a legacy folder without project.cfg, or a project saved by an older engine"},"force":{"type":"boolean","default":false,"description":"Discard unsaved scene and prefab edits"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();

             std::string path_arg;
             read_string(args, "path", path_arg);
             bool recent = false;
             read_bool(args, "recent", recent);
             int64_t recent_index = -1;
             if(args["recent_index"].get(recent_index))
             {
                 recent_index = -1;
             }
             std::string preset_str = "medium";
             read_string(args, "preset", preset_str);
             bool force = false;
             read_bool(args, "force", force);
             bool upgrade = false;
             read_bool(args, "upgrade", upgrade);

             auto result = mcp.invoke_on_main(
                 [&]() -> tool_result
                 {
                     std::string error;
                     if(!require_not_play_mode(ctx, error) || !require_not_deploying(ctx, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     if(!ctx.has<project_manager>())
                     {
                         return {.text = "project_manager unavailable", .is_error = true};
                     }

                     defaults::scene_preset preset{};
                     if(!defaults::parse_scene_preset(preset_str, preset))
                     {
                         return {.text = "Invalid preset (use low|medium|high|showcase)", .is_error = true};
                     }

                     auto& pm = ctx.get_cached<project_manager>();
                     fs::path project_path;
                     const auto& recent_projects = pm.get_editor_settings().projects.recent_projects;
                     if(recent || path_arg == "recent")
                     {
                         if(recent_projects.empty())
                         {
                             return {.text = "No recent projects", .is_error = true};
                         }
                         project_path = recent_projects.front();
                     }
                     else if(recent_index >= 0)
                     {
                         if(static_cast<size_t>(recent_index) >= recent_projects.size())
                         {
                             return {.text = "recent_index out of range", .is_error = true};
                         }
                         project_path = recent_projects[static_cast<size_t>(recent_index)];
                     }
                     else if(!path_arg.empty())
                     {
                         project_path = fs::path(path_arg);
                     }
                     else
                     {
                         return {.text = "Provide path, recent:true, or recent_index", .is_error = true};
                     }

                     fs::error_code ec;
                     if(!project_path.is_absolute() || !fs::is_directory(project_path, ec))
                     {
                         return {.text = "Not an existing absolute project folder: " + project_path.generic_string(),
                                 .is_error = true};
                     }
                     const auto report = pm.inspect_project(project_path);
                     if(const auto refusal = compat_refusal(project_path, report); !refusal.empty() && !upgrade)
                     {
                         return {.text = refusal, .is_error = true};
                     }
                     if(!release_unsaved_work(ctx, force, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     if(!pm.open_project(ctx, project_path))
                     {
                         return {.text = "Failed to open project: " + project_path.generic_string(), .is_error = true};
                     }
                     if(pm.is_restart_pending())
                     {
                         return {.text = R"({"ok":true,"restarting":true,"note":"The project uses another renderer or physics backend; the editor restarts and opens it. Reconnect once editor_get_status answers."})",
                                 .is_error = false};
                     }

                     bool created_from_preset = false;
                     if(create_scene_modal::complete_if_pending(preset))
                     {
                         created_from_preset = true;
                     }

                     return {.text = fmt::format(
                                 R"({{"ok":true,"project":{},"compat":{},"created_scene_from_preset":{},"preset":{}}})",
                                 project_info_json(pm),
                                 make_json_string(compat_to_string(report.status)),
                                 bool_to_json(created_from_preset),
                                 make_json_string(std::string(preset_str.empty() ? "medium" : preset_str))),
                             .is_error = false};
                 },
                 std::chrono::milliseconds(120000));

             if(!result)
             {
                 return {.text = "Timed out opening project on main thread", .is_error = true};
             }
             return *result;
         },
         .mutates_scene = true,
         .requires_main_thread = false,
         .effect = tool_effect::destructive,
         .idempotent = true});

    registry.add(
        {.name = "project_create",
         .description =
             "Create a new project directory and open it. The directory must not exist or must be "
             "empty. Seeds a scene from preset (default medium) and writes the project's agent "
             "instructions. Use project_open for a project that already exists.",
         .input_schema_json =
             R"json({"type":"object","properties":{"path":{"type":"string","description":"Absolute directory for the new project"},"preset":{"type":"string","enum":["low","medium","high","showcase"],"description":"Scene seeded into the new project; default medium"},"force":{"type":"boolean","default":false,"description":"Discard unsaved scene and prefab edits in the open project"}},"required":["path"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();

             std::string path_arg;
             read_string(args, "path", path_arg);
             std::string preset_str = "medium";
             read_string(args, "preset", preset_str);
             bool force = false;
             read_bool(args, "force", force);

             auto result = mcp.invoke_on_main(
                 [&]() -> tool_result
                 {
                     std::string error;
                     if(!require_not_play_mode(ctx, error) || !require_not_deploying(ctx, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     if(!ctx.has<project_manager>())
                     {
                         return {.text = "project_manager unavailable", .is_error = true};
                     }
                     if(path_arg.empty())
                     {
                         return {.text = "Provide path (an absolute directory for the new project)",
                                 .is_error = true};
                     }

                     defaults::scene_preset preset{};
                     if(!defaults::parse_scene_preset(preset_str, preset))
                     {
                         return {.text = "Invalid preset (use low|medium|high|showcase)", .is_error = true};
                     }

                     auto& pm = ctx.get_cached<project_manager>();
                     std::string create_error;
                     if(!pm.can_create_project(fs::path(path_arg), &create_error))
                     {
                         return {.text = create_error + " (project_open opens an existing project)",
                                 .is_error = true};
                     }
                     if(!release_unsaved_work(ctx, force, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     if(!pm.create_project(ctx, fs::path(path_arg), &create_error))
                     {
                         return {.text = create_error + " (project_open opens an existing project)",
                                 .is_error = true};
                     }

                     const bool created_from_preset = create_scene_modal::complete_if_pending(preset);
                     return {.text = fmt::format(
                                 R"({{"ok":true,"project":{},"created_scene_from_preset":{},"preset":{}}})",
                                 project_info_json(pm),
                                 bool_to_json(created_from_preset),
                                 make_json_string(preset_str)),
                             .is_error = false};
                 },
                 std::chrono::milliseconds(120000));

             if(!result)
             {
                 return {.text = "Timed out creating project on main thread", .is_error = true};
             }
             return *result;
         },
         .mutates_scene = true,
         .requires_main_thread = false,
         .effect = tool_effect::destructive});

    registry.add(
        {.name = "project_close",
         .description = "Close the current project. Refuses when the scene or prefab has unsaved edits, "
                        "unless force:true discards them.",
         .input_schema_json =
             R"({"type":"object","properties":{"force":{"type":"boolean","default":false,"description":"Discard unsaved scene and prefab edits"}}})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             if(!require_not_play_mode(ctx, error) || !require_not_deploying(ctx, error))
             {
                 return {.text = error, .is_error = true};
             }
             if(!ctx.has<project_manager>())
             {
                 return {.text = "project_manager unavailable", .is_error = true};
             }

             auto& pm = ctx.get_cached<project_manager>();
             if(!pm.has_open_project())
             {
                 return {.text = R"({"ok":true,"was_open":false})", .is_error = false};
             }

             bool force = false;
             read_bool(args, "force", force);
             if(!release_unsaved_work(ctx, force, error))
             {
                 return {.text = error, .is_error = true};
             }

             create_scene_modal::cancel_if_pending();
             pm.close_project(ctx);
             return {.text = R"({"ok":true,"was_open":true})", .is_error = false};
         },
         .mutates_scene = true,
         .effect = tool_effect::destructive});
}

} // namespace unravel::mcp
