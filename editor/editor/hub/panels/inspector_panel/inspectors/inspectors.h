#pragma once

#include "inspector.h"
#include "property_path_generator.h"
#include <context/context.hpp>
#include <engine/meta/ecs/entity.hpp>

namespace unravel
{
struct inspector_registry
{
    inspector_registry();

    std::unordered_map<rttr::type, std::shared_ptr<inspector>> type_map;
};

/**
 * @struct prefab_property_override
 * @brief Stores both pretty and serialization paths for a prefab override
 */
struct prefab_property_override
{
    std::string pretty_path;        // Human-readable path for inspector display (e.g., "Transform/Position/X")
    std::string serialization_path; // Technical path for deserialization matching (e.g.,
                                    // "entities[0]/components/Transform/local_transform/position/x")

    prefab_property_override() = default;
    prefab_property_override(const std::string& pretty, const std::string& serialization)
        : pretty_path(pretty)
        , serialization_path(serialization)
    {
    }

    auto operator==(const prefab_property_override& other) const -> bool
    {
        return serialization_path == other.serialization_path;
    }

    auto operator<(const prefab_property_override& other) const -> bool
    {
        return serialization_path < other.serialization_path;
    }
};

/**
 * @struct prefab_override_context
 * @brief Global context for tracking prefab override changes during inspection
 */
struct prefab_override_context
{
    prefab_override_context() = default;

    std::function<bool(const hpp::uuid& entity_uuid, const std::string& component_path)> is_path_overridden_callback;

    // Current property path context
    property_path_context path_context;
    property_path_context pretty_path_context;

    // The prefab root entity that contains the prefab_component
    entt::handle prefab_root_entity;

    scene prefab_scene{"prefab_diff_scene"};

    // Whether we're currently inspecting a prefab instance
    bool is_active = false;

    void set_entity_uuid(const hpp::uuid& uuid);

    void set_component_type(const std::string& type, const std::string& pretty_type);

    void push_segment(const std::string& segment, const std::string& pretty_segment);

    void pop_segment();

    /**
     * @brief Initialize context for inspecting a prefab instance
     * @param entity The prefab instance entity being inspected
     */
    auto begin_prefab_inspection(entt::handle entity) -> bool;

    /**
     * @brief End prefab inspection context
     */
    void end_prefab_inspection();

    /**
     * @brief Record a property override when a change is detected
     * Uses the current property path context to determine what was changed
     * Automatically removes any parent or child overrides to keep only the most specific path
     */
    void record_override();

    auto is_path_overridden() const -> bool
    {
        return is_path_overridden_callback
                   ? is_path_overridden_callback(path_context.get_entity_uuid(),
                                                 path_context.get_current_path_with_component_type())
                   : false;
    }

    static auto find_prefab_root_entity(entt::handle entity) -> entt::handle;
    static auto get_entity_name(entt::handle entity) -> std::string;
    static auto get_entity_prefab_uuid(entt::handle entity) -> hpp::uuid;
    static void set_entity_name(entt::handle entity, const std::string& name);
    static void mark_transform_as_changed(entt::handle entity, bool position, bool rotation, bool scale, bool skew);
    static void mark_active_as_changed(entt::handle entity);
    static void mark_text_area_as_changed(entt::handle entity);
    static void mark_material_as_changed(entt::handle entity);
    static void mark_property_as_changed(entt::handle entity,
                                         const rttr::type& component_type,
                                         const std::string& property_path);
    static auto exists_in_prefab(scene& cache_scene,
                                 const asset_handle<prefab>& prefab,
                                 hpp::uuid entity_uuid,
                                 const std::string& component_type,
                                 const std::string& property_path) -> bool;

    static void mark_entity_as_removed(entt::handle entity);
};

void push_debug_view();
void pop_debug_view();
auto is_debug_view() -> bool;

auto get_meta_empty(const rttr::variant& other) -> rttr::variant;
auto are_property_values_equal(const rttr::variant& val1, const rttr::variant& val2) -> bool;

auto inspect_var(rtti::context& ctx,
                 rttr::variant& var,
                 const var_info& info = {},
                 const inspector::meta_getter& get_metadata = get_meta_empty) -> inspect_result;

auto inspect_var_properties(rtti::context& ctx,
                            rttr::variant& var,
                            const var_info& info = {},
                            const inspector::meta_getter& get_metadata = get_meta_empty) -> inspect_result;

auto inspect_array(rtti::context& ctx,
                   rttr::variant& var,
                   const rttr::property& prop,
                   const var_info& info = {},
                   const inspector::meta_getter& get_metadata = get_meta_empty) -> inspect_result;

auto inspect_array(rtti::context& ctx,
                   rttr::variant& var,
                   const std::string& name,
                   const std::string& tooltip,
                   const var_info& info = {},
                   const inspector::meta_getter& get_metadata = get_meta_empty) -> inspect_result;

auto inspect_associative_container(rtti::context& ctx,
                                   rttr::variant& var,
                                   const rttr::property& prop,
                                   const var_info& info = {},
                                   const inspector::meta_getter& get_metadata = get_meta_empty) -> inspect_result;
auto inspect_enum(rtti::context& ctx,
                  rttr::variant& var,
                  rttr::enumeration& data,
                  const var_info& info = {}) -> inspect_result;

// Refresh inspector by rttr type
auto refresh_inspector(rtti::context& ctx, rttr::type type) -> void;

// Refresh inspector by template type
template<typename T>
auto refresh_inspector(rtti::context& ctx) -> void
{
    refresh_inspector(ctx, rttr::type::get<T>());
}

template<typename T>
auto inspect(rtti::context& ctx, T* obj) -> inspect_result
{
    rttr::variant var = obj;
    return inspect_var(ctx, var);
}

template<typename T>
auto inspect(rtti::context& ctx, T& obj) -> inspect_result
{
    return inspect(ctx, &obj);
}

} // namespace unravel
