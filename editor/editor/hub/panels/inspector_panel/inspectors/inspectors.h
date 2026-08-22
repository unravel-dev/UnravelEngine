#pragma once

#include "entt/core/fwd.hpp"
#include "inspector.h"
#include "property_path_generator.h"
#include <context/context.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <editor/editing/prefab_removal_record.h>

namespace unravel
{
/**
 * @brief Registry for managing type-specific inspector instances
 * 
 * Automatically discovers and registers all inspector types that derive from
 * the base inspector class. Maps type IDs to their corresponding inspector
 * instances for efficient lookup during property inspection.
 */
struct inspector_registry
{
    /**
     * @brief Constructor that auto-discovers and registers all inspector types
     * 
     * Uses reflection to find all types derived from inspector and creates
     * instances for each registered type-inspector pair.
     */
    inspector_registry();

    /// Map from type ID to inspector instance for fast lookup
    std::unordered_map<entt::id_type, std::shared_ptr<inspector>> type_map;
};

/**
 * @brief Stores both pretty and serialization paths for a prefab override
 * 
 * Prefab overrides need two path representations: a human-readable path for
 * display in the inspector, and a technical serialization path for matching
 * during deserialization and data persistence.
 */
struct prefab_property_override
{
    /// Human-readable path for inspector display (e.g., "Transform/Position/X")
    std::string pretty_path;
    /// Technical path for deserialization matching (e.g., "entities[0]/components/Transform/local_transform/position/x")
    std::string serialization_path;

    /**
     * @brief Default constructor
     */
    prefab_property_override() = default;
    
    /**
     * @brief Creates override from pretty path and serialization path
     * @param pretty Human-readable path for display
     * @param serialization Technical path for serialization
     * @return Constructed prefab_property_override instance
     */
    static auto create(const std::string& pretty, const std::string& serialization) -> prefab_property_override
    {
        prefab_property_override override;
        override.pretty_path = pretty;
        override.serialization_path = serialization;
        return override;
    }

    /**
     * @brief Equality comparison based on serialization path
     * @param other Other override to compare with
     * @return true if serialization paths match
     */
    auto operator==(const prefab_property_override& other) const -> bool
    {
        return serialization_path == other.serialization_path;
    }

    /**
     * @brief Less-than comparison for sorting based on serialization path
     * @param other Other override to compare with
     * @return true if this serialization path is lexicographically less
     */
    auto operator<(const prefab_property_override& other) const -> bool
    {
        return serialization_path < other.serialization_path;
    }
};

struct inspector_context
{
    entt::registry* inspected_registry{};
};

/**
 * @brief Global context for tracking prefab override changes during inspection
 * 
 * Manages the state needed to track which properties have been modified in
 * prefab instances, enabling the override system that allows instances to
 * deviate from their prefab templates while maintaining the connection.
 */
struct prefab_override_context
{
    /**
     * @brief Default constructor
     */
    prefab_override_context() = default;

    /// Callback function to check if a specific property path is already overridden
    std::function<bool(const hpp::uuid& entity_uuid, const std::string& component_path)> is_path_overridden_callback;

    /// Current property path context for serialization paths
    property_path_context path_context;
    /// Current property path context for human-readable paths
    property_path_context pretty_path_context;

    /// The prefab root entity that contains the prefab_component
    entt::handle prefab_root_entity;
    /// The specific entity currently being inspected
    entt::handle entity;

    /// Temporary scene for prefab comparison operations
    scene prefab_scene{"prefab_diff_scene"};
    /// Version tracking for prefab changes
    uintptr_t prefab_version{};

    /// Whether we're currently inspecting a prefab instance
    bool is_active = false;

    /**
     * @brief Sets the entity UUID for both path contexts
     * @param uuid UUID of the entity being inspected
     */
    void set_entity_uuid(const hpp::uuid& uuid);

    /**
     * @brief Sets the component type for both path contexts
     * @param type Technical component type name
     * @param pretty_type Human-readable component type name
     */
    void set_component_type(const std::string& type, const std::string& pretty_type);

    /**
     * @brief Pushes a new path segment onto both contexts and applies override styling
     * @param segment Technical path segment
     * @param pretty_segment Human-readable path segment
     */
    void push_segment(const std::string& segment, const std::string& pretty_segment);

    /**
     * @brief Pops the last path segment and removes override styling if needed
     */
    void pop_segment();

    /**
     * @brief Initialize context for inspecting a prefab instance
     * @param entity The prefab instance entity being inspected
     * @return true if successfully initialized for prefab inspection
     */
    auto begin_prefab_inspection(entt::handle entity) -> bool;

    /**
     * @brief End prefab inspection context and clean up state
     */
    void end_prefab_inspection();

