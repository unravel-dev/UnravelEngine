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
    /// Euler angles in degrees, in the body's space.
    math::vec3 rotation{};
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
    /// Length of the segment between the two sphere centers, along the local Y axis.
    float length{1.0f};
    /// Euler angles in degrees, in the body's space. Turns the local Y axis the capsule runs along.
    math::vec3 rotation{};
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
    /// Height along the local Y axis.
    float length{1.0f};
    /// Euler angles in degrees, in the body's space.
    math::vec3 rotation{};
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
 * @brief What fixed_step_runner keeps to show a dynamic body between fixed steps: its step poses alone.
 */
using rigidbody_motion = stepped_motion;

/**
 * @class physics_component
 * @brief Component that handles physics properties and behaviors.
 *
 * A dynamic body moves on fixed steps. With interpolation on, the entity's transform is placed between the last two
 * steps every frame, so the body moves evenly at any frame rate and trails its physics pose by at most one step;
 * during a fixed step it holds the physics pose. Writing the transform, in world or local space, teleports the body at
 * once; a moving parent carries it along without losing its travel.
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

    /// Gravity multiplier while Use Gravity is on. 0 floats, negative falls up.
    void set_gravity_scale(float scale);
    auto get_gravity_scale() const noexcept -> float;

    /**
     * @brief Linear damping d, per second: velocity changes by dv/dt = -d * v, so it falls by a factor of e^-d each
     *        second (about 63 percent at 1). 0 keeps it; large values make the body drift as in water.
     */
    void set_linear_damping(float damping);
    auto get_linear_damping() const noexcept -> float;

    /// Angular damping d, per second: angular velocity falls by a factor of e^-d each second, as the linear one does.
    void set_angular_damping(float damping);
    auto get_angular_damping() const noexcept -> float;

    /**
     * @brief Continuous collision against other moving bodies, for small fast bodies (a ball in a pinball
     *        machine). Every dynamic body already sweeps against static geometry.
     */
    void set_is_continuous(bool continuous);
    auto is_continuous() const noexcept -> bool;

    /// Whether the body may fall asleep when it comes to rest. A body that never sleeps costs a solve every step.
    void set_can_sleep(bool can_sleep);
    auto can_sleep() const noexcept -> bool;

    /// Whether the simulation put the body to sleep. Kept by the backend after every step.
    auto is_sleeping() const noexcept -> bool;
    void set_sleeping_internal(bool sleeping) noexcept;

    /**
     * @brief Sets whether a dynamic body's transform is placed between fixed steps every frame. Off by default: the
     *        transform then holds the last step's pose. A script that writes the transform every frame should leave it
     *        off, since each write teleports the body to the trailing pose.
     */
    void set_interpolate(bool interpolate) noexcept;
    auto get_interpolate() const noexcept -> bool;

    auto get_motion() noexcept -> rigidbody_motion&;

    /// Wakes a sleeping dynamic body.
    void wake_up();
    /// Puts a dynamic body to sleep until something touches it.
    void put_to_sleep();

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

    /**
     * @brief Speed of the body's surface along its contacts, in the body's space, without the body moving: a
     *        conveyor belt carries what lies on it.
     */
    void set_surface_velocity(const math::vec3& velocity);
    auto get_surface_velocity() const noexcept -> const math::vec3&;

    void apply_explosion_force(float explosion_force,
                               const math::vec3& explosion_position,
                               float explosion_radius,
                               float upwards_modifier = 0.0f,
                               force_mode mode = force_mode::force);

    void apply_force(const math::vec3& force, force_mode mode = force_mode::force);
    /// Pushes the body at a world point: off its center of mass the push also turns it.
    void apply_force_at_position(const math::vec3& force, const math::vec3& position, force_mode mode = force_mode::force);
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
    bool is_continuous_{};
    bool can_sleep_{true};
    bool is_sleeping_{};
    bool interpolate_{};
    rigidbody_motion motion_{};
    float mass_{1};
    float gravity_scale_{1.0f};
    float linear_damping_{};
    float angular_damping_{};

    contact_event_flags contact_events_{contact_event_flags_default};

    layer_mask collision_include_mask_{layer_reserved::everything_layer};
    layer_mask collision_exclude_mask_{layer_reserved::nothing_layer};

    math::vec3 velocity_{};
    math::vec3 angular_velocity_{};
    math::vec3 surface_velocity_{};

    math::bvec3 freeze_position_xyz_{};
    math::bvec3 freeze_rotation_xyz_{};

    asset_handle<physics_material> material_{};
    std::vector<physics_compound_shape> compound_shape_{};

    using underlying_t = std::underlying_type_t<physics_property>;
    std::bitset<static_cast<underlying_t>(physics_property::count)> dirty_properties_;
    std::bitset<8> dirty_;
};

} // namespace unravel
