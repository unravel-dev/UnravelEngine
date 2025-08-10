#pragma once
#include <engine/engine_export.h>

#include <engine/ecs/components/basic_component.h>
#include <engine/physics/physics_material.h>
#include <engine/layers/layer_mask.h>
#include <hpp/variant.hpp>
#include <math/math.h>


#include <bitset>

namespace unravel
{

/**
 * @struct physics_box_shape
 * @brief Represents a box shape for physics calculations.
 */
struct physics_box_shape
{
    friend auto operator==(const physics_box_shape& lhs, const physics_box_shape& rhs) -> bool = default;

    math::vec3 center{};                  ///< Center of the box.
    math::vec3 extends{1.0f, 1.0f, 1.0f}; ///< Extents of the box.
};

/**
 * @struct physics_sphere_shape
 * @brief Represents a sphere shape for physics calculations.
 */
struct physics_sphere_shape
{
    friend auto operator==(const physics_sphere_shape& lhs, const physics_sphere_shape& rhs) -> bool = default;

    math::vec3 center{}; ///< Center of the sphere.
    float radius{0.5f};  ///< Radius of the sphere.
};

/**
 * @struct physics_capsule_shape
 * @brief Represents a capsule shape for physics calculations.
 */
struct physics_capsule_shape
{
    friend auto operator==(const physics_capsule_shape& lhs, const physics_capsule_shape& rhs) -> bool = default;

    math::vec3 center{}; ///< Center of the capsule.
    float radius{0.5f};  ///< Radius of the capsule.
    float length{1.0f};  ///< Length of the capsule.
};

/**
 * @struct physics_cylinder_shape
 * @brief Represents a cylinder shape for physics calculations.
 */
struct physics_cylinder_shape
{
    friend auto operator==(const physics_cylinder_shape& lhs, const physics_cylinder_shape& rhs) -> bool = default;

    math::vec3 center{}; ///< Center of the cylinder.
    float radius{0.5f};  ///< Radius of the cylinder.
    float length{1.0f};  ///< Length of the cylinder.
};

/**
 * @struct physics_compound_shape
 * @brief Represents a compound shape that can contain multiple types of shapes.
 */
struct physics_compound_shape
{
    friend auto operator==(const physics_compound_shape& lhs, const physics_compound_shape& rhs) -> bool = default;

    using shape_t =
        hpp::variant<physics_box_shape, physics_sphere_shape, physics_capsule_shape, physics_cylinder_shape>;

    shape_t shape; ///< The shape contained in the compound shape.
};

/**
 * @enum physics_property
 * @brief Enum for different physics properties.
 */
enum class physics_property : uint8_t
{
    gravity,
    kind,
    mass,
    material,
    shape,
    sensor,
    constraints,
    velocity,
    angular_velocity,
    layer,
    count
};

enum class force_mode : uint8_t
{
    // Interprets the input as torque (measured in Newton-metres), and changes the angular velocity by the value of
    // torque * DT / mass. The effect depends on the simulation step length and the mass of the body.
    force,

    // Interprets the parameter as angular acceleration (measured in degrees per second squared), and changes the
    // angular velocity by the value of torque * DT. The effect depends on the simulation step length but does not
    // depend on the mass of the body.
    acceleration,

    // Interprets the parameter as an angular momentum (measured in kilogram-meters-squared per second), and changes the
    // angular velocity by the value of torque / mass. The effect depends on the mass of the body but doesn't depend on
    // the simulation step length.
    impulse,

    //: Interprets the parameter as a direct angular velocity change (measured in degrees per second), and changes the
    //: angular velocity by the value of torque. The effect doesn't depend on the mass of the body and the simulation
    //: step length.
    velocity_change,
};

struct manifold_point
{
    math::vec3 a{};
    math::vec3 b{};
    math::vec3 normal_on_a{};
    math::vec3 normal_on_b{};
    float distance{};
    float impulse{};
};

struct raycast_hit
{
    entt::entity entity{};
    math::vec3 point{};
    math::vec3 normal{};
    float distance{};
};

/**
 * @class physics_component
 * @brief Component that handles physics properties and behaviors.
 */
class physics_component : public component_crtp<physics_component, owned_component>
{
public:
    /**
     * @brief Called when the component is created.
     * @param r The registry containing the component.
     * @param e The entity associated with the component.
     */
    static void on_create_component(entt::registry& r, entt::entity e);

