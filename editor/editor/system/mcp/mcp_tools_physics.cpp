#include "mcp_meta_properties.h"
#include "mcp_tools_common.h"

#include <editor/assets/asset_actions.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/assets/impl/asset_writer.h>
#include <engine/meta/physics/physics_material.hpp>
#include <engine/physics/physics_material.h>
#include <filesystem/filesystem.h>
#include <uuid/uuid.h>

#include <string>
#include <vector>

namespace unravel::mcp
{
namespace
{

auto normalize_physics_material_key(std::string key) -> std::string
{
    const auto& ext = ex::get_format<physics_material>();
    if(!key.empty() && !ends_with_ci(key, ext))
    {
        key += ext;
    }
    return key;
}

auto resolve_physics_material_asset(rtti::context& ctx, const std::string& key_or_uid, std::string& error)
    -> asset_handle<physics_material>
{
    auto& am = ctx.get_cached<asset_manager>();
    auto handle = [&]
    {
        if(auto as_uuid = hpp::uuid::from_string(key_or_uid))
        {
            return am.get_asset<physics_material>(*as_uuid);
        }
        return am.get_asset<physics_material>(normalize_physics_material_key(key_or_uid));
    }();
    if(!handle || !handle.get())
    {
        error = "Physics material not found: " + key_or_uid;
        return {};
    }
    return handle;
}

auto material_properties_json(rtti::context& ctx, const asset_handle<physics_material>& handle) -> std::string
{
    auto object = entt::forward_as_meta(*handle.get());
    return meta_properties_to_json(ctx, object);
}

/// Edits the loaded instance through the reflection and saves it - the inspector's asset edit
/// path - so every handle already pointing at the material sees the new values. Applied onto a
/// copy first: a rejected entry leaves the asset untouched.
auto edit_physics_material(rtti::context& ctx,
                           const asset_handle<physics_material>& handle,
                           const simdjson::dom::object& properties,
                           component_apply_result& result) -> bool
{
    auto candidate = *handle.get();
    auto object = entt::forward_as_meta(candidate);
    apply_meta_properties(ctx, object, properties, result);
    if(!result.ok)
    {
        return false;
    }
    *handle.get() = candidate;
    if(!asset_writer::atomic_save_to_file(handle.id(), handle))
    {
        result.ok = false;
        result.errors.push_back("Failed to save " + handle.id());
        return false;
    }
    return true;
}

} // namespace

void register_physics_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "physics_materials_list_properties",
         .description = "The editable properties of a physics material, with ranges, enum values and "
                        "what each one does - read from the same reflection the inspector shows.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context&, const simdjson::dom::object&) -> tool_result
         {
             std::string json = "[";
             bool first = true;
             append_meta_schema(entt::resolve<physics_material>(), "Physics Material", json, first);
             json += "]";
             return {.text = fmt::format(R"({{"properties":{}}})", json), .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "physics_materials_create_batch",
         .description =
             "Create physics material assets (.phm) - what a surface feels like to the simulation "
             "(bounce, grip). Same create path as the Content Browser's Create menu. Each item: path, "
             "or folder + name, plus optional properties (see physics_materials_list_properties). "
             "Assign one with scene_set_component_properties_batch, component Physics, property "
             "material. Bodies read it when they are built, so it takes effect when play mode starts.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"path":{"type":"string"},"folder":{"type":"string"},"name":{"type":"string"},"properties":{"type":"object"}}}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             if(!require_open_project(ctx, error))
             {
                 return {.text = error, .is_error = true};
             }
             simdjson::dom::array items;
             if(!read_required_array(args, "items", items, error))
             {
                 return {.text = error, .is_error = true};
             }

             std::string results = "[";
             bool first = true;
             size_t ok_count = 0;
             size_t requested = 0;
             for(auto element : items)
             {
                 ++requested;
                 if(!first)
                 {
                     results += ",";
                 }
                 first = false;

                 simdjson::dom::object item;
                 if(!read_object(element, item, error))
                 {
                     results += fmt::format(R"({{"ok":false,"error":{}}})", make_json_string(error));
                     continue;
                 }
                 std::string path;
                 std::string folder;
                 std::string name;
                 read_string(item, "path", path);
                 read_string(item, "folder", folder);
                 read_string(item, "name", name);
                 std::string key = path;
                 if(key.empty() && !folder.empty() && !name.empty())
                 {
                     key = folder + (folder.back() == '/' ? "" : "/") + name;
                 }
                 key = normalize_physics_material_key(key);
                 if(key.empty())
                 {
                     results += R"({"ok":false,"error":"Provide path, or folder + name"})";
                     continue;
                 }

                 fs::error_code ec;
                 const auto absolute = fs::absolute(fs::resolve_protocol(key));
                 if(fs::exists(absolute, ec))
                 {
                     results += fmt::format(R"({{"ok":false,"key":{},"error":"Already exists; use physics_materials_set"}})",
                                            make_json_string(key));
                     continue;
                 }
                 // Validate against a scratch instance first, so rejected properties leave no
                 // half-configured asset behind.
                 simdjson::dom::object properties;
                 const bool has_properties = item["properties"].get_object().get(properties) == simdjson::SUCCESS;
                 if(has_properties)
                 {
                     physics_material scratch;
                     auto scratch_object = entt::forward_as_meta(scratch);
                     component_apply_result check;
                     apply_meta_properties(ctx, scratch_object, properties, check);
                     if(!check.ok)
                     {
                         results += fmt::format(R"({{"ok":false,"key":{},"errors":{},"note":"Nothing was created."}})",
                                                make_json_string(key),
                                                strings_to_json_array(check.errors));
                         continue;
                     }
                 }

                 fs::create_directories(absolute.parent_path(), ec);
                 auto handle = asset_actions::create_physics_material(ctx, absolute);
                 if(!handle)
                 {
                     results += fmt::format(R"({{"ok":false,"key":{},"error":"Failed to create or save"}})",
                                            make_json_string(key));
                     continue;
                 }
                 component_apply_result edit;
                 if(has_properties && !edit_physics_material(ctx, handle, properties, edit))
                 {
                     results += fmt::format(R"({{"ok":false,"key":{},"created":true,"errors":{}}})",
                                            make_json_string(handle.id()),
                                            strings_to_json_array(edit.errors));
                     continue;
                 }
                 ++ok_count;
                 results += fmt::format(R"({{"ok":true,"key":{},"uid":{},"properties":{}}})",
                                        make_json_string(handle.id()),
                                        make_json_string(hpp::to_string(handle.uid())),
                                        material_properties_json(ctx, handle));
             }
             results += "]";
             return {.text = fmt::format(R"({{"results":{},"count":{},"requested":{}}})",
                                         results,
                                         ok_count,
                                         requested),
                     .is_error = ok_count == 0};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "physics_materials_get_batch",
         .description = "Read physics material assets by key or uid.",
         .input_schema_json =
             R"json({"type":"object","properties":{"keys":{"type":"array","items":{"type":"string"}}},"required":["keys"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::vector<std::string> keys;
             std::string error;
             if(!read_string_array(args, "keys", keys, error) || keys.empty())
             {
                 return {.text = error.empty() ? "Pass keys" : error, .is_error = true};
             }
             std::string results = "[";
             bool first = true;
             for(const auto& key : keys)
             {
                 if(!first)
                 {
                     results += ",";
                 }
                 first = false;
                 std::string lookup_error;
                 auto handle = resolve_physics_material_asset(ctx, key, lookup_error);
                 if(!handle)
                 {
                     results += fmt::format(R"({{"key":{},"ok":false,"error":{}}})",
                                            make_json_string(key),
                                            make_json_string(lookup_error));
                     continue;
                 }
                 results += fmt::format(R"({{"key":{},"ok":true,"properties":{}}})",
                                        make_json_string(handle.id()),
                                        material_properties_json(ctx, handle));
             }
             results += "]";
             return {.text = results, .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "physics_materials_set",
         .description =
             "Edit a physics material asset and save it. A shared asset: every body using it changes. "
             "All-or-nothing: nothing is written when any property is rejected.",
         .input_schema_json =
             R"json({"type":"object","properties":{"key":{"type":"string"},"properties":{"type":"object"}},"required":["key","properties"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             if(!require_not_play_mode(ctx, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::string key;
             read_string(args, "key", key);
             simdjson::dom::object properties;
             if(key.empty() || args["properties"].get_object().get(properties) != simdjson::SUCCESS)
             {
                 return {.text = "Pass key and a properties object", .is_error = true};
             }
             auto handle = resolve_physics_material_asset(ctx, key, error);
             if(!handle)
             {
                 return {.text = error, .is_error = true};
             }
             component_apply_result edit;
             if(!edit_physics_material(ctx, handle, properties, edit))
             {
                 return {.text = fmt::format(R"({{"ok":false,"errors":{},"unknown":{},"note":"Nothing was written."}})",
                                             strings_to_json_array(edit.errors),
                                             strings_to_json_array(edit.unknown)),
                         .is_error = true};
             }
             return {.text = fmt::format(R"({{"ok":true,"key":{},"applied":{},"properties":{}}})",
                                         make_json_string(handle.id()),
                                         strings_to_json_array(edit.applied),
                                         material_properties_json(ctx, handle)),
                     .is_error = false};
         },
         .mutates_scene = false});
}

} // namespace unravel::mcp
