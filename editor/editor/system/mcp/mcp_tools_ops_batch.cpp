#include "mcp_component_utils.h"
#include "mcp_meta_properties.h"
#include "mcp_tools_common.h"
#include "mcp_asset_lookup.h"
#include "mcp_project_paths.h"

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
#include <math/math.h>

#include <map>
#include <set>
#include <tuple>

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

/// True when `node` is `root` or lies under it - a parent that would make a cycle.
auto is_in_subtree(entt::handle root, entt::handle node) -> bool
{
    for(auto current = node; current; current = current.get<transform_component>().get_parent())
    {
        if(current == root)
        {
            return true;
        }
    }
    return false;
}

/// Whether a part item adds or removes.
enum class part_change : uint8_t
{
    add,
    remove,
};

/// One component or script an add or remove item names, checked against the entity.
struct entity_part
{
    entt::handle entity;
    /// The component type; empty for a script.
    entt::meta_type component;
    /// A script's C# type full name.
    std::string script_type;
    /// Remove only: where the script sits in the entity's list (0 for components).
    size_t script_position{};
};

/// Reads a script part: the type must exist to add, the script must be on the entity to remove.
auto read_script_part(rtti::context& ctx,
                      const simdjson::dom::object& obj,
                      part_change change,
                      entity_part& part,
                      std::string& error) -> bool
{
    script_selector script;
    if(!read_script_selector(obj, script, error))
    {
        return false;
    }
    if(script.type.empty())
    {
        error = "component Script needs type_name";
        return false;
    }
    if(change == part_change::add)
    {
        return resolve_script_type_name(ctx, script.type, part.script_type, error);
    }
    const auto position = find_script_position(part.entity, script);
    if(!position)
    {
        error = fmt::format("{} has no {} script at script_index {}", entity_id_string(part.entity), script.type, script.index);
        return false;
    }
    part.script_type = position->type_fullname;
    part.script_position = position->index;
    return true;
}

/// Reads a component part: an addable component, absent to add and present to remove.
auto read_component_part(const std::string& name, part_change change, entity_part& part, std::string& error) -> bool
{
    part.component = resolve_addable_component(name);
    if(!part.component)
    {
        error = "Unknown component " + name + " (scene_list_components lists the ones you can add and remove)";
        return false;
    }
    const bool present = part.component.invoke("component_exists"_hs, {}, part.entity).cast<bool>();
    if(change == part_change::add && present)
    {
        error = entity_id_string(part.entity) + " already has " + name;
        return false;
    }
    if(change == part_change::remove && !present)
    {
        error = entity_id_string(part.entity) + " has no " + name;
        return false;
    }
    return true;
}

/// Reads every item of an add or remove call; false on the first item that would not apply, or
/// one that repeats another.
auto read_part_items(rtti::context& ctx,
                     const simdjson::dom::object& args,
                     part_change change,
                     std::vector<entity_part>& parts,
                     std::string& error) -> bool
{
    scene* scn = nullptr;
    simdjson::dom::array items;
    if(!require_edit_scene(ctx, scn, error) || !read_required_array(args, "items", items, error))
    {
        return false;
    }
    std::set<std::tuple<entt::entity, entt::id_type, std::string, size_t>> seen;
    for(auto element : items)
    {
        simdjson::dom::object obj;
        std::string entity_id;
        std::string component;
        if(!read_object(element, obj, error) || !read_string(obj, "entity_id", entity_id) ||
           !read_string(obj, "component", component))
        {
            error = fmt::format("Item {} needs entity_id and component", parts.size());
            return false;
        }
        entity_part part;
        part.entity = find_entity(*scn, entity_id);
        if(!part.entity)
        {
            error = "Entity not found: " + entity_id;
            return false;
        }
        const bool read = component == "Script" ? read_script_part(ctx, obj, change, part, error)
                                                : read_component_part(component, change, part, error);
        if(!read)
        {
            return false;
        }
        const auto key = std::make_tuple(part.entity.entity(),
                                         part.component ? part.component.id() : entt::id_type{},
                                         part.script_type,
                                         part.script_position);
        // Two adds of one script type are two scripts; every other repeat names one part twice.
        if(!seen.insert(key).second && (change == part_change::remove || part.component))
        {
            error = "Item " + std::to_string(parts.size()) + " repeats an earlier item";
            return false;
        }
        parts.push_back(std::move(part));
    }
    if(parts.empty())
    {
        error = "items array is empty";
        return false;
    }
    return true;
}

} // namespace

