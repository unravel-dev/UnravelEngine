#pragma once
#include <engine/engine_export.h>

#include <engine/assets/asset_handle.h>
#include <engine/ecs/components/basic_component.h>
#include <engine/layers/layer_mask.h>
#include <engine/physics/physics_material.h>
#include <engine/physics/physics_types.h>
#include <hpp/variant.hpp>
#include <math/math.h>

#include <bitset>
#include <vector>

namespace unravel
{
class mesh;

/**
 * @struct physics_box_shape
 * @brief Represents a box shape for physics calculations.
 */
struct physics_box_shape
{
    friend auto operator==(const physics_box_shape& lhs, const physics_box_shape& rhs) -> bool = default;

    math::vec3 center{};
    math::vec3 extends{1.0f, 1.0f, 1.0f};
};

/**
 * @struct physics_sphere_shape
 * @brief Represents a sphere shape for physics calculations.
 */
struct physics_sphere_shape
{
    friend auto operator==(const physics_sphere_shape& lhs, const physics_sphere_shape& rhs) -> bool = default;

    math::vec3 center{};
    float radius{0.5f};
};

/**
 * @struct physics_capsule_shape
 * @brief Represents a capsule shape for physics calculations.
 */
struct physics_capsule_shape
{
    friend auto operator==(const physics_capsule_shape& lhs, const physics_capsule_shape& rhs) -> bool = default;

    math::vec3 center{};
    float radius{0.5f};
    float length{1.0f};
};

/**
 * @struct physics_cylinder_shape
 * @brief Represents a cylinder shape for physics calculations.
 */
struct physics_cylinder_shape
{
    friend auto operator==(const physics_cylinder_shape& lhs, const physics_cylinder_shape& rhs) -> bool = default;

    math::vec3 center{};
    float radius{0.5f};
    float length{1.0f};
};

/**
 * @enum mesh_collision_type
 * @brief Specifies the type of mesh collision shape.
 */
enum class mesh_collision_type : uint8_t
{
    convex,
    concave
};

/**
 * @struct physics_mesh_shape
 * @brief Represents a mesh shape for physics calculations.
 */
struct physics_mesh_shape
{
    friend auto operator==(const physics_mesh_shape& lhs, const physics_mesh_shape& rhs) -> bool = default;

    math::vec3 center{};
    asset_handle<mesh> mesh_asset{};
    mesh_collision_type collision_type{mesh_collision_type::concave};
};

/**
 * @struct physics_compound_shape
 * @brief Represents a compound shape that can contain multiple types of shapes.
 */
struct physics_compound_shape
{
    friend auto operator==(const physics_compound_shape& lhs, const physics_compound_shape& rhs) -> bool = default;

    using shape_t =
        hpp::variant<physics_box_shape, physics_sphere_shape, physics_capsule_shape, physics_cylinder_shape, physics_mesh_shape>;

    shape_t shape;
};

/**
 * @class physics_component
 * @brief Component that handles physics properties and behaviors.
 */
class physics_component : public component_crtp<physics_component, owned_component>
{
public:
    static void on_create_component(entt::registry& r, entt::entity e);
    static void on_destroy_component(entt::registry& r, entt::entity e);

    void set_is_using_gravity(bool use_gravity);
    auto is_using_gravity() const noexcept -> bool;

    void set_body_type(rigidbody_type type);
    auto get_body_type() const noexcept -> rigidbody_type;

    void set_is_autoscaled(bool autoscaled);
    auto is_autoscaled() const noexcept -> bool;

    void set_mass(float mass);
    auto get_mass() const noexcept -> float;

    void set_is_sensor(bool sensor);
    auto is_sensor() const noexcept -> bool;

    /**
     * @brief Raw contact policy. Read once per pair when it enters the contact store.
     */
    auto get_contact_event_flags() const noexcept -> contact_event_flags;
    void set_contact_event_flags(contact_event_flags flags);

