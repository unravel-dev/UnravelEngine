#pragma once

#include "basic_component.h"
#include "id_component.h"
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
     * @brief Which nested instance of the document that produced this one it is.
     *
     * The only thing that can say. Prefab ids inside a nested instance come from the nested
     * asset, so two instances of one prefab - or an instance and a copy of it - are identical
     * by every other measure. Scoped to the containing document, unlike id_component (global)
     * and prefab_id_component (scoped to the asset the entity came from).
     *
     * Allocated once, when a subtree is written as a prefab file, and preserved from then on:
     * every instance of that file carries the same id for the same slot, which is what lets
     * the file name a slot and be understood by all of them. Regenerated on clone, because a
     * copy is a different slot.
     *
     * Nil means this instance was added where it stands rather than coming from a file - or
     * that the file predates instance ids. Both read the same way, and it is the safe
     * reading: a document only removes instances it can name.
     */
    hpp::uuid instance_id;

    /**
     * @brief Storage of property overrides
     * Each override is identified by entity UUID + component path
     * This allows entity renaming without invalidating overrides
     */
    std::set<prefab_property_override_data> property_overrides;

    /**
     * @brief The subset of property_overrides that came from the document containing this
     *        instance, rather than being made here.
     *
     * Only meaningful on a nested instance: the prefab that contains it authored some
     * overrides on it, and whoever placed that prefab may have authored more. Both end up in
     * property_overrides, and once merged they cannot be told apart - which makes the
     * containing prefab unable to change its mind. Anything it stops overriding would look
     * like a local edit and stay forever, and anything it changes the value of would look
     * like a local edit and be protected from the new value.
     *
     * Kept apart so a resync can replace this set wholesale and leave the difference alone.
     */
    std::set<prefab_property_override_data> inherited_overrides;

    std::set<hpp::uuid> removed_entities;

    /**
     * @brief Nested instances removed from this instance, by instance id.
     *
     * Separate from removed_entities, and keyed differently on purpose. A nested instance
     * root has a prefab uid too, but it is the *nested* asset's - shared with every other
     * instance of that prefab - so removing one by prefab uid would say "remove all of them".
     */
    std::set<hpp::uuid> removed_instances;

    /**
     * @brief Entities under this instance that its own asset does not own.
     *
     * The document that contains an instance can add entities inside it - an effect parented
     * under a nested prop, say. Those get a prefab uid in the *containing* document's space,
     * which the instance's own asset has never heard of, so its next sync sees an entity it
     * did not supply and no record claiming it, and removes it.
     *
     * Listed here so that sync can leave them alone. An entity the user adds by hand needs no
     * such list: it has no prefab uid at all, and that is already enough to be left alone.
     */
    std::set<hpp::uuid> foreign_entities;

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
     * @brief Record that a nested instance was removed from this one
     * @param instance_id The instance id of the nested instance, not its prefab uid
     */
    void remove_instance(const hpp::uuid& instance_id);

    /**
     * @brief Whether a nested instance was removed from this one
     * @param instance_id The instance id of the nested instance, not its prefab uid
     */
    auto is_instance_removed(const hpp::uuid& instance_id) const -> bool;

    
    /**
     * @brief Get all overrides
     * @return Set of all property overrides
     */
    auto get_all_overrides() const -> const std::set<prefab_property_override_data>&;
    
    /**
     * @brief Whether an override lies on, above, or below a point in an entity's properties.
     *
     * Overrides name a point in a tree of properties, and reading one is a walk down that
     * tree - so both directions count. "tag_component" has to be let through to reach an
     * override on "tag_component/name", and "transform/position/x" has to be let through
     * because of an override on "transform/position".
     *
     * @param entity_uuid The prefab uid of the entity
     * @param component_path A component name, optionally followed by a property path
     */
    auto has_override_touching(const hpp::uuid& entity_uuid, std::string_view component_path) const -> bool;

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
 struct prefab_id_component : public id_component_base<prefab_id_component>
 {

 };
 

} // namespace unravel
