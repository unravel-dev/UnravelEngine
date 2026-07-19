#include "mcp_tools_common.h"

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

struct primitive_batch_item
{
    std::string primitive;
    std::string name;
    std::string parent_id;
    std::string material_key;
    transform_snapshot pose{};
    bool is_local{true};
    uint32_t material_index{0};
};

auto apply_material_direct(rtti::context& ctx, entt::handle entity, const std::string& material_key, uint32_t index)
    -> bool
{
    if(!entity || !entity.all_of<model_component>() || material_key.empty())
    {
        return false;
    }
    auto& am = ctx.get_cached<asset_manager>();
    auto mat_handle = am.get_asset<::unravel::material>(material_key);
    if(!mat_handle)
    {
        return false;
    }
    auto& model_comp = entity.get<model_component>();
    auto model = model_comp.get_model();
    model.set_material(mat_handle, index);
    model_comp.set_model(model);
    prefab_override_context::mark_material_as_changed(entity);
    return true;
}

auto entity_matches_name(entt::handle entity, const std::string& name_contains, const std::string& name_exact) -> bool
{
    auto* tag = entity.try_get<tag_component>();
    if(!tag)
    {
        return false;
    }
    if(!name_exact.empty())
    {
        return tag->name == name_exact;
    }
    if(!name_contains.empty())
    {
        return contains_ci(tag->name, name_contains);
    }
    return true;
}

