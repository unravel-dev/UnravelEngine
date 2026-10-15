#include "mcp_material_utils.h"
#include "mcp_asset_lookup.h"
#include "mcp_meta_properties.h"
#include "mcp_project_paths.h"
#include "mcp_tools_common.h"

#include <editor/editing/thumbnail_manager.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_writer.h>
#include <engine/meta/rendering/material.hpp>

namespace unravel::mcp
{
namespace
{

/// @p mat as its most derived reflected type (a pbr_material), so the base's properties and its own are walked.
auto material_object(material& mat) -> entt::meta_any
{
    entt::meta_any object = entt::forward_as_meta(mat);
    entt::as_derived(object);
    return object;
}

} // namespace

auto list_material_property_schema_json() -> std::string
{
    const auto type = entt::resolve<pbr_material>();
    return fmt::format(R"({{"material_type":{},{}}})", make_json_string(entt::get_name(type)), meta_object_schema(type));
}

auto resolve_material_asset(rtti::context& ctx, const std::string& key, const std::string& uid, std::string& error)
    -> asset_handle<material>
{
    if(key.empty() && uid.empty())
    {
        error = "Provide key or uid";
        return {};
    }
    return find_existing_asset<material>(ctx, key.empty() ? uid : to_asset_key(key), error);
}

auto material_type_name(material& mat) -> std::string
{
    return entt::get_name(material_object(mat).type());
}

auto material_to_json(rtti::context& ctx, material& mat) -> std::string
{
    auto object = material_object(mat);
    return meta_properties_to_json(ctx, object);
}

auto apply_material_properties(rtti::context& ctx, material& mat, const simdjson::dom::object& properties)
    -> component_apply_result
{
    // Checked on a copy first: a rejected value leaves the material - shared by every renderer
    // using it - exactly as it was.
    auto candidate = mat.clone();
    auto candidate_object = material_object(*candidate);
    component_apply_result result;
    apply_meta_properties(ctx, candidate_object, properties, result);
    if(!result.ok)
    {
        return result;
    }
    auto object = material_object(mat);
    result = {};
    apply_meta_properties(ctx, object, properties, result);
    return result;
}

auto save_material_asset(rtti::context& ctx, const asset_handle<material>& handle, std::string& error) -> bool
{
    if(!handle)
    {
        error = "Invalid material handle";
        return false;
    }
    if(!asset_writer::atomic_save_to_file(handle.id(), handle))
    {
        error = "Failed to save material: " + handle.id();
        return false;
    }
    if(ctx.has<thumbnail_manager>())
    {
        ctx.get_cached<thumbnail_manager>().regenerate_thumbnail(handle.uid());
    }
    return true;
}

} // namespace unravel::mcp
