#pragma once

#include "basic_component.h"
#include <engine/assets/asset_handle.h>
#include <unordered_map>
#include <string>
#include <set>
#include <uuid/uuid.h>

namespace unravel
{

/**
 * @struct prefab_property_override_data
 * @brief Represents a property override with entity UUID and component/property path
 */
struct prefab_property_override_data
{
    hpp::uuid entity_uuid;              // Entity UUID for stable identification
    std::string component_path;         // Component type + property path (e.g., "transform_component/position/x")
    std::string pretty_component_path;  // Human-readable component path (e.g., "Transform/Position/X")
    
    prefab_property_override_data() = default;
    prefab_property_override_data(const hpp::uuid& uuid, const std::string& path);
    prefab_property_override_data(const hpp::uuid& uuid, const std::string& path, const std::string& pretty_path);
    
    auto operator==(const prefab_property_override_data& other) const -> bool;
    auto operator<(const prefab_property_override_data& other) const -> bool;

};

/**
 * @struct prefab_component
 * @brief Component that holds a reference to a prefab asset and tracks property overrides.
 */
struct prefab_component : public component_crtp<prefab_component, owned_component>
{
    static constexpr bool in_place_delete = false;

    /**
     * @brief Handle to the prefab asset.
     */
    asset_handle<prefab> source;

    /**
     * @brief Storage of property overrides
     * Each override is identified by entity UUID + component path
     * This allows entity renaming without invalidating overrides
     */
    std::set<prefab_property_override_data> property_overrides;

    std::set<hpp::uuid> removed_entities;

    bool changed = false;
    
    /**
     * @brief Clear all overrides (for applying all changes to prefab)
     */
    void clear_overrides();
    
    /**
     * @brief Add a property override
     * @param entity_uuid The UUID of the entity being overridden
     * @param component_path The component type + property path
     */
    void add_override(const hpp::uuid& entity_uuid, const std::string& component_path);
    
    /**
     * @brief Add a property override with pretty path
     * @param entity_uuid The UUID of the entity being overridden
     * @param component_path The component type + property path
     * @param pretty_component_path The human-readable component path
     */
    void add_override(const hpp::uuid& entity_uuid, const std::string& component_path, const std::string& pretty_component_path);
    
    /**
     * @brief Check if a property is overridden
     * @param entity_uuid The UUID of the entity
     * @param component_path The component type + property path
     * @return True if the property is overridden
     */
    auto has_override(const hpp::uuid& entity_uuid, const std::string& component_path) const -> bool;
    
    /**
     * @brief Remove a property override
     * @param entity_uuid The UUID of the entity
     * @param component_path The component type + property path
     */
    void remove_override(const hpp::uuid& entity_uuid, const std::string& component_path);

    /**
     * @brief Remove an entity from the prefab
     * @param entity_uuid The UUID of the entity
     */
    void remove_entity(const hpp::uuid& entity_uuid);

    
    /**
     * @brief Get all overrides
     * @return Set of all property overrides
     */
    auto get_all_overrides() const -> const std::set<prefab_property_override_data>&;
    
    /**
     * @brief Check if a serialization path has an override (for backward compatibility)
     * @param serialization_path The path in format "entities/uuid/components/component/path"
     * @return True if the path is overridden
     */
    auto has_serialization_override(const std::string& serialization_path) const -> bool;
};

/**
 * @struct prefab_id_component
 * @brief Component that provides a unique identifier (UUID) for a prefab.
 */
 struct prefab_id_component
 {
     void regenerate_id()
     {
         id = generate_uuid();
     }
 
     void generate_if_nil()
     {
         if(id.is_nil()) 
         {
             id = generate_uuid();
         }
     }
     /**
      * @brief The unique identifier for the entity.
      */
     hpp::uuid id;
 };
 

} // namespace unravel
