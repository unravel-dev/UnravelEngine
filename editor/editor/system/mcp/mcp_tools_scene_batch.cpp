#include "mcp_tools_common.h"
#include "mcp_asset_lookup.h"
#include "mcp_project_paths.h"

#include <editor/editing/actions/actions.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
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
             "space/position/rotation_euler/scale.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"space":{"type":"string","enum":["world","local"]},"position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"rotation_euler":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"scale":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3}},"required":["entity_id"]}}},"required":["items"]})json",
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
                 std::string space;
                 if(read_string(obj, "space", space) && space == "local")
                 {
                     e.is_local = true;
                 }
                 read_transform_snapshot(obj, e.pose);
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

             return {.text = fmt::format(R"({{"ok":true,"count":{}}})", entries.size()), .is_error = false};
         },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_set_model_materials_batch",
         .description =
             "Assign shared material assets to model slots in one undoable step. Each item: entity_id, "
             "material (asset key or uid), optional index (default 0; below the model's slot count). "
             "Several items may target one entity's different slots.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"material":{"type":"string"},"index":{"type":"integer","minimum":0}},"required":["entity_id","material"]}}},"required":["items"]})json",
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
                 const auto slot_count = std::max<size_t>(1, existing->new_model.get_materials().size());
                 const int64_t index = read_int64_or(obj, "index", 0);
                 if(index < 0 || static_cast<size_t>(index) >= slot_count)
                 {
                     return {.text = fmt::format("index {} is outside {}'s material slots 0..{}",
                                                 index,
                                                 entity_id,
                                                 slot_count - 1),
                             .is_error = true};
                 }
                 existing->new_model.set_material(mat_handle, static_cast<uint32_t>(index));
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
         .idempotent = true});

    registry.add(
        {.name = "scene_get_bounds_batch",
         .description =
             "Get world-space AABB for one entity_id or many entity_ids (union). Optional depth "
             "(-1 = full hierarchy). Allowed in play mode.",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_id":{"type":"string"},"entity_ids":{"type":"array","items":{"type":"string"}},"depth":{"type":"integer"}}})json",
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

             math::bbox bounds;
             bool first = true;
             for(auto entity : entities)
             {
                 auto eb = defaults::calc_bounds_global(entity, static_cast<int>(depth));
                 if(first)
                 {
                     bounds = eb;
                     first = false;
                 }
                 else
                 {
                     bounds.add_point(eb.min);
                     bounds.add_point(eb.max);
                 }
             }
             return {.text = fmt::format(R"({{"count":{},"bounds":{}}})", entities.size(), bbox_to_json(bounds)),
                     .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "scene_find_entities_batch",
         .description =
             "Find entities by name_contains and/or name_exact and/or component (a name from scene_list_components) "
             "and/or type_name (C# full name). Filters AND together. parent_id searches only below "
             "that entity. Returns up to limit (default 100, max 5000); truncated says more matched. "
             "Allowed in play mode.",
         .input_schema_json =
             R"json({"type":"object","properties":{"name_contains":{"type":"string"},"name_exact":{"type":"string"},"component":{"type":"string"},"type_name":{"type":"string"},"parent_id":{"type":"string"},"limit":{"type":"integer","minimum":1,"maximum":5000}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_active_scene(ctx, scn, error))
             {
                 return {.text = error, .is_error = true};
             }

             const auto filter = read_entity_filter(args);
             if(filter.is_empty())
             {
                 return {.text =
                             "Provide name_contains, name_exact, component, and/or type_name",
                         .is_error = true};
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
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "scene_duplicate_entities_batch",
         .description =
             "Duplicate entities with everything under them, under the same parent and at the same pose, in one "
             "undo step. Returns created [{id, name, parent_id}].",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_ids":{"type":"array","items":{"type":"string"}}},"required":["entity_ids"]})json",
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

             std::vector<entt::handle> created;
             auto& em = ctx.get_cached<editing_manager>();
             em.do_action<create_entities_action_t>(
                 "MCP Duplicate Entities",
                 [&]()
                 {
                     created.clear();
                     for(auto source : sources)
                     {
                         auto clone = scn->clone_entity(source);
                         if(clone)
                         {
                             created.push_back(clone);
                         }
                     }
                     return created;
                 });

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
             return {.text = fmt::format(R"({{"created":{},"count":{}}})", json, created.size()),
                     .is_error = created.empty()};
         },
         .mutates_scene = true});
}

} // namespace unravel::mcp
