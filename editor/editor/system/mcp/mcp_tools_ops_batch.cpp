#include "mcp_component_utils.h"
#include "mcp_meta_properties.h"
#include "mcp_tools_common.h"

#include <editor/editing/actions/actions.h>
#include <editor/editing/editor_actions.h>
#include <editor/editing/entity_inspect.h>
#include <engine/assets/asset_manager.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/components/layer_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/meta/ecs/components/all_components.h>
#include <engine/scripting/ecs/components/script_component.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <hpp/utility.hpp>
#include <map>
#include <math/math.h>

namespace unravel::mcp
{
namespace
{

auto resolve_addable_component(const std::string& name) -> entt::meta_type
{
    entt::meta_type found{};
    hpp::for_each_tuple_type<all_addable_components>(
        [&](auto index)
        {
            using ctype = std::tuple_element_t<decltype(index)::value, all_addable_components>;
            auto type = entt::resolve<ctype>();
            auto pretty = std::string(entt::get_pretty_name(type));
            auto raw = std::string(entt::get_name(type));
            if(pretty == name || raw == name)
            {
                found = type;
            }
        });
    return found;
}

auto item_space_is_local(const simdjson::dom::object& obj) -> bool
{
    std::string space;
    if(read_string(obj, "space", space) && space == "local")
    {
        return true;
    }
    return false;
}

auto summarize_created(const std::vector<entt::handle>& created, size_t requested) -> tool_result
{
    std::string json = "[";
    for(size_t i = 0; i < created.size(); ++i)
    {
        if(i > 0)
        {
            json += ",";
        }
        json += entity_to_lean_json(created[i], true);
    }
    json += "]";
    return {.text = fmt::format(R"({{"created":{},"count":{},"requested":{}}})", json, created.size(), requested),
            .is_error = created.empty() && requested > 0};
}

} // namespace

void register_ops_batch_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "scene_create_entities_batch",
         .description =
             "Create empty entities in one undoable action. Each item: name (required), optional "
             "parent_id, position/rotation_euler/scale, space (world default|local).",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"name":{"type":"string"},"parent_id":{"type":"string"},"space":{"type":"string","enum":["world","local"]},"position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"rotation_euler":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"scale":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3}},"required":["name"]}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 scene* scn = nullptr;
                 std::string error;
                 if(!require_edit_scene(ctx, scn, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 simdjson::dom::array items_arr;
                 if(!read_required_array(args, "items", items_arr, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 struct item_t
                 {
                     std::string name;
                     std::string parent_id;
                     transform_snapshot pose{};
                     bool is_local{false};
                 };
                 std::vector<item_t> items;
                 for(auto el : items_arr)
                 {
                     simdjson::dom::object obj;
                     if(!read_object(el, obj, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     item_t item{};
                     if(!read_string(obj, "name", item.name) || item.name.empty())
                     {
                         return {.text = "Item missing name", .is_error = true};
                     }
                     read_string(obj, "parent_id", item.parent_id);
                     item.is_local = item_space_is_local(obj);
                     read_transform_snapshot(obj, item.pose);
                     items.push_back(std::move(item));
                 }
                 if(items.empty())
                 {
                     return {.text = "items array is empty", .is_error = true};
                 }
                 std::vector<entt::handle> created;
                 auto& em = ctx.get_cached<editing_manager>();
                 em.do_action<create_entities_action_t>(
                     "MCP Batch Create Entities",
                     [&]()
                     {
                         created.clear();
                         created.reserve(items.size());
                         for(const auto& item : items)
                         {
                             entt::handle parent{};
                             if(!item.parent_id.empty())
                             {
                                 parent = find_entity(*scn, item.parent_id);
                             }
                             auto entity = scn->create_entity(item.name, parent);
                             if(!entity)
                             {
                                 continue;
                             }
                             apply_pose_direct(entity, item.pose, item.is_local);
                             created.push_back(entity);
                         }
                         return created;
                     });
                 return summarize_created(created, items.size());
             },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_create_from_prefab_batch",
         .description =
             "Instantiate prefab assets in one undoable action. Each item: asset_key (required), "
             "optional name/parent_id, position/rotation_euler/scale, space (world default|local).",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"asset_key":{"type":"string"},"name":{"type":"string"},"parent_id":{"type":"string"},"space":{"type":"string","enum":["world","local"]},"position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"rotation_euler":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"scale":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3}},"required":["asset_key"]}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 scene* scn = nullptr;
                 std::string error;
                 if(!require_edit_scene(ctx, scn, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 simdjson::dom::array items_arr;
                 if(!read_required_array(args, "items", items_arr, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 struct item_t
                 {
                     std::string asset_key;
                     std::string name;
                     std::string parent_id;
                     transform_snapshot pose{};
                     bool is_local{false};
                 };
                 std::vector<item_t> items;
                 for(auto el : items_arr)
                 {
                     simdjson::dom::object obj;
                     if(!read_object(el, obj, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     item_t item{};
                     if(!read_string(obj, "asset_key", item.asset_key) || item.asset_key.empty())
                     {
                         return {.text = "Item missing asset_key", .is_error = true};
                     }
                     read_string(obj, "name", item.name);
                     read_string(obj, "parent_id", item.parent_id);
                     item.is_local = item_space_is_local(obj);
                     read_transform_snapshot(obj, item.pose);
                     items.push_back(std::move(item));
                 }
                 if(items.empty())
                 {
                     return {.text = "items array is empty", .is_error = true};
                 }
                 std::vector<entt::handle> created;
                 auto& em = ctx.get_cached<editing_manager>();
                 em.do_action<create_entities_action_t>(
                     "MCP Batch Create Prefabs",
                     [&]()
                     {
                         created.clear();
                         for(const auto& item : items)
                         {
                             entt::handle entity{};
                             if(item.pose.has_position && !item.is_local)
                             {
                                 entity = defaults::create_prefab_at(ctx, *scn, item.asset_key, item.pose.position);
                             }
                             else
                             {
                                 entity = defaults::create_prefab_at(ctx, *scn, item.asset_key);
                             }
                             if(!entity)
                             {
                                 continue;
                             }
                             auto* tag = entity.try_get<tag_component>();
                             if(!item.name.empty() && tag != nullptr && tag->name != item.name)
                             {
                                 tag->name = item.name;
                                 prefab_override_context::mark_property_as_changed(entity,
                                                                                   entt::resolve<tag_component>(),
                                                                                   "name");
                             }
                             if(!item.parent_id.empty())
                             {
                                 auto parent = find_entity(*scn, item.parent_id);
                                 if(parent)
                                 {
                                     entity.get<transform_component>().set_parent(parent, true);
                                 }
                             }
                             apply_pose_direct(entity, item.pose, item.is_local);
                             created.push_back(entity);
                         }
                         return created;
                     });
                 return summarize_created(created, items.size());
             },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_create_meshes_batch",
         .description =
             "Create mesh entities from asset keys in one undoable action. Each item: asset_key "
             "(required), optional name/parent_id, position/rotation_euler/scale, space.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"asset_key":{"type":"string"},"name":{"type":"string"},"parent_id":{"type":"string"},"space":{"type":"string","enum":["world","local"]},"position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"rotation_euler":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"scale":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3}},"required":["asset_key"]}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 scene* scn = nullptr;
                 std::string error;
                 if(!require_edit_scene(ctx, scn, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 simdjson::dom::array items_arr;
                 if(!read_required_array(args, "items", items_arr, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 struct item_t
                 {
                     std::string asset_key;
                     std::string name;
                     std::string parent_id;
                     transform_snapshot pose{};
                     bool is_local{false};
                 };
                 std::vector<item_t> items;
                 for(auto el : items_arr)
                 {
                     simdjson::dom::object obj;
                     if(!read_object(el, obj, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     item_t item{};
                     if(!read_string(obj, "asset_key", item.asset_key) || item.asset_key.empty())
                     {
                         return {.text = "Item missing asset_key", .is_error = true};
                     }
                     read_string(obj, "name", item.name);
                     read_string(obj, "parent_id", item.parent_id);
                     item.is_local = item_space_is_local(obj);
                     read_transform_snapshot(obj, item.pose);
                     items.push_back(std::move(item));
                 }
                 if(items.empty())
                 {
                     return {.text = "items array is empty", .is_error = true};
                 }
                 std::vector<entt::handle> created;
                 auto& em = ctx.get_cached<editing_manager>();
                 em.do_action<create_entities_action_t>(
                     "MCP Batch Create Meshes",
                     [&]()
                     {
                         created.clear();
                         for(const auto& item : items)
                         {
                             const math::vec3 spawn =
                                 (!item.is_local && item.pose.has_position) ? item.pose.position : math::vec3{0, 0, 0};
                             auto entity = defaults::create_mesh_entity_at(ctx, *scn, item.asset_key, spawn);
                             if(!entity)
                             {
                                 continue;
                             }
                             if(!item.name.empty())
                             {
                                 if(auto* tag = entity.try_get<tag_component>())
                                 {
                                     tag->name = item.name;
                                 }
                             }
                             if(!item.parent_id.empty())
                             {
                                 auto parent = find_entity(*scn, item.parent_id);
                                 if(parent)
                                 {
                                     entity.get<transform_component>().set_parent(parent, true);
                                 }
                             }
                             apply_pose_direct(entity, item.pose, item.is_local);
                             created.push_back(entity);
                         }
                         return created;
                     });
                 return summarize_created(created, items.size());
             },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_set_parents_batch",
         .description =
             "Reparent many entities in one undoable action. Each item: entity_id, optional parent_id "
             "(omit/empty to detach). Keeps WORLD pose.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"parent_id":{"type":"string"}},"required":["entity_id"]}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 scene* scn = nullptr;
                 std::string error;
                 if(!require_edit_scene(ctx, scn, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 simdjson::dom::array items_arr;
                 if(!read_required_array(args, "items", items_arr, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 struct entry_t
                 {
                     entt::handle entity{};
                     entt::handle old_parent{};
                     entt::handle new_parent{};
                 };
                 std::vector<entry_t> entries;
                 for(auto el : items_arr)
                 {
                     simdjson::dom::object obj;
                     if(!read_object(el, obj, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     std::string entity_id;
                     if(!read_string(obj, "entity_id", entity_id))
                     {
                         return {.text = "Item missing entity_id", .is_error = true};
                     }
                     auto entity = find_entity(*scn, entity_id);
                     if(!entity || !entity.all_of<transform_component>())
                     {
                         return {.text = "Entity not found or missing transform: " + entity_id, .is_error = true};
                     }
                     entry_t e{};
                     e.entity = entity;
                     e.old_parent = entity.get<transform_component>().get_parent();
                     std::string parent_id;
                     if(read_string(obj, "parent_id", parent_id) && !parent_id.empty() && parent_id != "null")
                     {
                         e.new_parent = find_entity(*scn, parent_id);
                         if(!e.new_parent)
                         {
                             return {.text = "Parent not found: " + parent_id, .is_error = true};
                         }
                     }
                     entries.push_back(e);
                 }
                 if(entries.empty())
                 {
                     return {.text = "items array is empty", .is_error = true};
                 }
                 auto batch = std::make_shared<composite_action_t>();
                 for(const auto& e : entries)
                 {
                     batch->add_sub_action(
                         std::make_shared<transform_set_parent_action_t>(e.entity, e.old_parent, e.new_parent));
                 }
                 ctx.get_cached<editing_manager>().do_action("MCP Batch Set Parents", batch);
                 return {.text = fmt::format(R"({{"ok":true,"count":{}}})", entries.size()), .is_error = false};
             },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_set_names_batch",
         .description = "Set display names on many entities in one undoable action.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"name":{"type":"string"}},"required":["entity_id","name"]}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 scene* scn = nullptr;
                 std::string error;
                 if(!require_edit_scene(ctx, scn, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 simdjson::dom::array items_arr;
                 if(!read_required_array(args, "items", items_arr, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 struct entry_t
                 {
                     entt::handle entity{};
                     std::string old_name;
                     std::string new_name;
                 };
                 std::vector<entry_t> entries;
                 for(auto el : items_arr)
                 {
                     simdjson::dom::object obj;
                     if(!read_object(el, obj, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     std::string entity_id;
                     std::string name;
                     if(!read_string(obj, "entity_id", entity_id) || !read_string(obj, "name", name))
                     {
                         return {.text = "Item missing entity_id or name", .is_error = true};
                     }
                     auto entity = find_entity(*scn, entity_id);
                     if(!entity || !entity.all_of<tag_component>())
                     {
                         return {.text = "Entity not found: " + entity_id, .is_error = true};
                     }
                     entries.push_back({entity, entity.get<tag_component>().name, name});
                 }
                 if(entries.empty())
                 {
                     return {.text = "items array is empty", .is_error = true};
                 }
                 auto batch = std::make_shared<composite_action_t>();
                 for(const auto& e : entries)
                 {
                     batch->add_sub_action(std::make_shared<entity_set_name_action_t>(e.entity, e.old_name, e.new_name));
                 }
                 ctx.get_cached<editing_manager>().do_action("MCP Batch Set Names", batch);
                 return {.text = fmt::format(R"({{"ok":true,"count":{}}})", entries.size()), .is_error = false};
             },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_set_models_batch",
         .description =
             "Swap the mesh an existing entity shows, keeping the entity, its transform and its other "
             "components, in one undoable action. The new model is built exactly as for "
             "scene_create_meshes_batch: the mesh as LOD 0 with the mesh's own default materials. The "
             "entity needs a Model component. A skinned mesh is reported, since playing its clips needs "
             "an Animation component.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"asset_key":{"type":"string","description":"Mesh asset key"}},"required":["entity_id","asset_key"]}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 scene* scn = nullptr;
                 std::string error;
                 if(!require_edit_scene(ctx, scn, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 simdjson::dom::array items_arr;
                 if(!read_required_array(args, "items", items_arr, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 auto& am = ctx.get_cached<asset_manager>();
                 struct entry_t
                 {
                     entt::handle entity{};
                     model old_model;
                     model new_model;
                     /// Shared by the do and undo lambdas, which each hold a copy of the entries.
                     std::shared_ptr<prefab_override_record> overrides = std::make_shared<prefab_override_record>();
                 };
                 std::vector<entry_t> entries;
                 for(auto el : items_arr)
                 {
                     simdjson::dom::object obj;
                     std::string entity_id;
                     std::string asset_key;
                     if(!read_object(el, obj, error) || !read_string(obj, "entity_id", entity_id) ||
                        !read_string(obj, "asset_key", asset_key))
                     {
                         return {.text = "Each item needs entity_id and asset_key", .is_error = true};
                     }
                     auto entity = find_entity(*scn, entity_id);
                     if(!entity)
                     {
                         return {.text = "Entity not found: " + entity_id, .is_error = true};
                     }
                     auto* model_comp = entity.try_get<model_component>();
                     if(model_comp == nullptr)
                     {
                         return {.text = entity_id + " has no Model component (scene_add_components_batch Model, "
                                                     "or create it with scene_create_meshes_batch)",
                                 .is_error = true};
                     }
                     if(!am.get_asset<mesh>(asset_key))
                     {
                         return {.text = "Mesh not found: " + asset_key + " (assets_find_batch type mesh)",
                                 .is_error = true};
                     }
                     entries.push_back(
                         {entity, model_comp->get_model(), defaults::make_model_from_mesh(ctx, asset_key)});
                 }
                 if(entries.empty())
                 {
                     return {.text = "items array is empty", .is_error = true};
                 }
                 ctx.get_cached<editing_manager>().do_action(
                     "MCP Batch Set Models",
                     [entries]()
                     {
                         const auto type = entt::resolve<model_component>();
                         for(const auto& e : entries)
                         {
                             if(auto* comp = e.entity ? e.entity.try_get<model_component>() : nullptr)
                             {
                                 const auto state = prefab_override_context::save_component_state(e.entity, type);
                                 comp->set_model(e.new_model);
                                 *e.overrides = prefab_override_context::mark_changed_properties(e.entity, type, state);
                             }
                         }
                     },
                     [entries]()
                     {
                         for(auto it = entries.rbegin(); it != entries.rend(); ++it)
                         {
                             if(auto* comp = it->entity ? it->entity.try_get<model_component>() : nullptr)
                             {
                                 comp->set_model(it->old_model);
                                 prefab_override_context::restore_overrides(*it->overrides);
                             }
                         }
                     });

                 std::vector<std::string> skinned_without_animation;
                 for(const auto& e : entries)
                 {
                     const auto* comp = e.entity.try_get<model_component>();
                     if(comp != nullptr && comp->is_skinned() && !e.entity.all_of<animation_component>())
                     {
                         skinned_without_animation.push_back(entity_id_string(e.entity));
                     }
                 }
                 return {.text = fmt::format(R"({{"ok":true,"count":{},"skinned_without_animation":{}}})",
                                             entries.size(),
                                             strings_to_json_array(skinned_without_animation)),
                         .is_error = false};
             },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_set_layers_batch",
         .description =
             "Set which layers entities are on, in one undoable action - the hierarchy's layer picker "
             "does the same, and records it as a prefab override on prefab instances. layers is an array "
             "of layer names (the project's layer.layers setting, see settings_get) or indices, or "
             "\"everything\" / \"nothing\". Physics queries and rendering filter on these; "
             "scene_get_entities_batch detail summary reports them.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"layers":{"description":"Layer names or indices, or \"everything\" / \"nothing\""}},"required":["entity_id","layers"]}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 scene* scn = nullptr;
                 std::string error;
                 if(!require_edit_scene(ctx, scn, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 simdjson::dom::array items_arr;
                 if(!read_required_array(args, "items", items_arr, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 auto batch = std::make_shared<composite_action_t>();
                 for(auto el : items_arr)
                 {
                     simdjson::dom::object obj;
                     std::string entity_id;
                     simdjson::dom::element layers_el;
                     if(!read_object(el, obj, error) || !read_string(obj, "entity_id", entity_id) ||
                        obj["layers"].get(layers_el))
                     {
                         return {.text = "Each item needs entity_id and layers", .is_error = true};
                     }
                     auto entity = find_entity(*scn, entity_id);
                     auto* current = entity ? entity.try_get<layer_component>() : nullptr;
                     if(current == nullptr)
                     {
                         return {.text = "Entity not found: " + entity_id, .is_error = true};
                     }
                     layer_mask mask{};
                     if(!parse_layer_mask(ctx, layers_el, mask, error))
                     {
                         return {.text = entity_id + ": " + error, .is_error = true};
                     }
                     batch->add_sub_action(
                         std::make_shared<entity_set_layers_action_t>(entity, current->layers.mask, mask.mask));
                 }
                 if(batch->sub_actions.empty())
                 {
                     return {.text = "items array is empty", .is_error = true};
                 }
                 const auto count = batch->sub_actions.size();
                 ctx.get_cached<editing_manager>().do_action("MCP Batch Set Layers", batch);
                 return {.text = fmt::format(R"({{"ok":true,"count":{}}})", count), .is_error = false};
             },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_set_active_batch",
         .description = "Set active flags on many entities in one undoable action.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"active":{"type":"boolean"}},"required":["entity_id","active"]}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 scene* scn = nullptr;
                 std::string error;
                 if(!require_edit_scene(ctx, scn, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 simdjson::dom::array items_arr;
                 if(!read_required_array(args, "items", items_arr, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 struct entry_t
                 {
                     entt::handle entity{};
                     bool old_active{true};
                     bool new_active{true};
                 };
                 std::vector<entry_t> entries;
                 for(auto el : items_arr)
                 {
                     simdjson::dom::object obj;
                     if(!read_object(el, obj, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     std::string entity_id;
                     bool active = true;
                     if(!read_string(obj, "entity_id", entity_id) || !read_bool(obj, "active", active))
                     {
                         return {.text = "Item missing entity_id or active", .is_error = true};
                     }
                     auto entity = find_entity(*scn, entity_id);
                     if(!entity || !entity.all_of<transform_component>())
                     {
                         return {.text = "Entity not found: " + entity_id, .is_error = true};
                     }
                     entries.push_back({entity, entity.get<transform_component>().is_active(), active});
                 }
                 if(entries.empty())
                 {
                     return {.text = "items array is empty", .is_error = true};
                 }
                 auto batch = std::make_shared<composite_action_t>();
                 for(const auto& e : entries)
                 {
                     batch->add_sub_action(
                         std::make_shared<entity_set_active_action_t>(e.entity, e.old_active, e.new_active));
                 }
                 ctx.get_cached<editing_manager>().do_action("MCP Batch Set Active", batch);
                 return {.text = fmt::format(R"({{"ok":true,"count":{}}})", entries.size()), .is_error = false};
             },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_add_components_batch",
         .description =
             "Add engine components to many entities in one undoable action. Use "
             "scene_list_component_types for valid names.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"component_type":{"type":"string"}},"required":["entity_id","component_type"]}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 scene* scn = nullptr;
                 std::string error;
                 if(!require_edit_scene(ctx, scn, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 simdjson::dom::array items_arr;
                 if(!read_required_array(args, "items", items_arr, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 struct entry_t
                 {
                     entt::handle entity{};
                     entt::meta_type type{};
                 };
                 std::vector<entry_t> entries;
                 for(auto el : items_arr)
                 {
                     simdjson::dom::object obj;
                     if(!read_object(el, obj, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     std::string entity_id;
                     std::string component_type;
                     if(!read_string(obj, "entity_id", entity_id) || !read_string(obj, "component_type", component_type))
                     {
                         return {.text = "Item missing entity_id or component_type", .is_error = true};
                     }
                     auto entity = find_entity(*scn, entity_id);
                     if(!entity)
                     {
                         return {.text = "Entity not found: " + entity_id, .is_error = true};
                     }
                     auto type = resolve_addable_component(component_type);
                     if(!type)
                     {
                         return {.text = "Unknown or non-addable component_type: " + component_type, .is_error = true};
                     }
                     entries.push_back({entity, type});
                 }
                 if(entries.empty())
                 {
                     return {.text = "items array is empty", .is_error = true};
                 }
                 auto& em = ctx.get_cached<editing_manager>();
                 for(const auto& e : entries)
                 {
                     em.do_action<entity_add_component_action_t>("MCP Add Component", e.entity, e.type);
                 }
                 return {.text = fmt::format(R"({{"ok":true,"count":{}}})", entries.size()), .is_error = false};
             },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_remove_components_batch",
         .description = "Remove components from many entities in one undoable action.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"component_type":{"type":"string"}},"required":["entity_id","component_type"]}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 scene* scn = nullptr;
                 std::string error;
                 if(!require_edit_scene(ctx, scn, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 simdjson::dom::array items_arr;
                 if(!read_required_array(args, "items", items_arr, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 struct entry_t
                 {
                     entt::handle entity{};
                     entt::meta_type type{};
                 };
                 std::vector<entry_t> entries;
                 for(auto el : items_arr)
                 {
                     simdjson::dom::object obj;
                     if(!read_object(el, obj, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     std::string entity_id;
                     std::string component_type;
                     if(!read_string(obj, "entity_id", entity_id) || !read_string(obj, "component_type", component_type))
                     {
                         return {.text = "Item missing entity_id or component_type", .is_error = true};
                     }
                     auto entity = find_entity(*scn, entity_id);
                     if(!entity)
                     {
                         return {.text = "Entity not found: " + entity_id, .is_error = true};
                     }
                     auto type = resolve_addable_component(component_type);
                     if(!type)
                     {
                         type = entt::resolve(entt::hashed_string{component_type.c_str()});
                     }
                     if(!type)
                     {
                         return {.text = "Unknown component_type: " + component_type, .is_error = true};
                     }
                     entries.push_back({entity, type});
                 }
                 if(entries.empty())
                 {
                     return {.text = "items array is empty", .is_error = true};
                 }
                 auto& em = ctx.get_cached<editing_manager>();
                 for(const auto& e : entries)
                 {
                     em.do_action<entity_remove_component_action_t>("MCP Remove Component", e.entity, e.type);
                 }
                 return {.text = fmt::format(R"({{"ok":true,"count":{}}})", entries.size()), .is_error = false};
             },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_add_scripts_batch",
         .description =
             "Add C# ScriptComponent types to many entities. type_name from scripts_list_types.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"type_name":{"type":"string"}},"required":["entity_id","type_name"]}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 scene* scn = nullptr;
                 std::string error;
                 if(!require_edit_scene(ctx, scn, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 if(!ctx.has<script_system>())
                 {
                     return {.text = "Script system unavailable", .is_error = true};
                 }
                 simdjson::dom::array items_arr;
                 if(!read_required_array(args, "items", items_arr, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 auto& script_sys = ctx.get_cached<script_system>();
                 auto& em = ctx.get_cached<editing_manager>();
                 size_t count = 0;
                 for(auto el : items_arr)
                 {
                     simdjson::dom::object obj;
                     if(!read_object(el, obj, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     std::string entity_id;
                     std::string type_name;
                     if(!read_string(obj, "entity_id", entity_id) || !read_string(obj, "type_name", type_name))
                     {
                         return {.text = "Item missing entity_id or type_name", .is_error = true};
                     }
                     auto entity = find_entity(*scn, entity_id);
                     if(!entity)
                     {
                         return {.text = "Entity not found: " + entity_id, .is_error = true};
                     }
                     if(!script_sys.get_type_by_fullname(type_name).valid())
                     {
                         return {.text = "Unknown script type: " + type_name, .is_error = true};
                     }
                     em.do_action<entity_add_script_component_action_t>("MCP Add Script", entity, type_name);
                     ++count;
                 }
                 return {.text = fmt::format(R"({{"ok":true,"count":{}}})", count), .is_error = count == 0};
             },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_remove_scripts_batch",
         .description =
             "Remove C# ScriptComponent scripts from many entities by type_name; script_index (from 0, "
             "as scene_get_entities_batch summary lists scripts) picks one of several of a type. On a "
             "prefab instance the removal is recorded as an override, as the inspector's is.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"type_name":{"type":"string"},"script_index":{"type":"integer","minimum":0}},"required":["entity_id","type_name"]}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 scene* scn = nullptr;
                 std::string error;
                 if(!require_edit_scene(ctx, scn, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 simdjson::dom::array items_arr;
                 if(!read_required_array(args, "items", items_arr, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 auto& em = ctx.get_cached<editing_manager>();
                 size_t count = 0;
                 for(auto el : items_arr)
                 {
                     simdjson::dom::object obj;
                     if(!read_object(el, obj, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     std::string entity_id;
                     std::string type_name;
                     if(!read_string(obj, "entity_id", entity_id) || !read_string(obj, "type_name", type_name))
                     {
                         return {.text = "Item missing entity_id or type_name", .is_error = true};
                     }
                     auto entity = find_entity(*scn, entity_id);
                     if(!entity)
                     {
                         return {.text = "Entity not found: " + entity_id, .is_error = true};
                     }
                     script_selector script{type_name};
                     if(!read_script_selector(obj, script, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     const auto position = find_script_position(entity, script);
                     if(!position)
                     {
                         return {.text = fmt::format("{} has no {} script at script_index {}", entity_id, type_name, script.index),
                                 .is_error = true};
                     }
                     em.do_action<entity_remove_script_component_action_t>("MCP Remove Script",
                                                                          entity,
                                                                          position->type_fullname,
                                                                          static_cast<int>(position->index));
                     ++count;
                 }
                 return {.text = fmt::format(R"({{"ok":true,"count":{}}})", count), .is_error = count == 0};
             },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_get_transforms_batch",
         .description =
             "Get transforms for many entities. Each item: entity_id, optional space world|local "
             "(omit space for both world and local fields).",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"space":{"type":"string","enum":["world","local"]}},"required":["entity_id"]}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 auto& em = ctx.get_cached<editing_manager>();
                 auto* scn = em.get_active_scene(ctx);
                 if(!scn || !scn->registry)
                 {
                     return {.text = "No active scene", .is_error = true};
                 }
                 simdjson::dom::array items_arr;
                 std::string error;
                 if(!read_required_array(args, "items", items_arr, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 std::string json = "[";
                 size_t count = 0;
                 for(auto el : items_arr)
                 {
                     simdjson::dom::object obj;
                     if(!read_object(el, obj, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     std::string entity_id;
                     if(!read_string(obj, "entity_id", entity_id))
                     {
                         return {.text = "Item missing entity_id", .is_error = true};
                     }
                     auto entity = find_entity(*scn, entity_id);
                     if(!entity || !entity.all_of<transform_component>())
                     {
                         return {.text = "Entity not found or missing transform: " + entity_id, .is_error = true};
                     }
                     if(count > 0)
                     {
                         json += ",";
                     }
                     std::string space;
                     read_string(obj, "space", space);
                     if(space.empty())
                     {
                         json += entity_to_pose_json(entity);
                     }
                     else
                     {
                         bool is_local = false;
                         if(!read_transform_space(obj, is_local, error))
                         {
                             return {.text = error, .is_error = true};
                         }
                         auto& t = entity.get<transform_component>();
                         const auto pos = is_local ? t.get_position_local() : t.get_position_global();
                         const auto rot = is_local ? t.get_rotation_euler_local() : t.get_rotation_euler_global();
                         const auto scl = is_local ? t.get_scale_local() : t.get_scale_global();
                         json += fmt::format(
                             R"({{"id":{},"space":{},"position":[{:.6g},{:.6g},{:.6g}],"rotation_euler":[{:.6g},{:.6g},{:.6g}],"scale":[{:.6g},{:.6g},{:.6g}]}})",
                             make_json_string(entity_id_string(entity)),
                             make_json_string(is_local ? "local" : "world"),
                             pos.x,
                             pos.y,
                             pos.z,
                             rot.x,
                             rot.y,
                             rot.z,
                             scl.x,
                             scl.y,
                             scl.z);
                     }
                     ++count;
                 }
                 json += "]";
                 return {.text = fmt::format(R"({{"transforms":{},"count":{}}})", json, count), .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "scene_list_scripts_batch",
         .description = "List ScriptComponent instances on many entities (entity_ids required).",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_ids":{"type":"array","items":{"type":"string"}}},"required":["entity_ids"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 scene* scn = nullptr;
                 std::string error;
                 if(!require_active_scene(ctx, scn, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 std::vector<entt::handle> entities;
                 if(!read_entity_ids(args, *scn, entities, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 std::string json = "[";
                 size_t count = 0;
                 for(auto entity : entities)
                 {
                     if(count > 0)
                     {
                         json += ",";
                     }
                     std::string scripts = "[";
                     bool first_script = true;
                     if(auto* sc = entity.try_get<script_component>())
                     {
                         std::map<std::string, size_t> type_counts;
                         for(const auto& obj : sc->get_script_components())
                         {
                             if(!obj.pinned)
                             {
                                 continue;
                             }
                             if(!first_script)
                             {
                                 scripts += ",";
                             }
                             first_script = false;
                             const auto type_name = obj.pinned->get_object().get_type().get_fullname();
                             const auto source = sc->get_script_source_location(obj);
                             scripts += fmt::format(R"({{"type":{},"script_index":{},"source_path":{}}})",
                                                    make_json_string(type_name),
                                                    type_counts[type_name]++,
                                                    make_json_string(source));
                         }
                     }
                     scripts += "]";
                     json += fmt::format(R"({{"entity_id":{},"scripts":{}}})",
                                         make_json_string(entity_id_string(entity)),
                                         scripts);
                     ++count;
                 }
                 json += "]";
                 return {.text = fmt::format(R"({{"entities":{},"count":{}}})", json, count), .is_error = false};
             },
         .mutates_scene = false});
}

} // namespace unravel::mcp
