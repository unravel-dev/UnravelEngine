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
    void set_emission_lifetime(float lifetime);
    auto get_emission_lifetime() const -> float;

    void set_gravity_scale(float scale);
    auto get_gravity_scale() const -> float;

    void set_explosiveness(float explosiveness);
    auto get_explosiveness() const -> float;

    // Life span properties
    void set_life_span_range(const frange_t& life_span);
    auto get_life_span_range() const -> const frange_t&;

    // Offset properties
    void set_offset_start_range(const math::vec2& offset_start);
    auto get_offset_start_range() const -> const math::vec2&;

    void set_offset_end_range(const math::vec2& offset_end);
    auto get_offset_end_range() const -> const math::vec2&;

    // Scale properties
    void set_scale_start_range(const math::vec2& scale_start);
    auto get_scale_start_range() const -> const math::vec2&;

    void set_scale_end_range(const math::vec2& scale_end);
    auto get_scale_end_range() const -> const math::vec2&;

    // Blend properties
    void set_blend_start_range(const math::vec2& blend_start);
    auto get_blend_start_range() const -> const math::vec2&;

    void set_blend_end_range(const math::vec2& blend_end);
    auto get_blend_end_range() const -> const math::vec2&;

    // Color properties
    void set_rgba_colors(const std::array<math::color, 5>& colors);
    auto get_rgba_colors() const -> const std::array<math::color, 5>&;
    
    void set_rgba_color(int index, const math::color& color);
    auto get_rgba_color(int index) const -> const math::color&;

    // Easing functions
    void set_position_easing(bx::Easing::Enum easing);
    auto get_position_easing() const -> bx::Easing::Enum;

    void set_rgba_easing(bx::Easing::Enum easing);
    auto get_rgba_easing() const -> bx::Easing::Enum;

    void set_blend_easing(bx::Easing::Enum easing);
    auto get_blend_easing() const -> bx::Easing::Enum;

    void set_scale_easing(bx::Easing::Enum easing);
    auto get_scale_easing() const -> bx::Easing::Enum;


    auto get_num_particles() const -> uint32_t;
    auto get_world_bounds() const -> math::bbox;

    // Sprite handle
    void set_texture(const asset_handle<gfx::texture>& texture);
    auto get_texture() const -> const asset_handle<gfx::texture>&;

    /**
     * @brief Updates the emitter with current uniforms.
     */
    void update_emitter();

    void render_emitter(uint8_t view, bgfx::ProgramHandle program, const float* mtxView, const bx::Vec3& eye);

    /**
     * @brief Updates the emitter with external transform data.
     * @param world_transform The world transform to apply to the emitter.
     */
    void update_emitter(const math::transform& world_transform, delta_t dt);

    /**
     * @brief Gets the emitter uniforms for direct access.
     * @return Reference to the emitter uniforms.
     */
    auto get_uniforms() -> EmitterUniforms&;
    auto get_uniforms() const -> const EmitterUniforms&;

    /**
     * @brief Recreates the emitter with current shape and direction.
     */
    void recreate_emitter();

private:
    /**
     * @brief Syncs uniforms from member variables.
     */
    void sync_uniforms_from_members();
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
    
    /// Range properties for easier manipulation
    frange_t life_span_range_{1.0f, 2.0f};
    math::vec2 offset_start_range_{0.0f, 1.0f};
    math::vec2 offset_end_range_{2.0f, 3.0f};
    math::vec2 scale_start_range_{0.1f, 0.2f};
    math::vec2 scale_end_range_{0.3f, 0.4f};
    math::vec2 blend_start_range_{0.8f, 1.0f};
    math::vec2 blend_end_range_{0.0f, 0.2f};
    
    /// Color array for easier manipulation
    std::array<math::color, 5> rgba_colors_{
        math::color(0x00ffffff),  // RGBA0
        math::color(0xffffffff),  // RGBA1
        math::color(0xffffffff),  // RGBA2
        math::color(0xffffffff),  // RGBA3
        math::color(0x00ffffff)   // RGBA4
    };

    asset_handle<gfx::texture> texture_;
};

} // namespace unravel
