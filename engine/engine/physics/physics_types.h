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
 *
 * Persisted by value, so the order is a contract: auto_detect took over the slot Bullet
 * used to have, which is how every legacy project silently moves onto the engine default.
 */
enum class physics_backend_type : uint8_t
{
    /// Engine default. Resolved by resolve_physics_backend.
    auto_detect,
    box3d,
    bullet,
};

/**
 * @brief The concrete backend a selection stands for. Box3D is the engine default.
 */
constexpr auto resolve_physics_backend(physics_backend_type type) noexcept -> physics_backend_type
{
    return type == physics_backend_type::auto_detect ? physics_backend_type::box3d : type;
}

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
    contact_events,
    count
};

/**
 * @enum contact_event_flags
 * @brief Per-body opt-in for contact bookkeeping.
 *
 * A pair is tracked only when at least one side opts into that event kind, and an
 * exit is synthesized on removal only when at least one side opts into it. Pairs
 * nobody asked for are never inserted into the contact store, so they cost nothing
 * to maintain and nothing to tear down.
 */
enum class contact_event_flags : uint8_t
{
    none = 0,
    /// Track sensor overlaps and deliver enter/exit while both sides live.
    sensor_events = 1 << 0,
    /// Track solid contacts and deliver enter/exit while both sides live.
    collision_events = 1 << 1,
    /// Synthesize a sensor exit when either side is destroyed or deactivated.
    sensor_exit_on_destroy = 1 << 2,
    /// Synthesize a collision exit when either side is destroyed or deactivated.
    collision_exit_on_destroy = 1 << 3,
};

/**
 * @brief Default contact policy.
 *
 * Sensors report exit when the other side dies, because that is what sensor logic
 * almost always needs and getting it wrong is silent. Collision exit on destroy is
 * opt-in: a body in permanent contact with the world (a projectile, a ragdoll part)
 * would otherwise pay teardown cost for an event nobody consumes.
 */
inline constexpr auto contact_event_flags_default =
    static_cast<contact_event_flags>(static_cast<uint8_t>(contact_event_flags::sensor_events) |
                                     static_cast<uint8_t>(contact_event_flags::collision_events) |
                                     static_cast<uint8_t>(contact_event_flags::sensor_exit_on_destroy));

constexpr auto operator|(contact_event_flags lhs, contact_event_flags rhs) noexcept -> contact_event_flags
{
    return static_cast<contact_event_flags>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

constexpr auto operator&(contact_event_flags lhs, contact_event_flags rhs) noexcept -> contact_event_flags
{
    return static_cast<contact_event_flags>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
}

constexpr auto operator~(contact_event_flags value) noexcept -> contact_event_flags
{
    return static_cast<contact_event_flags>(~static_cast<uint8_t>(value));
}

constexpr auto has_any(contact_event_flags value, contact_event_flags test) noexcept -> bool
{
    return static_cast<uint8_t>(value & test) != 0;
}

/**
 * @enum contact_end_reason
 * @brief Why an exit event was produced.
 *
 * Mirrors the distinction PhysX draws with eREMOVED_SHAPE_OTHER: gameplay that only
 * decrements a counter treats every reason alike, but anything that reacts to the
 * other side leaving (spawning a trail, re-targeting) must not fire on a death.
 */
enum class contact_end_reason : uint8_t
{
    /// The pair moved apart. The only reason an enter event ever carries.
    separated,
    /// The other entity is being destroyed. Still valid for this callback only.
    other_destroyed,
    /// The other entity was deactivated.
    other_disabled,
    /// This entity is being destroyed.
    self_destroyed,
    /// This entity was deactivated.
    self_disabled,
};

/**
 * @brief Restates a reason from the other participant's point of view.
 *
 * A collision exit is delivered to both sides, and each must be told what happened
 * relative to itself: the body being destroyed sees self_destroyed, the survivor sees
 * other_destroyed.
 */
constexpr auto mirror_contact_end_reason(contact_end_reason reason) noexcept -> contact_end_reason
{
    switch(reason)
    {
        case contact_end_reason::other_destroyed:
            return contact_end_reason::self_destroyed;
        case contact_end_reason::self_destroyed:
            return contact_end_reason::other_destroyed;
        case contact_end_reason::other_disabled:
            return contact_end_reason::self_disabled;
        case contact_end_reason::self_disabled:
            return contact_end_reason::other_disabled;
        case contact_end_reason::separated:
        default:
            return contact_end_reason::separated;
    }
}

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
