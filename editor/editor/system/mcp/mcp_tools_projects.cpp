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
         .mutates_scene = false});

    registry.add(
        {.name = "project_open",
         .description =
             "Open a project by path, recent:true, or recent_index. Uses preset (default medium) "
             "when no startup scene exists.",
         .input_schema_json =
             R"json({"type":"object","properties":{"path":{"type":"string","description":"Absolute project directory, or 'recent'"},"recent":{"type":"boolean"},"recent_index":{"type":"integer","minimum":0},"preset":{"type":"string","enum":["low","medium","high","showcase"],"description":"Used only when a new scene modal would appear; default medium"},"force":{"type":"boolean","description":"Discard unsaved scene changes before open; default true"}}})json",
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
             bool force = true;
             read_bool(args, "force", force);

             auto result = mcp.invoke_on_main(
                 [&ctx, path_arg, recent, recent_index, preset_str, force]() -> tool_result
                 {
                     std::string error;
                     if(!require_not_play_mode(ctx, error))
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
                     auto& em = ctx.get_cached<editing_manager>();

                     if(em.has_unsaved_changes() && !force)
                     {
                         return {.text = "Unsaved scene changes; pass force:true to discard", .is_error = true};
                     }
                     if(force)
                     {
                         em.clear_unsaved_changes();
                     }

                     // Rebuild args object fields into a path resolution using a temp object isn't
                     // available; resolve manually from captured values.
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
                     if(!fs::exists(project_path, ec))
                     {
                         return {.text = "Project directory does not exist: " + project_path.generic_string(),
                                 .is_error = true};
                     }

                     const auto report = pm.inspect_project(project_path);
                     if(!pm.open_project(ctx, project_path))
                     {
                         return {.text = "Failed to open project: " + project_path.generic_string(), .is_error = true};
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
         .requires_main_thread = false});

    registry.add(
        {.name = "project_close",
         .description = "Close the current project. force:true (default) discards unsaved scene changes.",
         .input_schema_json =
             R"({"type":"object","properties":{"force":{"type":"boolean","default":true}}})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             if(!require_not_play_mode(ctx, error))
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

             bool force = true;
             read_bool(args, "force", force);

             auto& em = ctx.get_cached<editing_manager>();
             if(em.has_unsaved_changes() && !force)
             {
                 return {.text = "Unsaved scene changes; pass force:true to discard", .is_error = true};
             }
             if(force)
             {
                 em.clear_unsaved_changes();
             }

             create_scene_modal::cancel_if_pending();
             pm.close_project(ctx);
             return {.text = R"({"ok":true,"was_open":true})", .is_error = false};
         },
         .mutates_scene = true});
}

} // namespace unravel::mcp
