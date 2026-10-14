#pragma once

#include "mcp_component_utils.h"

#include <context/context.hpp>
#include <engine/layers/layer_mask.h>
#include <entt/meta/meta.hpp>
#include <ser20/external/simdjson/simdjson.h>

#include <string>
#include <string_view>
#include <unordered_set>

namespace unravel::mcp
{

/**
 * @brief Reflected properties of `object` as a JSON object, keyed by the reflection's names.
 *
 * Reads through the reflected getters, the same ones the inspector shows, so a property that
 * is reflected is reachable without a hand-written accessor. Properties the reflection hides
 * (their predicate: a setting of another mode) are left out unless @p include_hidden, as are
 * value types this bridge cannot express in JSON.
 *
 * @param only When set, only these names are read.
 */
auto meta_properties_to_json(rtti::context& ctx,
                             entt::meta_any& object,
                             const std::unordered_set<std::string>* only = nullptr,
                             bool include_hidden = false) -> std::string;

/**
 * @brief Writes `properties` through the reflected setters.
 *
 * The setter is the one the reflection binds (the component's own set_x), so dirty flags and
 * side effects run exactly as for an inspector edit. min / max attributes are enforced, enums
 * take their reflected value names, and read-only properties are rejected with a reason.
 *
 * @param only When set, keys outside it are reported unknown even if reflected.
 */
void apply_meta_properties(rtti::context& ctx,
                           entt::meta_any& object,
                           const simdjson::dom::object& properties,
                           component_apply_result& result,
                           const std::unordered_set<std::string>* only = nullptr);

/// A layer mask as the names of its set bits, from the project's layer names (the ones the running
/// game uses); an unnamed set bit reads "#index".
auto layer_mask_to_json(rtti::context& ctx, const layer_mask& mask) -> std::string;

/// Reads a layer mask from an array of layer names or indices, or "everything" / "nothing".
auto parse_layer_mask(rtti::context& ctx, const simdjson::dom::element& value, layer_mask& out, std::string& error)
    -> bool;

/// The reflected property of `type` whose "name" attribute is `name`; empty when there is none.
auto find_meta_property(const entt::meta_type& type, std::string_view name) -> entt::meta_data;

/**
 * @brief Schema fields for one reflected property (no braces): "type", plus "enum", "minimum",
 *        "maximum", "readOnly" and "description" when the reflection carries them; a nested
 *        reflected struct also lists its own "properties".
 */
auto meta_property_schema(const entt::meta_data& prop) -> std::string;

/**
 * @brief Schema fields for a reflected struct as a whole (no braces): "type":"object" and its
 *        "properties", each described as meta_property_schema describes it.
 */
auto meta_object_schema(const entt::meta_type& type) -> std::string;

/**
 * @brief True when the reflection exposes at least one property of `type` this bridge can
 *        read and write (a known value type, an enum, or a nested reflected struct).
 */
auto has_meta_properties(const entt::meta_type& type) -> bool;

/**
 * @brief Appends schema rows for a reflected type, in the shape list_component_property_schema_json
 *        emits: component, name, type, plus enum / minimum / maximum / readOnly / description
 *        from the reflection's attributes.
 */
void append_meta_schema(const entt::meta_type& type,
                        const std::string& component,
                        std::string& json,
                        bool& first,
                        const std::unordered_set<std::string>* only = nullptr);

} // namespace unravel::mcp
