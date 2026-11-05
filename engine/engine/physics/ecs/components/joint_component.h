#pragma once
#include <engine/engine_export.h>

#include <engine/ecs/components/basic_component.h>
#include <math/math.h>

#include <bitset>
#include <cstdint>

namespace unravel
{

/// Box3D asserts that a ball joint's cone half angle is at most a quarter turn, in degrees.
constexpr float MAX_CONE_ANGLE = 90.0f;

/**
 * @enum joint_type
 * @brief How a joint lets its two bodies move against each other. Persisted by value: append only.
 *
 * The joint's axis is the X axis of its frame (the joint entity's transform turned by the axis rotation), as in
 * Unreal's constraints: hinges turn about it, sliders move along it, a ball joint twists about it and swings in a
 * cone around it, a wheel spins about it.
 */
enum class joint_type : uint8_t
{
    /// Holds the bodies together rigidly.
    fixed,
    /// Turns about the axis (doors, levers, wheels on an axle).
    hinge,
    /// Slides along the axis without turning (pistons, lifts).
    slider,
    /// Turns any way about the anchor (shoulders, chains), limited to a cone and a twist range.
    ball,
    /// Keeps the anchor on body A and a point on body B at a distance (ropes, springs).
    distance,
    /// Spins about the axis, travels on a suspension along the frame's Y axis and steers about it (vehicles).
    wheel,
};

/**
 * @struct joint_limits
 * @brief The range a joint moves in. Angles are in degrees, distances in meters.
 */
struct joint_limits
{
    friend auto operator==(const joint_limits& lhs, const joint_limits& rhs) -> bool = default;

    bool enabled{false};
    /// Hinge and ball twist: lowest angle. Slider and wheel suspension: lowest position. Distance: shortest length.
    float lower{-45.0f};
    /// Hinge and ball twist: highest angle. Slider and wheel suspension: highest position. Distance: longest length.
    float upper{45.0f};
    /// Ball: half angle of the cone the axis may swing in, at most MAX_CONE_ANGLE.
    float cone_angle{45.0f};
};

/**
 * @struct joint_spring
 * @brief Pulls a joint towards a target, stated as a frequency and a damping ratio: mass independent, stable at any
 *        stiffness. A fixed joint uses it to become soft.
 */
struct joint_spring
{
    friend auto operator==(const joint_spring& lhs, const joint_spring& rhs) -> bool = default;

    bool enabled{false};
    /// Oscillations per second. Higher is stiffer.
    float frequency{2.0f};
    /// 0 bounces forever, 1 settles without overshoot.
    float damping_ratio{0.7f};
    /// Hinge: angle in degrees. Slider: position in meters. The wheel suspension and the distance spring rest at 0
    /// and at the joint's length.
    float target{0.0f};
    /// Ball: the rotation of body B's frame against body A's the spring turns towards, Euler angles in degrees.
    math::vec3 target_rotation{0.0f};
};

/**
 * @struct joint_motor
 * @brief Drives a joint at a speed with at most a force (or torque).
 */
struct joint_motor
{
    friend auto operator==(const joint_motor& lhs, const joint_motor& rhs) -> bool = default;

    bool enabled{false};
    /// Hinge and wheel: degrees per second. Slider and distance: meters per second.
    float speed{90.0f};
    /// Hinge and wheel: newton meters. Slider and distance: newtons.
    float max_force{100.0f};
};

/**
 * @struct joint_steering
 * @brief Turns a wheel about its frame's Y axis, like a car's front wheels.
 */
struct joint_steering
{
    friend auto operator==(const joint_steering& lhs, const joint_steering& rhs) -> bool = default;

    bool enabled{false};
    /// The angle the wheel steers to, in degrees.
    float angle{0.0f};
    /// The largest angle either way, in degrees.
    float limit{35.0f};
    /// Newton meters available to steer with.
    float max_torque{500.0f};
};

/**
 * @struct joint_state
 * @brief What the simulation reports about a joint after every fixed step. Runtime only.
 */
struct joint_state
{
    /// The joint exists in the running simulation.
    bool is_active{false};
    /// The joint broke (a force or torque above its threshold, a call, or one of its bodies destroyed); it stays broken
    /// until repair() or the end of play.
    bool is_broken{false};
    /// Hinge: angle in degrees. Ball: twist in degrees. Wheel: steering angle in degrees.
    float angle{0.0f};
    /// Slider: position in meters. Distance: length in meters.
    float position{0.0f};
    /// Hinge and wheel spin: degrees per second. Slider: meters per second.
    float speed{0.0f};
    /// Force and torque the joint applies to hold the bodies, in world space.
    math::vec3 force{0.0f};
    math::vec3 torque{0.0f};
};

/**
 * @class joint_component
 * @brief Connects two bodies so they move together in a constrained way.
 *
 * A joint is an entity of its own, like Unreal's physics constraint actor and Godot's Joint3D: its transform, the
 * anchor offset and the axis rotation make the joint frame, read once when the joint is created (at play start, or
 * when the component appears during play) and fixed to both bodies from then on. Body A is the body the joint hangs
 * on; none means the world. Body B is the body it holds; none means the joint's own entity, which then needs a
 * physics component.
 */
class joint_component : public component_crtp<joint_component, owned_component>
{
public:
    static void on_create_component(entt::registry& r, entt::entity e);
    static void on_destroy_component(entt::registry& r, entt::entity e);

