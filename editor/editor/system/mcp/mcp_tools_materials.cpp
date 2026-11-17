#include "mcp_material_utils.h"
#include "mcp_tools_common.h"

#include <editor/editing/editor_actions.h>
#include <editor/editing/editing_manager.h>
#include <editor/editing/actions/composite_action.h>
#include <engine/defaults/defaults.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/post_process_material_component.h>
#include <engine/rendering/material.h>
#include <engine/rendering/material_graph/graph_material.h>
#include <engine/rendering/model.h>

#include <algorithm>
#include <functional>
#include <utility>

namespace unravel::mcp
{
namespace
{

/// The do and undo of replacing an entity's model with one whose material instance in `slot` changed: each
/// puts one of the two in place and records the instance change as the inspector does. The entity is held by
/// its uuid, so the step still finds it after it was deleted and restored.
auto make_model_swap(entt::handle entity, const model& before, const model& after, uint32_t slot)
    -> std::pair<std::function<void()>, std::function<void()>>
{
    const auto handle = entt::make_uhandle(entity);
    const auto put = [handle, slot](const model& value) -> std::function<void()>
    {
        return [handle, slot, value]()
        {
            const auto target = handle.resolve();
            if(auto* model_comp = target ? target.try_get<model_component>() : nullptr)
            {
                model_comp->set_model(value);
                prefab_override_context::mark_material_instance_as_changed(target, slot);
            }
        };
    };
    return {put(after), put(before)};
}

/// The slot an item names (default 0), when the model has it; nullopt with `error` past its slots.
auto read_slot_index(const simdjson::dom::object& item, const model& mdl, std::string& error) -> hpp::optional<uint32_t>
{
    const int64_t index = read_int64_or(item, "material_index", 0);
    const auto slot_count = std::max<size_t>(1, mdl.get_shared_materials().size());
    if(index < 0 || static_cast<uint64_t>(index) >= slot_count)
    {
        error = fmt::format("material_index {} is outside the model's material slots 0..{}", index, slot_count - 1);
        return std::nullopt;
    }
    return static_cast<uint32_t>(index);
}

auto item_error_json(const std::string& entity_id, const std::string& error) -> std::string
{
    return fmt::format(R"({{"ok":false,"entity_id":{},"error":{}}})", make_json_string(entity_id), make_json_string(error));
}

/// The entity a post-process material call names, or an empty handle for the scene's global volume. False, with the
/// reason, for an id that names nothing.
auto read_post_process_target(scene& scn, const simdjson::dom::object& args, entt::handle& target, std::string& error) -> bool
{
    std::string entity_id;
    if(!read_string(args, "entity_id", entity_id) || entity_id.empty())
    {
        target = {};
        return true;
    }
    target = find_entity(scn, entity_id);
    if(!target)
    {
        error = "Entity not found: " + entity_id;
        return false;
    }
    return true;
}

/// One item of scene_set_post_process_materials_batch, read and checked before any is applied.
struct post_process_material_item
{
    entt::handle target;
    asset_handle<material> material;
    post_process_material_change change;
    component_apply_result applied;
};

/// Reads one item: its target, its Post Process material, and the change of the entry, with its parameter and texture
/// overrides made on a copy of the entry's (or a new entry's) so a refused value changes nothing. False, with the
/// reason, for an item set_post_process_material would refuse.
auto read_post_process_material_item(rtti::context& ctx,
                                     scene& scn,
                                     const simdjson::dom::object& item,
                                     post_process_material_item& out,
                                     std::string& error) -> bool
{
    if(!read_post_process_target(scn, item, out.target, error))
    {
        return false;
    }
    std::string key;
    read_string(item, "material", key);
    out.material = resolve_domain_material(ctx, key, material_domain::post_process, error);
    if(!out.material)
    {
        return false;
    }
    error = editor_actions::find_post_process_material_problem(ctx, out.target, out.material);
    if(!error.empty())
    {
        return false;
    }
    double weight = 0.0;
    if(read_double(item, "weight", weight))
    {
        out.change.weight = float(weight);
    }
    bool enabled = true;
    if(item["enabled"].get(enabled) == simdjson::SUCCESS)
    {
        out.change.enabled = enabled;
    }
    simdjson::dom::object parameters;
    simdjson::dom::object textures;
    const bool has_parameters = item["parameters"].get(parameters) == simdjson::SUCCESS;
    const bool has_textures = item["textures"].get(textures) == simdjson::SUCCESS;
    if(!has_parameters && !has_textures)
    {
        return true;
    }
    material_instance overrides;
    overrides.set_shared_material(out.material);
    const entt::handle holder = out.target ? out.target : defaults::find_global_volume(scn);
    const auto* materials = holder ? holder.try_get<post_process_material_component>() : nullptr;
    if(const auto* entry = materials != nullptr ? materials->find(out.material) : nullptr)
    {
        overrides = entry->get_material_instance();
    }
    apply_material_instance_graph_values(ctx,
                                         overrides,
                                         has_parameters ? &parameters : nullptr,
                                         has_textures ? &textures : nullptr,
                                         out.applied);
    if(!out.applied.ok)
    {
        error = fmt::format("Its parameters or textures were refused: {}", apply_result_to_json(out.applied));
        return false;
    }
    out.change.overrides = std::move(overrides);
    return true;
}

/// The entity a post-process item's entry lives on before the call: its target, or the scene's global volume (empty
/// when the scene has none yet). Two items with the same holder and material name one entry.
auto get_post_process_holder(scene& scn, entt::handle target) -> entt::handle
{
    return target ? target : defaults::find_global_volume(scn);
}

} // namespace

void register_material_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "scene_set_model_material_instances_batch",
         .description =
             "Override properties of a model slot's shared material for that entity only (its material instance): "
             "each item's properties (assets_list_properties type material) become overrides, every other value "
             "keeps following the shared material, and the .mat file and other renderers are untouched. "
             "shares_draw in the result is true while every override is a per-instance property (colors and "
             "numbers), so the entity still draws together with the shared material's other renderers. A material "
             "graph's parameters and texture nodes go by name: parameters {name: number or [x, y, z, w]}, textures "
             "{node id: texture key, \"\" for the default texture}; null reverts one. A parameter in the graph's "
             "first four registers shares the draw; a texture, a later parameter, or any override of a graph whose World "
             "Position Offset moves its vertices does not. "
             "Each item: entity_id, properties and/or parameters and/or textures, optional material_index (slot, "
             "default 0). All or nothing per item; one undo step.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","minItems":1,"items":{"type":"object","properties":{"entity_id":{"type":"string"},"material_index":{"type":"integer","minimum":0},"properties":{"type":"object"},"parameters":{"type":"object"},"textures":{"type":"object"}},"required":["entity_id"]}}},"required":["items"]})json",
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
                 std::string entity_id;
                 std::string item_json;
                 std::string props_json;
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
                 if(!read_string(obj, "entity_id", item.entity_id) || item.entity_id.empty())
                 {
                     return {.text = "Item missing entity_id", .is_error = true};
                 }
                 if(obj["properties"].error() && obj["parameters"].error() && obj["textures"].error())
                 {
                     return {.text = "Item has none of properties, parameters and textures", .is_error = true};
                 }
                 item.item_json = std::string(simdjson::minify(el));
                 item.props_json = obj["properties"].error() ? std::string("{}") : std::string(simdjson::minify(obj["properties"]));
                 items.push_back(std::move(item));
             }
             if(items.empty())
             {
                 return {.text = "items array is empty", .is_error = true};
             }
             std::string results = "[";
             bool first = true;
             size_t ok_count = 0;
             auto& em = ctx.get_cached<editing_manager>();
             for(const auto& item : items)
             {
                 if(!first)
                 {
                     results += ",";
                 }
                 first = false;
                 auto entity = find_entity(*scn, item.entity_id);
                 if(!entity || !entity.all_of<model_component>())
                 {
                     results += fmt::format(R"({{"ok":false,"entity_id":{},"error":{}}})",
                                            make_json_string(item.entity_id),
                                            make_json_string(entity ? "Entity has no model_component"
                                                                    : "Entity not found"));
                     continue;
                 }
                 simdjson::dom::parser parser;
                 simdjson::dom::element root;
                 if(parser.parse(item.props_json).get(root))
                 {
                     results += fmt::format(R"({{"ok":false,"entity_id":{},"error":"Failed to parse properties"}})",
                                            make_json_string(item.entity_id));
                     continue;
                 }
                 simdjson::dom::object properties;
                 if(root.get(properties))
                 {
                     results += fmt::format(R"({{"ok":false,"entity_id":{},"error":"properties must be an object"}})",
                                            make_json_string(item.entity_id));
                     continue;
                 }
                 const auto old_model = entity.get<model_component>().get_model();
                 simdjson::dom::parser item_parser;
                 simdjson::dom::object item_object;
                 if(item_parser.parse(item.item_json).get(item_object))
                 {
                     results += item_error_json(item.entity_id, "Failed to read the item");
                     continue;
                 }
                 const auto index = read_slot_index(item_object, old_model, error);
                 if(!index)
                 {
                     results += item_error_json(item.entity_id, error);
                     continue;
                 }
                 // The instance is edited on a copy of the model: the swap below is one undo step, and a rejected
                 // edit changes nothing.
                 auto new_model = old_model;
                 auto& instance = new_model.get_material_instance(*index);
                 auto applied = apply_material_instance_properties(ctx, instance, properties);
                 if(applied.ok)
                 {
                     simdjson::dom::object parameters;
                     simdjson::dom::object textures;
                     const bool has_parameters = item_object["parameters"].get(parameters) == simdjson::SUCCESS;
                     const bool has_textures = item_object["textures"].get(textures) == simdjson::SUCCESS;
                     apply_material_instance_graph_values(ctx,
                                                          instance,
                                                          has_parameters ? &parameters : nullptr,
                                                          has_textures ? &textures : nullptr,
                                                          applied);
                 }
                 if(!applied.ok)
                 {
                     results += fmt::format(R"({{"ok":false,"entity_id":{},"result":{}}})",
                                            make_json_string(item.entity_id),
                                            apply_result_to_json(applied));
                     continue;
                 }
                 const bool shares_draw = instance.shares_draw();
                 const auto swap = make_model_swap(entity, old_model, new_model, *index);
                 em.do_action("MCP Set Model Material Instance", swap.first, swap.second);
                 ++ok_count;
                 results += fmt::format(R"({{"ok":true,"entity_id":{},"material_index":{},"shares_draw":{},"result":{}}})",
                                        make_json_string(entity_id_string(entity)),
                                        *index,
                                        shares_draw ? "true" : "false",
                                        apply_result_to_json(applied));
             }
             results += "]";
             return {.text = fmt::format(R"({{"results":{},"count":{},"requested":{}}})", results, ok_count, items.size()),
                     .is_error = ok_count == 0};
         },
         .mutates_scene = true,
         .former_arguments = {{"items[].index", "material_index"}}});

    registry.add(
        {.name = "scene_clear_model_material_instances_batch",
         .description =
             "Revert every override of model slots' material instances (batch), so the slots draw exactly like "
             "their shared materials again. Each item: entity_id, optional material_index (slot, default 0). cleared is false "
             "for a slot that had no overrides.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","minItems":1,"items":{"type":"object","properties":{"entity_id":{"type":"string"},"material_index":{"type":"integer","minimum":0}},"required":["entity_id"]}}},"required":["items"]})json",
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
             std::string results = "[";
             bool first = true;
             size_t ok_count = 0;
             size_t requested = 0;
             auto& em = ctx.get_cached<editing_manager>();
             for(auto el : items_arr)
             {
                 ++requested;
                 simdjson::dom::object obj;
                 if(!read_object(el, obj, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 std::string entity_id;
                 if(!read_string(obj, "entity_id", entity_id) || entity_id.empty())
                 {
                     return {.text = "Item missing entity_id", .is_error = true};
                 }
                 if(!first)
                 {
                     results += ",";
                 }
                 first = false;
                 auto entity = find_entity(*scn, entity_id);
                 if(!entity || !entity.all_of<model_component>())
                 {
                     results += fmt::format(R"({{"ok":false,"entity_id":{},"error":{}}})",
                                            make_json_string(entity_id),
                                            make_json_string(entity ? "Entity has no model_component"
                                                                    : "Entity not found"));
                     continue;
                 }
                 const auto old_model = entity.get<model_component>().get_model();
                 const auto index = read_slot_index(obj, old_model, error);
                 if(!index)
                 {
                     results += item_error_json(entity_id, error);
                     continue;
                 }
                 const bool had_overrides = old_model.get_material_instance(*index).has_overrides();
                 if(had_overrides)
                 {
                     auto new_model = old_model;
                     new_model.get_material_instance(*index).revert_all();
                     const auto swap = make_model_swap(entity, old_model, new_model, *index);
                     em.do_action("MCP Clear Model Material Instance", swap.first, swap.second);
                 }
                 ++ok_count;
                 results += fmt::format(R"({{"ok":true,"entity_id":{},"material_index":{},"cleared":{}}})",
                                        make_json_string(entity_id_string(entity)),
                                        *index,
                                        bool_to_json(had_overrides));
             }
             results += "]";
             return {.text = fmt::format(R"({{"results":{},"count":{},"requested":{}}})", results, ok_count, requested),
                     .is_error = ok_count == 0 && requested > 0};
         },
         .mutates_scene = true,
         .idempotent = true,
         .former_arguments = {{"items[].index", "material_index"}}});

    registry.add(
        {.name = "scene_set_post_process_materials_batch",
         .description =
             "Run Post Process materials over the view, as the Material Graph window's Add to Scene does. Each item: "
             "material (a Post Process material); entity_id (a post process volume, whose contribution fades it, or a "
             "camera, on which it always applies; default the scene's global volume, which is made when the scene has "
             "none); weight (0..1, default 1) scales the material's Opacity; enabled switches the entry; parameters "
             "{name: number or [x, y, z, w]} and textures {node id: texture key} override the material's values for "
             "that entry only (null reverts one). An entry of the same material on that entity is changed instead of "
             "added. The material's Location and Priority settings say where and in which order it runs. Every item "
             "is checked first: one that is refused changes nothing, and two items may not name the same entry. "
             "Returns each item's entity_id, entry index and whether a volume was made. One undo step.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","minItems":1,"items":{"type":"object","properties":{"material":{"type":"string","description":"Key or uid of a Post Process material"},"entity_id":{"type":"string","description":"A volume or camera; default: the scene's global volume"},"weight":{"type":"number","minimum":0,"maximum":1},"enabled":{"type":"boolean"},"parameters":{"type":"object"},"textures":{"type":"object"}},"required":["material"]}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text = error, .is_error = true};
             }
             simdjson::dom::array items;
             if(!read_required_array(args, "items", items, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::vector<post_process_material_item> planned;
             for(auto element : items)
             {
                 simdjson::dom::object item;
                 post_process_material_item plan;
                 if(!read_object(element, item, error) || !read_post_process_material_item(ctx, *scn, item, plan, error))
                 {
                     return {.text = make_batch_item_error(planned.size(), error, "Nothing changed."), .is_error = true};
                 }
                 const auto holder = get_post_process_holder(*scn, plan.target);
                 for(size_t i = 0; i < planned.size(); ++i)
                 {
                     if(planned[i].material.uid() == plan.material.uid() && get_post_process_holder(*scn, planned[i].target) == holder)
                     {
                         return {.text = make_batch_item_error(planned.size(),
                                                               fmt::format("It names the entry item {} names: give them as one item", i),
                                                               "Nothing changed."),
                                 .is_error = true};
                     }
                 }
                 planned.push_back(std::move(plan));
             }
             // Sized before the steps are made: each step fills its placement when the sequence runs.
             std::vector<post_process_material_placement> placements(planned.size());
             auto sequence = std::make_shared<sequence_action_t>();
             for(size_t i = 0; i < planned.size(); ++i)
             {
                 editor_actions::add_post_process_material_step(ctx,
                                                                *scn,
                                                                planned[i].target,
                                                                planned[i].material,
                                                                planned[i].change,
                                                                *sequence,
                                                                placements[i]);
             }
             ctx.get_cached<editing_manager>().do_action("MCP Set Post Process Materials", sequence);
             std::string results = "[";
             for(size_t i = 0; i < planned.size(); ++i)
             {
                 const auto& placement = placements[i];
                 const auto& mat = planned[i].material;
                 const auto loaded = mat.get();
                 const auto* graph = loaded ? loaded->safe_cast<graph_material>() : nullptr;
                 results += fmt::format(
                     R"({}{{"entity_id":{},"index":{},"added":{},"created_volume":{},"location":{},"priority":{},"result":{}}})",
                     i == 0 ? "" : ",",
                     make_json_string(entity_id_string(placement.entity)),
                     placement.index,
                     bool_to_json(placement.added),
                     bool_to_json(placement.created_volume),
                     make_json_string(graph ? get_material_post_process_location_key(graph->get_post_process_location()) : ""),
                     graph ? graph->get_post_process_priority() : 0,
                     apply_result_to_json(planned[i].applied));
             }
             results += "]";
             return {.text = fmt::format(R"({{"results":{},"count":{}}})", results, planned.size()), .is_error = false};
         },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_remove_post_process_materials_batch",
         .description =
             "Stop running Post Process materials. Each item: material, and entity_id (a volume or camera; default the "
             "scene's global volume) whose entry of it is removed. To pause one instead, "
             "scene_set_post_process_materials_batch with enabled false. Every item is checked first: one naming no "
             "entry removes none. One undo step.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","minItems":1,"items":{"type":"object","properties":{"material":{"type":"string","description":"Key or uid of the material"},"entity_id":{"type":"string","description":"A volume or camera; default: the scene's global volume"}},"required":["material"]}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text = error, .is_error = true};
             }
             simdjson::dom::array items;
             if(!read_required_array(args, "items", items, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::vector<std::pair<entt::handle, asset_handle<material>>> removals;
             for(auto element : items)
             {
                 simdjson::dom::object item;
                 entt::handle target;
                 if(!read_object(element, item, error) || !read_post_process_target(*scn, item, target, error))
                 {
                     return {.text = make_batch_item_error(removals.size(), error, "Nothing changed."), .is_error = true};
                 }
                 target = get_post_process_holder(*scn, target);
                 if(!target)
                 {
                     return {.text = make_batch_item_error(removals.size(),
                                                           "The scene has no global volume: name the volume or camera with entity_id",
                                                           "Nothing changed."),
                             .is_error = true};
                 }
                 std::string key;
                 read_string(item, "material", key);
                 const auto mat = resolve_material_asset(ctx, key, {}, error);
                 if(!mat)
                 {
                     return {.text = make_batch_item_error(removals.size(), error, "Nothing changed."), .is_error = true};
                 }
                 error = editor_actions::find_post_process_material_removal_problem(ctx, target, mat);
                 const bool is_repeated = std::any_of(removals.begin(),
                                                      removals.end(),
                                                      [&](const auto& removal)
                                                      {
                                                          return removal.first == target && removal.second.uid() == mat.uid();
                                                      });
                 if(error.empty() && is_repeated)
                 {
                     error = "An earlier item removes the same entry";
                 }
                 if(!error.empty())
                 {
                     return {.text = make_batch_item_error(removals.size(), error, "Nothing changed."), .is_error = true};
                 }
                 removals.emplace_back(target, mat);
             }
             auto sequence = std::make_shared<sequence_action_t>();
             std::string removed = "[";
             for(size_t i = 0; i < removals.size(); ++i)
             {
                 editor_actions::add_remove_post_process_material_step(removals[i].first, removals[i].second, *sequence);
                 removed += fmt::format(R"({}{{"entity_id":{}}})", i == 0 ? "" : ",", make_json_string(entity_id_string(removals[i].first)));
             }
             removed += "]";
             ctx.get_cached<editing_manager>().do_action("MCP Remove Post Process Materials", sequence);
             return {.text = fmt::format(R"({{"removed":{},"count":{}}})", removed, removals.size()), .is_error = false};
         },
         .mutates_scene = true});
}

} // namespace unravel::mcp

