#include "mcp_tools_common.h"
#include "mcp_asset_lookup.h"
#include "mcp_component_utils.h"
#include "mcp_meta_properties.h"
#include "mcp_project_paths.h"

#include <engine/ecs/components/layer_component.h>

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

/// What each preset creates, read by building it into a scratch scene with the same function
/// scene_new_from_preset uses - so the description cannot drift from the presets themselves.
auto describe_scene_presets(rtti::context& ctx) -> std::string
{
    const std::vector<std::pair<std::string, std::vector<std::string>>> settings_of_interest{
        {"Skylight", {"mode", "cloud_mode"}},
        {"Light", {"type", "shadow_type", "shadow_resolution", "contact_shadow_enabled"}},
    };
    std::string json = "[";
    bool first_preset = true;
    for(const auto preset : {defaults::scene_preset::low,
                             defaults::scene_preset::medium,
                             defaults::scene_preset::high,
                             defaults::scene_preset::showcase})
    {
        scene preview{"mcp_preset_preview"};
        defaults::create_scene_from_preset(ctx, preview, preset);

        std::string entities = "[";
        bool first_entity = true;
        preview.registry->view<tag_component>().each(
            [&](entt::entity id, tag_component& tag)
            {
                auto entity = preview.create_handle(id);
                simdjson::dom::parser parser;
                simdjson::dom::object summary;
                std::string components = "[]";
                const auto summary_json = entity_to_summary_json(entity, 0, 0);
                if(!parser.parse(summary_json).get(summary))
                {
                    simdjson::dom::element list;
                    if(!summary["components"].get(list))
                    {
                        components = std::string(simdjson::minify(list));
                    }
                }
                std::string settings;
                for(const auto& [component, keys] : settings_of_interest)
                {
                    std::string error;
                    const auto props = component_properties_to_json(ctx, entity, component, {}, &keys, error);
                    if(!props.empty())
                    {
                        settings += (settings.empty() ? "" : ",") + make_json_string(component) + ":" + props;
                    }
                }
                if(!first_entity)
                {
                    entities += ",";
                }
                first_entity = false;
                entities += fmt::format(R"({{"name":{},"components":{}{}}})",
                                        make_json_string(tag.name),
                                        components,
                                        settings.empty() ? std::string{} : R"(,"settings":{)" + settings + "}");
            });
        entities += "]";
        preview.unload();

        if(!first_preset)
        {
            json += ",";
        }
        first_preset = false;
        json += fmt::format(R"({{"preset":{},"entities":{}}})",
                            make_json_string(defaults::scene_preset_to_string(preset)),
                            entities);
    }
    json += "]";
    return fmt::format(R"({{"presets":{}}})", json);
}

/// The entity summary plus the layers it is on, named from the project's layer settings, and its
/// scripts by type and index - the index script_index takes.
auto summary_with_layers(rtti::context& ctx, entt::handle entity) -> std::string
{
    auto summary = entity_to_summary_json(entity, 0, 0);
    if(summary.empty() || summary.back() != '}')
    {
        return summary;
    }
    summary.pop_back();
    if(const auto* layer = entity.try_get<layer_component>())
    {
        summary += R"(,"layers":)" + layer_mask_to_json(ctx, layer->layers);
    }
    if(entity.all_of<script_component>())
    {
        summary += R"(,"scripts":)" + scripts_summary_json(entity);
    }
    return summary + "}";
}

/// Writes @p props_json, an object of properties, through the component's setters.
auto apply_properties_json(rtti::context& ctx,
                           entt::handle entity,
                           const std::string& component,
                           const script_selector& script,
                           const std::string& props_json) -> component_apply_result
{
    simdjson::dom::parser parser;
    simdjson::dom::object props;
    if(parser.parse(props_json).get(props) != simdjson::SUCCESS)
    {
        return {.errors = {"properties must be an object"}, .ok = false};
    }
    return apply_component_properties(ctx, entity, component, script, props);
}