void register_ops_batch_tools(mcp_tool_registry& registry)
{
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
                         if(is_in_subtree(entity, e.new_parent))
                         {
                             return {.text = parent_id + " is " + entity_id + " or one of its children; nothing was "
                                                         "reparented",
                                     .is_error = true};
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
         .mutates_scene = true,
         .idempotent = true});

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
         .mutates_scene = true,
         .idempotent = true});

    registry.add(
        {.name = "scene_set_models_batch",
         .description =
             "Swap the mesh an entity's Model shows, keeping the entity, its transform and other components, in "
             "one undo step. The model is built as scene_create_batch kind mesh builds it: the mesh as LOD 0 with "
             "its own default materials. A skinned mesh is reported, since playing its clips needs an Animation "
             "component.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"asset":{"type":"string","description":"Mesh asset key or uid"}},"required":["entity_id","asset"]}}},"required":["items"]})json",
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
                        !read_string(obj, "asset", asset_key))
                     {
                         return {.text = "Each item needs entity_id and asset", .is_error = true};
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
                                                     "or create it with scene_create_batch kind mesh)",
                                 .is_error = true};
                     }
                     const auto mesh_asset = find_existing_asset<mesh>(ctx, to_asset_key(asset_key), error);
                     if(!mesh_asset)
                     {
                         return {.text = error + " (assets_find_batch type mesh)", .is_error = true};
                     }
                     entries.push_back(
                         {entity, model_comp->get_model(), defaults::make_model_from_mesh(ctx, mesh_asset.id())});
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
         .mutates_scene = true,
         .idempotent = true});

    registry.add(
        {.name = "scene_set_layers_batch",
         .description =
             "Set which layers entities are on, in one undo step (a prefab override on instances). layers: names "
             "(the project's layer.layers setting) or indices, or \"everything\" / \"nothing\". Physics queries "
             "and rendering filter on them; scene_get_entities_batch detail summary reports them.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"layers":{"anyOf":[{"type":"array","items":{"anyOf":[{"type":"string"},{"type":"integer","minimum":0,"maximum":31}]}},{"type":"string","enum":["everything","nothing"]}],"description":"Layer names or indices, or \"everything\" / \"nothing\""}},"required":["entity_id","layers"]}}},"required":["items"]})json",
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
         .mutates_scene = true,
         .idempotent = true});

    registry.add(
        {.name = "scene_set_active_batch",
         .description =
             "Activate or deactivate entities in one undo step; the children of an inactive entity are inactive "
             "too.",
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
         .mutates_scene = true,
         .idempotent = true});

    registry.add(
        {.name = "scene_add_components_batch",
         .description =
             "Add components to entities in one undo step. Each item: entity_id, component (a name from "
             "scene_list_components, or Script with type_name from scripts_list_types). Every item is checked "
             "first: an unknown name, a component the entity already has or a duplicate item adds nothing. "
             "Configure them with scene_set_component_properties_batch.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"component":{"type":"string"},"type_name":{"type":"string","description":"component Script: C# type"}},"required":["entity_id","component"]}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::vector<entity_part> parts;
             std::string error;
             if(!read_part_items(ctx, args, part_change::add, parts, error))
             {
                 return {.text = error + "; nothing was added", .is_error = true};
             }
             auto batch = std::make_shared<composite_action_t>();
             for(const auto& part : parts)
             {
                 if(part.component)
                 {
                     batch->add_sub_action(std::make_shared<entity_add_component_action_t>(part.entity, part.component));
                 }
                 else
                 {
                     batch->add_sub_action(
                         std::make_shared<entity_add_script_component_action_t>(part.entity, part.script_type));
                 }
             }
             ctx.get_cached<editing_manager>().do_action("MCP Add Components", batch);
             return {.text = fmt::format(R"({{"ok":true,"count":{}}})", parts.size()), .is_error = false};
         },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_remove_components_batch",
         .description =
             "Remove components from entities in one undo step. Each item: entity_id, component (a name from "
             "scene_list_components, or Script with type_name and script_index). Transform, Id, Tag, Layer and "
             "Prefab cannot be removed. Every item is checked first. On a prefab instance each removal is "
             "recorded as an override.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"component":{"type":"string"},"type_name":{"type":"string","description":"component Script: C# type"},"script_index":{"type":"integer","minimum":0,"description":"component Script: which of the entity's scripts of that type, from 0"}},"required":["entity_id","component"]}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::vector<entity_part> parts;
             std::string error;
             if(!read_part_items(ctx, args, part_change::remove, parts, error))
             {
                 return {.text = error + "; nothing was removed", .is_error = true};
             }
             // A script removal shifts the list positions after it: remove from the back.
             std::stable_sort(parts.begin(),
                              parts.end(),
                              [](const entity_part& lhs, const entity_part& rhs)
                              {
                                  return lhs.script_position > rhs.script_position;
                              });
             auto batch = std::make_shared<composite_action_t>();
             for(const auto& part : parts)
             {
                 if(part.component)
                 {
                     batch->add_sub_action(
                         std::make_shared<entity_remove_component_action_t>(part.entity, part.component));
                 }
                 else
                 {
                     batch->add_sub_action(std::make_shared<entity_remove_script_component_action_t>(
                         part.entity,
                         part.script_type,
                         static_cast<int>(part.script_position)));
                 }
             }
             ctx.get_cached<editing_manager>().do_action("MCP Remove Components", batch);
             return {.text = fmt::format(R"({{"ok":true,"count":{}}})", parts.size()), .is_error = false};
         },
         .mutates_scene = true});

}

} // namespace unravel::mcp
