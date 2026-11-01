#pragma once
#include <engine/engine_export.h>

#include <engine/ecs/components/basic_component.h>
#include <engine/layers/layer_mask.h>
#include <math/math.h>

#include <bitset>

namespace unravel
{

/**
 * @enum character_controller_property
 * @brief Enum for trackable character controller properties.
 */
enum class character_controller_property : uint8_t
{
    shape,
    step_height,
    slope_limit,
    skin_width,
    gravity_scale,
    layer,
    movement_params,
    count
};

/**
 * @struct character_motion
 * @brief What fixed_step_runner keeps to show a character between fixed steps. Runtime state, not serialized.
 */
struct character_motion
{
    /// Moves waiting for a fixed step, and the game time they span.
    math::vec3 queued_move{};
    float queued_time{};
    /// Where the last fixed step began and ended. The end is where the character really is.
    math::vec3 step_start{};
    math::vec3 step_end{};
    /// The position last written to the transform. A transform holding anything else was moved by someone else.
    math::vec3 shown{};
    bool is_tracked{false};

    /**
     * @brief Starts over at a position, as if the character had always stood there. Queued moves stay.
     */
    void place_at(const math::vec3& position) noexcept;

    /**
     * @brief Counts a frame's game time for the moves waiting, up to a limit that keeps a slow frame rate from
     *        building a backlog.
     */
    void add_queued_time(float seconds, float max_seconds) noexcept;

    /**
     * @brief Takes the share of the queued moves one fixed step covers.
     *
     * Moves arrive per frame and steps per fixed time: handing a step all that waits gives it one frame's move, then
     * two, and a character shown between steps speeds up and slows down with them. A share by time gives every step
     * of a steady walk the same length.
     */
    auto take_step_move(float step_seconds) noexcept -> math::vec3;
};

/**
 * @class character_controller_component
 * @brief Component for character controller physics with sweep-based movement.
 *
 * Uses a capsule shape internally. Movement is performed via move(displacement)
 * rather than forces/impulses. Mutually exclusive with physics_component.
 *
 * With interpolation on (the default) the entity's transform is placed between the last two fixed steps every frame,
 * so it trails the character's physics position by at most one step and moves evenly at any frame rate. During a
 * fixed step it holds the physics position. Writing the position or warping teleports at once.
 */
class character_controller_component : public component_crtp<character_controller_component, owned_component>
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

    void set_radius(float radius);
    auto get_radius() const noexcept -> float;

    void set_height(float height);
    auto get_height() const noexcept -> float;

    void set_center(const math::vec3& center);
    auto get_center() const noexcept -> const math::vec3&;

    void set_step_height(float step_height);
    auto get_step_height() const noexcept -> float;

    void set_slope_limit(float slope_limit_degrees);
    auto get_slope_limit() const noexcept -> float;

    void set_skin_width(float skin_width);
    auto get_skin_width() const noexcept -> float;

    void set_gravity_scale(float scale);
    auto get_gravity_scale() const noexcept -> float;

    void set_collision_include_mask(layer_mask mask);
    auto get_collision_include_mask() const -> layer_mask;

    void set_collision_exclude_mask(layer_mask mask);
    auto get_collision_exclude_mask() const -> layer_mask;

    auto get_collision_mask() const -> layer_mask;

    /**
     * @brief Queue a collision-constrained displacement. Applied once, spread over the
     *        fixed steps that cover the frames it was queued in; from a fixed update, in
     *        that step. Not re-applied across fixed-step catch-up.
     */
    void move(const math::vec3& displacement);

    /**
     * @brief Trigger a jump straight up with the given speed (m/s).
     * @param speed Initial upward speed; must be > 0.
     */
    void jump(float speed);

    /**
     * @brief Trigger a directional jump. The velocity's magnitude is the jump speed
     *        and its direction is the jump axis.
     * @param velocity World-space jump velocity; must be non-zero.
     */
    void jump(const math::vec3& velocity);

    void apply_impulse(const math::vec3& impulse);
    void warp(const math::vec3& position);

    void set_terminal_velocity(float speed);
    auto get_terminal_velocity() const noexcept -> float;

    void set_linear_velocity(const math::vec3& velocity);
    auto get_linear_velocity() const noexcept -> math::vec3;

    void set_linear_damping(float damping);
    auto get_linear_damping() const noexcept -> float;

    auto can_jump() const noexcept -> bool;
    auto is_grounded() const noexcept -> bool;
    auto get_velocity() const noexcept -> const math::vec3&;

    auto is_dirty(uint8_t id) const noexcept -> bool;
    void set_dirty(uint8_t id, bool dirty) noexcept;

    auto is_property_dirty(character_controller_property prop) const noexcept -> bool;
    auto are_any_properties_dirty() const noexcept -> bool;
    void set_property_dirty(character_controller_property prop, bool dirty) noexcept;

    void set_grounded(bool grounded) noexcept;
    void set_velocity_internal(const math::vec3& vel) noexcept;

    /**
     * @brief Sets whether the transform is placed between fixed steps every frame.
     */
    void set_interpolate(bool interpolate) noexcept;
    auto get_interpolate() const noexcept -> bool;

    /**
     * @brief Where the last fixed step left the character (entity position, without the center), or where a teleport
     *        placed it since. The transform trails this while interpolating.
     */
    auto get_position() const -> math::vec3;

    auto get_motion() noexcept -> character_motion&;

private:
    void on_change_shape();

    float radius_{0.5f};
    float height_{1.0f};
    math::vec3 center_{};
    float step_height_{0.3f};
    float slope_limit_{45.0f};
    float skin_width_{0.08f};
    float gravity_scale_{1.0f};

    float terminal_velocity_{55.0f};
    float linear_damping_{0.0f};

    layer_mask collision_include_mask_{layer_reserved::everything_layer};
    layer_mask collision_exclude_mask_{layer_reserved::nothing_layer};

    math::vec3 velocity_{};
    bool grounded_{false};

    bool interpolate_{true};
    character_motion motion_{};

    using underlying_t = std::underlying_type_t<character_controller_property>;
    std::bitset<static_cast<underlying_t>(character_controller_property::count)> dirty_properties_;
    std::bitset<8> dirty_;
};

} // namespace unravel
