#include "particle_emitter_component.h"
#include "engine/rendering/particles/ps/particle_system.h"
#include "math/transform.hpp"
#include <engine/ecs/components/transform_component.h>
#include <logging/logging.h>
#include <engine/rendering/material.h>


namespace math
{
template<>
auto gradient_lerp(const frange_t& start, const frange_t& end, float progress) -> frange_t
{
    return frange_t(gradient_lerp(start.min, end.min, progress), gradient_lerp(start.max, end.max, progress));
}
}


namespace unravel
{

void particle_emitter_component::on_create_component(entt::registry& r, entt::entity e)
{
    entt::handle entity(r, e);
    auto& component = entity.get<particle_emitter_component>();
    component.set_owner(entity);
    
    // Initialize uniforms with default values
    component.uniforms_.reset();
    // Create the particle emitter
    component.recreate_emitter();
}

void particle_emitter_component::on_destroy_component(entt::registry& r, entt::entity e)
{
    entt::handle entity(r, e);
    if(entity.all_of<particle_emitter_component>())
    {
        auto& component = entity.get<particle_emitter_component>();

        // Destroy the emitter if it exists
        if(isValid(component.emitter_handle_))
        {
            psDestroyEmitter(component.emitter_handle_);
            component.emitter_handle_.idx = UINT16_MAX;
        }
    }
}

void particle_emitter_component::set_enabled(bool enabled)
{
    enabled_ = enabled;
}

auto particle_emitter_component::is_enabled() const -> bool
{
    return enabled_;
}

auto particle_emitter_component::get_emitter_handle() const -> EmitterHandle
{
    return emitter_handle_;
}

void particle_emitter_component::set_shape(EmitterShape::Enum shape)
{
    if(shape_ != shape)
    {
        shape_ = shape;
        recreate_emitter();
    }
}

auto particle_emitter_component::get_shape() const -> EmitterShape::Enum
{
    return shape_;
}

void particle_emitter_component::set_direction(EmitterDirection::Enum direction)
{
    if(direction_ != direction)
    {
        direction_ = direction;
        recreate_emitter();
    }
}

auto particle_emitter_component::get_direction() const -> EmitterDirection::Enum
{
    return direction_;
}

void particle_emitter_component::set_max_particles(uint32_t max_particles)
{
    if(max_particles_ != max_particles)
    {
        max_particles_ = max_particles;
        recreate_emitter();
    }
}

auto particle_emitter_component::get_max_particles() const -> uint32_t
{
    return max_particles_;
}


void particle_emitter_component::set_emission_lifetime(std::chrono::duration<float> lifetime)
{
    uniforms_.m_emissionLifetime = lifetime.count();
}

auto particle_emitter_component::get_emission_lifetime() const -> std::chrono::duration<float>
{
    return std::chrono::duration<float>(uniforms_.m_emissionLifetime);
}

void particle_emitter_component::set_gravity_scale(float scale)
{
    uniforms_.m_gravityScale = scale;
}

auto particle_emitter_component::get_gravity_scale() const -> float
{
    return uniforms_.m_gravityScale;
}

void particle_emitter_component::set_emission_rate(float emission_rate)
{
    uniforms_.m_particlesPerSecond = math::max(emission_rate, 0.0f);

    reset_emitter();
}

auto particle_emitter_component::get_emission_rate() const -> float
{
    return uniforms_.m_particlesPerSecond;
}

void particle_emitter_component::set_temporal_motion(float temporal_motion)
{
    uniforms_.m_temporalMotion = math::clamp(temporal_motion, 0.0f, 1.0f);
}

auto particle_emitter_component::get_temporal_motion() const -> float
{
    return uniforms_.m_temporalMotion;
}

void particle_emitter_component::set_velocity_damping(float velocity_damping)
{
    uniforms_.m_velocityDamping = math::clamp(velocity_damping, 0.0f, 1.0f);
}

auto particle_emitter_component::get_velocity_damping() const -> float
{
    return uniforms_.m_velocityDamping;
}

void particle_emitter_component::set_force_over_lifetime(const math::vec3& force)
{
    uniforms_.m_forceOverLifetime = force;
}

auto particle_emitter_component::get_force_over_lifetime() const -> math::vec3
{
    return uniforms_.m_forceOverLifetime;
}

void particle_emitter_component::set_emission_shape_scale(const math::vec3& scale)
{
    uniforms_.m_emissionShapeScale = scale;
}

auto particle_emitter_component::get_emission_shape_scale() const -> math::vec3
{
    return uniforms_.m_emissionShapeScale;
}

void particle_emitter_component::set_size_by_speed_range(const frange_t& size_range)
{
    uniforms_.m_sizeBySpeedRange = size_range;
}

auto particle_emitter_component::get_size_by_speed_range() const -> const frange_t&
{
    return uniforms_.m_sizeBySpeedRange;
}

void particle_emitter_component::set_size_by_speed_velocity_range(const frange_t& velocity_range)
{
    uniforms_.m_sizeBySpeedVelocityRange = velocity_range;
}

auto particle_emitter_component::get_size_by_speed_velocity_range() const -> const frange_t&
{
    return uniforms_.m_sizeBySpeedVelocityRange;
}

void particle_emitter_component::set_color_by_speed_gradient(const math::gradient<math::color>& gradient)
{
    uniforms_.m_colorBySpeedGradient = gradient;
    uniforms_.m_colorBySpeedGradient.generate_lut(256); // Generate LUT for optimization
}

auto particle_emitter_component::get_color_by_speed_gradient() const -> const math::gradient<math::color>&
{
    return uniforms_.m_colorBySpeedGradient;
}

void particle_emitter_component::set_color_by_speed_velocity_range(const frange_t& velocity_range)
{
    uniforms_.m_colorBySpeedVelocityRange = velocity_range;
}

auto particle_emitter_component::get_color_by_speed_velocity_range() const -> const frange_t&
{
    return uniforms_.m_colorBySpeedVelocityRange;
}

void particle_emitter_component::set_lifetime(std::chrono::duration<float> lifetime)
{
    uniforms_.m_lifetime  = math::max(lifetime.count(), 0.0f);
    reset_emitter();
}

auto particle_emitter_component::get_lifetime() const -> std::chrono::duration<float>
{
    return std::chrono::duration<float>(uniforms_.m_lifetime);
}

void particle_emitter_component::set_velocity_gradient(const math::gradient<frange_t>& gradient)
{
    uniforms_.m_velocityGradient = gradient;
    uniforms_.m_velocityGradient.generate_lut(256); // Generate LUT for optimization
}

auto particle_emitter_component::get_velocity_gradient() const -> const math::gradient<frange_t>&
{
    return uniforms_.m_velocityGradient;
}

void particle_emitter_component::set_scale_gradient(const math::gradient<frange_t>& gradient)
{
    uniforms_.m_scaleGradient = gradient;
    uniforms_.m_scaleGradient.generate_lut(256); // Generate LUT for optimization
}

auto particle_emitter_component::get_scale_gradient() const -> const math::gradient<frange_t>&
{
    return uniforms_.m_scaleGradient;
}

void particle_emitter_component::set_blend_gradient(const math::gradient<frange_t>& gradient)
{
    uniforms_.m_blendGradient = gradient;
    uniforms_.m_blendGradient.generate_lut(256); // Generate LUT for optimization
}

auto particle_emitter_component::get_blend_gradient() const -> const math::gradient<frange_t>&
{
    return uniforms_.m_blendGradient;
}

void particle_emitter_component::set_blend_multiplier(float multiplier)
{
    uniforms_.m_blendMultiplier = math::clamp(multiplier, 0.0f, 1.0f);
}

auto particle_emitter_component::get_blend_multiplier() const -> float
{
    return uniforms_.m_blendMultiplier;
}

void particle_emitter_component::set_color_gradient(const math::gradient<math::color>& gradient)
{
    uniforms_.m_colorGradient = gradient;
    uniforms_.m_colorGradient.generate_lut(256); // Generate LUT for optimization
}

auto particle_emitter_component::get_color_gradient() const -> const math::gradient<math::color>&
{
    return uniforms_.m_colorGradient;
}

void particle_emitter_component::set_position_easing(bx::Easing::Enum easing)
{
    uniforms_.m_easePos = easing;
}

auto particle_emitter_component::get_position_easing() const -> bx::Easing::Enum
{
    return uniforms_.m_easePos;
}

// Blend and scale easing removed - interpolation is now handled by gradients

auto particle_emitter_component::get_num_particles() const -> uint32_t
{
    return psGetNumParticles(emitter_handle_);
}

auto particle_emitter_component::get_world_bounds() const -> math::bbox
{
    math::bbox bounds(math::vec3(-1.0f), math::vec3(1.0f));
    psGetAabb(emitter_handle_, bounds);
    return bounds;
}

auto particle_emitter_component::get_updated_world_bounds(const math::transform& world_transform) const -> math::bbox
{
    auto bounds = get_world_bounds();
    if(!psHasUpdated(emitter_handle_))
    {
        bounds.mul(world_transform);
    }
    return bounds;
}

void particle_emitter_component::set_texture(const asset_handle<gfx::texture>& texture)
{
    texture_ = texture;
}

auto particle_emitter_component::get_texture() const -> const asset_handle<gfx::texture>&
{
    if(texture_.is_valid())
    {
        return texture_;
    }
    return material::default_color_map();
}


void particle_emitter_component::update_emitter(const math::transform& world_transform, delta_t dt)
{
    if(isValid(emitter_handle_) && enabled_)
    {
        auto prev_position = uniforms_.m_position;
        // Update position from transform
        uniforms_.m_position = world_transform.get_position();

        uniforms_.m_prevPosition = prev_position;
        
        // Update rotation from transform (convert quaternion to Euler angles)
        const auto& world_rot = world_transform.get_rotation();
        uniforms_.m_angle = math::eulerAngles(glm::inverse(world_rot));
        
        // Update scale from transform
        uniforms_.m_scale = world_transform.get_scale();

        auto tex = [&]()
        {
            if(texture_.is_valid())
            {
                return texture_.get();
            }
            return material::default_color_map().get();
        }();
        uniforms_.m_texture = tex->native_handle();
        
        psUpdateEmitter(emitter_handle_, dt.count(), &uniforms_);

    }
}


auto particle_emitter_component::get_uniforms() const -> const EmitterUniforms&
{
    return uniforms_;
}

void particle_emitter_component::recreate_emitter()
{
    // Destroy existing emitter
    if(isValid(emitter_handle_))
    {
        psDestroyEmitter(emitter_handle_);
        emitter_handle_.idx = UINT16_MAX;
    }
    
    // Create new emitter
    emitter_handle_ = psCreateEmitter(shape_, direction_, max_particles_);
    
    if(!isValid(emitter_handle_))
    {
        APPLOG_ERROR("Failed to create particle emitter");
        return;
    }

}

void particle_emitter_component::reset_emitter()
{
    if(isValid(emitter_handle_))
    {
        psResetEmitter(emitter_handle_);
    }
}


} // namespace unravel
