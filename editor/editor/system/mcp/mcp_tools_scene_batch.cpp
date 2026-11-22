#include "mcp_tools_common.h"
#include "mcp_asset_lookup.h"
#include "mcp_project_paths.h"

#include <editor/editing/actions/actions.h>
#include <editor/editing/actions/composite_action.h>
#include <editor/editing/component_clipboard.h>
#include <editor/editing/entity_clipboard.h>
#include <editor/editing/entity_inspect.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspector_model.h>
#include <engine/assets/asset_manager.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/material.h>
#include <engine/rendering/model.h>
#include <math/math.h>

namespace unravel::mcp
{
namespace
{

/// [{id, name, parent_id}] of the entities.
auto entities_to_lean_json(const std::vector<entt::handle>& entities) -> std::string
{
    std::string json = "[";
    for(size_t i = 0; i < entities.size(); ++i)
    {
        json += (i == 0 ? "" : ",") + entity_to_lean_json(entities[i], true);
    }
    return json + "]";
}

/// The placement scene_paste_entities names: next_to, child, child_keep_world or root.
auto to_paste_placement(const std::string& name) -> paste_placement
{
    if(name == "child")
    {
        return paste_placement::child_keep_local;
    }
    if(name == "child_keep_world")
    {
        return paste_placement::child_keep_world;
    }
    if(name == "root")
    {
        return paste_placement::scene_root;
    }
    return paste_placement::next_to;
}

auto is_placement_under_anchor(paste_placement placement) -> bool
{
    return placement == paste_placement::child_keep_local || placement == paste_placement::child_keep_world;
}

/// The copied components @p names and @p scripts name; all of them when both are empty. False, with the names nothing
/// matched in @p error, when one names nothing the source has.
auto pick_copied_components(const std::vector<component_clipboard::copied_component>& all,
                            const std::vector<std::string>& names,
                            const std::vector<std::string>& scripts,
                            std::vector<component_clipboard::copied_component>& out,
                            std::string& error) -> bool
{
    if(names.empty() && scripts.empty())
    {
        out = all;
        return true;
    }
    std::vector<std::string> missing;
    const auto pick = [&](const std::string& wanted, bool is_script)
    {
        const auto it = std::find_if(all.begin(),
                                     all.end(),
                                     [&](const component_clipboard::copied_component& item)
                                     {
                                         return is_script ? item.script_type == wanted : (!item.is_script() && item.name == wanted);
                                     });
        if(it == all.end())
        {
            missing.push_back(wanted);
            return;
        }
        out.push_back(*it);
    };
    for(const auto& name : names)
    {
        pick(name, false);
    }
    for(const auto& script : scripts)
    {
        pick(script, true);
    }
    if(missing.empty())
    {
        return true;
    }
    std::string available;
    for(const auto& item : all)
    {
        available += (available.empty() ? "" : ", ") + (item.is_script() ? item.script_type : item.name);
    }
    error = fmt::format("The source has no {} to copy. It has {}. Nothing changed.", fmt::join(missing, ", "), available);
    return false;
}

void collect_matching_children(entt::handle entity,
                               const entity_filter& filter,
                               std::vector<entt::handle>& out,
                               size_t limit);

/// Matches in @p entity's subtree, @p entity included, depth first.
void collect_matching_entities(entt::handle entity,
                               const entity_filter& filter,
                               std::vector<entt::handle>& out,
                               size_t limit)
{
    if(out.size() >= limit)
    {
        return;
    }
    if(entity_matches_filter(entity, filter))
    {
        out.push_back(entity);
        if(out.size() >= limit)
        {
            return;
        }
    }
    collect_matching_children(entity, filter, out, limit);
}

/// Matches below @p entity, depth first, not @p entity itself.
void collect_matching_children(entt::handle entity,
                               const entity_filter& filter,
                               std::vector<entt::handle>& out,
                               size_t limit)
{
    if(!entity.all_of<transform_component>())
    {
        return;
    }
    for(auto child : entity.get<transform_component>().get_children())
    {
        collect_matching_entities(child, filter, out, limit);
        if(out.size() >= limit)
        {
            return;
        }
    }
}

} // namespace

void register_scene_batch_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "scene_set_transforms_batch",
         .description =
             "Set transforms on many entities in one undoable action. Each item: entity_id, optional "
             "space/position/rotation_euler/scale." +
             std::string(k_play_edit_note),
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","minItems":1,"items":{"type":"object","properties":{"entity_id":{"type":"string"},"space":{"type":"string","enum":["world","local"]},"position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"rotation_euler":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"scale":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3}},"required":["entity_id"]}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             bool is_temporary = false;
             if(!require_live_scene(ctx, scn, is_temporary, error))
             {
                 return {.text = error, .is_error = true};
             }

             simdjson::dom::array items_arr;
             if(!read_required_array(args, "items", items_arr, error))
             {
                 return {.text = error, .is_error = true};
             }

             struct entry
             {
                 entt::uhandle entity{};
                 bool is_local{false};
                 transform_snapshot pose{};
                 /// The local values of the channels pose sets. Restored locally, they come back exactly
                 /// (authored Euler angles included) whatever order parents and children are undone in.
                 transform_snapshot old_local{};
             };
             std::vector<entry> entries;
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
                 entry e{};
                 e.entity = entt::make_uhandle(entity);
                 if(!read_transform_space(obj, e.is_local, error) || !read_transform_snapshot(obj, e.pose, error))
                 {
                     return {.text = entity_id + ": " + error, .is_error = true};
                 }
                 if(!e.pose.has_position && !e.pose.has_rotation && !e.pose.has_scale)
                 {
                     return {.text = "Item sets nothing (pass position, rotation_euler and/or scale): " + entity_id,
                             .is_error = true};
                 }
                 const auto& t = entity.get<transform_component>();
                 e.old_local.position = t.get_position_local();
                 e.old_local.rotation_euler = t.get_rotation_euler_local();
                 e.old_local.scale = t.get_scale_local();
                 e.old_local.has_position = e.pose.has_position;
                 e.old_local.has_rotation = e.pose.has_rotation;
                 e.old_local.has_scale = e.pose.has_scale;
                 entries.push_back(e);
             }
             if(entries.empty())
             {
                 return {.text = "items array is empty", .is_error = true};
             }
             auto& em = ctx.get_cached<editing_manager>();
             em.do_action(
                 "MCP Batch Set Transforms",
                 [entries]()
                 {
                     for(const auto& e : entries)
                     {
                         apply_pose_direct(e.entity.resolve(), e.pose, e.is_local);
                     }
                 },
                 [entries]()
                 {
                     for(const auto& e : entries)
                     {
                         apply_pose_direct(e.entity.resolve(), e.old_local, true);
                     }
                 });

             return {.text = fmt::format(R"({{"ok":true,"count":{}{}}})", entries.size(), temporary_field(is_temporary)),
                     .is_error = false};
         },
         .mutates_scene = true,
         .idempotent = true});

    registry.add(
        {.name = "scene_set_model_materials_batch",
         .description =
             "Assign shared material assets to model slots in one undoable step. Each item: entity_id, "
             "material (asset key or uid), optional material_index (default 0; below the model's slot count). "
             "Several items may target one entity's different slots.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","minItems":1,"items":{"type":"object","properties":{"entity_id":{"type":"string"},"material":{"type":"string"},"material_index":{"type":"integer","minimum":0}},"required":["entity_id","material"]}}},"required":["items"]})json",
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

             struct entry
             {
                 entt::uhandle entity{};
                 model old_model{};
                 model new_model{};
             };
             std::vector<entry> entries;
             for(auto el : items_arr)
             {
                 simdjson::dom::object obj;
                 if(!read_object(el, obj, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 std::string entity_id;
                 std::string material_key;
                 if(!read_string(obj, "entity_id", entity_id) || !read_string(obj, "material", material_key))
                 {
                     return {.text = "Item missing entity_id or material", .is_error = true};
                 }
                 auto entity = find_entity(*scn, entity_id);
                 if(!entity || !entity.all_of<model_component>())
                 {
                     return {.text = "Entity missing model_component: " + entity_id, .is_error = true};
                 }
                 auto mat_handle = find_existing_asset<::unravel::material>(ctx, to_asset_key(material_key), error);
                 if(!mat_handle)
                 {
                     return {.text = error, .is_error = true};
                 }
                 // Items for one entity build on each other; each entity's model is replaced once.
                 const auto uhandle = entt::make_uhandle(entity);
                 auto existing = std::find_if(entries.begin(),
                                              entries.end(),
                                              [&](const entry& e)
                                              {
                                                  return e.entity == uhandle;
                                              });
                 if(existing == entries.end())
                 {
                     const auto& current = entity.get<model_component>().get_model();
                     existing = entries.insert(entries.end(), entry{uhandle, current, current});
                 }
                 const auto slot_count = std::max<size_t>(1, existing->new_model.get_shared_materials().size());
                 const int64_t index = read_int64_or(obj, "material_index", 0);
                 if(index < 0 || static_cast<size_t>(index) >= slot_count)
                 {
                     return {.text = fmt::format("material_index {} is outside {}'s material slots 0..{}",
                                                 index,
                                                 entity_id,
                                                 slot_count - 1),
                             .is_error = true};
                 }
                 existing->new_model.set_shared_material(mat_handle, static_cast<uint32_t>(index));
             }

             auto& em = ctx.get_cached<editing_manager>();
             em.do_action(
                 "MCP Batch Set Model Materials",
                 [entries]()
                 {
                     for(const auto& e : entries)
                     {
                         const auto entity = e.entity.resolve();
                         if(auto* mc = entity ? entity.try_get<model_component>() : nullptr)
                         {
                             mc->set_model(e.new_model);
                             prefab_override_context::mark_material_as_changed(entity);
                         }
                     }
                 },
                 [entries]()
                 {
                     for(const auto& e : entries)
                     {
                         const auto entity = e.entity.resolve();
                         if(auto* mc = entity ? entity.try_get<model_component>() : nullptr)
                         {
                             mc->set_model(e.old_model);
                             prefab_override_context::mark_material_as_changed(entity);
                         }
                     }
                 });

             return {.text = fmt::format(R"({{"ok":true,"count":{}}})", entries.size()), .is_error = false};
         },
         .mutates_scene = true,
         .idempotent = true,
         .former_arguments = {{"items[].index", "material_index"}}});

    registry.add(
        {.name = "scene_set_morph_weights_batch",
         .description =
             "Set the morph target (blend shape) weights of models in one undoable step, as the Model component's "
             "Morph Targets sliders do. Each item: entity_id; weights, an object of target name to weight (0 is the "
             "rest shape, 1 the full shape; weights past 1 or below 0 are kept); optional reset true first returns "
             "every target to its mesh default, so the item states the whole face. Items naming the same entity apply "
             "in order. Each weight that changes is a prefab override by its target's name. assets_get_mesh_info lists "
             "a mesh's targets; an animation keying a target sets it again while playing.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","minItems":1,"items":{"type":"object","properties":{"entity_id":{"type":"string"},"weights":{"type":"object","additionalProperties":{"type":"number"}},"reset":{"type":"boolean"}},"required":["entity_id"]}}},"required":["items"]})json",
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
             // One change per model: an entity named by several items takes them in order.
             std::vector<morph_weights_change> changes;
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
                 auto* model_comp = entity ? entity.try_get<model_component>() : nullptr;
                 if(model_comp == nullptr)
                 {
                     return {.text = "Entity missing model_component: " + entity_id, .is_error = true};
                 }
                 const auto targets = model_comp->get_morph_targets();
                 if(!targets)
                 {
                     return {.text = fmt::format("The model of {} has no morph targets, or its mesh is still loading.", entity_id),
                             .is_error = true};
                 }
                 auto change = std::find_if(changes.begin(),
                                            changes.end(),
                                            [&entity](const morph_weights_change& existing)
                                            {
                                                return existing.entity == entity;
                                            });
                 if(change == changes.end())
                 {
                     changes.push_back({entity, model_comp->get_morph_weights(), model_comp->get_morph_weights()});
                     change = std::prev(changes.end());
                 }
                 bool is_reset = false;
                 read_bool(obj, "reset", is_reset);
                 if(is_reset)
                 {
                     change->after.clear();
                 }
                 simdjson::dom::element weights_value;
                 if(obj["weights"].get(weights_value) != simdjson::SUCCESS)
                 {
                     continue;
                 }
                 simdjson::dom::object weights;
                 if(weights_value.get(weights) != simdjson::SUCCESS)
                 {
                     return {.text = fmt::format("The weights of {} must be an object of target name to weight.", entity_id),
                             .is_error = true};
                 }
                 for(auto field : weights)
                 {
                     const std::string name(field.key);
                     if(targets->find_target(name) < 0)
                     {
                         std::vector<std::string> names;
                         for(const auto& target : targets->targets)
                         {
                             names.push_back(target.name);
                         }
                         return {.text = fmt::format("The mesh of {} has no morph target named {}. Its targets: {}.",
                                                     entity_id,
                                                     name,
                                                     fmt::join(names, ", ")),
                                 .is_error = true};
                     }
                     double value = 0.0;
                     if(field.value.get(value) != simdjson::SUCCESS || !std::isfinite(value))
                     {
                         return {.text = fmt::format("The weight of {} must be a finite number.", name), .is_error = true};
                     }
                     auto found = std::find_if(change->after.begin(),
                                               change->after.end(),
                                               [&name](const morph_weight& weight)
                                               {
                                                   return weight.name == name;
                                               });
                     if(found != change->after.end())
                     {
                         found->weight = float(value);
                     }
                     else
                     {
                         change->after.push_back({name, float(value)});
                     }
                 }
             }
             record_morph_weights_change(ctx, changes, "MCP Batch Set Morph Weights");
             return {.text = fmt::format(R"({{"ok":true,"count":{}}})", changes.size()), .is_error = false};
         },
         .mutates_scene = true,
         .idempotent = true});

    registry.add(
        {.name = "scene_get_bounds_batch",
         .description =
             "Get world-space AABB for one entity_id or many entity_ids (union). Optional depth "
             "(-1 = full hierarchy). Entities with nothing to measure (no model, a mesh still loading) are listed in "
             "unmeasured and left out; bounds is null when none had any. Allowed in play mode.",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_id":{"type":"string"},"entity_ids":{"type":"array","minItems":1,"items":{"type":"string"}},"depth":{"type":"integer","minimum":-1,"description":"Levels of children to include; -1 (default) all"}}})json",
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
             if(!resolve_entity_id_or_ids(*scn, args, entities, error))
             {
                 return {.text = error, .is_error = true};
             }

             const int64_t depth = read_int64_or(args, "depth", -1);
             // Entities with nothing to measure (no model, a mesh still loading) are listed, not sized up.
             hpp::optional<math::bbox> bounds;
             std::vector<std::string> unmeasured;
             for(auto entity : entities)
             {
                 const auto measured = defaults::calc_measured_bounds_global(entity, static_cast<int>(depth));
                 if(!measured)
                 {
                     unmeasured.push_back(entity_id_string(entity));
                     continue;
                 }
                 if(!bounds)
                 {
                     bounds = measured;
                     continue;
                 }
                 bounds->add_point(measured->min);
                 bounds->add_point(measured->max);
             }
             return {.text = fmt::format(R"({{"count":{},"bounds":{},"unmeasured":{}}})",
                                         entities.size(),
                                         bounds ? bbox_to_json(*bounds) : std::string("null"),
                                         strings_to_json_array(unmeasured)),
                     .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "scene_find_entities",
         .description =
             "Find entities by name (name_contains, name_exact), tag, component (a name from scene_list_components), "
             "type_name (C# full or short name), layer and active; the filters given AND together. parent_id searches "
             "only below that entity. Returns up to limit (default 100, max 5000); truncated says more matched. "
             "Allowed in play mode.",
         .input_schema_json =
             R"json({"type":"object","properties":{"name_contains":{"type":"string","description":"Part of the name, any case"},"name_exact":{"type":"string"},"tag":{"type":"string","description":"The entity's tag, exactly"},"component":{"type":"string","description":"A name from scene_list_components"},"type_name":{"type":"string","description":"A C# script type, full or short name"},"layer":{"type":"string","description":"A layer name from settings_get group layer"},"active":{"type":"boolean","description":"Active in the hierarchy (true) or not (false)"},"parent_id":{"type":"string","description":"Search only below this entity"},"limit":{"type":"integer","minimum":1,"maximum":5000}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_active_scene(ctx, scn, error))
             {
                 return {.text = error, .is_error = true};
             }

             auto filter = read_entity_filter(args);
             if(filter.is_empty())
             {
                 return {.text = "Provide name_contains, name_exact, tag, component, type_name, layer and/or active",
                         .is_error = true};
             }
             if(!resolve_layer_filter(ctx, filter, error))
             {
                 return {.text = error, .is_error = true};
             }

             const auto limit = static_cast<size_t>(std::clamp<int64_t>(read_int64_or(args, "limit", 100), 1, 5000));
             // One match past the limit tells whether the result was cut short.
             const size_t collect_limit = limit + 1;
             std::vector<entt::handle> matches;
             std::string parent_id;
             if(read_string(args, "parent_id", parent_id) && !parent_id.empty())
             {
                 auto parent = find_entity(*scn, parent_id);
                 if(!parent)
                 {
                     return {.text = "Parent not found: " + parent_id, .is_error = true};
                 }
                 collect_matching_children(parent, filter, matches, collect_limit);
             }
             else
             {
                 matches = find_matching_entities(*scn, filter, collect_limit);
             }

             const bool truncated = matches.size() > limit;
             matches.resize(std::min(matches.size(), limit));
             std::string json = "[";
             for(size_t i = 0; i < matches.size(); ++i)
             {
                 if(i > 0)
                 {
                     json += ",";
                 }
                 json += entity_to_lean_json(matches[i], true);
             }
             json += "]";
             return {.text = fmt::format(R"({{"entities":{},"count":{},"truncated":{},"limit":{}}})",
                                         json,
                                         matches.size(),
                                         bool_to_json(truncated),
                                         limit),
                     .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only,
         .former_names = {"scene_find_entities_batch"}});

    registry.add(
        {.name = "scene_duplicate_entities_batch",
         .description =
             "Duplicate entities with everything under them, each copy right after its source among its siblings and at "
             "its pose, in one undo step, as Ctrl+D does. An entity inside another one in the list is copied with it, "
             "once. A copy whose name a sibling has already gets the next free number: Crate becomes Crate (1). Links "
             "inside a copy land on the copy. Returns created [{id, name, parent_id}].",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_ids":{"type":"array","minItems":1,"items":{"type":"string"}}},"required":["entity_ids"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text = error, .is_error = true};
             }

             std::vector<entt::handle> sources;
             if(!read_entity_ids(args, *scn, sources, error))
             {
                 return {.text = error, .is_error = true};
             }

             auto result = std::make_shared<paste_result>();
             auto action = entity_clipboard::make_duplicate_action(ctx, sources, result);
             if(!action)
             {
                 return {.text = result->problem.empty() ? std::string("Nothing to duplicate.") : result->problem, .is_error = true};
             }
             ctx.get_cached<editing_manager>().do_action("MCP " + action->get_name(), action);
             return {.text = fmt::format(R"({{"created":{},"count":{}}})", entities_to_lean_json(result->roots), result->roots.size()),
                     .is_error = result->roots.empty()};
         },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_copy_entities",
         .description =
             "Copy entities with everything under them as a text document, the one the editor's Copy puts on the "
             "clipboard. It pastes into any scene, prefab or project with scene_paste_entities, at any later time: keep "
             "it to move content between scenes or as a reusable snippet. to_clipboard also puts it on the OS clipboard, "
             "where the user's Ctrl+V pastes it. Returns {document, count}.",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_ids":{"type":"array","minItems":1,"items":{"type":"string"}},"to_clipboard":{"type":"boolean"}},"required":["entity_ids"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto* scn = ctx.get_cached<editing_manager>().get_active_scene(ctx);
             if(scn == nullptr)
             {
                 return {.text = "No scene is open.", .is_error = true};
             }
             std::vector<entt::handle> entities;
             std::string error;
             if(!read_entity_ids(args, *scn, entities, error))
             {
                 return {.text = error, .is_error = true};
             }
             const auto document = entity_clipboard::copy(entities);
             bool to_clipboard = false;
             read_bool(args, "to_clipboard", to_clipboard);
             if(to_clipboard && !document.empty())
             {
                 ImGui::SetClipboardText(document.c_str());
             }
             return {.text = fmt::format(R"({{"document":{},"count":{}}})",
                                         make_json_string(document),
                                         transform_component::get_top_level_entities(entities).size()),
                     .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "scene_paste_entities",
         .description =
             "Paste a copy of entities made by scene_copy_entities or the editor's Copy into the scene being edited, as "
             "new entities with fresh ids, in one undo step. document is the copy's text; without it the OS clipboard is "
             "pasted. placement: next_to (default: right after entity_id among its siblings, keeping world poses; "
             "without entity_id, right after each original when the scene has it, else under its old parent when the "
             "scene has that, else at the root), child (last under entity_id, keeping poses relative to the parent), "
             "child_keep_world (last under entity_id, keeping world poses), root. Links inside the copy land on the copy; "
             "links out of it land on this scene's entity with that id, or are left empty. A copy whose name a sibling "
             "has gets the next free number. In prefab mode the copies land inside the prefab, and a copy holding an "
             "instance of the prefab being edited is refused. Returns created [{id, name, parent_id}].",
         .input_schema_json =
             R"json({"type":"object","properties":{"document":{"type":"string"},"entity_id":{"type":"string"},"placement":{"type":"string","enum":["next_to","child","child_keep_world","root"]}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::string document;
             if(!read_string(args, "document", document))
             {
                 const char* text = ImGui::GetClipboardText();
                 document = text != nullptr ? text : "";
             }
             entt::handle anchor;
             std::string anchor_id;
             if(read_string(args, "entity_id", anchor_id))
             {
                 anchor = find_entity(*scn, anchor_id);
                 if(!anchor)
                 {
                     return {.text = "Entity not found: " + anchor_id + ". Nothing changed.", .is_error = true};
                 }
             }
             std::string placement_name = "next_to";
             read_string(args, "placement", placement_name);
             const auto placement = to_paste_placement(placement_name);
             if(is_placement_under_anchor(placement) && !anchor)
             {
                 return {.text = "placement " + placement_name + " needs entity_id, the entity to paste under. Nothing changed.",
                         .is_error = true};
             }
             auto result = std::make_shared<paste_result>();
             auto action = entity_clipboard::make_paste_action(ctx, std::move(document), placement, anchor, result);
             if(!action)
             {
                 return {.text = result->problem + " Nothing changed.", .is_error = true};
             }
             ctx.get_cached<editing_manager>().do_action("MCP " + action->get_name(), action);
             return {.text = fmt::format(R"({{"created":{},"count":{}}})", entities_to_lean_json(result->roots), result->roots.size()),
                     .is_error = result->roots.empty()};
         },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_copy_components_batch",
         .description =
             "Copy components of one entity to others, as the Inspector's Copy Component and Paste Component Values or "
             "Paste Component as New do, in one undo step. components names native components (Light, Model, "
             "Transform), scripts names C# scripts by full type name; neither copies every component the Inspector "
             "copies (not Id, Name, Layers or the prefab's own). mode values (default) loads the copied values into the "
             "targets' component of that type and skips targets without it (a script goes into each target's first "
             "script of its type); as_new adds them, skipping a native component a target has already. A Transform "
             "copies its local position, rotation and scale only. On prefab instances the changed fields become "
             "overrides. Returns {pasted, components}.",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_id":{"type":"string","description":"The entity to copy from"},"entity_ids":{"type":"array","minItems":1,"items":{"type":"string"},"description":"The entities to paste onto"},"components":{"type":"array","items":{"type":"string"}},"scripts":{"type":"array","items":{"type":"string"}},"mode":{"type":"string","enum":["values","as_new"]}},"required":["entity_id","entity_ids"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::string source_id;
             read_string(args, "entity_id", source_id);
             const auto source = find_entity(*scn, source_id);
             if(!source)
             {
                 return {.text = "Entity not found: " + source_id + ". Nothing changed.", .is_error = true};
             }
             std::vector<entt::handle> targets;
             if(!read_entity_ids(args, *scn, targets, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::vector<std::string> names;
             std::vector<std::string> scripts;
             if(!read_string_array(args, "components", names, error, false) ||
                !read_string_array(args, "scripts", scripts, error, false))
             {
                 return {.text = error, .is_error = true};
             }
             const auto all = component_clipboard::read_components(component_clipboard::copy_all_components(source));
             std::vector<component_clipboard::copied_component> items;
             if(!pick_copied_components(all, names, scripts, items, error))
             {
                 return {.text = error, .is_error = true};
             }
             if(items.empty())
             {
                 return {.text = "The source has no component to copy. Nothing changed.", .is_error = true};
             }
             std::string mode = "values";
             read_string(args, "mode", mode);
             size_t pasted = 0;
             if(mode == "as_new")
             {
                 pasted = component_clipboard::paste_as_new(ctx, targets, items);
             }
             else
             {
                 auto edits = std::make_shared<composite_action_t>();
                 for(const auto& item : items)
                 {
                     component_clipboard::add_paste_values_steps(*edits, targets, item);
                 }
                 pasted = edits->sub_actions.size();
                 if(pasted > 0)
                 {
                     ctx.get_cached<editing_manager>().do_action("MCP Paste Component Values", edits);
                 }
             }
             if(pasted == 0)
             {
                 return {.text = mode == "as_new"
                                     ? std::string("Every target has these components already. Nothing changed.")
                                     : std::string("No target has these components, which mode as_new adds. Nothing changed."),
                         .is_error = true};
             }
             std::string copied;
             for(const auto& item : items)
             {
                 copied += (copied.empty() ? "" : ",") + make_json_string(item.is_script() ? item.script_type : item.name);
             }
             return {.text = fmt::format(R"({{"pasted":{},"components":[{}]}})", pasted, copied), .is_error = false};
         },
         .mutates_scene = true});
}

} // namespace unravel::mcp
