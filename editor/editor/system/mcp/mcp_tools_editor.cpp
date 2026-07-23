#include "mcp_tools_common.h"

#include <editor/editing/editor_actions.h>

#include <algorithm>
#include <logging/logging.h>

namespace unravel::mcp
{
namespace
{

auto level_to_string(level::level_enum level) -> const char*
{
    switch(level)
    {
        case level::trace:
            return "trace";
        case level::debug:
            return "debug";
        case level::info:
            return "info";
        case level::warn:
            return "warn";
        case level::err:
            return "error";
        case level::critical:
            return "critical";
        default:
            return "off";
    }
}

auto parse_min_level(const std::string& name, level::level_enum& out) -> bool
{
    if(name.empty() || name == "trace")
    {
        out = level::trace;
        return true;
    }
    if(name == "debug")
    {
        out = level::debug;
        return true;
    }
    if(name == "info")
    {
        out = level::info;
        return true;
    }
    if(name == "warn" || name == "warning")
    {
        out = level::warn;
        return true;
    }
    if(name == "error" || name == "err")
    {
        out = level::err;
        return true;
    }
    if(name == "critical")
    {
        out = level::critical;
        return true;
    }
    return false;
}

auto play_state_to_json(const play_state_info& info) -> std::string
{
    return fmt::format(
        R"({{"phase":{},"is_active":{},"is_paused":{},"is_splash":{},"is_simulation_running":{},"frames_running":{}}})",
        make_json_string(info.phase),
        info.is_active ? "true" : "false",
        info.is_paused ? "true" : "false",
        info.is_splash ? "true" : "false",
        info.is_simulation_running ? "true" : "false",
        info.frames_running);
}

} // namespace

void register_editor_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "play_get_state",
         .description = "Get editor play mode state: phase, active, paused, splash, frames_running.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
             {
                 return {.text = play_state_to_json(editor_actions::get_play_state(ctx)), .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "play_set_active",
         .description =
             "Enter or exit play mode. Blocked when scripts have compile errors. Optional allow_splash.",
         .input_schema_json =
             R"({"type":"object","properties":{"active":{"type":"boolean"},"allow_splash":{"type":"boolean"}},"required":["active"]})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 bool active = false;
                 if(!read_bool(args, "active", active))
                 {
                     return {.text = "Missing active", .is_error = true};
                 }
                 bool allow_splash = true;
                 read_bool(args, "allow_splash", allow_splash);
                 std::string error;
                 if(!editor_actions::set_play_active(ctx, active, allow_splash, &error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 return {.text = fmt::format(R"({{"ok":true,"active":{}}})", active ? "true" : "false"),
                         .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "play_set_paused",
         .description = "Pause or resume simulation while play mode is active.",
         .input_schema_json =
             R"({"type":"object","properties":{"paused":{"type":"boolean"}},"required":["paused"]})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 bool paused = false;
                 if(!read_bool(args, "paused", paused))
                 {
                     return {.text = "Missing paused", .is_error = true};
                 }
                 std::string error;
                 if(!editor_actions::set_play_paused(ctx, paused, &error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 return {.text = fmt::format(R"({{"ok":true,"paused":{}}})", paused ? "true" : "false"),
                         .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "play_skip_frame",
         .description = "Advance one simulation frame while play mode is active and paused.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
             {
                 std::string error;
                 if(!editor_actions::skip_play_frame(ctx, &error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 return {.text = R"({"ok":true})", .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "selection_get",
         .description = "Get the active entity selection (active_entity_id + entity_ids).",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
             {
                 const auto sel = editor_actions::get_selection(ctx);
                 std::string ids = "[";
                 for(size_t i = 0; i < sel.entity_ids.size(); ++i)
                 {
                     if(i > 0)
                     {
                         ids += ",";
                     }
                     ids += make_json_string(sel.entity_ids[i]);
                 }
                 ids += "]";
                 return {.text = fmt::format(R"({{"active_entity_id":{},"entity_ids":{}}})",
                                             sel.active_entity_id.empty() ? "null"
                                                                          : make_json_string(sel.active_entity_id),
                                             ids),
                         .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "selection_set_batch",
         .description =
             "Set editor entity selection by UUID (or integral) ids. "
             "mode=normal (default) replaces selection; mode=add appends (ctrl-select).",
         .input_schema_json =
             R"({"type":"object","properties":{"entity_ids":{"type":"array","items":{"type":"string"}},"mode":{"type":"string","enum":["normal","add"]}},"required":["entity_ids"]})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 simdjson::dom::array arr;
                 if(args["entity_ids"].get(arr))
                 {
                     return {.text = "Missing entity_ids", .is_error = true};
                 }
                 std::vector<std::string> ids;
                 for(auto el : arr)
                 {
                     std::string_view view;
                     if(el.get(view))
                     {
                         return {.text = "entity_ids must be strings", .is_error = true};
                     }
                     ids.emplace_back(view);
                 }
                 bool add = false;
                 std::string mode;
                 if(read_string(args, "mode", mode) && mode == "add")
                 {
                     add = true;
                 }
                 std::string error;
                 if(!editor_actions::set_selection(ctx, ids, add, &error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 const auto sel = editor_actions::get_selection(ctx);
                 std::string out_ids = "[";
                 for(size_t i = 0; i < sel.entity_ids.size(); ++i)
                 {
                     if(i > 0)
                     {
                         out_ids += ",";
                     }
                     out_ids += make_json_string(sel.entity_ids[i]);
                 }
                 out_ids += "]";
                 return {.text = fmt::format(R"({{"active_entity_id":{},"entity_ids":{}}})",
                                             sel.active_entity_id.empty() ? "null"
                                                                          : make_json_string(sel.active_entity_id),
                                             out_ids),
                         .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "selection_clear",
         .description = "Clear the editor selection.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
             {
                 editor_actions::clear_selection(ctx);
                 return {.text = R"({"cleared":true})", .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "window_request_focus",
         .description =
             "Focus/raise the editor OS window so filesystem watchers can process asset changes.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
             {
                 std::string error;
                 if(!editor_actions::request_main_window_focus(ctx, &error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 return {.text = R"({"ok":true,"requested":true})", .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "panel_focus_scene",
         .description = "Focus the Scene panel (editor viewport tab) so it becomes the active ImGui window.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
             {
                 std::string error;
                 if(!editor_actions::focus_scene_panel(ctx, &error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 return {.text = R"({"panel":"scene","focused":true})", .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "panel_focus_game",
         .description = "Focus the Game panel (play view tab) so it becomes the active ImGui window.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
             {
                 std::string error;
                 if(!editor_actions::focus_game_panel(ctx, &error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 return {.text = R"({"panel":"game","focused":true})", .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "logs_get_recent",
         .description =
             "Get recent console log entries. Optional min_level (trace|debug|info|warn|error|critical), "
             "max_count (default 50), after_id (exclusive cursor).",
         .input_schema_json =
             R"({"type":"object","properties":{"min_level":{"type":"string"},"max_count":{"type":"integer","minimum":1,"maximum":1024},"after_id":{"type":"integer","minimum":0}}})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 level::level_enum min_level = level::info;
                 std::string level_name;
                 if(read_string(args, "min_level", level_name) && !parse_min_level(level_name, min_level))
                 {
                     return {.text = "Invalid min_level", .is_error = true};
                 }
                 double max_count_d = 50.0;
                 size_t max_count = 50;
                 if(read_double(args, "max_count", max_count_d))
                 {
                     max_count = static_cast<size_t>(std::clamp(max_count_d, 1.0, 1024.0));
                 }
                 double after_d = 0.0;
                 uint64_t after_id = 0;
                 if(read_double(args, "after_id", after_d) && after_d >= 0.0)
                 {
                     after_id = static_cast<uint64_t>(after_d);
                 }
                 const auto logs = editor_actions::get_recent_logs(ctx, min_level, max_count, after_id);
                 std::string json = "[";
                 for(size_t i = 0; i < logs.size(); ++i)
                 {
                     if(i > 0)
                     {
                         json += ",";
                     }
                     const auto& e = logs[i];
                     json += fmt::format(R"({{"id":{},"level":{},"text":{},"filename":{},"funcname":{},"line":{}}})",
                                         e.id,
                                         make_json_string(level_to_string(e.level)),
                                         make_json_string(e.text),
                                         make_json_string(e.filename),
                                         make_json_string(e.funcname),
                                         e.line);
                 }
                 json += "]";
                 return {.text = std::move(json), .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "edit_undo",
         .description = "Undo the last undoable editor action (same as Ctrl+Z).",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
             {
                 std::string error;
                 if(!require_not_play_mode(ctx, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 auto& em = ctx.get_cached<editing_manager>();
                 if(!em.can_undo())
                 {
                     return {.text = R"({"ok":false,"undone":false,"reason":"nothing to undo"})", .is_error = false};
                 }
                 auto action = em.undo();
                 const auto name = action ? action->name : std::string{};
                 return {.text = fmt::format(R"({{"ok":true,"undone":true,"action":{}}})", make_json_string(name)),
                         .is_error = false};
             },
         .mutates_scene = true});

    registry.add(
        {.name = "edit_redo",
         .description = "Redo the last undone editor action (same as Ctrl+Y).",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
             {
                 std::string error;
                 if(!require_not_play_mode(ctx, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 auto& em = ctx.get_cached<editing_manager>();
                 if(!em.can_redo())
                 {
                     return {.text = R"({"ok":false,"redone":false,"reason":"nothing to redo"})", .is_error = false};
                 }
                 auto action = em.redo();
                 const auto name = action ? action->name : std::string{};
                 return {.text = fmt::format(R"({{"ok":true,"redone":true,"action":{}}})", make_json_string(name)),
                         .is_error = false};
             },
         .mutates_scene = true});
}

} // namespace unravel::mcp
