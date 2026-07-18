#pragma once

#include <engine/ecs/scene.h>

#include <string>
#include <vector>

namespace unravel
{

/**
 * @brief Resolve an entity by UUID string or integral entt id.
 */
auto find_entity_by_id(scene& scn, const std::string& id) -> entt::handle;

/**
 * @brief Stable id string for MCP/editor tooling (UUID preferred).
 */
auto entity_id_string(entt::handle entity) -> std::string;

/**
 * @brief JSON summary: transform, component pretty-names, optional children.
 */
auto entity_to_summary_json(entt::handle entity, int depth = 0, int max_depth = 0) -> std::string;

/**
 * @brief Full associative serialization of serializeable components (scene-file format).
 */
auto entity_components_serialized(entt::handle entity) -> std::string;

/**
 * @brief Component pretty-names present on the entity (inspectable + Script).
 */
auto collect_component_pretty_names(entt::handle entity) -> std::vector<std::string>;

} // namespace unravel