    /**
     * @brief Called when the component is destroyed.
     * @param r The registry containing the component.
     * @param e The entity associated with the component.
     */
    static void on_destroy_component(entt::registry& r, entt::entity e);

    /**
     * @brief Sets whether the component uses gravity.
     * @param use_gravity True to use gravity, false otherwise.
     */
    void set_is_using_gravity(bool use_gravity);

    /**
     * @brief Checks if the component uses gravity.
     * @return True if using gravity, false otherwise.
     */
    auto is_using_gravity() const noexcept -> bool;

    /**
     * @brief Sets whether the component is kinematic.
     * @param kinematic True if kinematic, false otherwise.
     */
    void set_is_kinematic(bool kinematic);

    /**
     * @brief Checks if the component is kinematic.
     * @return True if kinematic, false otherwise.
     */
    auto is_kinematic() const noexcept -> bool;

    /**
     * @brief Sets whether to autoscale the physics shape.
     * @param autoscaled True if autoscaled, false otherwise.
     */
    void set_is_autoscaled(bool autoscaled);

    /**
     * @brief Checks if the physics shape is autoscaled.
     * @return True if autoscaled, false otherwise.
     */
    auto is_autoscaled() const noexcept -> bool;

    /**
     * @brief Sets the mass of the component.
     * @param mass The mass to set.
     */
    void set_mass(float mass);

    /**
     * @brief Gets the mass of the component.
     * @return The mass of the component.
     */
    auto get_mass() const noexcept -> float;

    /**
     * @brief Sets whether the component is a sensor.
     * @param sensor True if sensor, false otherwise.
     */
    void set_is_sensor(bool sensor);

    /**
     * @brief Checks if the component is a sensor.
     * @return True if sensor, false otherwise.
     */
    auto is_sensor() const noexcept -> bool;

    /**
     * @brief Checks if a specific property is dirty.
     * @param id The property ID.
     * @return True if the property is dirty, false otherwise.
     */
    auto is_dirty(uint8_t id) const noexcept -> bool;

    /**
     * @brief Sets the dirty flag for a specific property.
     * @param id The property ID.
     * @param dirty True to set the property as dirty, false otherwise.
     */
    void set_dirty(uint8_t id, bool dirty) noexcept;

    /**
     * @brief Checks if a specific physics property is dirty.
     * @param prop The physics property.
     * @return True if the property is dirty, false otherwise.
     */
    auto is_property_dirty(physics_property prop) const noexcept -> bool;

    /**
     * @brief Checks if any properties are dirty.
     * @return True if any properties are dirty, false otherwise.
     */
    auto are_any_properties_dirty() const noexcept -> bool;

    /**
     * @brief Checks if all properties are dirty.
     * @return True if all properties are dirty, false otherwise.
     */
    auto are_all_properties_dirty() const noexcept -> bool;

    /**
     * @brief Sets the dirty flag for a specific physics property.
     * @param prop The physics property.
     * @param dirty True to set the property as dirty, false otherwise.
     */
    void set_property_dirty(physics_property prop, bool dirty) noexcept;

    /**
     * @brief Gets the count of shapes.
     * @return The number of shapes.
     */
    auto get_shapes_count() const -> size_t;

    /**
     * @brief Gets a shape by its index.
     * @param index The index of the shape.
     * @return A constant reference to the shape.
     */
    auto get_shape_by_index(size_t index) const -> const physics_compound_shape&;

    /**
     * @brief Sets a shape by its index.
     * @param index The index of the shape.
     * @param shape The shape to set.
     */
    void set_shape_by_index(size_t index, const physics_compound_shape& shape);