void collect_matching_entities(entt::handle entity,
                               const std::string& name_contains,
                               const std::string& name_exact,
                               std::vector<entt::handle>& out,
                               size_t limit)
{
    if(out.size() >= limit)
    {
        return;
    }
    if(entity_matches_name(entity, name_contains, name_exact))
    {
        out.push_back(entity);
        if(out.size() >= limit)
        {
            return;
        }
    }
    if(!entity.all_of<transform_component>())
    {
        return;
    }
    for(auto child : entity.get<transform_component>().get_children())
    {
        collect_matching_entities(child, name_contains, name_exact, out, limit);
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
        {.name = "scene_create_primitives_batch",
         .description =
             "Create many embedded mesh primitives in one undoable action. Each item: primitive, optional "
             "name/parent_id/material_key/material_index, position/rotation_euler/scale, space "
             "(local default|world). Axes: X-right, Y-up, Z-forward. Prefer space:\"local\" with "
             "rotation_euler:[0,0,0] under rotated parents.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"primitive":{"type":"string"},"name":{"type":"string"},"parent_id":{"type":"string"},"material_key":{"type":"string"},"material_index":{"type":"integer","minimum":0},"space":{"type":"string","enum":["world","local"]},"position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"rotation_euler":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"scale":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3}},"required":["primitive"]}}},"required":["items"]})json",
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
             if(args["items"].get(items_arr))
             {
                 return {.text = "Missing items array", .is_error = true};
             }

             std::vector<primitive_batch_item> items;
             for(auto el : items_arr)
             {
                 simdjson::dom::object obj;
                 if(el.get(obj))
                 {
                     return {.text = "Each item must be an object", .is_error = true};
                 }
                 primitive_batch_item item{};
                 if(!read_string(obj, "primitive", item.primitive) || item.primitive.empty())
                 {
                     return {.text = "Item missing primitive", .is_error = true};
                 }
                 read_string(obj, "name", item.name);
                 read_string(obj, "parent_id", item.parent_id);
                 read_string(obj, "material_key", item.material_key);
                 int64_t mat_index = 0;
                 if(!obj["material_index"].get(mat_index) && mat_index >= 0)
                 {
                     item.material_index = static_cast<uint32_t>(mat_index);
                 }
                 std::string space;
                 if(read_string(obj, "space", space) && space == "world")
                 {
                     item.is_local = false;
                 }
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
                 "MCP Batch Create Primitives",
                 [&]()
                 {
                     created.clear();
                     created.reserve(items.size());
                     for(const auto& item : items)
                     {
                         auto entity = defaults::create_embedded_mesh_entity(ctx, *scn, item.primitive);
                         if(!entity)
                         {
                             continue;
                         }
                         if(!item.name.empty())
                         {
                             entity.get<tag_component>().name = item.name;
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
                         if(!item.material_key.empty())
                         {
                             apply_material_direct(ctx, entity, item.material_key, item.material_index);
                         }
                         created.push_back(entity);
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
                 json += entity_to_summary_json(created[i], 0, 0);
             }
             json += "]";
             return {.text = fmt::format(R"({{"created":{},"count":{},"requested":{}}})",
                                         json,
                                         created.size(),
                                         items.size()),
                     .is_error = created.empty()};
         },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_set_transforms_batch",
         .description =
             "Set transforms on many entities in one undoable action. Each item: entity_id, optional "
             "space/position/rotation_euler/scale. Axes: X-right, Y-up, Z-forward.",
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
             if(args["items"].get(items_arr))
             {
                 return {.text = "Missing items array", .is_error = true};
             }

             struct entry
             {
                 entt::handle entity{};
                 bool is_local{false};
                 transform_snapshot pose{};
                 transform_snapshot old_local{};
                 transform_snapshot old_world{};
             };
             std::vector<entry> entries;
             for(auto el : items_arr)
             {
                 simdjson::dom::object obj;
                 if(el.get(obj))
                 {
                     return {.text = "Each item must be an object", .is_error = true};
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
                 e.entity = entity;
                 std::string space;
                 if(read_string(obj, "space", space) && space == "local")
                 {
                     e.is_local = true;
                 }
                 read_transform_snapshot(obj, e.pose);
                 auto& t = entity.get<transform_component>();
                 e.old_local.position = t.get_position_local();
                 e.old_local.rotation_euler = t.get_rotation_euler_local();
                 e.old_local.scale = t.get_scale_local();
                 e.old_local.has_position = e.old_local.has_rotation = e.old_local.has_scale = true;
                 e.old_world.position = t.get_position_global();
                 e.old_world.rotation_euler = t.get_rotation_euler_global();
                 e.old_world.scale = t.get_scale_global();
                 e.old_world.has_position = e.old_world.has_rotation = e.old_world.has_scale = true;
                 entries.push_back(e);
             }

             auto& em = ctx.get_cached<editing_manager>();
             em.do_action(
                 "MCP Batch Set Transforms",
                 [entries]()
                 {
                     for(const auto& e : entries)
                     {
                         apply_pose_direct(e.entity, e.pose, e.is_local);
                     }
                 },
                 [entries]()
                 {
                     for(const auto& e : entries)
                     {
                         apply_pose_direct(e.entity, e.is_local ? e.old_local : e.old_world, e.is_local);
                     }
                 });

             return {.text = fmt::format(R"({{"ok":true,"count":{}}})", entries.size()), .is_error = false};
         },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_set_model_materials_batch",
         .description =
             "Assign shared material assets to many model slots in one undoable action. Each item: "
             "entity_id, material_key, optional index (default 0).",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"material_key":{"type":"string"},"index":{"type":"integer","minimum":0}},"required":["entity_id","material_key"]}}},"required":["items"]})json",
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
             if(args["items"].get(items_arr))
             {
                 return {.text = "Missing items array", .is_error = true};
             }

             struct entry
             {
                 entt::handle entity{};
                 uint32_t index{0};
                 model old_model{};
                 model new_model{};
             };
             std::vector<entry> entries;
             auto& am = ctx.get_cached<asset_manager>();
             for(auto el : items_arr)
             {
                 simdjson::dom::object obj;
                 if(el.get(obj))
                 {
                     return {.text = "Each item must be an object", .is_error = true};
                 }
                 std::string entity_id;
                 std::string material_key;
                 if(!read_string(obj, "entity_id", entity_id) || !read_string(obj, "material_key", material_key))
                 {
                     return {.text = "Item missing entity_id or material_key", .is_error = true};
                 }
                 auto entity = find_entity(*scn, entity_id);
                 if(!entity || !entity.all_of<model_component>())
                 {
                     return {.text = "Entity missing model_component: " + entity_id, .is_error = true};
                 }
                 auto mat_handle = am.get_asset<::unravel::material>(material_key);
                 if(!mat_handle)
                 {
                     return {.text = "Material not found: " + material_key, .is_error = true};
                 }
                 int64_t index_i = 0;
                 if(obj["index"].get(index_i))
                 {
                     index_i = 0;
                 }
                 if(index_i < 0)
                 {
                     index_i = 0;
                 }
                 entry e{};
                 e.entity = entity;
                 e.index = static_cast<uint32_t>(index_i);
                 e.old_model = entity.get<model_component>().get_model();
                 e.new_model = e.old_model;
                 e.new_model.set_material(mat_handle, e.index);
                 entries.push_back(std::move(e));
             }

             auto& em = ctx.get_cached<editing_manager>();
             em.do_action(
                 "MCP Batch Set Model Materials",
                 [entries]()
                 {
                     for(const auto& e : entries)
                     {
                         if(auto* mc = e.entity.try_get<model_component>())
                         {
                             mc->set_model(e.new_model);
                             prefab_override_context::mark_material_as_changed(e.entity);
                         }
                     }
                 },
                 [entries]()
                 {
                     for(const auto& e : entries)
                     {
                         if(auto* mc = e.entity.try_get<model_component>())
                         {
                             mc->set_model(e.old_model);
                             prefab_override_context::mark_material_as_changed(e.entity);
                         }
                     }
                 });

             return {.text = fmt::format(R"({{"ok":true,"count":{}}})", entries.size()), .is_error = false};
         },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_get_bounds_batch",
         .description =
             "Get world-space AABB for one entity_id or many entity_ids (union). Optional depth "
             "(-1 = full hierarchy). Axes: X-right, Y-up, Z-forward.",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_id":{"type":"string"},"entity_ids":{"type":"array","items":{"type":"string"}},"depth":{"type":"integer"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text = error, .is_error = true};
             }

             std::vector<entt::handle> entities;
             std::string entity_id;
             if(read_string(args, "entity_id", entity_id) && !entity_id.empty())
             {
                 auto entity = find_entity(*scn, entity_id);
                 if(!entity)
                 {
                     return {.text = "Entity not found: " + entity_id, .is_error = true};
                 }
                 entities.push_back(entity);
             }
             simdjson::dom::array ids;
             if(!args["entity_ids"].get(ids))
             {
                 for(auto el : ids)
                 {
                     std::string_view id_view;
                     if(el.get(id_view))
                     {
                         return {.text = "entity_ids must be strings", .is_error = true};
                     }
                     auto entity = find_entity(*scn, std::string(id_view));
                     if(!entity)
                     {
                         return {.text = "Entity not found: " + std::string(id_view), .is_error = true};
                     }
                     entities.push_back(entity);
                 }
             }
             if(entities.empty())
             {
                 return {.text = "Provide entity_id or entity_ids", .is_error = true};
             }

             int64_t depth = -1;
             if(args["depth"].get(depth))
             {
                 depth = -1;
             }

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
         .mutates_scene = false});

    registry.add(
        {.name = "scene_find_entities_batch",
         .description =
             "Find entities by name_contains (case-insensitive) and/or name_exact. Optional parent_id "
             "limits search to that subtree; omit to search whole scene. Optional limit (default 100).",
         .input_schema_json =
             R"json({"type":"object","properties":{"name_contains":{"type":"string"},"name_exact":{"type":"string"},"parent_id":{"type":"string"},"limit":{"type":"integer","minimum":1,"maximum":5000}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text = error, .is_error = true};
             }

             std::string name_contains;
             std::string name_exact;
             read_string(args, "name_contains", name_contains);
             read_string(args, "name_exact", name_exact);
             if(name_contains.empty() && name_exact.empty())
             {
                 return {.text = "Provide name_contains and/or name_exact", .is_error = true};
             }

             int64_t limit = 100;
             if(args["limit"].get(limit))
             {
                 limit = 100;
             }
             if(limit < 1)
             {
                 limit = 1;
             }
             if(limit > 5000)
             {
                 limit = 5000;
             }

             std::vector<entt::handle> matches;
             std::string parent_id;
             if(read_string(args, "parent_id", parent_id) && !parent_id.empty())
             {
                 auto parent = find_entity(*scn, parent_id);
                 if(!parent)
                 {
                     return {.text = "Parent not found: " + parent_id, .is_error = true};
                 }
                 collect_matching_entities(parent, name_contains, name_exact, matches, static_cast<size_t>(limit));
             }
             else
             {
                 scn->registry->view<tag_component>().each(
                     [&](auto entt_id, auto&)
                     {
                         if(matches.size() >= static_cast<size_t>(limit))
                         {
                             return;
                         }
                         entt::handle entity(*scn->registry, entt_id);
                         if(entity_matches_name(entity, name_contains, name_exact))
                         {
                             matches.push_back(entity);
                         }
                     });
             }

             std::string json = "[";
             for(size_t i = 0; i < matches.size(); ++i)
             {
                 if(i > 0)
                 {
                     json += ",";
                 }
                 json += entity_to_summary_json(matches[i], 0, 0);
             }
             json += "]";
             return {.text = fmt::format(R"({{"entities":{},"count":{},"limit":{}}})",
                                         json,
                                         matches.size(),
                                         limit),
                     .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "scene_duplicate_entities_batch",
         .description =
             "Duplicate entities (clone hierarchy) in one undoable action. Returns created entity summaries.",
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

             simdjson::dom::array ids;
             if(args["entity_ids"].get(ids))
             {
                 return {.text = "Missing entity_ids", .is_error = true};
             }
             std::vector<entt::handle> sources;
             for(auto el : ids)
             {
                 std::string_view id_view;
                 if(el.get(id_view))
                 {
                     return {.text = "entity_ids must be strings", .is_error = true};
                 }
                 auto entity = find_entity(*scn, std::string(id_view));
                 if(!entity)
                 {
                     return {.text = "Entity not found: " + std::string(id_view), .is_error = true};
                 }
                 sources.push_back(entity);
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
                 json += entity_to_summary_json(created[i], 0, 0);
             }
             json += "]";
             return {.text = fmt::format(R"({{"created":{},"count":{}}})", json, created.size()),
                     .is_error = created.empty()};
         },
         .mutates_scene = true});
}

} // namespace unravel::mcp
