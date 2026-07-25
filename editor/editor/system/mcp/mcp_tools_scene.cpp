#include "mcp_tools_common.h"
#include "mcp_component_utils.h"

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
        {.name = "scene_get_hierarchy_batch",
         .description =
             "Browse scene hierarchy as lean id/name/children nodes. Optional parent_id, max_depth "
             "(default 2), limit (default 200).",
         .input_schema_json =
             R"({"type":"object","properties":{"parent_id":{"type":"string"},"max_depth":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1,"maximum":5000}}})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& em = ctx.get_cached<editing_manager>();
             auto* scn = em.get_active_scene(ctx);
             if(!scn || !scn->registry)
             {
                 return {.text = "No active scene", .is_error = true};
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

             int64_t limit = 200;
             if(args["limit"].get(limit))
             {
                 limit = 200;
             }
             if(limit < 1)
             {
                 limit = 1;
             }

             std::string parent_id;
             read_string(args, "parent_id", parent_id);

             size_t nodes_emitted = 0;
             bool truncated = false;
             std::string json = "[";
             bool first = true;
             auto append = [&](entt::handle entity)
             {
                 if(nodes_emitted >= static_cast<size_t>(limit))
                 {
                     truncated = true;
                     return;
                 }
                 auto node = entity_hierarchy_node_json(entity,
                                                       0,
                                                       static_cast<int>(max_depth),
                                                       nodes_emitted,
                                                       static_cast<size_t>(limit),
                                                       truncated);
                 if(node == "null")
                 {
                     return;
                 }
                 if(!first)
                 {
                     json += ",";
                 }
                 first = false;
                 json += node;
             };

             if(!parent_id.empty())
             {
                 auto parent = find_entity(*scn, parent_id);
                 if(!parent)
                 {
                     return {.text = "Entity not found: " + parent_id, .is_error = true};
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
                     [&](auto, auto&&, auto&& transform)
                     {
                         append(transform.get_owner());
                     });
             }

             json += "]";
             return {.text = fmt::format(R"({{"entities":{},"count":{},"limit":{},"truncated":{}}})",
                                         json,
                                         nodes_emitted,
                                         limit,
                                         truncated ? "true" : "false"),
                     .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "scene_get_entities_batch",
         .description =
             "Get many entities. detail=pose (default), summary (pose + component names), or "
             "components (summary + typed property bags for supported components). Optional "
             "components[] filter when detail=components (pretty names: Light, Skylight, ...).",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_ids":{"type":"array","items":{"type":"string"}},"detail":{"type":"string","enum":["pose","summary","components"]},"components":{"type":"array","items":{"type":"string"}}},"required":["entity_ids"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_active_scene(ctx, scn, error))
             {
                 return {.text = error, .is_error = true};
             }
             simdjson::dom::array ids;
             if(args["entity_ids"].get(ids))
             {
                 return {.text = "Missing entity_ids", .is_error = true};
             }
             std::string detail = "pose";
             read_string(args, "detail", detail);
             if(detail != "pose" && detail != "summary" && detail != "components")
             {
                 return {.text = "Invalid detail (use pose, summary, or components)", .is_error = true};
             }
             std::vector<std::string> component_filter;
             simdjson::dom::array components_arr;
             if(!args["components"].get(components_arr))
             {
                 for(auto el : components_arr)
                 {
                     std::string_view name_view;
                     if(el.get(name_view))
                     {
                         return {.text = "components must be strings", .is_error = true};
                     }
                     component_filter.emplace_back(name_view);
                 }
             }
             std::string json = "[";
             size_t count = 0;
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
                 if(count > 0)
                 {
                     json += ",";
                 }
                 if(detail == "pose")
                 {
                     json += entity_to_pose_json(entity);
                 }
                 else if(detail == "summary")
                 {
                     json += entity_to_summary_json(entity, 0, 0);
                 }
                 else
                 {
                     auto summary = entity_to_summary_json(entity, 0, 0);
                     if(!summary.empty() && summary.back() == '}')
                     {
                         summary.pop_back();
                     }
                     const auto* filter_ptr = component_filter.empty() ? nullptr : &component_filter;
                     json += summary;
                     json += R"(,"component_properties":)";
                     json += entity_supported_component_properties_json(ctx, entity, filter_ptr);
                     json += "}";
                 }
                 ++count;
             }
             json += "]";
             return {.text = fmt::format(R"({{"entities":{},"count":{},"detail":{}}})",
                                         json,
                                         count,
                                         make_json_string(detail)),
                     .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "scene_get_children_batch",
         .description = "Get immediate children (id/name) for many entities.",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_ids":{"type":"array","items":{"type":"string"}}},"required":["entity_ids"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& em = ctx.get_cached<editing_manager>();
             auto* scn = em.get_active_scene(ctx);
             if(!scn || !scn->registry)
             {
                 return {.text = "No active scene", .is_error = true};
             }
             simdjson::dom::array ids;
             if(args["entity_ids"].get(ids))
             {
                 return {.text = "Missing entity_ids", .is_error = true};
             }
             std::string json = "[";
             size_t count = 0;
             for(auto el : ids)
             {
                 std::string_view id_view;
                 if(el.get(id_view))
                 {
                     return {.text = "entity_ids must be strings", .is_error = true};
                 }
                 const auto entity_id = std::string(id_view);
                 auto entity = find_entity(*scn, entity_id);
                 if(!entity)
                 {
                     return {.text = "Entity not found: " + entity_id, .is_error = true};
                 }
                 std::string children = "[";
                 bool first_child = true;
                 if(auto* transform = entity.try_get<transform_component>())
                 {
                     for(auto child : transform->get_children())
                     {
                         if(!first_child)
                         {
                             children += ",";
                         }
                         first_child = false;
                         children += entity_to_lean_json(child, false);
                     }
                 }
                 children += "]";
                 if(count > 0)
                 {
                     json += ",";
                 }
                 json += fmt::format(R"({{"entity_id":{},"children":{}}})", make_json_string(entity_id), children);
                 ++count;
             }
             json += "]";
             return {.text = fmt::format(R"({{"results":{},"count":{}}})", json, count), .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "scene_list_component_properties",
         .description =
             "List MCP-editable typed property schema. Optional component filter (Light, Skylight, "
             "Audio Source, Camera, Volume, Script). Prefer over leaking ser20 JSON blobs.",
         .input_schema_json =
             R"json({"type":"object","properties":{"component":{"type":"string"}}})json",
         .handler =
             [](rtti::context& /*ctx*/, const simdjson::dom::object& args) -> tool_result
         {
             std::string component;
             read_string(args, "component", component);
             return {.text = fmt::format(R"({{"properties":{}}})", list_component_property_schema_json(component)),
                     .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "scene_get_component_properties_batch",
         .description =
             "Read typed properties for supported components. Each item: entity_id, component "
             "(Light|Skylight|Audio Source|Camera|Volume|Script), optional script_type (required for "
             "Script), optional properties[] key filter.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"component":{"type":"string"},"script_type":{"type":"string"},"properties":{"type":"array","items":{"type":"string"}}},"required":["entity_id","component"]}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_active_scene(ctx, scn, error))
             {
                 return {.text = error, .is_error = true};
             }
             simdjson::dom::array items;
             if(args["items"].get(items))
             {
                 return {.text = "Missing items", .is_error = true};
             }
             std::string json = "[";
             size_t count = 0;
             for(auto el : items)
             {
                 simdjson::dom::object obj;
                 if(el.get(obj))
                 {
                     return {.text = "Each item must be an object", .is_error = true};
                 }
                 std::string entity_id;
                 std::string component;
                 if(!read_string(obj, "entity_id", entity_id) || !read_string(obj, "component", component))
                 {
                     return {.text = "Item requires entity_id and component", .is_error = true};
                 }
                 std::string script_type;
                 read_string(obj, "script_type", script_type);
                 std::vector<std::string> property_filter;
                 simdjson::dom::array props_arr;
                 if(!obj["properties"].get(props_arr))
                 {
                     for(auto prop_el : props_arr)
                     {
                         std::string_view name_view;
                         if(prop_el.get(name_view))
                         {
                             return {.text = "properties must be strings", .is_error = true};
                         }
                         property_filter.emplace_back(name_view);
                     }
                 }
                 auto entity = find_entity(*scn, entity_id);
                 if(count > 0)
                 {
                     json += ",";
                 }
                 if(!entity)
                 {
                     json += fmt::format(R"({{"ok":false,"entity_id":{},"component":{},"error":{}}})",
                                         make_json_string(entity_id),
                                         make_json_string(component),
                                         make_json_string("Entity not found: " + entity_id));
                     ++count;
                     continue;
                 }
                 if(!is_supported_component_pretty_name(component))
                 {
                     json += fmt::format(R"({{"ok":false,"entity_id":{},"component":{},"error":{}}})",
                                         make_json_string(entity_id),
                                         make_json_string(component),
                                         make_json_string("Unsupported component for typed properties"));
                     ++count;
                     continue;
                 }
                 if(component == "Script" && script_type.empty())
                 {
                     json += fmt::format(R"({{"ok":false,"entity_id":{},"component":{},"error":{}}})",
                                         make_json_string(entity_id),
                                         make_json_string(component),
                                         make_json_string("script_type is required for Script"));
                     ++count;
                     continue;
                 }
                 const auto* filter_ptr = property_filter.empty() ? nullptr : &property_filter;
                 auto props = component_properties_to_json(ctx, entity, component, script_type, filter_ptr, error);
                 if(props.empty())
                 {
                     json += fmt::format(R"({{"ok":false,"entity_id":{},"component":{},"error":{}}})",
                                         make_json_string(entity_id),
                                         make_json_string(component),
                                         make_json_string(error));
                 }
                 else
                 {
                     json += fmt::format(R"({{"ok":true,"entity_id":{},"component":{},"properties":{}}})",
                                         make_json_string(entity_id),
                                         make_json_string(component),
                                         props);
                 }
                 ++count;
             }
             json += "]";
             return {.text = fmt::format(R"({{"results":{},"count":{}}})", json, count), .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "scene_set_component_properties_batch",
         .description =
             "Set typed properties on supported components in one undoable action. Each item: "
             "entity_id, component, properties object; script_type required for Script. Component must "
             "already exist (use scene_add_components_batch / scene_add_scripts_batch first). Hierarchy "
             "parent/children stay on transform tools — not here.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"component":{"type":"string"},"script_type":{"type":"string"},"properties":{"type":"object"}},"required":["entity_id","component","properties"]}}},"required":["items"]})json",
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
             if(args["items"].get(items))
             {
                 return {.text = "Missing items", .is_error = true};
             }
             struct entry
             {
                 entt::handle entity{};
                 std::string component;
                 std::string script_type;
                 std::string new_props_json;
                 std::string old_props_json;
             };
             auto apply_props_json = [&ctx](entt::handle entity,
                                            const std::string& component,
                                            const std::string& script_type,
                                            const std::string& props_json,
                                            std::string& apply_error) -> bool
             {
                 simdjson::dom::parser parser;
                 simdjson::dom::element root;
                 if(parser.parse(props_json).get(root))
                 {
                     apply_error = "Failed to parse properties JSON";
                     return false;
                 }
                 simdjson::dom::object props;
                 if(root.get(props))
                 {
                     apply_error = "properties must be an object";
                     return false;
                 }
                 auto result = apply_component_properties(ctx, entity, component, script_type, props);
                 if(!result.ok)
                 {
                     apply_error = component_apply_result_to_json(result);
                     return false;
                 }
                 return true;
             };
             std::vector<entry> entries;
             for(auto el : items)
             {
                 simdjson::dom::object obj;
                 if(el.get(obj))
                 {
                     return {.text = "Each item must be an object", .is_error = true};
                 }
                 std::string entity_id;
                 std::string component;
                 if(!read_string(obj, "entity_id", entity_id) || !read_string(obj, "component", component))
                 {
                     return {.text = "Item requires entity_id and component", .is_error = true};
                 }
                 std::string script_type;
                 read_string(obj, "script_type", script_type);
                 simdjson::dom::object properties;
                 if(obj["properties"].get(properties))
                 {
                     return {.text = "Item requires properties object", .is_error = true};
                 }
                 auto entity = find_entity(*scn, entity_id);
                 if(!entity)
                 {
                     return {.text = "Entity not found: " + entity_id, .is_error = true};
                 }
                 if(!is_supported_component_pretty_name(component))
                 {
                     return {.text = "Unsupported component for typed properties: " + component, .is_error = true};
                 }
                 if(component == "Script" && script_type.empty())
                 {
                     return {.text = "script_type is required for Script", .is_error = true};
                 }
                 std::vector<std::string> changed_keys;
                 for(auto field_el : properties)
                 {
                     const std::string key(field_el.key);
                     if(key != "script_type")
                     {
                         changed_keys.push_back(key);
                     }
                 }
                 const auto* filter_ptr = changed_keys.empty() ? nullptr : &changed_keys;
                 auto old_props = component_properties_to_json(ctx, entity, component, script_type, filter_ptr, error);
                 if(old_props.empty())
                 {
                     return {.text = error.empty() ? ("Failed to read properties before set: " + component) : error,
                             .is_error = true};
                 }
                 const std::string new_props = std::string(simdjson::minify(obj["properties"]));
                 std::string apply_error;
                 if(!apply_props_json(entity, component, script_type, new_props, apply_error))
                 {
                     (void)apply_props_json(entity, component, script_type, old_props, apply_error);
                     return {.text = apply_error.empty() ? ("Failed to apply properties: " + component) : apply_error,
                             .is_error = true};
                 }
                 (void)apply_props_json(entity, component, script_type, old_props, apply_error);
                 entry e{};
                 e.entity = entity;
                 e.component = std::move(component);
                 e.script_type = std::move(script_type);
                 e.new_props_json = new_props;
                 e.old_props_json = std::move(old_props);
                 entries.push_back(std::move(e));
             }
             if(entries.empty())
             {
                 return {.text = "No items to apply", .is_error = true};
             }
             auto& em = ctx.get_cached<editing_manager>();
             em.do_action(
                 "MCP Batch Set Component Properties",
                 [entries, apply_props_json]()
                 {
                     for(const auto& e : entries)
                     {
                         std::string apply_error;
                         (void)apply_props_json(e.entity, e.component, e.script_type, e.new_props_json, apply_error);
                     }
                 },
                 [entries, apply_props_json]()
                 {
                     for(const auto& e : entries)
                     {
                         std::string apply_error;
                         (void)apply_props_json(e.entity, e.component, e.script_type, e.old_props_json, apply_error);
                     }
                 });
             return {.text = fmt::format(R"({{"ok":true,"count":{}}})", entries.size()), .is_error = false};
         },
         .mutates_scene = true});

    registry.add(
        {.name="scene_create_light",
         .description=
             "Create a light entity. Args: light_type (directional|point|spot), name, optional parent_id/position (WORLD).",
             .input_schema_json=R"json({"type":"object","properties":{"light_type":{"type":"string"},"name":{"type":"string"},"parent_id":{"type":"string"},"position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3}},"required":["light_type","name"]})json",
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
             return {.text = entity_to_lean_json(created, true), .is_error = false};
         },
         .mutates_scene=true});

    registry.add(
        {.name="scene_create_camera",
         .description="Create a camera entity. Args: name, optional parent_id/position (WORLD).",
         .input_schema_json=R"json({"type":"object","properties":{"name":{"type":"string"},"parent_id":{"type":"string"},"position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3}},"required":["name"]})json",
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
             return {.text = entity_to_lean_json(created, true), .is_error = false};
         },
         .mutates_scene=true});

    registry.add(
        {.name="scene_delete_entities_batch",
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
         .description="List addable component pretty names for scene_add_components_batch.",
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
         .description = "List scene presets: low, medium, high, showcase.",
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
             "Save the active edit scene to .spfb. Omit key/path to overwrite source; else save-as. "
             "Requires open project; refuses play/prefab mode.",
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
             "Open a .spfb scene by key or path. force:true (default) discards unsaved changes.",
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
             "Create a new unsaved scene from preset (low|medium|high|showcase, default medium). "
             "force:true (default) discards unsaved changes.",
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