    /**
     * @brief Gets all shapes.
     * @return A constant reference to the vector of shapes.
     */
    auto get_shapes() const -> const std::vector<physics_compound_shape>&;

    /**
     * @brief Sets the shapes.
     * @param shape The vector of shapes to set.
     */
    void set_shapes(const std::vector<physics_compound_shape>& shape);

    /**
     * @brief Gets the material of the component.
     * @return The asset handle to the material.
     */
    auto get_material() const -> const asset_handle<physics_material>&;

    /**
     * @brief Sets the material of the component.
     * @param material The material to set.
     */
    void set_material(const asset_handle<physics_material>& material);

    void apply_explosion_force(float explosion_force,
                               const math::vec3& explosion_position,
                               float explosion_radius,
                               float upwards_modifier = 0.0f,
                               force_mode mode = force_mode::force);

    void apply_force(const math::vec3& force, force_mode mode = force_mode::force);

    /**
     * @brief Applies a torque impulse to the component.
     * @param torque_impulse The torque impulse vector.
     */
    void apply_torque(const math::vec3& torque, force_mode mode = force_mode::force);

    void set_freeze_rotation(const math::bvec3& xyz);
    void set_freeze_position(const math::bvec3& xyz);

    auto get_freeze_rotation() const -> const math::bvec3&;
    auto get_freeze_position() const -> const math::bvec3&;

    auto get_velocity() const -> const math::vec3&;
    void set_velocity(const math::vec3& velocity);

    auto get_angular_velocity() const -> const math::vec3&;
    void set_angular_velocity(const math::vec3& velocity);

    auto get_collision_include_mask() const -> layer_mask;
    void set_collision_include_mask(layer_mask group);

    auto get_collision_exclude_mask() const -> layer_mask;
    void set_collision_exclude_mask(layer_mask group);

    auto get_collision_mask() const -> layer_mask;

    /**
     * @brief Clears kinematic velocities.
     */
    void clear_kinematic_velocities();

private:
    /**
     * @brief Called when the gravity setting is changed.
     */
    void on_change_gravity();

    /**
     * @brief Called when the mass is changed.
     */
    void on_change_mass();

    /**
     * @brief Called when the kinematic setting is changed.
     */
    void on_change_kind();

    /**
     * @brief Called when the shape is changed.
     */
    void on_change_shape();

    /**
     * @brief Called when the material is changed.
     */
    void on_change_material();

    /**
     * @brief Called when the sensor setting is changed.
     */
    void on_change_sensor();

    ///< Indicates if the component is kinematic.
    bool is_kinematic_{};
    ///< Indicates if the component uses gravity.
    bool is_using_gravity_{};
    ///< Indicates if the component is a sensor.
    bool is_sensor_{};
    ///< Indicates if the physics shape is autoscaled with transform.
    bool is_autoscaled_{true};
    ///< The mass of the component.
    float mass_{1};

    layer_mask collision_include_mask_{layer_reserved::everything_layer};

    layer_mask collision_exclude_mask_{layer_reserved::nothing_layer};

    ///< The velocity of the rigidbody
    math::vec3 velocity_{};

    ///< The angular velocity of the rigidbody
    math::vec3 angular_velocity_{};

    ///< Freeze position updates due to physics for each axis.
    math::bvec3 freeze_position_xyz_{};

    ///< Freeze rotation updates due to physics for each axis.
    math::bvec3 freeze_rotation_xyz_{};

    ///< The material of the component.
    asset_handle<physics_material> material_{};

    ///< The vector of compound shapes.
    std::vector<physics_compound_shape> compound_shape_{};

    ///< Bitset for dirty properties.
    using underlying_t = std::underlying_type_t<physics_property>;
    std::bitset<static_cast<underlying_t>(physics_property::count)> dirty_properties_;

    ///< Bitset for general dirty flags.
    std::bitset<8> dirty_;
};

} // namespace unravel