/**
 * @brief Records an edit already made to one script's fields.
 *
 * Undone and redone through the fields themselves (loading a script component rebuilds its
 * scripts); @p state_before, the script component before the edit, tells which fields became
 * prefab overrides.
 */
auto make_script_fields_action(rtti::context& ctx,
                               entt::handle entity,
                               const script_selector& script,
                               std::string old_fields,
                               std::string new_fields,
                               std::string state_before) -> std::shared_ptr<editing_action_t>
{
    struct edit_state
    {
        bool is_edit_live{true};
        prefab_override_record overrides;
    };
    auto state = std::make_shared<edit_state>();
    const auto handle = entt::make_uhandle(entity);
    const auto script_type = entt::resolve<script_component>();
    auto action = std::make_shared<tracked_lambda_action_t>(
        [&ctx, handle, script, new_fields, state_before, script_type, state]()
        {
            const auto entity = handle.resolve();
            if(!entity)
            {
                return;
            }
            if(!state->is_edit_live)
            {
                (void)apply_properties_json(ctx, entity, "Script", script, new_fields);
            }
            state->is_edit_live = false;
            state->overrides = prefab_override_context::mark_changed_properties(entity, script_type, state_before);
        },
        [&ctx, handle, script, old_fields, state]()
        {
            const auto entity = handle.resolve();
            if(!entity)
            {
                return;
            }
            (void)apply_properties_json(ctx, entity, "Script", script, old_fields);
            prefab_override_context::restore_overrides(state->overrides);
            state->overrides = {};
        });
    action->name = "Edit Script " + script.type;
    return action;
}

/// The names of the properties an item sets.
auto property_names(const simdjson::dom::object& properties) -> std::vector<std::string>
{
    std::vector<std::string> names;
    for(auto field : properties)
    {
        names.emplace_back(field.key);
    }
    return names;
}

} // namespace

