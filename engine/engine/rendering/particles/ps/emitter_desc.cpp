#include "emitter_desc.h"
#include "emitter_runtime.h"

namespace unravel
{
namespace ps_soa
{

void emitter_emission_desc::reset()
{
    particles_per_second = 50.0f;
    emission_lifetime = 2.0f;
    start_delay = 0.0f;
    loop = true;
    spawn_location = spawn_location::inside;
    shape_position = math::vec3(0.0f, 0.0f, 0.0f);
    shape_scale = math::vec3(1.0f, 1.0f, 1.0f);
}

void emitter_motion_desc::reset()
{
    space = simulation_space::world;
    lifetime = 1.0f;
    gravity_scale = 0.0f;
    temporal_motion = 1.0f;
    velocity_damping = 0.0f;
    force_over_lifetime = math::vec3(0.0f, 0.0f, 0.0f);
    velocity_gradient = math::gradient<frange_t>();
    velocity_gradient.add_point(frange_t(0.0f, 1.0f), 0.0f);
    velocity_gradient.add_point(frange_t(2.0f, 3.0f), 1.0f);
    velocity_gradient.generate_lut(256);
    position_easing = bx::Easing::Linear;
    lifetime_by_emitter_speed_gradient = math::gradient<float>();
    lifetime_by_emitter_speed_gradient.add_point(1.0f, 0.0f);
    lifetime_by_emitter_speed_gradient.add_point(1.0f, 1.0f);
    lifetime_by_emitter_speed_gradient.generate_lut(1024);
    lifetime_by_emitter_speed_range = frange_t(0.0f, 10.0f);
}

void emitter_appearance_desc::reset()
{
    color_gradient = math::gradient<math::color>();
    color_gradient.add_point(math::color(0x00ffffff), 0.0f);
    color_gradient.add_point(math::color(0xffffffff), 0.25f);
    color_gradient.add_point(math::color(0xffffffff), 0.5f);
    color_gradient.add_point(math::color(0xffffffff), 0.75f);
    color_gradient.add_point(math::color(0x00ffffff), 1.0f);
    color_gradient.generate_lut(256);
    scale_gradient = math::gradient<frange_t>();
    scale_gradient.add_point(frange_t(0.1f, 0.2f), 0.0f);
    scale_gradient.add_point(frange_t(0.3f, 0.4f), 1.0f);
    scale_gradient.generate_lut(256);
    initial_scale_3d = math::vec3(1.0f, 1.0f, 1.0f);
    opacity = 1.0f;
    color_intensity = 1.0f;
    size_by_speed_range = frange_t(1.0f, 1.0f);
    size_by_speed_velocity_range = frange_t(0.0f, 10.0f);
    color_by_speed_gradient = math::gradient<math::color>();
    color_by_speed_gradient.add_point(math::color(0xffffffff), 0.0f);
    color_by_speed_gradient.add_point(math::color(0xffffffff), 1.0f);
    color_by_speed_gradient.generate_lut(256);
    color_by_speed_velocity_range = frange_t(0.0f, 10.0f);
}

void emitter_render_desc::reset()
{
    texture_mode = texture_mode::multi_channel;
    render_mode = render_mode::billboard;
    blend_mode = blend_mode::normal;
    align_to_direction = false;
    pivot = math::vec2(0.5f, 0.5f);
    tex_sheet_tiles = math::vec2(1.0f, 1.0f);
    tex_sheet_cycles = 0.0f;
    tex_sheet_randomize = false;
}

void emitter_playback_desc::reset()
{
    playing = true;
    paused = false;
}

void emitter_desc::reset()
{
    emission.reset();
    motion.reset();
    appearance.reset();
    render.reset();
    playback.reset();
}

auto emitter_desc::bake_features() const -> emitter_feature
{
    emitter_feature features = emitter_feature::none;
    if(render.align_to_direction)
    {
        features = features | emitter_feature::align_to_direction;
    }
    if(render.tex_sheet_cycles > 0.0f && render.tex_sheet_tiles.x > 0.0f && render.tex_sheet_tiles.y > 0.0f)
    {
        features = features | emitter_feature::texsheet;
    }
    if(appearance.color_by_speed_velocity_range.max > appearance.color_by_speed_velocity_range.min)
    {
        features = features | emitter_feature::color_by_speed;
    }
    if(appearance.size_by_speed_velocity_range.max > appearance.size_by_speed_velocity_range.min &&
       appearance.size_by_speed_range.min != appearance.size_by_speed_range.max)
    {
        features = features | emitter_feature::size_by_speed;
    }
    if(motion.lifetime_by_emitter_speed_range.max > motion.lifetime_by_emitter_speed_range.min)
    {
        features = features | emitter_feature::lifetime_by_emitter_speed;
    }
    if(motion.space == simulation_space::local)
    {
        features = features | emitter_feature::local_space;
    }
    if(motion.position_easing != bx::Easing::Linear)
    {
        features = features | emitter_feature::non_linear_ease;
    }
    return features;
}

void emitter_sim_state::reset()
{
    emission_time_accum = 0.0f;
    start_delay_elapsed = 0.0f;
    total_particles_spawned = 0;
    first_update = true;
    playing = true;
    loop = true;
    features = emitter_feature::none;
    temporal_count = 0;
    temporal_positions.fill(math::vec3(0.0f));
    temporal_dts.fill(0.0f);
    world_bounds = math::bbox(math::vec3(-1.0f), math::vec3(1.0f));
}

void emitter_sim_state::push_temporal_sample(const math::vec3& position, float dt)
{
    if(temporal_count < temporal_buffer_size)
    {
        temporal_positions[temporal_count] = position;
        temporal_dts[temporal_count] = dt;
        ++temporal_count;
        return;
    }
    for(uint32_t i = 1; i < temporal_buffer_size; ++i)
    {
        temporal_positions[i - 1] = temporal_positions[i];
        temporal_dts[i - 1] = temporal_dts[i];
    }
    temporal_positions[temporal_buffer_size - 1] = position;
    temporal_dts[temporal_buffer_size - 1] = dt;
}

auto emitter_sim_state::calculate_smoothed_emitter_speed(float max_speed) const -> float
{
    if(temporal_count < temporal_buffer_size)
    {
        return max_speed;
    }
    float total_distance = 0.0f;
    float total_time = 0.0f;
    for(uint32_t i = 1; i < temporal_count; ++i)
    {
        const math::vec3 delta = temporal_positions[i] - temporal_positions[i - 1];
        total_distance += math::length(delta);
        total_time += temporal_dts[i];
    }
    if(total_time > 0.0f)
    {
        return total_distance / total_time;
    }
    return 0.0f;
}

} // namespace ps_soa
} // namespace unravel
