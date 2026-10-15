#pragma once

#include "mcp_component_utils.h"

#include <engine/assets/asset_handle.h>
#include <engine/rendering/material.h>

#include <context/context.hpp>
#include <ser20/external/simdjson/simdjson.h>

#include <string>

namespace unravel::mcp
{

/// The settable properties of a PBR material, from its reflection: names, types, enums and ranges.
auto list_material_property_schema_json() -> std::string;

/// The material asset a key (or a path relative to app:/data) or uid names, when it exists.
auto resolve_material_asset(rtti::context& ctx, const std::string& key, const std::string& uid, std::string& error)
    -> asset_handle<material>;

/// The reflected type name of @p mat ("pbr_material").
auto material_type_name(material& mat) -> std::string;

/// The material's reflected properties as a JSON object, its base type's included.
auto material_to_json(rtti::context& ctx, material& mat) -> std::string;

/**
 * @brief Writes @p properties through the material's reflected setters, all or nothing.
 *
 * Every value is checked on a copy before the material itself changes, so a rejected value leaves
 * it - and every renderer sharing it - as it was.
 */
auto apply_material_properties(rtti::context& ctx, material& mat, const simdjson::dom::object& properties)
    -> component_apply_result;

/// Saves a material asset to its file, as the inspector's save does, and refreshes its thumbnail.
auto save_material_asset(rtti::context& ctx, const asset_handle<material>& handle, std::string& error) -> bool;

} // namespace unravel::mcp