void register_scene_tools(mcp_tool_registry& registry)
{
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
             scene* scn = nullptr;
             std::string error;
             if(!require_active_scene(ctx, scn, error))
             {
                 return {.text = error, .is_error = true};
             }

             const int64_t max_depth = read_clamped_int64(args, "max_depth", 2, 0);
             const int64_t limit = read_clamped_int64(args, "limit", 200, 1);

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
                 auto parent = resolve_entity(*scn, parent_id, error);
                 if(!parent)
                 {
                     return {.text = error, .is_error = true};
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
                                         bool_to_json(truncated)),
                     .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "scene_get_entities_batch",
         .description =
             "Get many entities. detail pose (default), summary (pose + component names, layers, scripts, "
             "child_count) or components (summary + property bags; narrow with components[], names from "
             "scene_list_components).",
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
             std::vector<entt::handle> entities;
             if(!read_entity_ids(args, *scn, entities, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::string detail = "pose";
             read_string(args, "detail", detail);
             if(detail != "pose" && detail != "summary" && detail != "components")
             {
                 return {.text = "Invalid detail (use pose, summary, or components)", .is_error = true};
             }
             std::vector<std::string> component_filter;
             if(!read_string_array(args, "components", component_filter, error, false))
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
                 if(detail == "pose")
                 {
                     json += entity_to_pose_json(entity);
                 }
                 else if(detail == "summary")
                 {
                     json += summary_with_layers(ctx, entity);
                 }
                 else
                 {
                     auto summary = summary_with_layers(ctx, entity);
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
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "scene_list_components",
         .description =
             "Without component: the components scene_add_components_batch adds, each with how many "
             "properties it has (C# scripts are component Script). With component (a name from that list, "
             "or Script): its properties from the reflection - names, types, enum values, ranges, read-only - "
             "as scene_get/set_component_properties_batch read and write them.",
         .input_schema_json = R"json({"type":"object","properties":{"component":{"type":"string"}}})json",
         .handler =
             [](rtti::context&, const simdjson::dom::object& args) -> tool_result
         {
             std::string component;
             if(!read_string(args, "component", component) || component.empty())
             {
                 return {.text = fmt::format(R"({{"components":{}}})", list_components_json()), .is_error = false};
             }
             if(!is_supported_component_pretty_name(component))
             {
                 return {.text = "Unknown component: " + component + " (call without component for the list)",
                         .is_error = true};
             }
             return {.text = fmt::format(R"({{"component":{},"properties":{}}})",
                                         make_json_string(component),
                                         list_component_property_schema_json(component)),
                     .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "scene_get_component_properties_batch",
         .description =
             "Read component properties (a component from scene_list_components, or Script). Each "
             "item: entity_id, component, optional type_name "
             "(and script_index, from 0, for the second script of a type and on) "
             "(required for Script), optional names: only these properties (read even while hidden).",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"component":{"type":"string"},"type_name":{"type":"string"},"script_index":{"type":"integer","minimum":0,"description":"Which script of that type, from 0 (default 0)"},"names":{"type":"array","items":{"type":"string"}}},"required":["entity_id","component"]}}},"required":["items"]})json",
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
             if(!read_required_array(args, "items", items, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::string json = "[";
             size_t count = 0;
             for(auto el : items)
             {
                 simdjson::dom::object obj;
                 if(!read_object(el, obj, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 std::string entity_id;
                 std::string component;
                 if(!read_string(obj, "entity_id", entity_id) || !read_string(obj, "component", component))
                 {
                     return {.text = "Item requires entity_id and component", .is_error = true};
                 }
                 script_selector script;
                 if(!read_script_selector(obj, script, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 std::vector<std::string> property_filter;
                 if(!read_string_array(obj, "names", property_filter, error, false))
                 {
                     return {.text = error, .is_error = true};
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
                 if(component == "Script" && script.type.empty())
                 {
                     json += fmt::format(R"({{"ok":false,"entity_id":{},"component":{},"error":{}}})",
                                         make_json_string(entity_id),
                                         make_json_string(component),
                                         make_json_string("type_name is required for Script"));
                     ++count;
                     continue;
                 }
                 const auto* filter_ptr = property_filter.empty() ? nullptr : &property_filter;
                 auto props = component_properties_to_json(ctx, entity, component, script, filter_ptr, error);
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
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "scene_set_component_properties_batch",
         .description =
             "Set component properties (names, types, ranges and enums: scene_list_components), all items in one "
             "undo step. Script also takes type_name (and script_index for the second script of a type and on). A "
             "partial nested object merges into the current value; a list takes the array's length. Any rejected "
             "value or unknown name rejects the whole call and nothing changes. The component must exist "
             "(scene_add_components_batch). On a prefab instance each changed property becomes an override. "
             "Transforms and parents have their own tools (scene_set_transforms_batch, scene_set_parents_batch).",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"component":{"type":"string"},"type_name":{"type":"string"},"script_index":{"type":"integer","minimum":0,"description":"Which script of that type, from 0 (default 0)"},"properties":{"type":"object"}},"required":["entity_id","component","properties"]}}},"required":["items"]})json",
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
             // Each item is written through the component's setters once, as it is read, so a later item
             // builds on an earlier one; the edits are recorded as made. A rejected item puts back every
             // component edited so far, newest first.
             auto edits = std::make_shared<composite_action_t>();
             const auto reject = [&edits](const std::string& message) -> tool_result
             {
                 edits->undo_action();
                 return {.text = message, .is_error = true};
             };
             std::vector<std::string> warnings;
             for(auto el : items)
             {
                 simdjson::dom::object obj;
                 if(!read_object(el, obj, error))
                 {
                     return reject(error);
                 }
                 std::string entity_id;
                 std::string component;
                 if(!read_string(obj, "entity_id", entity_id) || !read_string(obj, "component", component))
                 {
                     return reject("Item requires entity_id and component");
                 }
                 script_selector script;
                 if(!read_script_selector(obj, script, error))
                 {
                     return reject(error);
                 }
                 simdjson::dom::object properties;
                 if(obj["properties"].get(properties))
                 {
                     return reject("Item requires properties object");
                 }
                 auto entity = find_entity(*scn, entity_id);
                 if(!entity)
                 {
                     return reject("Entity not found: " + entity_id);
                 }
                 if(!is_supported_component_pretty_name(component))
                 {
                     return reject("Unsupported component for typed properties: " + component);
                 }
                 const bool is_script = component == "Script";
                 if(is_script && script.type.empty())
                 {
                     return reject("type_name is required for Script");
                 }
                 const auto component_type = component_meta_type(component);
                 const auto state_before = prefab_override_context::save_component_state(entity, component_type);
                 if(state_before.empty())
                 {
                     return reject(component_missing_error(component));
                 }
                 std::string old_fields;
                 if(is_script)
                 {
                     const auto names = property_names(properties);
                     old_fields = component_properties_to_json(ctx, entity, component, script, &names, error);
                     if(old_fields.empty())
                     {
                         return reject(error);
                     }
                 }
                 const auto result = apply_component_properties(ctx, entity, component, script, properties);
                 // Recorded whether or not it was accepted: a rejected item may have written some of its
                 // properties, and the reject puts them back with the rest.
                 edits->add_sub_action(is_script ? make_script_fields_action(ctx,
                                                                             entity,
                                                                             script,
                                                                             old_fields,
                                                                             std::string(simdjson::minify(properties)),
                                                                             state_before)
                                                 : std::make_shared<entity_component_state_action_t>(entity,
                                                                                                     component_type,
                                                                                                     state_before));
                 if(!result.ok)
                 {
                     return reject(component_apply_result_to_json(result));
                 }
                 if(!result.read_only.empty())
                 {
                     warnings.push_back(fmt::format("{} {}: read-only, left as is: {}",
                                                    entity_id,
                                                    component,
                                                    fmt::join(result.read_only, ", ")));
                 }
             }
             if(edits->sub_actions.empty())
             {
                 return {.text = "No items to apply", .is_error = true};
             }
             const auto count = edits->sub_actions.size();
             ctx.get_cached<editing_manager>().do_action("MCP Set Component Properties", edits);
             return {.text = fmt::format(R"({{"ok":true,"count":{}}})", count),
                     .is_error = false,
                     .warnings = std::move(warnings)};
         },
         .mutates_scene = true});

    registry.add(
        {.name="scene_delete_entities_batch",
         .description=
             "Delete entities with everything under them, in one undo step. On a prefab instance the deletion is "
             "recorded as a removal.",
         .input_schema_json=R"({"type":"object","properties":{"entity_ids":{"type":"array","items":{"type":"string"}}},"required":["entity_ids"]})",
         .handler=[](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             scene* scn = nullptr;
             std::string error;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text=error, .is_error=true};
             }

             std::vector<entt::handle> entities;
             if(!read_entity_ids(args, *scn, entities, error))
             {
                 return {.text=error, .is_error=true};
             }

             ctx.get_cached<editing_manager>().do_action<delete_entities_action_t>("MCP Delete Entities", entities);
             return {.text=fmt::format(R"({{"deleted":{}}})", entities.size()), .is_error=false};
         },
         .mutates_scene=true});

    registry.add(
        {.name = "scene_list_presets",
         .description =
             "List the scene presets scene_new_from_preset and project_create accept, each with what it creates: "
             "the entities, their components, and the lighting settings that differ between presets. Pick the "
             "cheapest preset that has what the game needs, or build from an empty scene.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
         {
             // Presets are fixed for a build, so each is built once and described from then on.
             static std::string cached;
             if(cached.empty())
             {
                 cached = describe_scene_presets(ctx);
             }
             return {.text = cached, .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "scene_save",
         .description =
             "Save the active edit scene. Omit key to overwrite the scene's own file; else save-as "
             "(a missing folder is created). Writes only inside the project's data folder. Refuses "
             "play and prefab mode.",
         .input_schema_json =
             R"json({"type":"object","properties":{"key":{"type":"string","description":"app:/data/... key or a path relative to app:/data, e.g. Levels/Village (.spfb is added)"}}})json",
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

             std::string requested;
             read_string(args, "key", requested);
             if(requested.empty())
             {
                 if(!scene.source)
                 {
                     return {.text = "Scene has no file yet; provide key for save-as", .is_error = true};
                 }
                 requested = scene.source.id();
             }
             std::string key;
             if(!to_project_data_file_key(ctx, requested, ex::get_suported_formats<scene_prefab>(), key, error))
             {
                 return {.text = error, .is_error = true};
             }

             const auto absolute = fs::absolute(fs::resolve_protocol(key));
             fs::error_code folder_error;
             fs::create_directories(absolute.parent_path(), folder_error);
             if(folder_error)
             {
                 return {.text = "Failed to create the scene's folder: " + folder_error.message(), .is_error = true};
             }
             if(!editor_actions::save_scene_to_path(ctx, absolute, true, false))
             {
                 return {.text = "Failed to save scene: " + key, .is_error = true};
             }

             return {.text = fmt::format(R"({{"ok":true,"key":{},"path":{}}})",
                                         make_json_string(scene.source ? scene.source.id() : key),
                                         make_json_string(absolute.generic_string())),
                     .is_error = false};
         },
         .mutates_scene = true,
         .effect = tool_effect::destructive,
         .idempotent = true});

    registry.add(
        {.name = "scene_open",
         .description =
             "Open a scene by key or uid. Refuses when the open scene or prefab has unsaved edits, "
             "unless force:true discards them.",
         .input_schema_json =
             R"json({"type":"object","properties":{"key":{"type":"string","description":"Scene key or uid, e.g. app:/data/MyScene.spfb"},"force":{"type":"boolean","default":false,"description":"Discard unsaved scene and prefab edits"}},"required":["key"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             if(!require_not_play_mode(ctx, error) || !require_open_project(ctx, error))
             {
                 return {.text = error, .is_error = true};
             }

             std::string key;
             if(!read_string(args, "key", key) || key.empty())
             {
                 return {.text = "Provide key", .is_error = true};
             }
             auto asset = find_existing_asset<scene_prefab>(ctx, to_asset_key(key), error);
             if(!asset)
             {
                 return {.text = error, .is_error = true};
             }
             bool force = false;
             read_bool(args, "force", force);
             if(!release_unsaved_work(ctx, force, error))
             {
                 return {.text = error, .is_error = true};
             }

             auto& em = ctx.get_cached<editing_manager>();
             if(!editor_actions::load_scene_from_asset(ctx, asset, &error))
             {
                 return {.text = error, .is_error = true};
             }

             auto* scn = em.get_active_scene(ctx);
             const size_t entity_count = scn && scn->registry ? count_live_entities(*scn->registry) : 0;
             return {.text = fmt::format(R"({{"ok":true,"key":{},"entity_count":{}}})",
                                         make_json_string(asset.id()),
                                         entity_count),
                     .is_error = false};
         },
         .mutates_scene = true,
         .effect = tool_effect::destructive,
         .idempotent = true});

    registry.add(
        {.name = "scene_new_from_preset",
         .description =
             "Create a new unsaved scene from a preset (scene_list_presets; default medium). Refuses when the "
             "open scene or prefab has unsaved edits, unless force:true discards them.",
         .input_schema_json =
             R"({"type":"object","properties":{"preset":{"type":"string","enum":["low","medium","high","showcase"]},"force":{"type":"boolean","default":false,"description":"Discard unsaved scene and prefab edits"}}})",
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

             bool force = false;
             read_bool(args, "force", force);
             if(!release_unsaved_work(ctx, force, error))
             {
                 return {.text = error, .is_error = true};
             }

             auto& em = ctx.get_cached<editing_manager>();
             editor_actions::new_scene_from_preset(ctx, preset);

             auto* scn = em.get_active_scene(ctx);
             const size_t entity_count = scn && scn->registry ? count_live_entities(*scn->registry) : 0;
             return {.text = fmt::format(R"({{"ok":true,"preset":{},"entity_count":{}}})",
                                         make_json_string(defaults::scene_preset_to_string(preset)),
                                         entity_count),
                     .is_error = false};
         },
         .mutates_scene = true,
         .effect = tool_effect::destructive});
}

} // namespace unravel::mcp
