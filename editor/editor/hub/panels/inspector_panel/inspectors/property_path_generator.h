#pragma once

#include <reflection/reflection.h>
#include <string>
#include <vector>
#include <uuid/uuid.h>

namespace unravel
{

/**
 * @class property_path_context
 * @brief Context holder for tracking the current property path during inspection
 */
class property_path_context
{
public:
    property_path_context() = default;
    
    /**
     * @brief Push a new path segment onto the context stack
     * @param segment The path segment to add
     */
    void push_segment(const std::string& segment);
    
    /**
     * @brief Pop the last path segment from the context stack
     */
    void pop_segment();
    
    /**
     * @brief Get the current full property path
     * @return The dot-separated property path
     */
    auto get_current_path() const -> std::string;
    auto get_current_path_with_component_type() const -> std::string;

    /**
     * @brief Set the component type for this context
     * @param type The RTTR type of the component
     */
    void set_component_type(const std::string& type);
    
    /**
     * @brief Get the component type name
     * @return The component type name
     */
    auto get_component_type_name() const -> std::string;
    
    /**
     * @brief Check if we're currently inspecting a prefab instance
     * @return True if inspecting a prefab instance
     */
    auto is_prefab_context() const -> bool;
    
    /**
     * @brief Set whether this is a prefab inspection context
     * @param is_prefab True if inspecting a prefab instance
     */
    void set_prefab_context(bool is_prefab);

    /**
     * @brief Set the entity UUID for nested entity tracking
     * @param entity_uuid The UUID of the entity being inspected
     */
    void set_entity_uuid(const hpp::uuid& entity_uuid);
    
    /**
     * @brief Get the entity UUID
     * @return The entity UUID if set, or a nil UUID otherwise
     */
    auto get_entity_uuid() const -> hpp::uuid;



private:
    std::vector<std::string> path_segments_;
    std::string component_type_name_;
    hpp::uuid entity_uuid_;
    // std::vector<std::string> added_components_;
    // std::vector<std::string> removed_components_;
};

} // namespace unravel