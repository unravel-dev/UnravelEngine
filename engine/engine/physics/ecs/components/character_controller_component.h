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
 * @class character_controller_component
 * @brief Component for character controller physics with sweep-based movement.
 *
 * Uses a capsule shape internally. Movement is performed via move(displacement)
 * rather than forces/impulses. Mutually exclusive with physics_component.
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
     * @brief Queue a collision-constrained displacement applied once on the next
     *        physics step. Not re-applied across fixed-step catch-up.
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

    using underlying_t = std::underlying_type_t<character_controller_property>;
    std::bitset<static_cast<underlying_t>(character_controller_property::count)> dirty_properties_;
    std::bitset<8> dirty_;
};

} // namespace unravel
