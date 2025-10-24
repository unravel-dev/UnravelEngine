#pragma once

#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/particles/ps/particle_system.h>
#include <graphics/texture.h>
#include <engine/assets/asset_handle.h>
#include <bx/easing.h>
#include <glm/vec3.hpp>
#include <math/math.h>
#include <base/basetypes.hpp>
#include <array>


namespace unravel
{

/**
 * @class particle_emitter_component
 * @brief Component that wraps particle system emitter functionality.
 */
class particle_emitter_component : public component_crtp<particle_emitter_component, owned_component>
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
     * @brief Sets whether the emitter is enabled.
     * @param enabled True if enabled, false otherwise.
     */
    void set_enabled(bool enabled);

    /**
     * @brief Checks if the emitter is enabled.
     * @return True if enabled, false otherwise.
     */
    auto is_enabled() const -> bool;

    /**
     * @brief Gets the emitter handle.
     * @return The emitter handle.
     */
    auto get_emitter_handle() const -> EmitterHandle;

    /**
     * @brief Sets the emitter shape.
     * @param shape The emitter shape.
     */
    void set_shape(EmitterShape::Enum shape);

    /**
     * @brief Gets the emitter shape.
     * @return The emitter shape.
     */
    auto get_shape() const -> EmitterShape::Enum;

    /**
     * @brief Sets the emitter direction.
     * @param direction The emitter direction.
     */
    void set_direction(EmitterDirection::Enum direction);

    /**
     * @brief Gets the emitter direction.
     * @return The emitter direction.
     */
    auto get_direction() const -> EmitterDirection::Enum;

    /**
     * @brief Sets the maximum number of particles.
     * @param max_particles Maximum particles count.
     */
    void set_max_particles(uint32_t max_particles);

    /**
     * @brief Gets the maximum number of particles.
     * @return Maximum particles count.
     */
    auto get_max_particles() const -> uint32_t;


    // Particle emission properties
    void set_emission_lifetime(std::chrono::duration<float> lifetime);
    auto get_emission_lifetime() const -> std::chrono::duration<float>;

    void set_gravity_scale(float scale);
    auto get_gravity_scale() const -> float;

    void set_emission_rate(float emission_rate);
    auto get_emission_rate() const -> float;

    void set_temporal_motion(float temporal_motion);
    auto get_temporal_motion() const -> float;

    void set_velocity_damping(float velocity_damping);
    auto get_velocity_damping() const -> float;

    void set_force_over_lifetime(const math::vec3& force);
    auto get_force_over_lifetime() const -> math::vec3;

    void set_emission_shape_scale(const math::vec3& scale);
    auto get_emission_shape_scale() const -> math::vec3;

    void set_size_by_speed_range(const frange_t& size_range);
    auto get_size_by_speed_range() const -> const frange_t&;

    void set_size_by_speed_velocity_range(const frange_t& velocity_range);
    auto get_size_by_speed_velocity_range() const -> const frange_t&;

    void set_color_by_speed_gradient(const math::gradient<math::color>& gradient);
    auto get_color_by_speed_gradient() const -> const math::gradient<math::color>&;

    void set_color_by_speed_velocity_range(const frange_t& velocity_range);
    auto get_color_by_speed_velocity_range() const -> const frange_t&;

    void set_lifetime_by_emitter_speed_gradient(const math::gradient<float>& gradient);
    auto get_lifetime_by_emitter_speed_gradient() const -> const math::gradient<float>&;

    void set_lifetime_by_emitter_speed_range(const frange_t& speed_range);
    auto get_lifetime_by_emitter_speed_range() const -> const frange_t&;

    // Lifetime properties
    void set_lifetime(std::chrono::duration<float> lifetime);
    auto get_lifetime() const -> std::chrono::duration<float>;

    // Velocity properties
    void set_velocity_gradient(const math::gradient<frange_t>& gradient);
    auto get_velocity_gradient() const -> const math::gradient<frange_t>&;

    // Scale properties
    void set_scale_gradient(const math::gradient<frange_t>& gradient);
    auto get_scale_gradient() const -> const math::gradient<frange_t>&;

    // Blend properties
    void set_blend_gradient(const math::gradient<frange_t>& gradient);
    auto get_blend_gradient() const -> const math::gradient<frange_t>&;

    void set_blend_multiplier(float multiplier);
    auto get_blend_multiplier() const -> float;

    // Playback control
    void play();
    void stop();
    void stop_and_reset();
    void pause();
    void resume();
    auto is_playing() const -> bool;
    auto is_paused() const -> bool;

    // Color properties
    void set_color_gradient(const math::gradient<math::color>& gradient);
    auto get_color_gradient() const -> const math::gradient<math::color>&;

    // Easing functions (only position easing remains - others handled by gradients)
    void set_position_easing(bx::Easing::Enum easing);
    auto get_position_easing() const -> bx::Easing::Enum;

    // Simulation method properties
    void set_simulation_space(SimulationSpace::Enum space);
    auto get_simulation_space() const -> SimulationSpace::Enum;

    auto get_num_particles() const -> uint32_t;
    auto get_world_bounds() const -> math::bbox;
    auto get_updated_world_bounds(const math::transform& world_transform) const -> math::bbox;

    // Sprite handle
    void set_texture(const asset_handle<gfx::texture>& texture);
    auto get_texture() const -> const asset_handle<gfx::texture>&;

    /**
     * @brief Updates the emitter with external transform data.
     * @param world_transform The world transform to apply to the emitter.
     */
    void update_emitter(const math::transform& world_transform, delta_t dt);

    /**
     * @brief Gets the emitter uniforms for direct access.
     * @return Reference to the emitter uniforms.
     */
    auto get_uniforms() const -> const EmitterUniforms&;

    /**
     * @brief Recreates the emitter with current shape and direction.
     */
    void recreate_emitter();

    /**
     * @brief Resets the emitter, clearing all particles and resetting internal state.
     */
    void reset_emitter();

private:
    /// Whether the emitter is enabled
    bool enabled_ = true;

    /// Emitter shape
    EmitterShape::Enum shape_ = EmitterShape::Sphere;

    /// Emitter direction
    EmitterDirection::Enum direction_ = EmitterDirection::Up;

    /// Maximum number of particles
    uint32_t max_particles_ = 1024;

    /// Particle system emitter handle
    EmitterHandle emitter_handle_{UINT16_MAX};

    /// Emitter uniforms containing all particle properties
    EmitterUniforms uniforms_;
    
    asset_handle<gfx::texture> texture_;
};

} // namespace unravel
