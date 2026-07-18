#include "particle_emitter_component.h"
#include "math/transform.hpp"
#include <engine/ecs/ecs_utils.h>
#include <engine/rendering/material.h>
#include <graphics/graphics.h>
#include <logging/logging.h>

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
    component.desc_.reset();
    component.recreate_emitter();
}

void particle_emitter_component::on_destroy_component(entt::registry& r, entt::entity e)
{
    entt::handle entity(r, e);
    if(entity.all_of<particle_emitter_component>())
    {
        auto& component = entity.get<particle_emitter_component>();
        if(ps_soa::is_valid(component.emitter_handle_))
        {
            ps_soa::destroy_emitter(component.emitter_handle_);
            component.emitter_handle_ = {};
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

auto particle_emitter_component::get_emitter_handle() const -> ps_soa::emitter_handle
{
    return emitter_handle_;
}

void particle_emitter_component::set_shape(ps_soa::emitter_shape shape)
{
    if(shape_ != shape)
    {
        shape_ = shape;
        recreate_emitter();
    }
}

auto particle_emitter_component::get_shape() const -> ps_soa::emitter_shape
{
    return shape_;
}

void particle_emitter_component::set_direction(ps_soa::emitter_direction direction)
{
    if(direction_ != direction)
    {
        direction_ = direction;
        recreate_emitter();
    }
}

auto particle_emitter_component::get_direction() const -> ps_soa::emitter_direction
{
    return direction_;
}

void particle_emitter_component::set_spawn_location(ps_soa::spawn_location spawn_location)
{
    desc_.emission.spawn_location = spawn_location;
}

auto particle_emitter_component::get_spawn_location() const -> ps_soa::spawn_location
{
    return desc_.emission.spawn_location;
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
    desc_.emission.emission_lifetime = lifetime.count();
}

auto particle_emitter_component::get_emission_lifetime() const -> std::chrono::duration<float>
{
    return std::chrono::duration<float>(desc_.emission.emission_lifetime);
}

void particle_emitter_component::set_gravity_scale(float scale)
{
    desc_.motion.gravity_scale = scale;
}

auto particle_emitter_component::get_gravity_scale() const -> float
{
    return desc_.motion.gravity_scale;
}

void particle_emitter_component::set_emission_rate(float emission_rate)
{
    desc_.emission.particles_per_second = math::max(emission_rate, 0.0f);
    reset_emitter();
}

auto particle_emitter_component::get_emission_rate() const -> float
{
    return desc_.emission.particles_per_second;
}

void particle_emitter_component::set_temporal_motion(float temporal_motion)
{
    desc_.motion.temporal_motion = math::clamp(temporal_motion, 0.0f, 1.0f);
}

auto particle_emitter_component::get_temporal_motion() const -> float
{
    return desc_.motion.temporal_motion;
}

void particle_emitter_component::set_velocity_damping(float velocity_damping)
{
    desc_.motion.velocity_damping = math::clamp(velocity_damping, 0.0f, 1.0f);
}

auto particle_emitter_component::get_velocity_damping() const -> float
{
    return desc_.motion.velocity_damping;
}

void particle_emitter_component::set_force_over_lifetime(const math::vec3& force)
{
    desc_.motion.force_over_lifetime = force;
}

auto particle_emitter_component::get_force_over_lifetime() const -> math::vec3
{
    return desc_.motion.force_over_lifetime;
}

void particle_emitter_component::set_emission_shape_position(const math::vec3& position)
{
    desc_.emission.shape_position = position;
}

auto particle_emitter_component::get_emission_shape_position() const -> math::vec3
{
    return desc_.emission.shape_position;
}

void particle_emitter_component::set_emission_shape_scale(const math::vec3& scale)
{
    desc_.emission.shape_scale = scale;
}

auto particle_emitter_component::get_emission_shape_scale() const -> math::vec3
{
    return desc_.emission.shape_scale;
}

void particle_emitter_component::set_size_by_speed_range(const frange_t& size_range)
{
    desc_.appearance.size_by_speed_range = size_range;
}

auto particle_emitter_component::get_size_by_speed_range() const -> const frange_t&
{
    return desc_.appearance.size_by_speed_range;
}

void particle_emitter_component::set_size_by_speed_velocity_range(const frange_t& velocity_range)
{
    desc_.appearance.size_by_speed_velocity_range = velocity_range;
}

auto particle_emitter_component::get_size_by_speed_velocity_range() const -> const frange_t&
{
    return desc_.appearance.size_by_speed_velocity_range;
}

void particle_emitter_component::set_color_by_speed_gradient(const math::gradient<math::color>& gradient)
{
    desc_.appearance.color_by_speed_gradient = gradient;
    desc_.appearance.color_by_speed_gradient.generate_lut(256);
}

auto particle_emitter_component::get_color_by_speed_gradient() const -> const math::gradient<math::color>&
{
    return desc_.appearance.color_by_speed_gradient;
}

void particle_emitter_component::set_color_by_speed_velocity_range(const frange_t& velocity_range)
{
    desc_.appearance.color_by_speed_velocity_range = velocity_range;
}

auto particle_emitter_component::get_color_by_speed_velocity_range() const -> const frange_t&
{
    return desc_.appearance.color_by_speed_velocity_range;
}

void particle_emitter_component::set_lifetime_by_emitter_speed_gradient(const math::gradient<float>& gradient)
{
    desc_.motion.lifetime_by_emitter_speed_gradient = gradient;
}

auto particle_emitter_component::get_lifetime_by_emitter_speed_gradient() const -> const math::gradient<float>&
{
    return desc_.motion.lifetime_by_emitter_speed_gradient;
}

void particle_emitter_component::set_lifetime_by_emitter_speed_range(const frange_t& speed_range)
{
    desc_.motion.lifetime_by_emitter_speed_range = speed_range;
}

auto particle_emitter_component::get_lifetime_by_emitter_speed_range() const -> const frange_t&
{
    return desc_.motion.lifetime_by_emitter_speed_range;
}

void particle_emitter_component::set_lifetime(std::chrono::duration<float> lifetime)
{
    desc_.motion.lifetime = math::max(lifetime.count(), 0.0f);
    reset_emitter();
}

auto particle_emitter_component::get_lifetime() const -> std::chrono::duration<float>
{
    return std::chrono::duration<float>(desc_.motion.lifetime);
}

void particle_emitter_component::set_velocity_gradient(const math::gradient<frange_t>& gradient)
{
    desc_.motion.velocity_gradient = gradient;
    desc_.motion.velocity_gradient.generate_lut(256);
}

auto particle_emitter_component::get_velocity_gradient() const -> const math::gradient<frange_t>&
{
    return desc_.motion.velocity_gradient;
}

void particle_emitter_component::set_scale_gradient(const math::gradient<frange_t>& gradient)
{
    desc_.appearance.scale_gradient = gradient;
    desc_.appearance.scale_gradient.generate_lut(256);
}

auto particle_emitter_component::get_scale_gradient() const -> const math::gradient<frange_t>&
{
    return desc_.appearance.scale_gradient;
}

void particle_emitter_component::set_initial_scale_3d(const math::vec3& scale)
{
    desc_.appearance.initial_scale_3d = scale;
}

auto particle_emitter_component::get_initial_scale_3d() const -> math::vec3
{
    return desc_.appearance.initial_scale_3d;
}

void particle_emitter_component::set_opacity(float opacity)
{
    desc_.appearance.opacity = math::clamp(opacity, 0.0f, 1.0f);
}

auto particle_emitter_component::get_opacity() const -> float
{
    return desc_.appearance.opacity;
}

void particle_emitter_component::set_color_intensity(float intensity)
{
    desc_.appearance.color_intensity = math::max(intensity, 0.0f);
}

auto particle_emitter_component::get_color_intensity() const -> float
{
    return desc_.appearance.color_intensity;
}

void particle_emitter_component::play()
{
    desc_.playback.playing = true;
    desc_.playback.paused = false;
}

void particle_emitter_component::stop()
{
    desc_.playback.playing = false;
    desc_.playback.paused = false;
}

void particle_emitter_component::stop_and_reset()
{
    desc_.playback.playing = false;
    desc_.playback.paused = false;
    reset_emitter();
}

void particle_emitter_component::pause()
{
    if(desc_.playback.playing)
    {
        desc_.playback.paused = true;
    }
}

void particle_emitter_component::resume()
{
    if(desc_.playback.playing)
    {
        desc_.playback.paused = false;
    }
}

void particle_emitter_component::play_sub_emitters()
{
    auto owner = get_owner();
    if(owner.valid())
    {
        for_each_component_in_children<particle_emitter_component>(owner,
                                                                   [](particle_emitter_component& emitter)
                                                                   {
                                                                       emitter.play();
                                                                   });
    }
}

void particle_emitter_component::stop_sub_emitters()
{
    auto owner = get_owner();
    if(owner.valid())
    {
        for_each_component_in_children<particle_emitter_component>(owner,
                                                                   [](particle_emitter_component& emitter)
                                                                   {
                                                                       emitter.stop();
                                                                   });
    }
}

void particle_emitter_component::stop_and_reset_sub_emitters()
{
    auto owner = get_owner();
    if(owner.valid())
    {
        for_each_component_in_children<particle_emitter_component>(owner,
                                                                   [](particle_emitter_component& emitter)
                                                                   {
                                                                       emitter.stop_and_reset();
                                                                   });
    }
}

void particle_emitter_component::pause_sub_emitters()
{
    auto owner = get_owner();
    if(owner.valid())
    {
        for_each_component_in_children<particle_emitter_component>(owner,
                                                                   [](particle_emitter_component& emitter)
                                                                   {
                                                                       emitter.pause();
                                                                   });
    }
}

void particle_emitter_component::resume_sub_emitters()
{
    auto owner = get_owner();
    if(owner.valid())
    {
        for_each_component_in_children<particle_emitter_component>(owner,
                                                                   [](particle_emitter_component& emitter)
                                                                   {
                                                                       emitter.resume();
                                                                   });
    }
}

auto particle_emitter_component::is_playing() const -> bool
{
    return desc_.playback.playing;
}

auto particle_emitter_component::is_paused() const -> bool
{
    return desc_.playback.paused;
}

auto particle_emitter_component::is_stopped() const -> bool
{
    return !desc_.playback.playing && !desc_.playback.paused;
}

void particle_emitter_component::set_loop(bool loop)
{
    desc_.emission.loop = loop;
}

auto particle_emitter_component::is_loop() const -> bool
{
    return desc_.emission.loop;
}

void particle_emitter_component::set_start_delay(std::chrono::duration<float> delay)
{
    desc_.emission.start_delay = math::max(0.0f, delay.count());
    reset_emitter();
}

auto particle_emitter_component::get_start_delay() const -> std::chrono::duration<float>
{
    return std::chrono::duration<float>(desc_.emission.start_delay);
}

void particle_emitter_component::set_align_to_direction(bool align)
{
    desc_.render.align_to_direction = align;
}

auto particle_emitter_component::get_align_to_direction() const -> bool
{
    return desc_.render.align_to_direction;
}

void particle_emitter_component::set_pivot(const math::vec2& pivot)
{
    desc_.render.pivot = pivot;
}

auto particle_emitter_component::get_pivot() const -> math::vec2
{
    return desc_.render.pivot;
}

void particle_emitter_component::set_color_gradient(const math::gradient<math::color>& gradient)
{
    desc_.appearance.color_gradient = gradient;
    desc_.appearance.color_gradient.generate_lut(256);
}

auto particle_emitter_component::get_color_gradient() const -> const math::gradient<math::color>&
{
    return desc_.appearance.color_gradient;
}

void particle_emitter_component::set_position_easing(bx::Easing::Enum easing)
{
    desc_.motion.position_easing = easing;
}

auto particle_emitter_component::get_position_easing() const -> bx::Easing::Enum
{
    return desc_.motion.position_easing;
}

auto particle_emitter_component::get_num_particles() const -> uint32_t
{
    return ps_soa::get_num_particles(emitter_handle_);
}

auto particle_emitter_component::get_world_bounds() const -> math::bbox
{
    math::bbox bounds(math::vec3(-1.0f), math::vec3(1.0f));
    ps_soa::get_aabb(emitter_handle_, bounds);
    return bounds;
}

auto particle_emitter_component::get_updated_world_bounds(const math::transform& world_transform) const -> math::bbox
{
    auto bounds = get_world_bounds();
    if(!ps_soa::has_updated(emitter_handle_))
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

void particle_emitter_component::set_texture_mode(ps_soa::texture_mode mode)
{
    desc_.render.texture_mode = mode;
}

auto particle_emitter_component::get_texture_mode() const -> ps_soa::texture_mode
{
    return desc_.render.texture_mode;
}

void particle_emitter_component::set_render_mode(ps_soa::render_mode mode)
{
    desc_.render.render_mode = mode;
}

auto particle_emitter_component::get_render_mode() const -> ps_soa::render_mode
{
    return desc_.render.render_mode;
}

void particle_emitter_component::set_blend_mode(ps_soa::blend_mode mode)
{
    desc_.render.blend_mode = mode;
}

auto particle_emitter_component::get_blend_mode() const -> ps_soa::blend_mode
{
    return desc_.render.blend_mode;
}

void particle_emitter_component::set_texture_sheet_tiles(math::vec2 tiles)
{
    desc_.render.tex_sheet_tiles = math::vec2(math::max(tiles.x, 1.0f), math::max(tiles.y, 1.0f));
}

auto particle_emitter_component::get_texture_sheet_tiles() const -> math::vec2
{
    return desc_.render.tex_sheet_tiles;
}

void particle_emitter_component::set_texture_sheet_cycles(float cycles)
{
    desc_.render.tex_sheet_cycles = math::max(cycles, 0.0f);
}

auto particle_emitter_component::get_texture_sheet_cycles() const -> float
{
    return desc_.render.tex_sheet_cycles;
}

void particle_emitter_component::set_texture_sheet_randomize(bool randomize)
{
    desc_.render.tex_sheet_randomize = randomize;
}

auto particle_emitter_component::get_texture_sheet_randomize() const -> bool
{
    return desc_.render.tex_sheet_randomize;
}

void particle_emitter_component::set_culling_mode(culling_mode mode)
{
    culling_mode_ = mode;
}

auto particle_emitter_component::get_culling_mode() const -> culling_mode
{
    return culling_mode_;
}

void particle_emitter_component::set_last_render_frame(uint64_t frame)
{
    last_render_frame_ = frame;
}

auto particle_emitter_component::get_last_render_frame() const noexcept -> uint64_t
{
    return last_render_frame_;
}

auto particle_emitter_component::is_newly_created() const noexcept -> bool
{
    return last_render_frame_ == 0;
}

auto particle_emitter_component::was_used_last_frame() const noexcept -> bool
{
    const auto current_frame = uint64_t(gfx::get_render_frame());
    const bool is_new = is_newly_created();
    const bool was_used_recently = current_frame - last_render_frame_ <= 1;
    return is_new || was_used_recently;
}

void particle_emitter_component::update_emitter(const math::transform& world_transform, delta_t dt)
{
    if(!ps_soa::is_valid(emitter_handle_) || !enabled_)
    {
        return;
    }
    transform_state_.current = world_transform;
    bool should_simulate = true;
    if(culling_mode_ == culling_mode::renderer_based && !was_used_last_frame())
    {
        should_simulate = false;
    }
    if(should_simulate)
    {
        ps_soa::update_emitter(emitter_handle_, dt.count(), desc_, transform_state_, desc_.playback);
    }
    else
    {
        ps_soa::update_emitter_bounds_only(emitter_handle_, desc_, transform_state_);
    }
}

auto particle_emitter_component::get_desc() const -> const ps_soa::emitter_desc&
{
    return desc_;
}

void particle_emitter_component::recreate_emitter()
{
    if(ps_soa::is_valid(emitter_handle_))
    {
        ps_soa::destroy_emitter(emitter_handle_);
        emitter_handle_ = {};
    }
    emitter_handle_ = ps_soa::create_emitter(shape_, direction_, max_particles_);
    if(!ps_soa::is_valid(emitter_handle_))
    {
        APPLOG_ERROR("Failed to create particle emitter");
        return;
    }
    ps_soa::set_emitter_sim_backend(emitter_handle_, simulation_backend_);
}

void particle_emitter_component::reset_emitter()
{
    if(ps_soa::is_valid(emitter_handle_))
    {
        ps_soa::reset_emitter(emitter_handle_);
    }
}

void particle_emitter_component::set_simulation_space(ps_soa::simulation_space space)
{
    desc_.motion.space = space;
    reset_emitter();
}

auto particle_emitter_component::get_simulation_space() const -> ps_soa::simulation_space
{
    return desc_.motion.space;
}

void particle_emitter_component::set_simulation_backend(ps_soa::particle_sim_backend backend)
{
    simulation_backend_ = backend;
    if(ps_soa::is_valid(emitter_handle_))
    {
        ps_soa::set_emitter_sim_backend(emitter_handle_, simulation_backend_);
    }
}

auto particle_emitter_component::get_simulation_backend() const -> ps_soa::particle_sim_backend
{
    return simulation_backend_;
}

} // namespace unravel
