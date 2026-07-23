#pragma once

#include <engine/ecs/scene.h>

#include <cstddef>
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
 * @brief Lean create/find result: id, name, optional parent_id.
 */
auto entity_to_lean_json(entt::handle entity, bool include_parent_id = true) -> std::string;

/**
 * @brief Pose + meta JSON without children or component lists.
 */
auto entity_to_pose_json(entt::handle entity) -> std::string;

/**
 * @brief Hierarchy node JSON: id, name, nested children. Counts nodes against limit.
 */
auto entity_hierarchy_node_json(entt::handle entity,
                                int depth,
                                int max_depth,
                                size_t& nodes_emitted,
                                size_t node_limit,
                                bool& truncated) -> std::string;

/**
 * @brief Full associative serialization of serializeable components (scene-file format).
 */
auto entity_components_serialized(entt::handle entity) -> std::string;

/**
 * @brief Serialize a single component by pretty name. Sets error on failure.
 */
auto entity_component_serialized(entt::handle entity, const std::string& component_pretty_name, std::string& error)
    -> std::string;

/**
 * @brief Component pretty-names present on the entity (inspectable + Script).
 */
auto collect_component_pretty_names(entt::handle entity) -> std::vector<std::string>;

} // namespace unravel
