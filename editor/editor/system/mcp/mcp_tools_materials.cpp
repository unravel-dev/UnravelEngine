#include "mcp_async.h"
#include "mcp_material_utils.h"
#include "mcp_tools_common.h"

#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <editor/system/mcp_manager.h>
#include <editor/system/project_manager.h>
#include <engine/assets/asset_manager.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/material.h>
#include <engine/rendering/model.h>
#include <filesystem/filesystem.h>

#include <chrono>
#include <thread>

namespace unravel::mcp
{

void register_material_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "materials_list_properties",
         .description = "List supported PBR material property names/types for materials_set and "
                        "scene_set_model_material_instances_batch.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context&, const simdjson::dom::object&) -> tool_result
         {
             return {.text = list_material_property_schema_json(), .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "materials_set",
         .description =
             "Set PBR material asset properties by key/uid. `properties` is an object of supported "
             "keys. Saves to disk by default (inspector parity). Set save:false for in-memory only.",
         .input_schema_json =
             R"({"type":"object","properties":{"key":{"type":"string"},"uid":{"type":"string"},"properties":{"type":"object"},"save":{"type":"boolean","default":true},"wait_ms":{"type":"integer","minimum":0,"maximum":15000}},"required":["properties"]})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             const auto wait_ms = read_wait_ms(args, 1000);

             std::string key;
             std::string uid;
             read_string(args, "key", key);
             read_string(args, "uid", uid);

             bool save = true;
             read_bool(args, "save", save);

             simdjson::dom::object properties;
             if(args["properties"].get(properties))
             {
                 return {.text = "Missing properties object", .is_error = true};
             }

             // Copy args JSON fragment by re-parsing is awkward; apply on main with captured strings
             // and re-get properties from a serialized form.
             const std::string props_json = std::string(simdjson::minify(args["properties"]));

             auto set_result = mcp.invoke_on_main(
                 [&ctx, key, uid, save, props_json]() -> tool_result
                 {
                     std::string error;
                     auto handle = resolve_material_asset(ctx, key, uid, error);
                     if(!handle)
                     {
                         return {.text = error, .is_error = true};
                     }

                     auto mat = handle.get();
                     if(!mat)
                     {
                         return {.text = "Material asset not loaded", .is_error = true};
                     }

                     simdjson::dom::parser parser;
                     simdjson::dom::element root;
                     if(parser.parse(props_json).get(root))
                     {
                         return {.text = "Failed to parse properties", .is_error = true};
                     }
                     simdjson::dom::object props;
                     if(root.get(props))
                     {
                         return {.text = "properties must be an object", .is_error = true};
                     }

                     auto applied = apply_material_properties(ctx, mat, props);
                     if(!applied.ok)
                     {
                         return {.text = apply_result_to_json(applied), .is_error = true};
                     }

                     bool saved = false;
                     if(save)
                     {
                         if(!save_material_asset(ctx, handle, error))
                         {
                             return {.text = error, .is_error = true};
                         }
                         saved = true;
                     }

                     return {.text = fmt::format(R"({{"key":{},"uid":{},"saved":{},"result":{}}})",
                                                 make_json_string(handle.id()),
                                                 make_json_string(hpp::to_string(handle.uid())),
                                                 saved ? "true" : "false",
                                                 apply_result_to_json(applied)),
                             .is_error = false};
                 });

             if(!set_result)
             {
                 return {.text = "Timed out setting material on main thread", .is_error = true};
             }
             if(!set_result->is_error && save)
             {
                 sleep_worker(wait_ms);
             }
             return *set_result;
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "materials_get_batch",
         .description = "Read PBR material properties for many assets. Each item: key and/or uid.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"key":{"type":"string"},"uid":{"type":"string"}}}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             simdjson::dom::array items_arr;
             if(!read_required_array(args, "items", items_arr, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::string results = "[";
             bool first = true;
             size_t ok_count = 0;
             size_t requested = 0;
             for(auto el : items_arr)
             {
                 ++requested;
                 simdjson::dom::object obj;
                 if(!read_object(el, obj, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 std::string key;
                 std::string uid;
                 read_string(obj, "key", key);
                 read_string(obj, "uid", uid);
                 auto handle = resolve_material_asset(ctx, key, uid, error);
                 if(!first)
                 {
                     results += ",";
                 }
                 first = false;
                 if(!handle)
                 {
                     results += fmt::format(R"({{"ok":false,"key":{},"uid":{},"error":{}}})",
                                            make_json_string(key),
                                            make_json_string(uid),
                                            make_json_string(error));
                     continue;
                 }
                 auto mat = handle.get();
                 if(!mat)
                 {
                     results += fmt::format(R"({{"ok":false,"key":{},"uid":{},"error":{}}})",
                                            make_json_string(handle.id()),
                                            make_json_string(hpp::to_string(handle.uid())),
                                            make_json_string("Material asset not loaded"));
                     continue;
                 }
                 ++ok_count;
                 results += fmt::format(R"({{"ok":true,"key":{},"uid":{},"properties":{}}})",
                                        make_json_string(handle.id()),
                                        make_json_string(hpp::to_string(handle.uid())),
                                        material_to_json(*mat));
             }
             results += "]";
             return {.text = fmt::format(R"({{"results":{},"count":{},"requested":{}}})", results, ok_count, requested),
                     .is_error = ok_count == 0 && requested > 0};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "materials_create_batch",
         .description =
             "Create many PBR .mat assets. Each item: path, or folder + name. Optional wait_ms after "
             "the batch (shared).",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"path":{"type":"string"},"folder":{"type":"string"},"name":{"type":"string"}}}},"wait_ms":{"type":"integer","minimum":0,"maximum":15000}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             const auto wait_ms = read_wait_ms(args, 1000);
             std::string error;
             simdjson::dom::array items_arr;
             if(!read_required_array(args, "items", items_arr, error))
             {
                 return {.text = error, .is_error = true};
             }
             struct item_t
             {
                 std::string path;
                 std::string folder;
                 std::string name;
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
                 read_string(obj, "path", item.path);
                 read_string(obj, "folder", item.folder);
                 read_string(obj, "name", item.name);
                 items.push_back(std::move(item));
             }
             if(items.empty())
             {
                 return {.text = "items array is empty", .is_error = true};
             }
             auto create_result = mcp.invoke_on_main(
                 [&ctx, items]() -> tool_result
                 {
                     if(ctx.has<project_manager>())
                     {
                         auto& pm = ctx.get_cached<project_manager>();
                         if(!pm.has_open_project())
                         {
                             for(const auto& item : items)
                             {
                                 const auto probe = item.path.empty() ? item.folder : item.path;
                                 if(probe.rfind("app:/", 0) == 0)
                                 {
                                     return {.text = "No project open", .is_error = true};
                                 }
                             }
                         }
                     }
                     auto& am = ctx.get_cached<asset_manager>();
                     std::string results = "[";
                     bool first = true;
                     size_t ok_count = 0;
                     for(const auto& item : items)
                     {
                         if(!first)
                         {
                             results += ",";
                         }
                         first = false;
                         std::string key;
                         std::string key_error;
                         if(!item.path.empty())
                         {
                             key = normalize_material_key(item.path);
                         }
                         else if(!item.folder.empty() && !item.name.empty())
                         {
                             key = item.folder;
                             if(!key.empty() && key.back() != '/')
                             {
                                 key.push_back('/');
                             }
                             key += item.name;
                             key = normalize_material_key(key);
                         }
                         else
                         {
                             key_error = "Provide path, or folder + name";
                         }
                         if(key.empty())
                         {
                             results += fmt::format(R"({{"ok":false,"error":{}}})", make_json_string(key_error));
                             continue;
                         }
                         fs::error_code ec;
                         const auto absolute = fs::absolute(fs::resolve_protocol(key));
                         if(fs::exists(absolute, ec))
                         {
                             results += fmt::format(R"({{"ok":false,"key":{},"error":{}}})",
                                                    make_json_string(key),
                                                    make_json_string("Material already exists: " + key));
                             continue;
                         }
                         fs::create_directories(absolute.parent_path(), ec);
                         auto handle = am.get_asset_from_instance<material>(key, std::make_shared<pbr_material>());
                         if(!handle)
                         {
                             results += fmt::format(R"({{"ok":false,"key":{},"error":{}}})",
                                                    make_json_string(key),
                                                    make_json_string("Failed to create material instance"));
                             continue;
                         }
                         std::string save_error;
                         if(!save_material_asset(ctx, handle, save_error))
                         {
                             results += fmt::format(R"({{"ok":false,"key":{},"error":{}}})",
                                                    make_json_string(key),
                                                    make_json_string(save_error));
                             continue;
                         }
                         ++ok_count;
                         results += fmt::format(R"({{"ok":true,"key":{},"uid":{},"saved":true}})",
                                                make_json_string(handle.id()),
                                                make_json_string(hpp::to_string(handle.uid())));
                     }
                     results += "]";
                     return {.text = fmt::format(R"({{"results":{},"count":{},"requested":{}}})",
                                                 results,
                                                 ok_count,
                                                 items.size()),
                             .is_error = ok_count == 0};
                 });
             if(!create_result)
             {
                 return {.text = "Timed out creating materials on main thread", .is_error = true};
             }
             if(!create_result->is_error)
             {
                 sleep_worker(wait_ms);
             }
             return *create_result;
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "scene_set_model_material_instances_batch",
         .description =
             "Edit per-entity runtime material instances on model slots (batch). Does NOT write .mat "
             "files. Each item: entity_id, properties object, optional index.",
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
                 auto& model_comp = entity.get<model_component>();
                 const auto old_model = model_comp.get_model();
                 auto new_model = old_model;
                 if(!new_model.get_material(item.index).is_valid())
                 {
                     new_model.set_material_instance(std::make_shared<pbr_material>(), item.index);
                 }
                 auto instance = new_model.get_or_emplace_material_instance(item.index);
                 if(!instance)
                 {
                     results += fmt::format(R"({{"ok":false,"entity_id":{},"error":"Failed to create material instance"}})",
                                            make_json_string(item.entity_id));
                     continue;
                 }
                 auto applied = apply_material_properties(ctx, instance, properties);
                 if(!applied.ok)
                 {
                     results += fmt::format(R"({{"ok":false,"entity_id":{},"result":{}}})",
                                            make_json_string(item.entity_id),
                                            apply_result_to_json(applied));
                     continue;
                 }
                 em.do_action(
                     "MCP Set Model Material Instance",
                     [entity, new_model]()
                     {
                         if(auto* mc = entity.try_get<model_component>())
                         {
                             mc->set_model(new_model);
                             prefab_override_context::mark_material_as_changed(entity);
                         }
                     },
                     [entity, old_model]()
                     {
                         if(auto* mc = entity.try_get<model_component>())
                         {
                             mc->set_model(old_model);
                             prefab_override_context::mark_material_as_changed(entity);
                         }
                     });
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
                 auto& model_comp = entity.get<model_component>();
                 const auto old_model = model_comp.get_model();
                 auto new_model = old_model;
                 new_model.set_material_instance(nullptr, index);
                 em.do_action(
                     "MCP Clear Model Material Instance",
                     [entity, new_model]()
                     {
                         if(auto* mc = entity.try_get<model_component>())
                         {
                             mc->set_model(new_model);
                             prefab_override_context::mark_material_as_changed(entity);
                         }
                     },
                     [entity, old_model]()
                     {
                         if(auto* mc = entity.try_get<model_component>())
                         {
                             mc->set_model(old_model);
                             prefab_override_context::mark_material_as_changed(entity);
                         }
                     });
                 ++ok_count;
                 results += fmt::format(R"({{"ok":true,"entity_id":{},"index":{},"cleared":true}})",
                                        make_json_string(entity_id_string(entity)),
                                        index);
             }
             results += "]";
             return {.text = fmt::format(R"({{"results":{},"count":{},"requested":{}}})", results, ok_count, requested),
                     .is_error = ok_count == 0 && requested > 0};
         },
         .mutates_scene = true});
}

} // namespace unravel::mcp

