#include "mcp_material_utils.h"
#include "mcp_tools_common.h"

#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/material.h>
#include <engine/rendering/model.h>

#include <functional>
#include <utility>

namespace unravel::mcp
{
namespace
{

/// The do and undo of replacing an entity's model with another: each puts one of the two in place and
/// records the material change as the inspector does. The entity is held by its uuid, so the step still
/// finds it after it was deleted and restored.
auto make_model_swap(entt::handle entity, const model& before, const model& after)
    -> std::pair<std::function<void()>, std::function<void()>>
{
    const auto handle = entt::make_uhandle(entity);
    const auto put = [handle](const model& value) -> std::function<void()>
    {
        return [handle, value]()
        {
            const auto target = handle.resolve();
            if(auto* model_comp = target ? target.try_get<model_component>() : nullptr)
            {
                model_comp->set_model(value);
                prefab_override_context::mark_material_as_changed(target);
            }
        };
    };
    return {put(after), put(before)};
}

} // namespace

void register_material_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "scene_set_model_material_instances_batch",
         .description =
             "Give model slots their own material: each item's properties (assets_list_properties type material) "
             "edit a per-entity instance of the slot's material, starting from the slot's material asset "
             "the first time; the .mat file and other renderers are untouched. Each item: entity_id, "
             "properties, optional index (slot, default 0). All or nothing per item; one undo step.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"index":{"type":"integer","minimum":0},"properties":{"type":"object"}},"required":["entity_id","properties"]}}},"required":["items"]})json",
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
                 uint32_t index{0};
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
                 int64_t index_i = 0;
                 if(!obj["index"].get(index_i) && index_i >= 0)
                 {
                     item.index = static_cast<uint32_t>(index_i);
                 }
                 if(obj["properties"].error())
                 {
                     return {.text = "Item missing properties object", .is_error = true};
                 }
                 item.props_json = std::string(simdjson::minify(obj["properties"]));
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
                 const auto slot_count = std::max<size_t>(1, old_model.get_materials().size());
                 if(item.index >= slot_count)
                 {
                     results += fmt::format(R"({{"ok":false,"entity_id":{},"error":{}}})",
                                            make_json_string(item.entity_id),
                                            make_json_string(fmt::format("index {} is outside the model's material slots 0..{}",
                                                                         item.index,
                                                                         slot_count - 1)));
                     continue;
                 }
                 // The instance is edited as a copy: the model it replaces keeps the instance it shares, so undo
                 // puts the old values back, and a rejected edit changes nothing.
                 auto new_model = old_model;
                 auto instance = new_model.get_or_emplace_material_instance(item.index)->clone();
                 auto applied = apply_material_properties(ctx, *instance, properties);
                 if(!applied.ok)
                 {
                     results += fmt::format(R"({{"ok":false,"entity_id":{},"result":{}}})",
                                            make_json_string(item.entity_id),
                                            apply_result_to_json(applied));
                     continue;
                 }
                 new_model.set_material_instance(instance, item.index);
                 const auto swap = make_model_swap(entity, old_model, new_model);
                 em.do_action("MCP Set Model Material Instance", swap.first, swap.second);
                 ++ok_count;
                 results += fmt::format(R"({{"ok":true,"entity_id":{},"index":{},"result":{}}})",
                                        make_json_string(entity_id_string(entity)),
                                        item.index,
                                        apply_result_to_json(applied));
             }
             results += "]";
             return {.text = fmt::format(R"({{"results":{},"count":{},"requested":{}}})", results, ok_count, items.size()),
                     .is_error = ok_count == 0};
         },
         .mutates_scene = true});

    registry.add(
        {.name = "scene_clear_model_material_instances_batch",
         .description =
             "Clear runtime material instances on model slots (batch) so shared assets are used again.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"index":{"type":"integer","minimum":0}},"required":["entity_id"]}}},"required":["items"]})json",
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
                 uint32_t index = 0;
                 int64_t index_i = 0;
                 if(!obj["index"].get(index_i) && index_i >= 0)
                 {
                     index = static_cast<uint32_t>(index_i);
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
                 auto new_model = old_model;
                 new_model.set_material_instance(nullptr, index);
                 const auto swap = make_model_swap(entity, old_model, new_model);
                 em.do_action("MCP Clear Model Material Instance", swap.first, swap.second);
                 ++ok_count;
                 results += fmt::format(R"({{"ok":true,"entity_id":{},"index":{},"cleared":true}})",
                                        make_json_string(entity_id_string(entity)),
                                        index);
             }
             results += "]";
             return {.text = fmt::format(R"({{"results":{},"count":{},"requested":{}}})", results, ok_count, requested),
                     .is_error = ok_count == 0 && requested > 0};
         },
         .mutates_scene = true,
         .idempotent = true});
}

} // namespace unravel::mcp

