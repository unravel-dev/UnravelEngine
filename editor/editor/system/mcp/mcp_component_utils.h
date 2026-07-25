#pragma once

#include "mcp_protocol.h"

#include <engine/ecs/scene.h>

#include <context/context.hpp>
#include <ser20/external/simdjson/simdjson.h>

#include <string>
#include <vector>

namespace unravel::mcp
{

struct component_apply_result
{
    std::vector<std::string> applied;
    std::vector<std::string> unknown;
    std::vector<std::string> errors;
    bool ok{true};
};

/**
 * @brief JSON array schema of MCP-editable component property descriptors.
 *        Optional component pretty-name filter (Light, Skylight, Audio Source, ...).
 */
auto list_component_property_schema_json(const std::string& component_filter = {}) -> std::string;

/**
 * @brief True if MCP exposes typed get/set for this component pretty name.
 */
auto is_supported_component_pretty_name(const std::string& component_pretty_name) -> bool;

/**
 * @brief Read typed properties for one component on an entity.
 *        For Script, script_type is required (C# type full name or short name).
 *        Optional property_filter limits keys; empty means all supported keys.
 */
auto component_properties_to_json(rtti::context& ctx,
                                  entt::handle entity,
                                  const std::string& component_pretty_name,
                                  const std::string& script_type,
                                  const std::vector<std::string>* property_filter,
                                  std::string& error) -> std::string;

/**
 * @brief Apply typed properties via real component setters (no ser20 round-trip).
 */
auto apply_component_properties(rtti::context& ctx,
                                entt::handle entity,
                                const std::string& component_pretty_name,
                                const std::string& script_type,
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
