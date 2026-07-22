#pragma once

#include <engine/engine_export.h>

#include <entt/entity/entity.hpp>
#include <hpp/optional.hpp>
#include <hpp/small_vector.hpp>
#include <math/math.h>

#include <cstdint>

namespace unravel
{

/**
 * @enum physics_backend_type
 * @brief Runtime selection for create_physics_backend.
 */
enum class physics_backend_type : uint8_t
{
    bullet,
};

/**
 * @enum rigidbody_type
 * @brief Simulation role of a rigid body. Maps to static/kinematic/dynamic across backends.
 */
enum class rigidbody_type : uint8_t
{
    /// Immovable for simulation; ECS may still teleport with AABB/broadphase update cost.
    static_body,
    /// ECS-driven; pushes dynamics via derived velocity over the fixed step.
    kinematic,
    /// Fully simulated; physics writes transforms back to ECS when awake.
    dynamic
};

/**
 * @enum physics_property
 * @brief Dirty flags for physics component properties.
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

/**
 * @enum force_mode
 * @brief How a force or torque value is interpreted when applied.
 */
enum class force_mode : uint8_t
{
    force,
    acceleration,
    impulse,
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
 * @struct physics_body_handle
 * @brief Opaque backend body id. Invalid when generation is zero.
 */
struct physics_body_handle
{
    uint32_t index{0};
    uint32_t generation{0};

    auto is_valid() const noexcept -> bool
    {
        return generation != 0;
    }

    friend auto operator==(const physics_body_handle& lhs, const physics_body_handle& rhs) -> bool = default;
};

template<typename T, size_t SmallSizeCapacity = 8>
using physics_vector = hpp::small_vector<T, SmallSizeCapacity>;

} // namespace unravel
