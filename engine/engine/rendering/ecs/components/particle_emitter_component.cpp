#include "particle_emitter_component.h"
#include "engine/rendering/particles/ps/particle_system.h"
#include "math/transform.hpp"
#include <engine/ecs/components/transform_component.h>
#include <logging/logging.h>
#include <engine/rendering/material.h>

namespace unravel
{

void particle_emitter_component::on_create_component(entt::registry& r, entt::entity e)
{
    entt::handle entity(r, e);
    auto& component = entity.get<particle_emitter_component>();
    component.set_owner(entity);
    
    // Initialize uniforms with default values
    component.uniforms_.reset();
    
    // Sync member variables with uniforms
    component.sync_uniforms_from_members();
    
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


void particle_emitter_component::set_emission_lifetime(float lifetime)
{
    uniforms_.m_emissionLifetime = lifetime;
}

auto particle_emitter_component::get_emission_lifetime() const -> float
{
    return uniforms_.m_emissionLifetime;
}

void particle_emitter_component::set_gravity_scale(float scale)
{
    uniforms_.m_gravityScale = scale;
}

auto particle_emitter_component::get_gravity_scale() const -> float
{
    return uniforms_.m_gravityScale;
}

void particle_emitter_component::set_explosiveness(float explosiveness)
{
    uniforms_.m_explosiveness = explosiveness;
}

auto particle_emitter_component::get_explosiveness() const -> float
{
    return uniforms_.m_explosiveness;
}

void particle_emitter_component::set_life_span_range(const frange_t& life_span)
{
    life_span_range_ = life_span;
    uniforms_.m_lifeSpan[0] = life_span.min;
    uniforms_.m_lifeSpan[1] = life_span.max;
}

auto particle_emitter_component::get_life_span_range() const -> const frange_t&
{
    return life_span_range_;
}

void particle_emitter_component::set_offset_start_range(const math::vec2& offset_start)
{
    offset_start_range_ = offset_start;
    uniforms_.m_offsetStart[0] = offset_start.x;
    uniforms_.m_offsetStart[1] = offset_start.y;
}

auto particle_emitter_component::get_offset_start_range() const -> const math::vec2&
{
    return offset_start_range_;
}

void particle_emitter_component::set_offset_end_range(const math::vec2& offset_end)
{
    offset_end_range_ = offset_end;
    uniforms_.m_offsetEnd[0] = offset_end.x;
    uniforms_.m_offsetEnd[1] = offset_end.y;
}

auto particle_emitter_component::get_offset_end_range() const -> const math::vec2&
{
    return offset_end_range_;
}

void particle_emitter_component::set_scale_start_range(const math::vec2& scale_start)
{
    scale_start_range_ = scale_start;
    uniforms_.m_scaleStart[0] = scale_start.x;
    uniforms_.m_scaleStart[1] = scale_start.y;
}

auto particle_emitter_component::get_scale_start_range() const -> const math::vec2&
{
    return scale_start_range_;
}

void particle_emitter_component::set_scale_end_range(const math::vec2& scale_end)
{
    scale_end_range_ = scale_end;
    uniforms_.m_scaleEnd[0] = scale_end.x;
    uniforms_.m_scaleEnd[1] = scale_end.y;
}

auto particle_emitter_component::get_scale_end_range() const -> const math::vec2&
{
    return scale_end_range_;
}

void particle_emitter_component::set_blend_start_range(const math::vec2& blend_start)
{
    blend_start_range_ = blend_start;
    uniforms_.m_blendStart[0] = blend_start.x;
    uniforms_.m_blendStart[1] = blend_start.y;
}

auto particle_emitter_component::get_blend_start_range() const -> const math::vec2&
{
    return blend_start_range_;
}

void particle_emitter_component::set_blend_end_range(const math::vec2& blend_end)
{
    blend_end_range_ = blend_end;
    uniforms_.m_blendEnd[0] = blend_end.x;
    uniforms_.m_blendEnd[1] = blend_end.y;
}

auto particle_emitter_component::get_blend_end_range() const -> const math::vec2&
{
    return blend_end_range_;
}

void particle_emitter_component::set_rgba_colors(const std::array<math::color, 5>& colors)
{
    rgba_colors_ = colors;
    for(int i = 0; i < 5; ++i)
    {
        uniforms_.m_rgba[i] = static_cast<uint32_t>(colors[i]);
    }
}

auto particle_emitter_component::get_rgba_colors() const -> const std::array<math::color, 5>&
{
    return rgba_colors_;
}

void particle_emitter_component::set_rgba_color(int index, const math::color& color)
{
    if(index >= 0 && index < 5)
    {
        rgba_colors_[index] = color;
        uniforms_.m_rgba[index] = static_cast<uint32_t>(color);
    }
}

auto particle_emitter_component::get_rgba_color(int index) const -> const math::color&
{
    if(index >= 0 && index < 5)
    {
        return rgba_colors_[index];
    }
    static const math::color default_color = math::color::white();
    return default_color;
}

void particle_emitter_component::set_position_easing(bx::Easing::Enum easing)
{
    uniforms_.m_easePos = easing;
}

auto particle_emitter_component::get_position_easing() const -> bx::Easing::Enum
{
    return uniforms_.m_easePos;
}

void particle_emitter_component::set_rgba_easing(bx::Easing::Enum easing)
{
    uniforms_.m_easeRgba = easing;
}

auto particle_emitter_component::get_rgba_easing() const -> bx::Easing::Enum
{
    return uniforms_.m_easeRgba;
}

void particle_emitter_component::set_blend_easing(bx::Easing::Enum easing)
{
    uniforms_.m_easeBlend = easing;
}

auto particle_emitter_component::get_blend_easing() const -> bx::Easing::Enum
{
    return uniforms_.m_easeBlend;
}

void particle_emitter_component::set_scale_easing(bx::Easing::Enum easing)
{
    uniforms_.m_easeScale = easing;
}

auto particle_emitter_component::get_scale_easing() const -> bx::Easing::Enum
{
    return uniforms_.m_easeScale;
}

auto particle_emitter_component::get_num_particles() const -> uint32_t
{
    return psGetNumParticles(emitter_handle_);
}

auto particle_emitter_component::get_world_bounds() const -> math::bbox
{
    bx::Aabb bounds;
    psGetAabb(emitter_handle_, bounds);
    return math::bbox(bounds.min.x, bounds.min.y, bounds.min.z, bounds.max.x, bounds.max.y, bounds.max.z);
}


void particle_emitter_component::set_texture(const asset_handle<gfx::texture>& texture)
{
    texture_ = texture;
}

auto particle_emitter_component::get_texture() const -> const asset_handle<gfx::texture>&
{
    return texture_;
}

void particle_emitter_component::update_emitter()
{
    if(isValid(emitter_handle_) && enabled_)
    {
        psUpdateEmitter(emitter_handle_, &uniforms_);
    }
}

void particle_emitter_component::update_emitter(const math::transform& world_transform, delta_t dt)
{
    if(isValid(emitter_handle_) && enabled_)
    {
        // Update position from transform
        const auto& world_pos = world_transform.get_position();
        uniforms_.m_position[0] = world_pos.x;
        uniforms_.m_position[1] = world_pos.y;
        uniforms_.m_position[2] = world_pos.z;
        
        // Update rotation from transform (convert quaternion to Euler angles)
        const auto& world_rot = world_transform.get_rotation();
        auto euler = math::eulerAngles(glm::inverse(world_rot));
        uniforms_.m_angle[0] = euler.x;
        uniforms_.m_angle[1] = euler.y;
        uniforms_.m_angle[2] = euler.z;

        sync_uniforms_from_members();
        
        psUpdateEmitter(emitter_handle_, &uniforms_);
        psUpdateEmitter(emitter_handle_, dt.count());

    }
}

void particle_emitter_component::render_emitter(uint8_t view, bgfx::ProgramHandle program, const float* mtxView, const bx::Vec3& eye)
{
    if(isValid(emitter_handle_) && enabled_)
    {
        psRenderEmitter(emitter_handle_, view, program, mtxView, eye);
    }
}


auto particle_emitter_component::get_uniforms() -> EmitterUniforms&
{
    return uniforms_;
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
    
    // Update with current uniforms
    update_emitter();
}

void particle_emitter_component::sync_uniforms_from_members()
{
    // Sync range properties
    uniforms_.m_lifeSpan[0] = life_span_range_.min;
    uniforms_.m_lifeSpan[1] = life_span_range_.max;
    
    uniforms_.m_offsetStart[0] = offset_start_range_.x;
    uniforms_.m_offsetStart[1] = offset_start_range_.y;
    
    uniforms_.m_offsetEnd[0] = offset_end_range_.x;
    uniforms_.m_offsetEnd[1] = offset_end_range_.y;
    
    uniforms_.m_scaleStart[0] = scale_start_range_.x;
    uniforms_.m_scaleStart[1] = scale_start_range_.y;
    
    uniforms_.m_scaleEnd[0] = scale_end_range_.x;
    uniforms_.m_scaleEnd[1] = scale_end_range_.y;
    
    uniforms_.m_blendStart[0] = blend_start_range_.x;
    uniforms_.m_blendStart[1] = blend_start_range_.y;
    
    uniforms_.m_blendEnd[0] = blend_end_range_.x;
    uniforms_.m_blendEnd[1] = blend_end_range_.y;
    
    // Sync color properties
    for(int i = 0; i < 5; ++i)
    {
        uniforms_.m_rgba[i] = static_cast<uint32_t>(rgba_colors_[i]);
    }

    if(texture_.is_valid())
    {
        auto tex = texture_.get();
        uniforms_.m_texture = tex->native_handle();
    }
    else
    {
        uniforms_.m_texture = material::default_color_map().get()->native_handle();
    }

}

} // namespace unravel
