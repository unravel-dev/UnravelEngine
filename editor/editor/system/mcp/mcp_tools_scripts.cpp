#include "mcp_tools_common.h"

#include <editor/assets/asset_actions.h>
#include <editor/editing/actions/actions.h>
#include <editor/system/project_manager.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/assets/impl/asset_writer.h>
#include <engine/scripting/ecs/components/script_component.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <engine/scripting/script.h>
#include <filesystem/filesystem.h>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace unravel::mcp
{
namespace
{

auto read_text_file(const fs::path& path, std::string& out, std::string& error) -> bool
{
    std::ifstream input(path, std::ios::binary);
    if(!input.is_open())
    {
        error = "Failed to open file: " + path.generic_string();
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    out = buffer.str();
    return true;
}

auto write_text_file_atomic(const fs::path& path, const std::string& contents, std::string& error) -> bool
{
    fs::error_code err;
    const auto absolute = fs::absolute(path);
    fs::create_directories(absolute.parent_path(), err);
    err.clear();
    asset_writer::atomic_write_file(
        absolute,
        [&](const fs::path& temp)
        {
            std::ofstream output(temp, std::ios::binary | std::ios::trunc);
            if(!output.is_open())
            {
                throw std::runtime_error("Failed to open temp file for write: " + temp.generic_string());
            }
            output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            if(!output.good())
            {
                throw std::runtime_error("Failed writing temp file: " + temp.generic_string());
            }
        },
        err);
    if(err)
    {
        error = "Atomic write failed: " + absolute.generic_string() + " (" + err.message() + ")";
        return false;
    }
    return true;
}

auto resolve_source_path(const std::string& path_or_key) -> fs::path
{
    if(path_or_key.empty())
    {
        return {};
    }
    fs::error_code ec;
    const fs::path as_path(path_or_key);
    if(as_path.is_absolute())
    {
        return as_path;
    }
    auto resolved = fs::resolve_protocol(path_or_key);
    if(!resolved.empty())
    {
        return resolved;
    }
    return as_path;
}

auto find_script_object(script_component& script_comp, const std::string& type_name)
    -> script_component::script_object
{
    for(const auto& obj : script_comp.get_script_components())
    {
        if(!obj.pinned)
        {
            continue;
        }
        if(obj.pinned->get_object().get_type().get_fullname() == type_name)
        {
            return obj;
        }
    }
    return {};
}

auto scripts_on_entity_json(entt::handle entity) -> std::string
{
    std::string json = "[";
    bool first = true;
    if(auto* script_comp = entity.try_get<script_component>())
    {
        for(const auto& obj : script_comp->get_script_components())
        {
            if(!obj.pinned)
            {
                continue;
            }
            const auto type_name = obj.pinned->get_object().get_type().get_fullname();
            const auto source = script_comp->get_script_source_location(obj);
            if(!first)
            {
                json += ",";
            }
            first = false;
            json += fmt::format(R"({{"type":{},"source_path":{}}})",
                                make_json_string(type_name),
                                make_json_string(source));
        }
    }
    json += "]";
    return json;
}

auto unique_script_path(const fs::path& dir, const std::string& stem) -> fs::path
{
    const auto ext = ex::get_format<script>();
    fs::path candidate = dir / (stem + ext);
    fs::error_code ec;
    int i = 0;
    while(fs::exists(candidate, ec))
    {
        ++i;
        candidate = dir / (fmt::format("{}{}", stem, i) + ext);
    }
    return candidate;
}

auto protocol_for_path(const fs::path& absolute) -> std::string
{
    const auto key = fs::convert_to_protocol(absolute).generic_string();
    return fs::extract_protocol(key).string();
}

} // namespace

void register_script_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "scripts_list_types",
         .description =
             "List C# ScriptComponent type full names available to add via scene_add_script "
             "(project + engine scriptable types from script_system).",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
         {
             if(!ctx.has<script_system>())
             {
                 return {.text = "Script system unavailable", .is_error = true};
             }
             const auto& types = ctx.get_cached<script_system>().get_all_scriptable_components();
             std::string json = "[";
             for(size_t i = 0; i < types.size(); ++i)
             {
                 if(i > 0)
                 {
                     json += ",";
                 }
                 json += make_json_string(types[i].get_fullname());
             }
             json += "]";
             return {.text = json, .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "scene_list_scripts",
         .description =
             "List ScriptComponent instances on an entity: type full name and source_path "
             "(from [ScriptSourceFile] / SourceFilePath).",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_id":{"type":"string"}},"required":["entity_id"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& em = ctx.get_cached<editing_manager>();
             auto* scn = em.get_active_scene(ctx);
             if(!scn || !scn->registry)
             {
                 return {.text = "No active scene", .is_error = true};
             }
             std::string entity_id;
             if(!read_string(args, "entity_id", entity_id))
             {
                 return {.text = "Missing entity_id", .is_error = true};
             }
             auto entity = find_entity(*scn, entity_id);
             if(!entity)
             {
                 return {.text = "Entity not found", .is_error = true};
             }
             return {.text = fmt::format(R"({{"entity_id":{},"scripts":{}}})",
                                         make_json_string(entity_id_string(entity)),
                                         scripts_on_entity_json(entity)),
                     .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "scene_add_script",
         .description =
             "Add a C# ScriptComponent type to an entity (same as inspector Add Component). "
             "type_name is the full name from scripts_list_types. Not available via scene_add_component.",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_id":{"type":"string"},"type_name":{"type":"string"}},"required":["entity_id","type_name"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::string entity_id;
             std::string type_name;
             if(!read_string(args, "entity_id", entity_id) || !read_string(args, "type_name", type_name))
             {
                 return {.text = "Missing entity_id or type_name", .is_error = true};
             }
             auto entity = find_entity(*scn, entity_id);
             if(!entity)
             {
                 return {.text = "Entity not found", .is_error = true};
             }
             auto& script_sys = ctx.get_cached<script_system>();
             if(!script_sys.get_type_by_fullname(type_name).valid())
             {
                 return {.text = "Unknown script type: " + type_name, .is_error = true};
             }
             ctx.get_cached<editing_manager>().do_action<entity_add_script_component_action_t>(
                 "MCP Add Script",
                 entity,
                 type_name);
             return {.text = fmt::format(R"({{"ok":true,"entity_id":{},"scripts":{}}})",
                                         make_json_string(entity_id_string(entity)),
                                         scripts_on_entity_json(entity)),
                     .is_error = false};
         },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_remove_script",
         .description = "Remove a C# ScriptComponent type instance from an entity by type_name.",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_id":{"type":"string"},"type_name":{"type":"string"}},"required":["entity_id","type_name"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::string entity_id;
             std::string type_name;
             if(!read_string(args, "entity_id", entity_id) || !read_string(args, "type_name", type_name))
             {
                 return {.text = "Missing entity_id or type_name", .is_error = true};
             }
             auto entity = find_entity(*scn, entity_id);
             if(!entity)
             {
                 return {.text = "Entity not found", .is_error = true};
             }
             ctx.get_cached<editing_manager>().do_action<entity_remove_script_component_action_t>(
                 "MCP Remove Script",
                 entity,
                 type_name);
             return {.text = fmt::format(R"({{"ok":true,"entity_id":{},"scripts":{}}})",
                                         make_json_string(entity_id_string(entity)),
                                         scripts_on_entity_json(entity)),
                     .is_error = false};
         },
         .mutates_scene = true});

    registry.add(
        {.name = "scripts_get_source",
         .description =
             "Read C# source for a script. Resolve by path/key, or by entity_id + type_name "
             "(uses get_script_source_location).",
         .input_schema_json =
             R"json({"type":"object","properties":{"path":{"type":"string"},"key":{"type":"string"},"entity_id":{"type":"string"},"type_name":{"type":"string"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string path;
             std::string key;
             read_string(args, "path", path);
             read_string(args, "key", key);
             if(path.empty() && !key.empty())
             {
                 path = key;
             }

             std::string type_name;
             read_string(args, "type_name", type_name);
             std::string entity_id;
             read_string(args, "entity_id", entity_id);

             if(path.empty())
             {
                 if(entity_id.empty() || type_name.empty())
                 {
                     return {.text = "Provide path/key, or entity_id + type_name", .is_error = true};
                 }
                 auto& em = ctx.get_cached<editing_manager>();
                 auto* scn = em.get_active_scene(ctx);
                 if(!scn || !scn->registry)
                 {
                     return {.text = "No active scene", .is_error = true};
                 }
                 auto entity = find_entity(*scn, entity_id);
                 auto* script_comp = entity ? entity.try_get<script_component>() : nullptr;
                 if(!script_comp)
                 {
                     return {.text = "Entity has no ScriptComponent", .is_error = true};
                 }
                 auto obj = find_script_object(*script_comp, type_name);
                 if(!obj.pinned)
                 {
                     return {.text = "Script type not on entity: " + type_name, .is_error = true};
                 }
                 path = script_comp->get_script_source_location(obj);
                 if(path.empty())
                 {
                     return {.text = "No source path for script type: " + type_name, .is_error = true};
                 }
             }

             const auto absolute = resolve_source_path(path);
             std::string code;
             std::string error;
             if(!read_text_file(absolute, code, error))
             {
                 return {.text = error, .is_error = true};
             }
             return {.text = fmt::format(R"({{"path":{},"bytes":{},"code":{}}})",
                                         make_json_string(absolute.generic_string()),
                                         code.size(),
                                         make_json_string(code)),
                     .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "scripts_set_source",
         .description =
             "Write C# source to disk with asset_writer::atomic_write_file. Resolve target by path/key "
             "or entity_id + type_name. Optional recompile:true (default) schedules script_system "
             "recompile for the file protocol.",
         .input_schema_json =
             R"json({"type":"object","properties":{"code":{"type":"string"},"path":{"type":"string"},"key":{"type":"string"},"entity_id":{"type":"string"},"type_name":{"type":"string"},"recompile":{"type":"boolean","default":true}},"required":["code"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             if(!require_not_play_mode(ctx, error))
             {
                 return {.text = error, .is_error = true};
             }

             std::string code;
             if(!read_string(args, "code", code))
             {
                 return {.text = "Missing code", .is_error = true};
             }

             std::string path;
             std::string key;
             read_string(args, "path", path);
             read_string(args, "key", key);
             if(path.empty() && !key.empty())
             {
                 path = key;
             }

             std::string type_name;
             std::string entity_id;
             read_string(args, "type_name", type_name);
             read_string(args, "entity_id", entity_id);

             if(path.empty())
             {
                 if(entity_id.empty() || type_name.empty())
                 {
                     return {.text = "Provide path/key, or entity_id + type_name", .is_error = true};
                 }
                 scene* scn = nullptr;
                 if(!require_edit_scene(ctx, scn, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 auto entity = find_entity(*scn, entity_id);
                 auto* script_comp = entity ? entity.try_get<script_component>() : nullptr;
                 if(!script_comp)
                 {
                     return {.text = "Entity has no ScriptComponent", .is_error = true};
                 }
                 auto obj = find_script_object(*script_comp, type_name);
                 if(!obj.pinned)
                 {
                     return {.text = "Script type not on entity: " + type_name, .is_error = true};
                 }
                 path = script_comp->get_script_source_location(obj);
                 if(path.empty())
                 {
                     return {.text = "No source path for script type: " + type_name, .is_error = true};
                 }
             }

             const auto absolute = resolve_source_path(path);
             if(!write_text_file_atomic(absolute, code, error))
             {
                 return {.text = error, .is_error = true};
             }

             bool recompile = true;
             read_bool(args, "recompile", recompile);
             std::string protocol;
             if(recompile)
             {
                 protocol = protocol_for_path(absolute);
                 if(protocol.empty())
                 {
                     protocol = "app";
                 }
                 script_system::set_needs_recompile(protocol, true);
             }

             return {.text = fmt::format(R"({{"ok":true,"path":{},"bytes":{},"recompile":{},"protocol":{}}})",
                                         make_json_string(absolute.generic_string()),
                                         code.size(),
                                         recompile ? "true" : "false",
                                         make_json_string(protocol)),
                     .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "scripts_create",
         .description =
             "Create a new C# ScriptComponent .cs file from engine TemplateComponent.cs.in "
             "(#SCRIPTNAME# = file stem). Writes via atomic_write_file under folder (default "
             "app:/data/scripts). Optional recompile:true (default).",
         .input_schema_json =
             R"json({"type":"object","properties":{"name":{"type":"string","description":"Class/file stem, valid C# identifier"},"folder":{"type":"string","description":"Directory key or absolute path (default app:/data/scripts)"},"recompile":{"type":"boolean","default":true}},"required":["name"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             if(!require_not_play_mode(ctx, error) || !require_open_project(ctx, error))
             {
                 return {.text = error, .is_error = true};
             }

             std::string name;
             if(!read_string(args, "name", name) || name.empty())
             {
                 return {.text = "Missing name", .is_error = true};
             }
             if(name.size() > 3 && name.substr(name.size() - 3) == ".cs")
             {
                 name = name.substr(0, name.size() - 3);
             }
             if(!asset_actions::is_valid_csharp_identifier(name))
             {
                 return {.text = "name must be a valid C# identifier", .is_error = true};
             }

             std::string folder = "app:/data/scripts";
             read_string(args, "folder", folder);
             if(folder.empty())
             {
                 folder = "app:/data/scripts";
             }

             const auto dir = resolve_source_path(folder);
             const auto dst = unique_script_path(dir, name);
             const auto template_path =
                 fs::resolve_protocol("engine:/data/templates/TemplateComponent" + ex::get_format<script>() + ".in");

             if(!asset_actions::create_script_from_template(template_path, dst, &error))
             {
                 return {.text = error, .is_error = true};
             }

             bool recompile = true;
             read_bool(args, "recompile", recompile);
             std::string protocol;
             if(recompile)
             {
                 protocol = protocol_for_path(dst);
                 if(protocol.empty())
                 {
                     protocol = "app";
                 }
                 script_system::set_needs_recompile(protocol, true);
             }

             const auto key = fs::convert_to_protocol(dst).generic_string();
             return {.text = fmt::format(
                         R"({{"ok":true,"path":{},"key":{},"type_name":{},"recompile":{},"protocol":{}}})",
                         make_json_string(dst.generic_string()),
                         make_json_string(key),
                         make_json_string(dst.stem().string()),
                         recompile ? "true" : "false",
                         make_json_string(protocol)),
                     .is_error = false};
         },
         .mutates_scene = false});
}

} // namespace unravel::mcp
