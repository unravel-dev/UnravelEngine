#pragma once

#include "mcp_component_utils.h"

#include <context/context.hpp>
#include <engine/layers/layer_mask.h>
#include <entt/meta/meta.hpp>
#include <hpp/string_view.hpp>
#include <ser20/external/simdjson/simdjson.h>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

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
 * take their reflected value names. A read-only property is left as it is: written back with
 * its current value it is skipped, with another value it is listed in result.read_only.
 *
 * @param only When set, keys outside it are reported unknown even if reflected.
 */
void apply_meta_properties(rtti::context& ctx,
                           entt::meta_any& object,
                           const simdjson::dom::object& properties,
                           component_apply_result& result,
                           const std::unordered_set<std::string>* only = nullptr);

/**
 * @brief One value of a type this bridge expresses - a codec type, an enum, a container of them,
 *        or a reflected struct - as JSON; nothing when it cannot.
 */
auto meta_value_to_json(rtti::context& ctx, entt::meta_any& value) -> std::optional<std::string>;

/**
 * @brief Reads @p value into @p target as a property write does: a struct merges the fields named,
 *        a container takes the array's length, anything else is replaced.
 * @return False with @p error set when @p value does not fit the type.
 */
auto meta_value_from_json(rtti::context& ctx,
                          const simdjson::dom::element& value,
                          entt::meta_any& target,
                          std::string& error) -> bool;

/// A layer mask as the names of its set bits, from the project's layer names (the ones the running
/// game uses); an unnamed set bit reads "#index".
auto layer_mask_to_json(rtti::context& ctx, const layer_mask& mask) -> std::string;

/// Reads a layer mask from an array of layer names or indices, or "everything" / "nothing".
auto parse_layer_mask(rtti::context& ctx, const simdjson::dom::element& value, layer_mask& out, std::string& error)
    -> bool;

/// The value names a reflected enum declares, in declaration order - the spellings MCP accepts.
auto meta_enum_names(const entt::meta_type& type) -> std::vector<std::string>;

/**
 * @brief The value of reflected enum `type` named `name`, matched against each value's name and
 *        pretty name without case, spaces or underscores ("OutExpo", "out_expo", "Out Expo").
 * @return False with `error` listing the accepted names when nothing matches.
 */
auto find_meta_enum_value(const entt::meta_type& type, hpp::string_view name, entt::meta_any& out, std::string& error)
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

/// How many top-level properties of @p type this bridge can read and write.
auto count_meta_properties(const entt::meta_type& type) -> size_t;

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
