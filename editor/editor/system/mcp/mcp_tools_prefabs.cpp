#include "mcp_tools_common.h"

#include <editor/editing/editing_manager.h>
#include <engine/assets/asset_manager.h>
#include <engine/ecs/prefab.h>
#include <uuid/uuid.h>

#include <string>

namespace unravel::mcp
{
namespace
{

auto resolve_prefab(rtti::context& ctx, const std::string& key_or_uid, std::string& error) -> asset_handle<prefab>
{
    auto& am = ctx.get_cached<asset_manager>();
    auto handle = [&]
    {
        if(auto as_uuid = hpp::uuid::from_string(key_or_uid))
        {
            return am.get_asset<prefab>(*as_uuid);
        }
        return am.get_asset<prefab>(key_or_uid);
    }();
    if(!handle)
    {
        error = "Prefab not found: " + key_or_uid;
    }
    return handle;
}

/// Reads the optional `save` argument. Prefab mode would otherwise ask with a modal dialog,
/// which no MCP client can answer, so an undecided caller is told to decide instead.
auto read_save_choice(const simdjson::dom::object& args,
                      const char* key,
                      editing_manager::save_option& out,
                      bool& provided) -> void
{
    bool save = false;
    provided = read_bool(args, key, save);
    out = save ? editing_manager::save_option::yes : editing_manager::save_option::no;
}

} // namespace

void register_prefab_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "prefab_open",
         .description =
             "Open a prefab (.pfb) for editing, as double-clicking it in the Content Browser does. "
             "While it is open every scene_* tool works on the prefab's contents instead of the scene; "
             "save with prefab_save, leave with prefab_close, and instances everywhere pick up the "
             "saved prefab. Switching from another prefab with unsaved edits needs save_current "
             "true or false.",
         .input_schema_json =
             R"json({"type":"object","properties":{"key":{"type":"string","description":"Prefab asset key or uid"},"save_current":{"type":"boolean","description":"When another prefab is open with unsaved edits: save them (true) or drop them (false)"}},"required":["key"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             if(!require_not_play_mode(ctx, error) || !require_open_project(ctx, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::string key;
             read_string(args, "key", key);
             if(key.empty())
             {
                 return {.text = "Provide key", .is_error = true};
             }
             auto target = resolve_prefab(ctx, key, error);
             if(!target)
             {
                 return {.text = error, .is_error = true};
             }

             auto& em = ctx.get_cached<editing_manager>();
             if(em.is_prefab_mode() && em.edited_prefab != target)
             {
                 editing_manager::save_option save{};
                 bool provided = false;
                 read_save_choice(args, "save_current", save, provided);
                 if(em.has_unsaved_prefab_changes() && !provided)
                 {
                     return {.text = fmt::format("{} has unsaved edits; pass save_current true to save them or "
                                                 "false to drop them",
                                                 em.edited_prefab.id()),
                             .is_error = true};
                 }
                 em.exit_prefab_mode(ctx, save);
             }

             em.enter_prefab_mode(ctx, target, false);
             if(!em.is_prefab_mode() || em.edited_prefab != target)
             {
                 return {.text = "The prefab did not open; see logs_get_recent", .is_error = true};
             }
             return {.text = fmt::format(R"({{"ok":true,"prefab_mode":{}}})", prefab_mode_status_json(em)),
                     .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "prefab_save",
         .description = "Save the prefab open in prefab mode. Every instance of it updates.",
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
             if(!em.is_prefab_mode())
             {
                 return {.text = "No prefab is open (prefab_open first)", .is_error = true};
             }
             em.save_prefab_changes(ctx);
             return {.text = fmt::format(R"({{"ok":true,"prefab_mode":{}}})", prefab_mode_status_json(em)),
                     .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "prefab_close",
         .description =
             "Leave prefab mode and return to the scene. With unsaved edits pass save true or false - "
             "the editor would otherwise ask with a dialog.",
         .input_schema_json =
             R"json({"type":"object","properties":{"save":{"type":"boolean","description":"Save unsaved edits first (true) or drop them (false)"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             if(!require_not_play_mode(ctx, error))
             {
                 return {.text = error, .is_error = true};
             }
             auto& em = ctx.get_cached<editing_manager>();
             if(!em.is_prefab_mode())
             {
                 return {.text = R"({"ok":true,"was_open":false})", .is_error = false};
             }
             editing_manager::save_option save{};
             bool provided = false;
             read_save_choice(args, "save", save, provided);
             if(em.has_unsaved_prefab_changes() && !provided)
             {
                 return {.text = fmt::format("{} has unsaved edits; pass save true to save them or false to drop them",
                                             em.edited_prefab.id()),
                         .is_error = true};
             }
             em.exit_prefab_mode(ctx, save);
             return {.text = R"({"ok":true,"was_open":true})", .is_error = false};
         },
         .mutates_scene = false});
}

} // namespace unravel::mcp