    void set_sensor_events_enabled(bool enabled);
    auto is_sensor_events_enabled() const noexcept -> bool;

    void set_collision_events_enabled(bool enabled);
    auto is_collision_events_enabled() const noexcept -> bool;

    void set_sensor_exit_on_destroy(bool enabled);
    auto is_sensor_exit_on_destroy() const noexcept -> bool;

    void set_collision_exit_on_destroy(bool enabled);
    auto is_collision_exit_on_destroy() const noexcept -> bool;

    auto is_dirty(uint8_t id) const noexcept -> bool;
    void set_dirty(uint8_t id, bool dirty) noexcept;

    auto is_property_dirty(physics_property prop) const noexcept -> bool;
    auto are_any_properties_dirty() const noexcept -> bool;
    auto are_all_properties_dirty() const noexcept -> bool;
    void set_property_dirty(physics_property prop, bool dirty) noexcept;

    auto get_shapes_count() const -> size_t;
    auto get_shape_by_index(size_t index) const -> const physics_compound_shape&;
    void set_shape_by_index(size_t index, const physics_compound_shape& shape);

    auto get_shapes() const -> const std::vector<physics_compound_shape>&;
    void set_shapes(const std::vector<physics_compound_shape>& shape);

    auto get_material() const -> const asset_handle<physics_material>&;
    void set_material(const asset_handle<physics_material>& material);

    void apply_explosion_force(float explosion_force,
                               const math::vec3& explosion_position,
                               float explosion_radius,
                               float upwards_modifier = 0.0f,
                               force_mode mode = force_mode::force);

    void apply_force(const math::vec3& force, force_mode mode = force_mode::force);
    void apply_torque(const math::vec3& torque, force_mode mode = force_mode::force);

    void set_freeze_rotation(const math::bvec3& xyz);
    void set_freeze_position(const math::bvec3& xyz);

    auto get_freeze_rotation() const -> const math::bvec3&;
    auto get_freeze_position() const -> const math::bvec3&;

    auto get_velocity() const -> const math::vec3&;
    void set_velocity(const math::vec3& velocity);
    void set_velocity_internal(const math::vec3& velocity) noexcept;

    auto get_angular_velocity() const -> const math::vec3&;
    void set_angular_velocity(const math::vec3& velocity);
    void set_angular_velocity_internal(const math::vec3& velocity) noexcept;

    auto get_collision_include_mask() const -> layer_mask;
    void set_collision_include_mask(layer_mask group);

    auto get_collision_exclude_mask() const -> layer_mask;
    void set_collision_exclude_mask(layer_mask group);

    auto get_collision_mask() const -> layer_mask;

    void clear_kinematic_velocities();

private:
    void on_change_gravity();
    void on_change_mass();
    void on_change_kind();
    void on_change_shape();
    void on_change_material();
    void on_change_sensor();
    void on_change_contact_events();
    void set_contact_event_flag(contact_event_flags flag, bool enabled);

    rigidbody_type body_type_{rigidbody_type::static_body};
    bool is_using_gravity_{};
    bool is_sensor_{};
    bool is_autoscaled_{true};
    float mass_{1};

    contact_event_flags contact_events_{contact_event_flags_default};

    layer_mask collision_include_mask_{layer_reserved::everything_layer};
    layer_mask collision_exclude_mask_{layer_reserved::nothing_layer};

    math::vec3 velocity_{};
    math::vec3 angular_velocity_{};

    math::bvec3 freeze_position_xyz_{};
    math::bvec3 freeze_rotation_xyz_{};

    asset_handle<physics_material> material_{};
    std::vector<physics_compound_shape> compound_shape_{};

    using underlying_t = std::underlying_type_t<physics_property>;
    std::bitset<static_cast<underlying_t>(physics_property::count)> dirty_properties_;
    std::bitset<8> dirty_;
};

} // namespace unravel
