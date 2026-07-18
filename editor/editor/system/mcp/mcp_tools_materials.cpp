#include "mcp_async.h"
#include "mcp_material_utils.h"
#include "mcp_tools_common.h"

#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <editor/system/mcp_manager.h>
#include <editor/system/project_manager.h>
#include <engine/assets/asset_manager.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/material.h>
#include <engine/rendering/model.h>
#include <filesystem/filesystem.h>

#include <chrono>
#include <thread>

namespace unravel::mcp
{
namespace
{

auto apply_result_to_json(const material_apply_result& result) -> std::string
{
    auto list_json = [](const std::vector<std::string>& items) -> std::string
    {
        std::string json = "[";
        for(size_t i = 0; i < items.size(); ++i)
        {
            if(i > 0)
            {
                json += ",";
            }
            json += make_json_string(items[i]);
        }
        json += "]";
        return json;
    };

    return fmt::format(R"({{"ok":{},"applied":{},"unknown":{},"errors":{}}})",
                       result.ok ? "true" : "false",
                       list_json(result.applied),
                       list_json(result.unknown),
                       list_json(result.errors));
}

auto read_wait_ms(const simdjson::dom::object& args, int64_t default_ms = 1000) -> std::chrono::milliseconds
{
    int64_t wait_ms = default_ms;
    if(args["wait_ms"].get(wait_ms))
    {
        wait_ms = default_ms;
    }
    if(wait_ms < 0)
    {
        wait_ms = 0;
    }
    if(wait_ms > 15000)
    {
        wait_ms = 15000;
    }
    return std::chrono::milliseconds(wait_ms);
}

auto resolve_create_key(const simdjson::dom::object& args, std::string& error) -> std::string
{
    std::string path;
    std::string folder;
    std::string name;
    read_string(args, "path", path);
    read_string(args, "folder", folder);
    read_string(args, "name", name);

    if(!path.empty())
    {
        return normalize_material_key(path);
    }
    if(folder.empty() || name.empty())
    {
        error = "Provide path, or folder + name";
        return {};
    }

    auto key = folder;
    if(!key.empty() && key.back() != '/')
    {
        key.push_back('/');
    }
    key += name;
    return normalize_material_key(key);
}

auto require_model_entity(rtti::context& ctx,
                          const simdjson::dom::object& args,
                          entt::handle& entity,
                          uint32_t& index,
                          std::string& error) -> bool
{
    scene* scn = nullptr;
    if(!require_edit_scene(ctx, scn, error))
    {
        return false;
    }

    std::string entity_id;
    if(!read_string(args, "entity_id", entity_id))
    {
        error = "Missing entity_id";
        return false;
    }

    entity = find_entity(*scn, entity_id);
    if(!entity)
    {
        error = "Entity not found: " + entity_id;
        return false;
    }
    if(!entity.all_of<model_component>())
    {
        error = "Entity has no model_component";
        return false;
    }

    int64_t index_i = 0;
    if(args["index"].get(index_i))
    {
        index_i = 0;
    }
    if(index_i < 0)
    {
        error = "index must be >= 0";
        return false;
    }
    index = static_cast<uint32_t>(index_i);
    return true;
}

auto sleep_worker(std::chrono::milliseconds wait_ms) -> void
{
    if(wait_ms.count() > 0)
    {
        std::this_thread::sleep_for(wait_ms);
    }
}

} // namespace

void register_material_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "materials_list_properties",
         .description = "List supported PBR material property names/types for materials_set and "
                        "scene_set_model_material_instance.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context&, const simdjson::dom::object&) -> tool_result
         {
             return {.text = list_material_property_schema_json(), .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "materials_get",
         .description = "Read PBR material properties as JSON by asset key or uid.",
         .input_schema_json =
             R"({"type":"object","properties":{"key":{"type":"string"},"uid":{"type":"string"}}})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string key;
             std::string uid;
             read_string(args, "key", key);
             read_string(args, "uid", uid);

             std::string error;
             auto handle = resolve_material_asset(ctx, key, uid, error);
             if(!handle)
             {
                 return {.text = error, .is_error = true};
             }

             auto mat = handle.get();
             if(!mat)
             {
                 return {.text = "Material asset not loaded", .is_error = true};
             }

             return {.text = fmt::format(R"({{"key":{},"uid":{},"properties":{}}})",
                                         make_json_string(handle.id()),
                                         make_json_string(hpp::to_string(handle.uid())),
                                         material_to_json(*mat)),
                     .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "materials_create",
         .description =
             "Create a new PBR .mat asset (content-browser parity). Provide path, or folder + name. "
             "Saves to disk then briefly waits for the asset watcher.",
         .input_schema_json =
             R"({"type":"object","properties":{"path":{"type":"string","description":"Full asset key e.g. app:/data/MyMat.mat"},"folder":{"type":"string"},"name":{"type":"string"},"wait_ms":{"type":"integer","minimum":0,"maximum":15000}}})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             const auto wait_ms = read_wait_ms(args, 1000);

             std::string key_error;
             const auto key = resolve_create_key(args, key_error);
             if(key.empty())
             {
                 return {.text = key_error, .is_error = true};
             }

             auto create_result = mcp.invoke_on_main(
                 [&ctx, key]() -> tool_result
                 {
                     if(ctx.has<project_manager>())
                     {
                         auto& pm = ctx.get_cached<project_manager>();
                         if(key.rfind("app:/", 0) == 0 && !pm.has_open_project())
                         {
                             return {.text = "No project open", .is_error = true};
                         }
                     }

                     fs::error_code ec;
                     const auto absolute = fs::absolute(fs::resolve_protocol(key));
                     if(fs::exists(absolute, ec))
                     {
                         return {.text = "Material already exists: " + key, .is_error = true};
                     }

                     fs::create_directories(absolute.parent_path(), ec);

                     auto& am = ctx.get_cached<asset_manager>();
                     auto handle = am.get_asset_from_instance<material>(key, std::make_shared<pbr_material>());
                     if(!handle)
                     {
                         return {.text = "Failed to create material instance", .is_error = true};
                     }

                     std::string save_error;
                     if(!save_material_asset(ctx, handle, save_error))
                     {
                         return {.text = save_error, .is_error = true};
                     }

                     return {.text = fmt::format(R"({{"key":{},"uid":{},"saved":true}})",
                                                 make_json_string(handle.id()),
                                                 make_json_string(hpp::to_string(handle.uid()))),
                             .is_error = false};
                 });

             if(!create_result)
             {
                 return {.text = "Timed out creating material on main thread", .is_error = true};
             }
             if(!create_result->is_error)
             {
                 sleep_worker(wait_ms);
             }
             return *create_result;
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "materials_set",
         .description =
             "Set PBR material asset properties by key/uid. `properties` is an object of supported "
             "keys. Saves to disk by default (inspector parity). Set save:false for in-memory only.",
         .input_schema_json =
             R"({"type":"object","properties":{"key":{"type":"string"},"uid":{"type":"string"},"properties":{"type":"object"},"save":{"type":"boolean","default":true},"wait_ms":{"type":"integer","minimum":0,"maximum":15000}},"required":["properties"]})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             const auto wait_ms = read_wait_ms(args, 1000);

             std::string key;
             std::string uid;
             read_string(args, "key", key);
             read_string(args, "uid", uid);

             bool save = true;
             read_bool(args, "save", save);

             simdjson::dom::object properties;
             if(args["properties"].get(properties))
             {
                 return {.text = "Missing properties object", .is_error = true};
             }

             // Copy args JSON fragment by re-parsing is awkward; apply on main with captured strings
             // and re-get properties from a serialized form.
             const std::string props_json = std::string(simdjson::minify(args["properties"]));

             auto set_result = mcp.invoke_on_main(
                 [&ctx, key, uid, save, props_json]() -> tool_result
                 {
                     std::string error;
                     auto handle = resolve_material_asset(ctx, key, uid, error);
                     if(!handle)
                     {
                         return {.text = error, .is_error = true};
                     }

                     auto mat = handle.get();
                     if(!mat)
                     {
                         return {.text = "Material asset not loaded", .is_error = true};
                     }

                     simdjson::dom::parser parser;
                     simdjson::dom::element root;
                     if(parser.parse(props_json).get(root))
                     {
                         return {.text = "Failed to parse properties", .is_error = true};
                     }
                     simdjson::dom::object props;
                     if(root.get(props))
                     {
                         return {.text = "properties must be an object", .is_error = true};
                     }

                     auto applied = apply_material_properties(ctx, mat, props);
                     if(!applied.ok)
                     {
                         return {.text = apply_result_to_json(applied), .is_error = true};
                     }

                     bool saved = false;
                     if(save)
                     {
                         if(!save_material_asset(ctx, handle, error))
                         {
                             return {.text = error, .is_error = true};
                         }
                         saved = true;
                     }

                     return {.text = fmt::format(R"({{"key":{},"uid":{},"saved":{},"result":{}}})",
                                                 make_json_string(handle.id()),
                                                 make_json_string(hpp::to_string(handle.uid())),
                                                 saved ? "true" : "false",
                                                 apply_result_to_json(applied)),
                             .is_error = false};
                 });

             if(!set_result)
             {
                 return {.text = "Timed out setting material on main thread", .is_error = true};
             }
             if(!set_result->is_error && save)
             {
                 sleep_worker(wait_ms);
             }
             return *set_result;
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "scene_set_model_material",
         .description =
             "Assign a shared material asset to a model_component material slot (writes scene/prefab "
             "overrides only; does not mutate the .mat file).",
         .input_schema_json =
             R"({"type":"object","properties":{"entity_id":{"type":"string"},"index":{"type":"integer","minimum":0},"material_key":{"type":"string"}},"required":["entity_id","material_key"]})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             entt::handle entity;
             uint32_t index = 0;
             std::string error;
             if(!require_model_entity(ctx, args, entity, index, error))
             {
                 return {.text = error, .is_error = true};
             }

             std::string material_key;
             if(!read_string(args, "material_key", material_key))
             {
                 return {.text = "Missing material_key", .is_error = true};
             }

             auto material = resolve_material_asset(ctx, material_key, {}, error);
             if(!material)
             {
                 return {.text = error, .is_error = true};
             }

             auto& model_comp = entity.get<model_component>();
             const auto old_model = model_comp.get_model();
             auto new_model = old_model;
             new_model.set_material(material, index);

             auto& em = ctx.get_cached<editing_manager>();
             em.do_action(
                 "MCP Set Model Material",
                 [entity, new_model]()
                 {
                     if(auto* mc = entity.try_get<model_component>())
                     {
                         mc->set_model(new_model);
                         prefab_override_context::mark_material_as_changed(entity);
                     }
                 },
                 [entity, old_model]()
                 {
                     if(auto* mc = entity.try_get<model_component>())
                     {
                         mc->set_model(old_model);
                         prefab_override_context::mark_material_as_changed(entity);
                     }
                 });

             return {.text = fmt::format(R"({{"entity_id":{},"index":{},"material_key":{}}})",
                                         make_json_string(entity_id_string(entity)),
                                         index,
                                         make_json_string(material.id())),
                     .is_error = false};
         },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_set_model_material_instance",
         .description =
             "Edit a per-entity runtime material instance on a model slot (get_or_emplace + set_model). "
             "Does NOT write a .mat file. Use for prototype/scene-only overrides.",
         .input_schema_json =
             R"({"type":"object","properties":{"entity_id":{"type":"string"},"index":{"type":"integer","minimum":0},"properties":{"type":"object"}},"required":["entity_id","properties"]})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             entt::handle entity;
             uint32_t index = 0;
             std::string error;
             if(!require_model_entity(ctx, args, entity, index, error))
             {
                 return {.text = error, .is_error = true};
             }

             simdjson::dom::object properties;
             if(args["properties"].get(properties))
             {
                 return {.text = "Missing properties object", .is_error = true};
             }

             auto& model_comp = entity.get<model_component>();
             const auto old_model = model_comp.get_model();
             auto new_model = old_model;
             // Avoid get_or_emplace path that clones a null slot material.
             if(!new_model.get_material(index).is_valid())
             {
                 new_model.set_material_instance(std::make_shared<pbr_material>(), index);
             }
             auto instance = new_model.get_or_emplace_material_instance(index);
             if(!instance)
             {
                 return {.text = "Failed to create material instance", .is_error = true};
             }

             auto applied = apply_material_properties(ctx, instance, properties);
             if(!applied.ok)
             {
                 return {.text = apply_result_to_json(applied), .is_error = true};
             }

             auto& em = ctx.get_cached<editing_manager>();
             em.do_action(
                 "MCP Set Model Material Instance",
                 [entity, new_model]()
                 {
                     if(auto* mc = entity.try_get<model_component>())
                     {
                         mc->set_model(new_model);
                         prefab_override_context::mark_material_as_changed(entity);
                     }
                 },
                 [entity, old_model]()
                 {
                     if(auto* mc = entity.try_get<model_component>())
                     {
                         mc->set_model(old_model);
                         prefab_override_context::mark_material_as_changed(entity);
                     }
                 });

             return {.text = fmt::format(R"({{"entity_id":{},"index":{},"result":{}}})",
                                         make_json_string(entity_id_string(entity)),
                                         index,
                                         apply_result_to_json(applied)),
                     .is_error = false};
         },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_clear_model_material_instance",
         .description =
             "Clear a runtime material instance on a model slot so the shared asset is used again.",
         .input_schema_json =
             R"({"type":"object","properties":{"entity_id":{"type":"string"},"index":{"type":"integer","minimum":0}},"required":["entity_id"]})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             entt::handle entity;
             uint32_t index = 0;
             std::string error;
             if(!require_model_entity(ctx, args, entity, index, error))
             {
                 return {.text = error, .is_error = true};
             }

             auto& model_comp = entity.get<model_component>();
             const auto old_model = model_comp.get_model();
             auto new_model = old_model;
             new_model.set_material_instance(nullptr, index);

             auto& em = ctx.get_cached<editing_manager>();
             em.do_action(
                 "MCP Clear Model Material Instance",
                 [entity, new_model]()
                 {
                     if(auto* mc = entity.try_get<model_component>())
                     {
                         mc->set_model(new_model);
                         prefab_override_context::mark_material_as_changed(entity);
                     }
                 },
                 [entity, old_model]()
                 {
                     if(auto* mc = entity.try_get<model_component>())
                     {
                         mc->set_model(old_model);
                         prefab_override_context::mark_material_as_changed(entity);
                     }
                 });

             return {.text = fmt::format(R"({{"entity_id":{},"index":{},"cleared":true}})",
                                         make_json_string(entity_id_string(entity)),
                                         index),
                     .is_error = false};
         },
         .mutates_scene = true});
}

} // namespace unravel::mcp