    /**
     * @brief Record a property override when a change is detected
     * 
     * Uses the current property path context to determine what was changed.
     * Automatically removes any parent or child overrides to keep only the most specific path.
     * @return true if override was successfully recorded
     */
    auto record_override() -> bool;

    auto reset_override() -> bool;

    /**
     * @brief Check if the current property path is already overridden
     * @return true if the current path has an existing override
     */
    auto is_path_overridden() const -> bool
    {
        return is_path_overridden_callback
                   ? is_path_overridden_callback(path_context.get_entity_uuid(),
                                                 path_context.get_current_path_with_component_type())
                   : false;
    }

    /**
     * @brief Finds the prefab root entity by traversing up the parent hierarchy
     * @param entity The entity to start searching from
     * @return Handle to the entity with prefab_component, or empty handle if not found
     */
    static auto find_prefab_root_entity(entt::handle entity) -> entt::handle;
    
    /**
     * @brief Gets the prefab UUID of an entity from its prefab_id_component
     * @param entity The entity to get the UUID from
     * @return The prefab UUID, or nil UUID if entity has no prefab_id_component
     */
    static auto get_entity_prefab_uuid(entt::handle entity) -> hpp::uuid;
    
    /**
     * @brief Marks transform properties as changed in prefab override system
     * @param entity Entity whose transform was modified
     * @param position Whether position was changed
     * @param rotation Whether rotation was changed
     * @param scale Whether scale was changed
     * @param skew Whether skew was changed
     */
    static void mark_transform_as_changed(entt::handle entity, bool position, bool rotation, bool scale, bool skew);
    static void mark_transform_global_as_changed(entt::handle entity, bool position, bool rotation, bool scale, bool skew);

    /**
     * @brief Marks entity active state as changed in prefab override system
     * @param entity Entity whose active state was modified
     */
    static void mark_active_as_changed(entt::handle entity);
    
    /**
     * @brief Marks text area as changed in prefab override system
     * @param entity Entity whose text area was modified
     */
    static void mark_text_area_as_changed(entt::handle entity);
    static void mark_ui_document_size_as_changed(entt::handle entity);
    /**
     * @brief Marks material as changed in prefab override system
     * @param entity Entity whose material was modified
     */
    static void mark_material_as_changed(entt::handle entity);

    /**
     * @brief Marks a specific property as changed using component type
     * @param entity Entity that was modified
     * @param component_type Meta type of the component
     * @param property_path Path to the specific property that changed
     */
    /**
     * @brief Records what a reparent means for the prefabs involved. Call after the move.
     *
     * Within one instance: parent, position and rotation become local overrides on the nearest
     * root (the prefab restates the old placement otherwise). Out of the instance that supplied
     * the subtree: the subtree is this scene's content now - prefab ids and slots dropped - and
     * removals are stated on the instances that supplied it, so their replays do not bring it
     * back. Into an instance from outside, or between plain entities: nothing.
     */
    static auto mark_entity_reparented(entt::handle entity, entt::handle old_parent, entt::handle new_parent)
        -> prefab_reparent_record;

    /// Undoes mark_entity_reparented. Call after the parent is restored.
    static void restore_entity_reparent(const prefab_reparent_record& record);

    static void mark_property_as_changed(entt::handle entity,
                                         const entt::meta_type& component_type,
                                         const std::string& property_path,
                                         const std::string& property_pretty_path = {});

    /**
     * @brief Marks a specific property as changed using component type names
     * @param entity Entity that was modified
     * @param component_type_name Technical name of the component type
     * @param component_pretty_type_name Human-readable name of the component type
     * @param property_path Path to the specific property that changed
     */
    static void mark_property_as_changed(entt::handle entity,
                                        const std::string& component_type_name,
                                        const std::string& component_pretty_type_name,
                                        const std::string& property_path,
                                        const std::string& property_pretty_path = {});
    
    /**
     * @brief Checks if a property exists in the original prefab
     * @param cache_scene Temporary scene for prefab loading
     * @param prefab Handle to the prefab asset
     * @param entity_uuid UUID of the entity in the prefab
     * @param component_type Type name of the component
     * @param property_path Path to the property
     * @return true if the property exists in the original prefab
     */
    static auto exists_in_prefab(scene& cache_scene,
                                 const asset_handle<prefab>& prefab,
                                 hpp::uuid entity_uuid,
                                 const std::string& component_type,
                                 const std::string& property_path) -> bool;

    /**
     * @brief Marks an entity as removed from the prefab instance
     * @param entity Entity that was removed
     * @return What was changed, to hand to restore_entity_removal on undo
     */
    static auto mark_entity_as_removed(entt::handle entity) -> prefab_removal_record;

