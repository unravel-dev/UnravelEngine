#pragma once

#include "mcp_protocol.h"

#include <engine/ecs/scene.h>

#include <context/context.hpp>
#include <ser20/external/simdjson/simdjson.h>

#include <hpp/optional.hpp>
#include <string>
#include <vector>

namespace unravel::mcp
{

struct component_apply_result
{
    std::vector<std::string> applied;
    std::vector<std::string> unknown;
    std::vector<std::string> errors;
    /// Read-only properties a write named with a value other than the current one (a statistic that
    /// moved since it was read, say): left as they are, not an error, so a read can be written back.
    std::vector<std::string> read_only;
    bool ok{true};
};

/**
 * @brief JSON array schema of MCP-editable component property descriptors.
 *        Optional component pretty-name filter (Light, Skylight, Audio Source, ...).
 */
auto list_component_property_schema_json(const std::string& component_filter = {}) -> std::string;

/// The components scene_add_components_batch adds, in the Add Component menu's order, as
/// [{"name":..., "properties": count}].
auto list_components_json() -> std::string;

/// Names the fix when an entity lacks @p component: it is added first, then configured.
auto component_missing_error(const std::string& component) -> std::string;

/**
 * @brief True if MCP exposes typed get/set for this component pretty name.
 */
auto is_supported_component_pretty_name(const std::string& component_pretty_name) -> bool;

/**
 * @brief The meta type a typed-properties component name refers to, Script included.
 * @return The type, or an invalid type for an unknown name
 */
auto component_meta_type(const std::string& component_pretty_name) -> entt::meta_type;

/**
 * @brief Which script a Script call addresses: a C# type (full or short name), and which of the
 *        entity's scripts of that type, counting from 0 in the order the entity lists them.
 */
struct script_selector
{
    std::string type;
    size_t index{};
};

/**
 * @brief Reads an item's type_name and optional script_index.
 * @return False with `error` set when script_index is not a whole number of 0 or more
 */
auto read_script_selector(const simdjson::dom::object& item, script_selector& out, std::string& error) -> bool;

/**
 * @brief The entity's scripts as [{"type":...,"script_index":k}], k counting scripts of one type.
 */
auto scripts_summary_json(entt::handle entity) -> std::string;

/**
 * @brief The full name of the scriptable C# type `name` names, full ("Game.Mover") or short ("Mover").
 * @return False with `error` set when no compiled script type has that name.
 */
auto resolve_script_type_name(rtti::context& ctx, const std::string& name, std::string& full_name, std::string& error)
    -> bool;

/// Where a selected script sits in its entity's list, and its type's full name.
struct script_position
{
    size_t index{};
    std::string type_fullname;
};

/**
 * @brief Finds the script a selector names, counted as scripts_summary_json counts them.
 * @return Its position, or nullopt when the entity has no such script
 */
auto find_script_position(entt::handle entity, const script_selector& script) -> hpp::optional<script_position>;

/**
 * @brief Read typed properties for one component on an entity.
 *        For Script, script.type is required (C# type full name or short name).
 *        Optional property_filter limits keys; empty means all supported keys.
 */
auto component_properties_to_json(rtti::context& ctx,
                                  entt::handle entity,
                                  const std::string& component_pretty_name,
                                  const script_selector& script,
                                  const std::vector<std::string>* property_filter,
                                  std::string& error) -> std::string;

/**
 * @brief Apply typed properties via real component setters (no ser20 round-trip).
 */
auto apply_component_properties(rtti::context& ctx,
                                entt::handle entity,
                                const std::string& component_pretty_name,
                                const script_selector& script,
                                const simdjson::dom::object& properties) -> component_apply_result;

/**
 * @brief JSON object of supported component pretty-name -> property bag for an entity.
 */
auto entity_supported_component_properties_json(rtti::context& ctx,
                                                 entt::handle entity,
                                                 const std::vector<std::string>* component_filter)
    -> std::string;

auto component_apply_result_to_json(const component_apply_result& result) -> std::string;

} // namespace unravel::mcp
