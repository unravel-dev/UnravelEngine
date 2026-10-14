#include "emitter_desc.h"
#include "emitter_runtime.h"

#include <algorithm>

namespace unravel
{
namespace ps_soa
{
namespace
{

/// How far a key may sit from a value (or a tangent from level) and still count as holding it.
constexpr float k_constant_tolerance = 1.0e-5f;

/// Read from the keys (every emitter asks every frame): keys at @p value make a flat channel when it steps or runs
/// straight between them, or when every tangent is level too. Anything else counts as varying, which at worst
/// bakes a curve that happens to be flat.
auto is_constant_channel(const math::curve_channel& channel, float value) -> bool
{
    const auto& keys = channel.get_keys();
    const bool has_smooth_key = std::any_of(keys.begin(),
                                            keys.end(),
                                            [](const math::curve_key& key)
                                            {
                                                return key.interpolation == math::curve_interpolation::smooth;
                                            });
    return std::all_of(keys.begin(),
                       keys.end(),
                       [&](const math::curve_key& key)
                       {
                           const bool is_level = !has_smooth_key || (math::abs(key.in_tangent) <= k_constant_tolerance &&
                                                                     math::abs(key.out_tangent) <= k_constant_tolerance);
                           return is_level && math::abs(key.value - value) <= k_constant_tolerance;
                       });
}

} // namespace

auto is_constant_curve(const math::gradient<float>& curve, float value) -> bool
{
    return !curve.is_valid() || is_constant_channel(curve.get_channel(0), value);
}

auto is_unit_curve(const math::gradient<float>& curve) -> bool
{
    return is_constant_curve(curve, 1.0f);
}

auto is_unit_curve(const math::gradient<math::vec3>& curve) -> bool
{
    const auto& channels = curve.get_channels();
    return !curve.is_valid() || std::all_of(channels.begin(),
                                            channels.end(),
                                            [](const math::curve_channel& channel)
                                            {
                                                return is_constant_channel(channel, 1.0f);
                                            });
}

auto make_flat_curve(float value) -> math::gradient<float>
{
    math::gradient<float> curve;
    curve.add_point(value, 0.0f);
    curve.add_point(value, 1.0f);
    curve.bake(curve_bake_resolution);
    return curve;
}

auto make_unit_axes_curve() -> math::gradient<math::vec3>
{
    math::gradient<math::vec3> curve;
    curve.add_point(math::vec3(1.0f), 0.0f);
    curve.add_point(math::vec3(1.0f), 1.0f);
    curve.bake(curve_bake_resolution);
    return curve;
}

void emitter_emission_desc::reset()
{
    particles_per_second = 50.0f;
    particles_per_meter = 0.0f;
    bursts.clear();
    duration = 2.0f;
    start_delay = 0.0f;
    loop = true;
    prewarm = false;
    shape = emitter_shape::sphere;
    direction = emitter_direction::up;
    direction_spread = 0.0f;
    spawn_location = spawn_location::inside;
    shape_position = math::vec3(0.0f, 0.0f, 0.0f);
    shape_scale = math::vec3(1.0f, 1.0f, 1.0f);
    cone_angle = 25.0f;
    torus_thickness = 0.25f;
    mesh_mode = mesh_spawn_mode::surface;
    mesh.reset();
    use_fixed_seed = false;
    random_seed = 0;
}

void emitter_motion_desc::reset()
{
    space = simulation_space::world;
    lifetime = frange_t(1.0f, 1.0f);
    start_speed = frange_t(2.0f, 3.0f);
    gravity_scale = 0.0f;
    drag = 0.0f;
    acceleration = math::vec3(0.0f, 0.0f, 0.0f);
    inherit_velocity = 0.0f;
    speed_over_life = math::gradient<float>();
    speed_over_life.add_point(1.0f, 0.0f);
    speed_over_life.add_point(1.0f, 1.0f);
    speed_over_life.bake(curve_bake_resolution);
    turbulence_strength = 0.0f;
    turbulence_frequency = 1.0f;
    turbulence_speed = 0.5f;
    turbulence_over_life = make_flat_curve(1.0f);
    collision = collision_mode::none;
    collision_planes.clear();
    restitution = 0.5f;
    friction = 0.2f;
    max_bounces = 3;
    collision_radius_scale = 1.0f;
    spawn_along_path = 1.0f;
    motion_easing = bx::Easing::Linear;
    lifetime_by_emitter_speed_curve = math::gradient<float>();
    lifetime_by_emitter_speed_curve.add_point(1.0f, 0.0f);
    lifetime_by_emitter_speed_curve.add_point(1.0f, 1.0f);
    lifetime_by_emitter_speed_curve.bake(lifetime_by_emitter_speed_bake_resolution);
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
    color_gradient.bake(curve_bake_resolution);
    size_over_life = math::gradient<frange_t>();
    size_over_life.add_point(frange_t(0.1f, 0.2f), 0.0f);
    size_over_life.add_point(frange_t(0.3f, 0.4f), 1.0f);
    size_over_life.bake(curve_bake_resolution);
    size_axes = math::vec3(1.0f, 1.0f, 1.0f);
    opacity = 1.0f;
    color_intensity = 1.0f;
    size_by_speed_multiplier = frange_t(1.0f, 1.0f);
    size_by_speed_speed_range = frange_t(0.0f, 10.0f);
    color_by_speed_gradient = math::gradient<math::color>();
    color_by_speed_gradient.add_point(math::color(0xffffffff), 0.0f);
    color_by_speed_gradient.add_point(math::color(0xffffffff), 1.0f);
    color_by_speed_gradient.bake(curve_bake_resolution);
    color_by_speed_speed_range = frange_t(0.0f, 10.0f);
    size_axes_over_life = make_unit_axes_curve();
    random_between_gradients = false;
    color_gradient_b = color_gradient;
    intensity_over_life = make_flat_curve(1.0f);
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
    frame_blending = false;
    start_rotation = frange_t(0.0f, 0.0f);
    spin_speed = frange_t(0.0f, 0.0f);
    random_spin_direction = false;
    spin_over_life = make_flat_curve(1.0f);
    tex_sheet_use_frame_curve = false;
    tex_sheet_frame_curve = math::gradient<float>();
    tex_sheet_frame_curve.add_point(0.0f, 0.0f);
    tex_sheet_frame_curve.add_point(1.0f, 1.0f);
    tex_sheet_frame_curve.bake(curve_bake_resolution);
    tex_sheet_random_row = false;
    soft_fade_distance = 0.0f;
    sort_priority = 0;
    sort_mode = sort_mode::automatic;
    primitive = particle_primitive::quad;
    half_resolution = false;
    write_velocity = false;
    velocity_alpha_threshold = 0.5f;
    trail_length = 0.5f;
    trail_width_end = 0.0f;
    cutout_alpha_threshold = 0.05f;
    ribbon_texture_mode = ribbon_texture_mode::age;
    ribbon_tile_time = 0.25f;
    stretch_length_scale = 1.0f;
    stretch_speed_scale = 0.1f;
    stretch_camera_velocity_scale = 0.0f;
    facing_axis = math::vec3(0.0f, 1.0f, 0.0f);
    camera_offset = 0.0f;
    camera_fade_range = frange_t(0.0f, 0.0f);
    max_screen_size = 0.0f;
    lighting = particle_lighting::unlit;
    lighting_wrap = 0.5f;
    receive_shadows = true;
    light_count = 0;
    light_intensity = 20.0f;
    light_range = 3.0f;
    light_color_from_particle = true;
    max_draw_distance = 0.0f;
    draw_distance_fade = 2.0f;
    subpixel_fade = false;
    erosion_over_life = math::gradient<float>();
    erosion_over_life.add_point(0.0f, 0.0f);
    erosion_over_life.add_point(0.0f, 1.0f);
    erosion_over_life.bake(curve_bake_resolution);
    taa_response = 0.5f;
    bounds_mode = bounds_mode::automatic;
    fixed_bounds = math::bbox(math::vec3(-5.0f), math::vec3(5.0f));
}

void emitter_playback_desc::reset()
{
    playing = true;
    paused = false;
    auto_play = true;
}

void emitter_desc::reset()
{
    emission.reset();
    motion.reset();
    appearance.reset();
    render.reset();
    playback.reset();
    event_mask = 0;
}

auto emitter_desc::bake_features() const -> emitter_feature
{
    emitter_feature features = emitter_feature::none;
    if(render.align_to_direction)
    {
        features = features | emitter_feature::align_to_direction;
    }
    // A sheet of more than one tile always draws one tile per particle; with no cycles a particle holds
    // one (the first, or a random one with randomize) instead of showing the whole sheet.
    const bool has_sheet = render.tex_sheet_tiles.x >= 1.0f && render.tex_sheet_tiles.y >= 1.0f &&
                           render.tex_sheet_tiles.x * render.tex_sheet_tiles.y > 1.0f;
    if(has_sheet)
    {
        features = features | emitter_feature::texsheet;
        if(render.frame_blending)
        {
            features = features | emitter_feature::frame_blending;
        }
        if(render.tex_sheet_use_frame_curve)
        {
            features = features | emitter_feature::texsheet_frame_curve;
        }
        if(render.tex_sheet_random_row)
        {
            features = features | emitter_feature::texsheet_random_row;
        }
        if(render.tex_sheet_randomize)
        {
            features = features | emitter_feature::texsheet_randomize;
        }
        else if(render.tex_sheet_use_frame_curve || render.tex_sheet_cycles <= 1.0f)
        {
            features = features | emitter_feature::texsheet_single_pass;
        }
    }
    if(appearance.color_by_speed_speed_range.max > appearance.color_by_speed_speed_range.min)
    {
        features = features | emitter_feature::color_by_speed;
    }
    if(appearance.size_by_speed_speed_range.max > appearance.size_by_speed_speed_range.min &&
       appearance.size_by_speed_multiplier.min != appearance.size_by_speed_multiplier.max)
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
    if(motion.motion_easing != bx::Easing::Linear)
    {
        features = features | emitter_feature::non_linear_ease;
    }
    if(!is_unit_curve(motion.speed_over_life))
    {
        features = features | emitter_feature::speed_curve;
    }
    if(render.random_spin_direction)
    {
        features = features | emitter_feature::random_spin_direction;
    }
    if(!is_constant_curve(render.erosion_over_life, 0.0f))
    {
        features = features | emitter_feature::erosion;
    }
    if(render.subpixel_fade)
    {
        features = features | emitter_feature::subpixel_fade;
    }
    if(render.camera_fade_range.max > render.camera_fade_range.min && render.camera_fade_range.max > 0.0f)
    {
        features = features | emitter_feature::camera_fade;
    }
    if(!is_unit_curve(appearance.size_axes_over_life))
    {
        features = features | emitter_feature::size_axis_curve;
    }
    if(!is_unit_curve(render.spin_over_life))
    {
        features = features | emitter_feature::spin_curve;
    }
    if(!is_unit_curve(appearance.intensity_over_life))
    {
        features = features | emitter_feature::intensity_curve;
    }
    if(appearance.random_between_gradients)
    {
        features = features | emitter_feature::two_gradients;
    }
    if(motion.turbulence_strength > 0.0f)
    {
        features = features | emitter_feature::turbulence;
    }
    if(render.max_draw_distance > 0.0f)
    {
        features = features | emitter_feature::draw_distance;
    }
    // Multiply blend filters the scene: it stays unlit.
    if(render.blend_mode != blend_mode::multiply)
    {
        if(render.lighting == particle_lighting::volumetric)
        {
            features = features | emitter_feature::lit_volumetric;
        }
        else if(render.lighting == particle_lighting::surface)
        {
            features = features | emitter_feature::lit_surface;
        }
    }
    return features;
}

void emitter_sim_state::reset()
{
    emission_time_accum = 0.0f;
    distance_accum = 0.0f;
    start_delay_elapsed = 0.0f;
    cycle_time = 0.0f;
    emission_finished = false;
    first_update = true;
    prewarm_pending = true;
    playing = true;
    frozen_time = 0.0f;
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