    /**
     * @brief Undoes exactly what mark_entity_as_removed recorded.
     */
    static void restore_entity_removal(const prefab_removal_record& record);
};

auto add_property_action(rtti::context& ctx,
                         prefab_override_context& override_ctx,
                         inspect_result& result,
                         meta_any_proxy& var_proxy,
                         const entt::meta_any& old_var,
                         const entt::meta_any& new_var,
                         const entt::meta_custom& custom) -> bool;
/**
 * @brief Pushes debug view mode (increases debug view counter)
 */
void push_debug_view();

/**
 * @brief Pops debug view mode (decreases debug view counter)
 */
void pop_debug_view();

/**
 * @brief Checks if currently in debug view mode
 * @return true if debug view counter is greater than 0
 */
auto is_debug_view() -> bool;

/**
 * @brief Main entry point for inspecting any variable with automatic type resolution
 * @param ctx Runtime type information context
 * @param var Variable to inspect (will be modified if changed)
 * @param var_proxy Proxy for accessing the variable
 * @param info Metadata about the variable (read-only, property flags, etc.)
 * @param custom Custom attributes for specialized behavior
 * @return Result indicating what changes occurred during inspection
 */
auto inspect_var(rtti::context& ctx,
                 entt::meta_any& var,
                 const meta_any_proxy& var_proxy,
                 const var_info& info = {},
                 const entt::meta_custom& custom = {}) -> inspect_result;

/**
 * @brief Inspects all properties of a complex object recursively
 * @param ctx Runtime type information context
 * @param var Object whose properties to inspect
 * @param var_proxy Proxy for accessing the object
 * @param info Metadata about the object
 * @param custom Custom attributes for specialized behavior
 * @return Result indicating what changes occurred during inspection
 */
auto inspect_var_properties(rtti::context& ctx,
                                entt::meta_any& var,
                                const meta_any_proxy& var_proxy,
                                const var_info& info = {},
                                const entt::meta_custom& custom = {}) -> inspect_result;

/**
 * @brief Inspects array-like containers with add/remove functionality
 * @param ctx Runtime type information context
 * @param var Array container to inspect
 * @param var_proxy Proxy for accessing the container
 * @param prop Meta property information for the array
 * @param info Metadata about the container
 * @param custom Custom attributes for specialized behavior
 * @return Result indicating what changes occurred during inspection
 */
auto inspect_array(rtti::context& ctx,
                       entt::meta_any& var,
                       const meta_any_proxy& var_proxy,
                       const entt::meta_data& prop,
                       const var_info& info = {},
                       const entt::meta_custom& custom = {}) -> inspect_result;

/**
 * @brief Inspects array-like containers with custom name and tooltip
 * @param ctx Runtime type information context
 * @param var Array container to inspect
 * @param var_proxy Proxy for accessing the container
 * @param name Display name for the array
 * @param tooltip Help text for the array
 * @param info Metadata about the container
 * @param custom Custom attributes for specialized behavior
 * @return Result indicating what changes occurred during inspection
 */
auto inspect_array(rtti::context& ctx,
                       entt::meta_any& var,
                       const meta_any_proxy& var_proxy,
                       const std::string& name,
                       const std::string& tooltip,
                       const var_info& info = {},
                       const entt::meta_custom& custom = {}) -> inspect_result;

/**
 * @brief Inspects associative containers like maps and sets
 * @param ctx Runtime type information context
 * @param var Associative container to inspect
 * @param var_proxy Proxy for accessing the container
 * @param prop Meta property information for the container
 * @param info Metadata about the container
 * @param custom Custom attributes for specialized behavior
 * @return Result indicating what changes occurred during inspection
 */
auto inspect_associative_container(rtti::context& ctx,
                                   entt::meta_any& var,
                                   const meta_any_proxy& var_proxy,
                                   const entt::meta_data& prop,
                                   const var_info& info = {},
                                   const entt::meta_custom& custom = {}) -> inspect_result;

/**
 * @brief Inspects enumeration types with dropdown selection
 * @param ctx Runtime type information context
 * @param var Enum variable to inspect
 * @param var_proxy Proxy for accessing the enum
 * @param info Metadata about the enum
 * @return Result indicating what changes occurred during inspection
 */
auto inspect_enum(rtti::context& ctx,
                  entt::meta_any& var,
                  const meta_any_proxy& var_proxy,
                  const var_info& info = {}) -> inspect_result;

/**
 * @brief Convenience template function for inspecting objects of known type
 * @tparam T Type of the object to inspect
 * @param ctx Runtime type information context
 * @param obj Object to inspect
 * @return Result indicating what changes occurred during inspection
 */
template<typename T>
auto inspect(rtti::context& ctx, T& obj, const std::string& name = {}) -> inspect_result
{
    entt::meta_any var = entt::forward_as_meta(obj);
    return inspect_var(ctx, var, make_proxy(var, name));
}

} // namespace unravel