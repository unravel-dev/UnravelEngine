#include "mcp_tools_common.h"

#include <editor/editing/actions/actions.h>
#include <editor/editing/editor_actions.h>
#include <editor/system/project_manager.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_writer.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/ecs.h>
#include <engine/ecs/prefab.h>
#include <engine/ecs/scene.h>
#include <engine/meta/ecs/components/all_components.h>
#include <engine/rendering/light.h>
#include <engine/scripting/ecs/components/script_component.h>
#include <filesystem/filesystem.h>
#include <hpp/utility.hpp>
#include <math/math.h>

namespace unravel::mcp
{
namespace
{

auto entity_to_json(entt::handle entity, int depth, int max_depth) -> std::string
{
    return entity_to_summary_json(entity, depth, max_depth);
}

auto parse_light_type(const std::string& value, light_type& out) -> bool
{
    if(value == "directional" || value == "Directional")
    {
        out = light_type::directional;
        return true;
    }
    if(value == "point" || value == "Point")
    {
        out = light_type::point;
        return true;
    }
    if(value == "spot" || value == "Spot")
    {
        out = light_type::spot;
        return true;
    }
    return false;
}

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

void maybe_set_parent(rtti::context& ctx, entt::handle entity, entt::handle parent)
{
    if(!entity || !parent)
    {
        return;
    }
    auto& em = ctx.get_cached<editing_manager>();
    auto old_parent = entity.get<transform_component>().get_parent();
    em.do_action<transform_set_parent_action_t>("MCP Set Parent", entity, old_parent, parent);
}

void maybe_set_position(rtti::context& ctx, entt::handle entity, const math::vec3& position)
{
    if(!entity)
    {
        return;
    }
    // Create tools place entities in WORLD space (matches prefab spawn / focus tools).
    auto& transform = entity.get<transform_component>();
    auto old_pos = transform.get_position_global();
    auto& em = ctx.get_cached<editing_manager>();
    em.do_action<transform_move_global_action_t>("MCP Set Position", entity, old_pos, position);
}

void apply_transform_fields(rtti::context& ctx,
                            entt::handle entity,
                            const simdjson::dom::object& args,
                            bool is_local)
{
    auto& transform = entity.get<transform_component>();
    auto& em = ctx.get_cached<editing_manager>();

    math::vec3 position{};
    if(read_vec3(args, "position", position))
    {
        if(is_local)
        {
            em.do_action<transform_move_action_t>("MCP Set Position Local",
                                                 entity,
                                                 transform.get_position_local(),
                                                 position);
        }
        else
        {
            em.do_action<transform_move_global_action_t>("MCP Set Position World",
                                                        entity,
                                                        transform.get_position_global(),
                                                        position);
        }
    }

    math::vec3 rotation{};
    if(read_vec3(args, "rotation_euler", rotation))
    {
        if(is_local)
        {
            const auto old_euler = transform.get_rotation_euler_local();
            const auto new_euler = rotation;
            em.do_action(
                "MCP Set Rotation Local",
                [entity, new_euler]()
                {
                    if(auto* t = entity.try_get<transform_component>())
                    {
                        t->set_rotation_euler_local(new_euler);
                    }
                },
                [entity, old_euler]()
                {
                    if(auto* t = entity.try_get<transform_component>())
                    {
                        t->set_rotation_euler_local(old_euler);
                    }
                });
        }
        else
        {
            const auto old_euler = transform.get_rotation_euler_global();
            const auto new_euler = rotation;
            em.do_action(
                "MCP Set Rotation World",
                [entity, new_euler]()
                {
                    if(auto* t = entity.try_get<transform_component>())
                    {
                        t->set_rotation_euler_global(new_euler);
                    }
                },
                [entity, old_euler]()
                {
                    if(auto* t = entity.try_get<transform_component>())
                    {
                        t->set_rotation_euler_global(old_euler);
                    }
                });
        }
    }

    math::vec3 scale{};
    if(read_vec3(args, "scale", scale))
    {
        if(is_local)
        {
            em.do_action<transform_scale_action_t>("MCP Set Scale Local",
                                                   entity,
                                                   transform.get_scale_local(),
                                                   scale);
        }
        else
        {
            const auto old_scale = transform.get_scale_global();
            const auto new_scale = scale;
            em.do_action(
                "MCP Set Scale World",
                [entity, new_scale]()
                {
                    if(auto* t = entity.try_get<transform_component>())
                    {
                        t->set_scale_global(new_scale);
                    }
                },
                [entity, old_scale]()
                {
                    if(auto* t = entity.try_get<transform_component>())
                    {
                        t->set_scale_global(old_scale);
                    }
                });
        }
    }
}

auto normalize_scene_key(std::string key) -> std::string
{
    if(key.empty())
    {
        return key;
    }
    fs::error_code ec;
    const fs::path as_path(key);
    if(as_path.is_absolute() && fs::exists(as_path, ec))
    {
        key = fs::convert_to_protocol(as_path).generic_string();
    }
    if(key.size() < 5 || key.substr(key.size() - 5) != ".spfb")
    {
        key += ".spfb";
    }
    return key;
}

} // namespace

void register_scene_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name="scene_get_info",
         .description="Get active scene info: tag/source, entity count, and play mode phase.",
         .input_schema_json=empty_object_schema(),
         .handler=[](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
         {
             auto& em = ctx.get_cached<editing_manager>();
             auto* scn = em.get_active_scene(ctx);
             std::string phase = "inactive";
             if(ctx.has<play_mode>())
             {
                 auto& play = ctx.get_cached<play_mode>();
                 if(play.is_splash())
                 {
                     phase = "splash";
                 }
                 else if(play.is_simulation_running())
                 {
                     phase = "running";
                 }
                 else if(play.is_active())
                 {
                     phase = "active";
                 }
             }

             if(!scn || !scn->registry)
             {
                 return {R"({"has_scene":false,"play_phase":")" + phase + "\"}", false};
             }

             const auto entity_count = scn->registry->storage<entt::entity>().size();
             const auto source = scn->source ? scn->source.id() : std::string{};
             return {.text=fmt::format(R"({{"has_scene":true,"tag":{},"source":{},"entity_count":{},"play_phase":{}}})",
                                 make_json_string(scn->tag),
                                 make_json_string(source),
                                 entity_count,
                                 make_json_string(phase)),
                     .is_error=false};
         },
         .mutates_scene=false});

    registry.add(
        {.name="scene_list_entities",
         .description=
             "List entities in the active scene hierarchy. Optional parent_id and max_depth (default 2). "
             "Axes: X-right, Y-up, Z-forward. "
             "Transform fields: position/rotation_euler/scale are WORLD (global); "
             "position_local/rotation_euler_local/scale_local are LOCAL (parent-relative).",
         .input_schema_json=R"({"type":"object","properties":{"parent_id":{"type":"string"},"max_depth":{"type":"integer","minimum":0}}})",
         .handler=[](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& em = ctx.get_cached<editing_manager>();
             auto* scn = em.get_active_scene(ctx);
             if(!scn || !scn->registry)
             {
                 return {.text="No active scene", .is_error=true};
             }

             int64_t max_depth = 2;
             if(args["max_depth"].get(max_depth))
             {
                 max_depth = 2;
             }
             if(max_depth < 0)
             {
                 max_depth = 0;
             }

             std::string parent_id;
             read_string(args, "parent_id", parent_id);

             std::string json = "[";
             bool first = true;
             auto append = [&](entt::handle entity)
             {
                 if(!first)
                 {
                     json += ",";
                 }
                 first = false;
                 json += entity_to_json(entity, 0, static_cast<int>(max_depth));
             };

             if(!parent_id.empty())
             {
                 auto parent = find_entity(*scn, parent_id);
                 if(!parent)
                 {
                     return {.text="Entity not found: " + parent_id, .is_error=true};
                 }
                 if(auto* transform = parent.try_get<transform_component>())
                 {
                     for(auto child : transform->get_children())
                     {
                         append(child);
                     }
                 }
             }
             else
             {
                 scn->registry->view<root_component, transform_component>().each(
                     [&](auto e, auto&&, auto&& transform)
                     {
                         append(transform.get_owner());
                     });
             }

             json += "]";
             return {.text=json, .is_error=false};
         },
         .mutates_scene=false});

    registry.add(
        {.name="scene_create_entity",
         .description="Create an empty entity. Args: name (required), optional parent_id.",
         .input_schema_json=R"({"type":"object","properties":{"name":{"type":"string"},"parent_id":{"type":"string"}},"required":["name"]})",
         .handler=[](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text=error, .is_error=true};
             }

             std::string name;
             if(!read_string(args, "name", name) || name.empty())
             {
                 return {.text="Missing name", .is_error=true};
             }

             entt::handle parent{};
             std::string parent_id;
             if(read_string(args, "parent_id", parent_id) && !parent_id.empty())
             {
                 parent = find_entity(*scn, parent_id);
                 if(!parent)
                 {
                     return {.text="Parent not found: " + parent_id, .is_error=true};
                 }
             }

             entt::handle created{};
             auto& em = ctx.get_cached<editing_manager>();
             em.do_action<create_entities_action_t>("MCP Create Entity",
                                                    [&]()
                                                    {
                                                        created = scn->create_entity(name, parent);
                                                        return created;
                                                    });
             if(!created)
             {
                 return {.text="Failed to create entity", .is_error=true};
             }
             return {.text=entity_to_json(created, 0, 0), .is_error=false};
         },
         .mutates_scene=true});

    registry.add(
        {.name="scene_create_primitive",
         .description=
             "Create an embedded mesh primitive (Cube, Sphere, Plane, Cylinder, Cone, Torus, Capsule 1m, Capsule 2m, ...). "
             "Axes: X-right, Y-up, Z-forward. Cube is 1x1x1 centered at origin. "
             "Optional name/parent_id/position. position is WORLD space even with parent_id -- "
             "use scene_set_transform with space:\"local\" for parent-relative placement.",
         .input_schema_json=R"json({"type":"object","properties":{"primitive":{"type":"string"},"name":{"type":"string"},"parent_id":{"type":"string"},"position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3,"description":"WORLD space [x,y,z] (X-right, Y-up, Z-forward)"}},"required":["primitive"]})json",
         .handler=[](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text=error, .is_error=true};
             }

             std::string primitive;
             if(!read_string(args, "primitive", primitive) || primitive.empty())
             {
                 return {.text="Missing primitive", .is_error=true};
             }

             entt::handle parent{};
             std::string parent_id;
             if(read_string(args, "parent_id", parent_id) && !parent_id.empty())
             {
                 parent = find_entity(*scn, parent_id);
                 if(!parent)
                 {
                     return {.text="Parent not found: " + parent_id, .is_error=true};
                 }
             }

             math::vec3 position{};
             const bool has_position = read_vec3(args, "position", position);

             entt::handle created{};
             auto& em = ctx.get_cached<editing_manager>();
             em.do_action<create_entities_action_t>("MCP Create Primitive",
                                                    [&]()
                                                    {
                                                        created = defaults::create_embedded_mesh_entity(ctx, *scn, primitive);
                                                        return created;
                                                    });
             if(!created)
             {
                 return {.text="Failed to create primitive (unknown name?)", .is_error=true};
             }

             std::string name;
             if(read_string(args, "name", name) && !name.empty())
             {
                 auto old_name = created.get<tag_component>().name;
                 em.do_action<entity_set_name_action_t>("MCP Set Name", created, old_name, name);
             }
             maybe_set_parent(ctx, created, parent);
             if(has_position)
             {
                 maybe_set_position(ctx, created, position);
             }
             return {.text=entity_to_json(created, 0, 0), .is_error=false};
         },
         .mutates_scene=true});

    registry.add(
        {.name="scene_create_light",
         .description=
             "Create a light entity. Args: light_type (directional|point|spot), name, optional parent_id/position. "
             "position is WORLD space.",
             .input_schema_json=R"json({"type":"object","properties":{"light_type":{"type":"string"},"name":{"type":"string"},"parent_id":{"type":"string"},"position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3,"description":"WORLD space [x,y,z] (X-right, Y-up, Z-forward)"}},"required":["light_type","name"]})json",
         .handler=[](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text=error, .is_error=true};
             }

             std::string type_name;
             std::string name;
             if(!read_string(args, "light_type", type_name) || !read_string(args, "name", name))
             {
                 return {.text="Missing light_type or name", .is_error=true};
             }
             light_type type{};
             if(!parse_light_type(type_name, type))
             {
                 return {.text="Invalid light_type", .is_error=true};
             }

             entt::handle parent{};
             std::string parent_id;
             if(read_string(args, "parent_id", parent_id) && !parent_id.empty())
             {
                 parent = find_entity(*scn, parent_id);
                 if(!parent)
                 {
                     return {.text="Parent not found: " + parent_id, .is_error=true};
                 }
             }

             math::vec3 position{};
             const bool has_position = read_vec3(args, "position", position);

             entt::handle created{};
             auto& em = ctx.get_cached<editing_manager>();
             em.do_action<create_entities_action_t>("MCP Create Light",
                                                    [&]()
                                                    {
                                                        created = defaults::create_light_entity(ctx, *scn, type, name);
                                                        return created;
                                                    });
             if(!created)
             {
                 return {.text="Failed to create light", .is_error=true};
             }
             maybe_set_parent(ctx, created, parent);
             if(has_position)
             {
                 maybe_set_position(ctx, created, position);
             }
             return {.text=entity_to_json(created, 0, 0), .is_error=false};
         },
         .mutates_scene=true});

    registry.add(
        {.name="scene_create_camera",
         .description=
             "Create a camera entity. Args: name, optional parent_id/position. position is WORLD space.",
         .input_schema_json=R"json({"type":"object","properties":{"name":{"type":"string"},"parent_id":{"type":"string"},"position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3,"description":"WORLD space [x,y,z] (X-right, Y-up, Z-forward)"}},"required":["name"]})json",
         .handler=[](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text=error, .is_error=true};
             }

             std::string name;
             if(!read_string(args, "name", name))
             {
                 return {.text="Missing name", .is_error=true};
             }

             entt::handle parent{};
             std::string parent_id;
             if(read_string(args, "parent_id", parent_id) && !parent_id.empty())
             {
                 parent = find_entity(*scn, parent_id);
                 if(!parent)
                 {
                     return {.text="Parent not found: " + parent_id, .is_error=true};
                 }
             }

             math::vec3 position{};
             const bool has_position = read_vec3(args, "position", position);

             entt::handle created{};
             auto& em = ctx.get_cached<editing_manager>();
             em.do_action<create_entities_action_t>("MCP Create Camera",
                                                    [&]()
                                                    {
                                                        created = defaults::create_camera_entity(ctx, *scn, name);
                                                        return created;
                                                    });
             if(!created)
             {
                 return {.text="Failed to create camera", .is_error=true};
             }
             maybe_set_parent(ctx, created, parent);
             if(has_position)
             {
                 maybe_set_position(ctx, created, position);
             }
             return {.text=entity_to_json(created, 0, 0), .is_error=false};
         },
         .mutates_scene=true});

    registry.add(
        {.name="scene_create_from_prefab",
         .description=
             "Instantiate a prefab asset by key (e.g. app:/data/foo.prefab). "
             "Optional position is WORLD space.",
         .input_schema_json=R"json({"type":"object","properties":{"asset_key":{"type":"string"},"position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3,"description":"WORLD space [x,y,z] (X-right, Y-up, Z-forward)"}},"required":["asset_key"]})json",
         .handler=[](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text=error, .is_error=true};
             }

             std::string asset_key;
             if(!read_string(args, "asset_key", asset_key) || asset_key.empty())
             {
                 return {.text="Missing asset_key", .is_error=true};
             }

             math::vec3 position{};
             const bool has_position = read_vec3(args, "position", position);

             entt::handle created{};
             auto& em = ctx.get_cached<editing_manager>();
             em.do_action<create_entities_action_t>("MCP Create Prefab",
                                                    [&]()
                                                    {
                                                        if(has_position)
                                                        {
                                                            created = defaults::create_prefab_at(ctx, *scn, asset_key, position);
                                                        }
                                                        else
                                                        {
                                                            created = defaults::create_prefab_at(ctx, *scn, asset_key);
                                                        }
                                                        return created;
                                                    });
             if(!created)
             {
                 return {.text="Failed to instantiate prefab", .is_error=true};
             }
             return {.text=entity_to_json(created, 0, 1), .is_error=false};
         },
         .mutates_scene=true});

    registry.add(
        {.name="scene_create_mesh",
         .description=
             "Create a mesh entity from an asset key (e.g. app:/data/SM_Platform.fbx) via "
             "defaults::create_mesh_entity_at. Optional name/parent_id/position. "
             "position is WORLD space even with parent_id.",
         .input_schema_json=
             R"json({"type":"object","properties":{"asset_key":{"type":"string","description":"Mesh asset key"},"name":{"type":"string"},"parent_id":{"type":"string"},"position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3,"description":"WORLD space [x,y,z] (X-right, Y-up, Z-forward)"}},"required":["asset_key"]})json",
         .handler=[](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text=error, .is_error=true};
             }

             std::string asset_key;
             if(!read_string(args, "asset_key", asset_key) || asset_key.empty())
             {
                 return {.text="Missing asset_key", .is_error=true};
             }

             entt::handle parent{};
             std::string parent_id;
             if(read_string(args, "parent_id", parent_id) && !parent_id.empty())
             {
                 parent = find_entity(*scn, parent_id);
                 if(!parent)
                 {
                     return {.text="Parent not found: " + parent_id, .is_error=true};
                 }
             }

             math::vec3 position{};
             const bool has_position = read_vec3(args, "position", position);

             entt::handle created{};
             auto& em = ctx.get_cached<editing_manager>();
             em.do_action<create_entities_action_t>("MCP Create Mesh",
                                                    [&]()
                                                    {
                                                        created = defaults::create_mesh_entity_at(
                                                            ctx,
                                                            *scn,
                                                            asset_key,
                                                            has_position ? position : math::vec3{0.0f, 0.0f, 0.0f});
                                                        return created;
                                                    });
             if(!created)
             {
                 return {.text="Failed to create mesh entity (check asset_key): " + asset_key, .is_error=true};
             }

             std::string name;
             if(read_string(args, "name", name) && !name.empty())
             {
                 if(auto* tag = created.try_get<tag_component>())
                 {
                     const auto old_name = tag->name;
                     em.do_action<entity_set_name_action_t>("MCP Set Name", created, old_name, name);
                 }
             }
             maybe_set_parent(ctx, created, parent);
             return {.text=entity_to_json(created, 0, 0), .is_error=false};
         },
         .mutates_scene=true});

    registry.add(
        {.name="scene_add_component",
         .description="Add an engine component to an entity. Use scene_list_component_types for valid names.",
         .input_schema_json=R"({"type":"object","properties":{"entity_id":{"type":"string"},"component_type":{"type":"string"}},"required":["entity_id","component_type"]})",
         .handler=[](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text=error, .is_error=true};
             }

             std::string entity_id;
             std::string component_type;
             if(!read_string(args, "entity_id", entity_id) || !read_string(args, "component_type", component_type))
             {
                 return {.text="Missing entity_id or component_type", .is_error=true};
             }

             auto entity = find_entity(*scn, entity_id);
             if(!entity)
             {
                 return {.text="Entity not found: " + entity_id, .is_error=true};
             }

             auto type = resolve_addable_component(component_type);
             if(!type)
             {
                 return {.text="Unknown or non-addable component_type: " + component_type, .is_error=true};
             }

             auto& em = ctx.get_cached<editing_manager>();
             em.do_action<entity_add_component_action_t>("MCP Add Component", entity, type);
             return {.text=entity_to_json(entity, 0, 0), .is_error=false};
         },
         .mutates_scene=true});

    registry.add(
        {.name="scene_remove_component",
         .description="Remove a component from an entity.",
         .input_schema_json=R"({"type":"object","properties":{"entity_id":{"type":"string"},"component_type":{"type":"string"}},"required":["entity_id","component_type"]})",
         .handler=[](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text=error, .is_error=true};
             }

             std::string entity_id;
             std::string component_type;
             if(!read_string(args, "entity_id", entity_id) || !read_string(args, "component_type", component_type))
             {
                 return {.text="Missing entity_id or component_type", .is_error=true};
             }

             auto entity = find_entity(*scn, entity_id);
             if(!entity)
             {
                 return {.text="Entity not found: " + entity_id, .is_error=true};
             }

             auto type = resolve_addable_component(component_type);
             if(!type)
             {
                 // Also allow removing inspectable non-addable? Keep addable-only for safety.
                 hpp::for_each_tuple_type<all_inspectable_components>(
                     [&](auto index)
                     {
                         using ctype = std::tuple_element_t<decltype(index)::value, all_inspectable_components>;
                         auto resolved = entt::resolve<ctype>();
                         if(std::string(entt::get_pretty_name(resolved)) == component_type ||
                            std::string(entt::get_name(resolved)) == component_type)
                         {
                             type = resolved;
                         }
                     });
             }
             if(!type)
             {
                 return {.text="Unknown component_type: " + component_type, .is_error=true};
             }

             auto& em = ctx.get_cached<editing_manager>();
             em.do_action<entity_remove_component_action_t>("MCP Remove Component", entity, type);
             return {.text=entity_to_json(entity, 0, 0), .is_error=false};
         },
         .mutates_scene=true});

    registry.add(
        {.name="scene_set_transform",
         .description=
             "Set transform fields on an entity. Any of position, rotation_euler (degrees [pitch_x,yaw_y,roll_z]), scale. "
             "Axes: X-right, Y-up, Z-forward. "
             "space:\"world\" (default) uses WORLD/global pose; space:\"local\" uses parent-relative "
             "LOCAL pose. Prefer local when parenting children (doors under houses, etc.).",
         .input_schema_json=
             R"({"type":"object","properties":{"entity_id":{"type":"string"},"space":{"type":"string","enum":["world","local"],"default":"world","description":"world=global pose (default); local=parent-relative"},"position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3,"description":"[x,y,z] X-right Y-up Z-forward"},"rotation_euler":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3,"description":"degrees [pitch_x,yaw_y,roll_z]"},"scale":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3}},"required":["entity_id"]})",
         .handler=[](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text=error, .is_error=true};
             }

             std::string entity_id;
             if(!read_string(args, "entity_id", entity_id))
             {
                 return {.text="Missing entity_id", .is_error=true};
             }
             auto entity = find_entity(*scn, entity_id);
             if(!entity || !entity.all_of<transform_component>())
             {
                 return {.text="Entity not found or missing transform", .is_error=true};
             }

             bool is_local = false;
             if(!read_transform_space(args, is_local, error))
             {
                 return {.text=error, .is_error=true};
             }

             apply_transform_fields(ctx, entity, args, is_local);
             return {.text=entity_to_json(entity, 0, 0), .is_error=false};
         },
         .mutates_scene=true});

    registry.add(
        {.name="scene_get_transform",
         .description=
             "Get an entity transform. Axes: X-right, Y-up, Z-forward. "
             "With space:\"world\" or \"local\" returns that space only; "
             "omit space to return both (same fields as scene_list_entities).",
         .input_schema_json=
             R"({"type":"object","properties":{"entity_id":{"type":"string"},"space":{"type":"string","enum":["world","local"]}},"required":["entity_id"]})",
         .handler=[](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& em = ctx.get_cached<editing_manager>();
             auto* scn = em.get_active_scene(ctx);
             if(!scn || !scn->registry)
             {
                 return {.text="No active scene", .is_error=true};
             }

             std::string entity_id;
             if(!read_string(args, "entity_id", entity_id))
             {
                 return {.text="Missing entity_id", .is_error=true};
             }
             auto entity = find_entity(*scn, entity_id);
             if(!entity || !entity.all_of<transform_component>())
             {
                 return {.text="Entity not found or missing transform", .is_error=true};
             }

             std::string space;
             read_string(args, "space", space);
             if(space.empty())
             {
                 return {.text=entity_to_json(entity, 0, 0), .is_error=false};
             }

             bool is_local = false;
             std::string error;
             if(!read_transform_space(args, is_local, error))
             {
                 return {.text=error, .is_error=true};
             }

             auto& t = entity.get<transform_component>();
             const auto pos = is_local ? t.get_position_local() : t.get_position_global();
             const auto rot = is_local ? t.get_rotation_euler_local() : t.get_rotation_euler_global();
             const auto scl = is_local ? t.get_scale_local() : t.get_scale_global();
             return {.text=fmt::format(
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
                         scl.z),
                     .is_error=false};
         },
         .mutates_scene=false});

    registry.add(
        {.name="scene_set_parent",
         .description=
             "Reparent an entity. Pass parent_id to attach, or null/empty parent_id to detach to scene root. "
             "Does not change WORLD position (engine keeps global pose when reparenting).",
         .input_schema_json=
             R"({"type":"object","properties":{"entity_id":{"type":"string"},"parent_id":{"type":"string","description":"Parent entity id, or omit/empty to unparent"}},"required":["entity_id"]})",
         .handler=[](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text=error, .is_error=true};
             }

             std::string entity_id;
             if(!read_string(args, "entity_id", entity_id))
             {
                 return {.text="Missing entity_id", .is_error=true};
             }
             auto entity = find_entity(*scn, entity_id);
             if(!entity || !entity.all_of<transform_component>())
             {
                 return {.text="Entity not found or missing transform", .is_error=true};
             }

             entt::handle new_parent{};
             std::string parent_id;
             if(read_string(args, "parent_id", parent_id) && !parent_id.empty() && parent_id != "null")
             {
                 new_parent = find_entity(*scn, parent_id);
                 if(!new_parent)
                 {
                     return {.text="Parent not found: " + parent_id, .is_error=true};
                 }
             }

             auto& em = ctx.get_cached<editing_manager>();
             auto old_parent = entity.get<transform_component>().get_parent();
             em.do_action<transform_set_parent_action_t>("MCP Set Parent", entity, old_parent, new_parent);
             return {.text=entity_to_json(entity, 0, 0), .is_error=false};
         },
         .mutates_scene=true});

    registry.add(
        {.name="scene_set_name",
         .description="Set an entity display name.",
         .input_schema_json=R"({"type":"object","properties":{"entity_id":{"type":"string"},"name":{"type":"string"}},"required":["entity_id","name"]})",
         .handler=[](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text=error, .is_error=true};
             }
             std::string entity_id;
             std::string name;
             if(!read_string(args, "entity_id", entity_id) || !read_string(args, "name", name))
             {
                 return {.text="Missing entity_id or name", .is_error=true};
             }
             auto entity = find_entity(*scn, entity_id);
             if(!entity || !entity.all_of<tag_component>())
             {
                 return {.text="Entity not found", .is_error=true};
             }
             auto old_name = entity.get<tag_component>().name;
             ctx.get_cached<editing_manager>().do_action<entity_set_name_action_t>("MCP Set Name", entity, old_name, name);
             return {.text=entity_to_json(entity, 0, 0), .is_error=false};
         },
         .mutates_scene=true});

    registry.add(
        {.name="scene_set_active",
         .description="Set an entity active flag.",
         .input_schema_json=R"({"type":"object","properties":{"entity_id":{"type":"string"},"active":{"type":"boolean"}},"required":["entity_id","active"]})",
         .handler=[](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text=error, .is_error=true};
             }
             std::string entity_id;
             bool active = true;
             if(!read_string(args, "entity_id", entity_id) || !read_bool(args, "active", active))
             {
                 return {.text="Missing entity_id or active", .is_error=true};
             }
             auto entity = find_entity(*scn, entity_id);
             if(!entity || !entity.all_of<transform_component>())
             {
                 return {.text="Entity not found", .is_error=true};
             }
             const bool old_active = entity.get<transform_component>().is_active();
             ctx.get_cached<editing_manager>().do_action<entity_set_active_action_t>("MCP Set Active",
                                                                                    entity,
                                                                                    old_active,
                                                                                    active);
             return {.text=entity_to_json(entity, 0, 0), .is_error=false};
         },
         .mutates_scene=true});

    registry.add(
        {.name="scene_delete_entities",
         .description="Delete one or more entities by id.",
         .input_schema_json=R"({"type":"object","properties":{"entity_ids":{"type":"array","items":{"type":"string"}}},"required":["entity_ids"]})",
         .handler=[](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text=error, .is_error=true};
             }

             simdjson::dom::array ids;
             if(args["entity_ids"].get(ids))
             {
                 return {.text="Missing entity_ids", .is_error=true};
             }

             std::vector<entt::handle> entities;
             for(auto el : ids)
             {
                 std::string_view id_view;
                 if(el.get(id_view))
                 {
                     return {.text="entity_ids must be strings", .is_error=true};
                 }
                 auto entity = find_entity(*scn, std::string(id_view));
                 if(!entity)
                 {
                     return {.text="Entity not found: " + std::string(id_view), .is_error=true};
                 }
                 entities.push_back(entity);
             }

             ctx.get_cached<editing_manager>().do_action<delete_entities_action_t>("MCP Delete Entities", entities);
             return {.text=fmt::format(R"({{"deleted":{}}})", entities.size()), .is_error=false};
         },
         .mutates_scene=true});

    registry.add(
        {.name="scene_list_component_types",
         .description="List addable component pretty names for scene_add_component.",
         .input_schema_json=empty_object_schema(),
         .handler=[](rtti::context&, const simdjson::dom::object&) -> tool_result
         {
             std::string json = "[";
             bool first = true;
             hpp::for_each_tuple_type<all_addable_components>(
                 [&](auto index)
                 {
                     using ctype = std::tuple_element_t<decltype(index)::value, all_addable_components>;
                     auto type = entt::resolve<ctype>();
                     if(!first)
                     {
                         json += ",";
                     }
                     first = false;
                     json += make_json_string(std::string(entt::get_pretty_name(type)));
                 });
             json += "]";
             return {.text=json, .is_error=false};
         },
         .mutates_scene=false});

    registry.add(
        {.name = "scene_list_presets",
         .description =
             "List defaults::scene_preset values usable with scene_new_from_preset / project_open "
             "(low, medium, high, showcase).",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context&, const simdjson::dom::object&) -> tool_result
         {
             return {.text = R"(["low","medium","high","showcase"])", .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "scene_save",
         .description =
             "Save the active edit scene to a .spfb via asset_writer::atomic_save_to_file. "
             "Omit key/path to overwrite scene.source; provide key or absolute path for save-as "
             "(sets scene.source). Requires an open project. Refuses play mode and prefab mode.",
         .input_schema_json =
             R"json({"type":"object","properties":{"key":{"type":"string","description":"Asset key e.g. app:/data/Village.spfb"},"path":{"type":"string","description":"Absolute filesystem path to a .spfb"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             if(!require_not_play_mode(ctx, error) || !require_open_project(ctx, error))
             {
                 return {.text = error, .is_error = true};
             }

             auto& em = ctx.get_cached<editing_manager>();
             if(em.is_prefab_mode())
             {
                 return {.text = "Cannot scene_save while in prefab mode", .is_error = true};
             }

             auto& ec = ctx.get_cached<ecs>();
             auto& scene = ec.get_scene();

             std::string key;
             std::string path;
             read_string(args, "key", key);
             read_string(args, "path", path);
             if(key.empty() && !path.empty())
             {
                 key = path;
             }

             if(key.empty())
             {
                 if(!scene.source)
                 {
                     return {.text = "Scene has no source; provide key or path for save-as", .is_error = true};
                 }
                 key = scene.source.id();
             }
             else
             {
                 key = normalize_scene_key(key);
             }

             const auto absolute = fs::absolute(fs::resolve_protocol(key));
             if(!editor_actions::save_scene_to_path(ctx, absolute, true, false))
             {
                 return {.text = "Failed to save scene: " + key, .is_error = true};
             }

             return {.text = fmt::format(R"({{"ok":true,"key":{},"path":{}}})",
                                         make_json_string(scene.source ? scene.source.id() : key),
                                         make_json_string(absolute.generic_string())),
                     .is_error = false};
         },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_open",
         .description =
             "Open a scene asset (.spfb) by key or absolute path. No ImGui save prompt; pass "
             "force:true (default) to discard unsaved changes. Requires an open project.",
         .input_schema_json =
             R"json({"type":"object","properties":{"key":{"type":"string","description":"Asset key e.g. app:/data/MyScene.spfb"},"path":{"type":"string","description":"Absolute filesystem path to a .spfb"},"force":{"type":"boolean","default":true}},"required":[]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             if(!require_not_play_mode(ctx, error) || !require_open_project(ctx, error))
             {
                 return {.text = error, .is_error = true};
             }

             std::string key;
             std::string path;
             read_string(args, "key", key);
             read_string(args, "path", path);
             if(key.empty() && !path.empty())
             {
                 key = path;
             }
             if(key.empty())
             {
                 return {.text = "Provide key or path", .is_error = true};
             }
             key = normalize_scene_key(key);

             bool force = true;
             read_bool(args, "force", force);

             auto& em = ctx.get_cached<editing_manager>();
             if(em.has_unsaved_changes() && !force)
             {
                 return {.text = "Unsaved scene changes; pass force:true to discard", .is_error = true};
             }
             if(force)
             {
                 em.clear_unsaved_changes();
             }

             auto& am = ctx.get_cached<asset_manager>();
             auto asset = am.get_asset<scene_prefab>(key);
             if(!asset)
             {
                 return {.text = "Scene asset not found: " + key, .is_error = true};
             }

             if(!editor_actions::load_scene_from_asset(ctx, asset, &error))
             {
                 return {.text = error, .is_error = true};
             }

             auto* scn = em.get_active_scene(ctx);
             size_t entity_count = 0;
             if(scn && scn->registry)
             {
                 for(auto entity : scn->registry->view<transform_component>())
                 {
                     (void)entity;
                     ++entity_count;
                 }
             }
             return {.text = fmt::format(R"({{"ok":true,"key":{},"entity_count":{}}})",
                                         make_json_string(asset.id()),
                                         entity_count),
                     .is_error = false};
         },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_new_from_preset",
         .description =
             "Create a new unsaved scene from defaults::scene_preset (camera, skylight, probe, "
             "volume). No ImGui modal. Presets: low|medium|high|showcase (default medium). "
             "Requires an open project. force:true (default) discards unsaved changes.",
         .input_schema_json =
             R"({"type":"object","properties":{"preset":{"type":"string","enum":["low","medium","high","showcase"]},"force":{"type":"boolean","default":true}}})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             if(!require_not_play_mode(ctx, error) || !require_open_project(ctx, error))
             {
                 return {.text = error, .is_error = true};
             }

             std::string preset_str = "medium";
             read_string(args, "preset", preset_str);
             defaults::scene_preset preset{};
             if(!defaults::parse_scene_preset(preset_str, preset))
             {
                 return {.text = "Invalid preset (use low|medium|high|showcase)", .is_error = true};
             }

             bool force = true;
             read_bool(args, "force", force);

             auto& em = ctx.get_cached<editing_manager>();
             if(em.has_unsaved_changes() && !force)
             {
                 return {.text = "Unsaved scene changes; pass force:true to discard", .is_error = true};
             }
             if(force)
             {
                 em.clear_unsaved_changes();
             }

             editor_actions::new_scene_from_preset(ctx, preset);

             auto* scn = em.get_active_scene(ctx);
             size_t entity_count = 0;
             if(scn && scn->registry)
             {
                 for(auto entity : scn->registry->view<transform_component>())
                 {
                     (void)entity;
                     ++entity_count;
                 }
             }
             return {.text = fmt::format(R"({{"ok":true,"preset":{},"entity_count":{}}})",
                                         make_json_string(defaults::scene_preset_to_string(preset)),
                                         entity_count),
                     .is_error = false};
         },
         .mutates_scene = true});
}

} // namespace unravel::mcp
