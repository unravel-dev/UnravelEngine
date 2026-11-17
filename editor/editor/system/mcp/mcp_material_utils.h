#pragma once

#include "mcp_component_utils.h"

#include <engine/assets/asset_handle.h>
#include <engine/rendering/material.h>
#include <engine/rendering/material_instance.h>
#include <engine/rendering/material_graph/material_graph.h>

#include <context/context.hpp>
#include <ser20/external/simdjson/simdjson.h>

#include <string>

namespace unravel::mcp
{

/// The settable properties of a material: the Standard graph's parameters and textures by their keys ("base_color"),
/// and the material's reflected ones (its cull type).
auto list_material_property_schema_json() -> std::string;

/// The material asset a key (or a path relative to app:/data) or uid names, when it exists.
auto resolve_material_asset(rtti::context& ctx, const std::string& key, const std::string& uid, std::string& error)
    -> asset_handle<material>;

/// The material asset @p key names, loaded, when it is a material of @p domain; empty with a reason in @p error
/// otherwise (one that says which output node the graph needs).
auto resolve_domain_material(rtti::context& ctx, const std::string& key, material_domain domain, std::string& error)
    -> asset_handle<material>;

/// The reflected type name of @p mat ("graph_material").
auto material_type_name(material& mat) -> std::string;

/// The material's reflected properties as a JSON object, its base type's included, and the Standard properties its
/// graph has, by key: colors as sRGB numbers, textures as keys.
auto material_to_json(rtti::context& ctx, material& mat) -> std::string;

/**
 * @brief Writes @p properties, all or nothing: the Standard ones ("base_color", "color_map") into the defaults of the
 * graph's parameters and texture nodes, the others through the material's reflected setters.
 *
 * Every value is checked on a copy before the material itself changes, so a rejected value leaves
 * it - and every renderer sharing it - as it was.
 */
auto apply_material_properties(rtti::context& ctx, material& mat, const simdjson::dom::object& properties)
    -> component_apply_result;

/**
 * @brief Overrides @p properties of the instance's shared material: the Standard ones as graph parameter and texture
 * overrides, the others (the cull type) as reflected overrides. Edits the instance as far as it gets: callers edit a
 * copy and keep it only when the result is ok.
 */
auto apply_material_instance_properties(rtti::context& ctx,
                                        material_instance& instance,
                                        const simdjson::dom::object& properties) -> component_apply_result;

/**
 * @brief Overrides a graph material's parameters and texture nodes by name on the instance: @p parameters maps names
 * to a number or a list of up to four numbers, @p textures node ids to a texture key ("" for the default texture);
 * null reverts one. Either may be null. Edits the instance as far as it gets and adds what it did to @p result, which
 * may hold the instance's other edits already: callers edit a copy and keep it only when the result is ok.
 */
void apply_material_instance_graph_values(rtti::context& ctx,
                                          material_instance& instance,
                                          const simdjson::dom::object* parameters,
                                          const simdjson::dom::object* textures,
                                          component_apply_result& result);

/// Saves a material asset to its file, as the inspector's save does, and refreshes its thumbnail.
auto save_material_asset(rtti::context& ctx, const asset_handle<material>& handle, std::string& error) -> bool;

} // namespace unravel::mcp