    void set_type(joint_type type);
    auto get_type() const noexcept -> joint_type;

    void set_body_a(entt::handle body);
    auto get_body_a() const noexcept -> entt::handle;

    void set_body_b(entt::handle body);
    auto get_body_b() const noexcept -> entt::handle;

    /// Body B as the simulation sees it: the set body (even destroyed), else the joint's own entity.
    auto get_resolved_body_b() noexcept -> entt::handle;

    /// Whether a body was set and its entity has been destroyed since; the joint then breaks.
    auto is_body_missing() const noexcept -> bool;

    /// Offset of the joint frame from the entity, in the entity's space.
    void set_anchor(const math::vec3& anchor);
    auto get_anchor() const noexcept -> const math::vec3&;

    /// Turn of the joint frame against the entity, Euler angles in degrees.
    void set_axis_rotation(const math::vec3& rotation);
    auto get_axis_rotation() const noexcept -> const math::vec3&;

    /// Whether the two jointed bodies still collide with each other.
    void set_collide_connected(bool collide);
    auto get_collide_connected() const noexcept -> bool;

    void set_limits(const joint_limits& limits);
    auto get_limits() const noexcept -> const joint_limits&;

    void set_spring(const joint_spring& spring);
    auto get_spring() const noexcept -> const joint_spring&;

    void set_motor(const joint_motor& motor);
    auto get_motor() const noexcept -> const joint_motor&;

    void set_steering(const joint_steering& steering);
    auto get_steering() const noexcept -> const joint_steering&;

    /// Resistance to motion while the motor is off, in newton meters (newtons for a slider): stiff hinges, creaking doors.
    void set_friction(float friction);
    auto get_friction() const noexcept -> float;

    /// Distance joint: the length it keeps, in meters. 0 takes the distance between the anchors when it is created.
    void set_distance(float distance);
    auto get_distance() const noexcept -> float;

    /// Distance joint: the point it holds on body B, in body B's space.
    void set_connected_anchor(const math::vec3& anchor);
    auto get_connected_anchor() const noexcept -> const math::vec3&;

    /// Force in newtons above which the joint breaks; 0 never breaks.
    void set_break_force(float force);
    auto get_break_force() const noexcept -> float;

    /// Torque in newton meters above which the joint breaks; 0 never breaks.
    void set_break_torque(float torque);
    auto get_break_torque() const noexcept -> float;

    /// Breaks the joint now, as if a force broke it.
    void break_joint();
    /// A broken joint comes back, holding the bodies where they are.
    void repair();

    /// The simulation's report on this joint.
    auto get_state() const noexcept -> const joint_state&;
    auto get_state() noexcept -> joint_state&;

    /// The joint frame in world space, as it would be created now.
    auto get_frame_global() const -> math::transform;

    auto is_dirty(uint8_t id) const noexcept -> bool;
    void set_dirty(uint8_t id, bool dirty) noexcept;

    /// Whether what changed needs the joint to be made again (type, bodies, frame), not only retuned.
    auto needs_rebuild() const noexcept -> bool;
    void set_needs_rebuild(bool rebuild) noexcept;

    /// A break or a repair asked for from outside the simulation, consumed by the backend.
    auto take_requested_break() noexcept -> bool;
    auto take_requested_repair() noexcept -> bool;

private:
    void mark_changed(bool rebuild);

    joint_type type_{joint_type::ball};
    entt::handle body_a_{};
    entt::handle body_b_{};
    math::vec3 anchor_{0.0f};
    math::vec3 axis_rotation_{0.0f};
    bool collide_connected_{false};

    joint_limits limits_{};
    joint_spring spring_{};
    joint_motor motor_{};
    joint_steering steering_{};
    float friction_{0.0f};
    float distance_{0.0f};
    math::vec3 connected_anchor_{0.0f};
    float break_force_{0.0f};
    float break_torque_{0.0f};

    joint_state state_{};
    bool needs_rebuild_{true};
    bool requested_break_{false};
    bool requested_repair_{false};
    std::bitset<8> dirty_;
};

} // namespace unravel
