#include "particle_eval.h"
#include "particle_noise.h"

#include <glm/gtc/packing.hpp>

#include <cmath>
#include <cstring>

namespace unravel
{
namespace ps_soa
{
namespace
{

/// Below this drag x time the exponential forms lose precision (the gravity term divides a small difference by
/// drag squared); their series are used instead, exact to float precision up to it (PARTICLE_SMALL_DRAG_TIME in
/// particle_common.sh).
constexpr float k_small_drag_time = 0.1f;
constexpr float k_unorm16_max = 65535.0f;
constexpr float k_min_pick_weight = 1.0e-6f;
constexpr uint32_t k_salt_multiplier = 2654435769u;

auto float_bits(float value) -> uint32_t
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

auto has_flag(uint32_t flags, emitter_feature feature) -> bool
{
    return (flags & static_cast<uint32_t>(feature)) != 0u;
}

auto speed_factor(float speed, float minimum, float inverse_span) -> float
{
    return math::clamp((speed - minimum) * inverse_span, 0.0f, 1.0f);
}

auto size_pick_blend(float life, float width_start, float width_end) -> float
{
    const float start_weight = (1.0f - life) * width_start;
    const float end_weight = life * width_end;
    const float total = start_weight + end_weight;
    return total > k_min_pick_weight ? end_weight / total : life;
}

} // namespace

auto pack_size_picks(float start, float end) -> uint32_t
{
    const uint32_t start_bits = uint32_t(math::clamp(start, 0.0f, 1.0f) * k_unorm16_max + 0.5f);
    const uint32_t end_bits = uint32_t(math::clamp(end, 0.0f, 1.0f) * k_unorm16_max + 0.5f);
    return start_bits | (end_bits << 16u);
}

auto unpack_size_picks(uint32_t picks) -> math::vec2
{
    return math::vec2(float(picks & 0xffffu), float(picks >> 16u)) / k_unorm16_max;
}

void pack_tint(const math::vec4& tint, particle_record& record)
{
    record.tint_rg = math::packHalf2x16(math::vec2(tint.r, tint.g));
    record.tint_ba = math::packHalf2x16(math::vec2(tint.b, tint.a));
}

auto unpack_tint(const particle_record& record) -> math::vec4
{
    const math::vec2 rg = math::unpackHalf2x16(record.tint_rg);
    const math::vec2 ba = math::unpackHalf2x16(record.tint_ba);
    return math::vec4(rg.x, rg.y, ba.x, ba.y);
}

auto pack_static_row(const emitter_eval_params& params) -> static_row
{
    static_row row{};
    row[static_slot_appearance] = math::vec4(params.opacity, params.intensity, params.size_width_start, params.size_width_end);
    row[static_slot_size_axes] = math::vec4(params.size_axes, float(params.lut_base));
    row[static_slot_size_by_speed] = params.size_by_speed;
    row[static_slot_render] = math::vec4(params.color_by_speed.x, params.color_by_speed.y, params.soft_fade_distance, float(int(params.render_mode)));
    row[static_slot_sheet] = math::vec4(params.pivot.x, params.pivot.y, params.tiles.x, params.tiles.y);
    row[static_slot_rotation] = math::vec4(params.cycles, float(params.flags & gpu_feature_bits), params.rotation.x, params.rotation.y);
    row[static_slot_spin] = math::vec4(params.spin.x, params.spin.y, params.pivot_pad, float(int(params.sort_mode)));
    row[static_slot_view_shaping] = math::vec4(params.camera_offset, params.camera_fade.x, params.camera_fade.y, params.max_screen_size);
    row[static_slot_stretch] = math::vec4(params.stretch_length_scale,
                        params.stretch_speed_scale,
                        params.taa_response,
                        params.velocity_alpha_threshold);
    row[static_slot_turbulence] = params.turbulence;
    row[static_slot_noise] = math::vec4(params.noise_offset, params.receive_shadows);
    row[static_slot_distance] = math::vec4(params.draw_distance, params.lighting_wrap, params.cutout_table);
    row[static_slot_trail] = math::vec4(params.trail_length, params.trail_width_end, float(int(params.ribbon_texture_mode)), params.ribbon_tiles_per_second);
    row[static_slot_facing] = math::vec4(params.facing_axis, params.stretch_camera_velocity_scale);
    row[static_slot_speed_bound] = math::vec4(params.speed_bound.x, params.speed_bound.y, 0.0f, 0.0f);
    return row;
}

auto pack_dynamic_row(const emitter_eval_params& params) -> dynamic_row
{
    dynamic_row row{};
    for(uint32_t r = 0; r < 3; ++r)
    {
        for(uint32_t c = 0; c < 4; ++c)
        {
            row[dynamic_slot_local_to_world + r * 4 + c] = float_bits(params.local_to_world[c][r]);
        }
    }
    row[dynamic_slot_rotation] = float_bits(params.emitter_rotation.x);
    row[dynamic_slot_rotation + 1] = float_bits(params.emitter_rotation.y);
    row[dynamic_slot_rotation + 2] = float_bits(params.emitter_rotation.z);
    row[dynamic_slot_rotation + 3] = float_bits(params.emitter_rotation.w);
    for(uint32_t i = 0; i < 3; ++i)
    {
        row[dynamic_slot_acceleration + i] = float_bits(params.acceleration[int(i)]);
        row[dynamic_slot_origin_motion + i] = float_bits(params.origin_motion[int(i)]);
    }
    row[dynamic_slot_drag] = float_bits(params.drag);
    row[dynamic_slot_now] = params.now;
    row[dynamic_slot_system_scale] = float_bits(params.system_scale);
    row[dynamic_slot_frame_time] = float_bits(params.frame_time);
    return row;
}

auto lut_set::sample(lut_row row, float t) const -> math::vec4
{
    const uint32_t base = uint32_t(row) * lut_size;
    const float position = math::clamp(t, 0.0f, 1.0f) * float(lut_size - 1);
    const uint32_t index = math::min(uint32_t(position), lut_size - 2);
    return math::mix(data[base + index], data[base + index + 1], position - float(index));
}

auto particle_hash(uint32_t value) -> uint32_t
{
    const uint32_t state = value * 747796405u + 2891336453u;
    const uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

auto particle_random(uint32_t seed, uint32_t salt) -> float
{
    return float(particle_hash(seed ^ (salt * k_salt_multiplier)) >> 8u) * (1.0f / 16777216.0f);
}

auto drag_lambda(float drag, float time) -> float
{
    const float x = drag * time;
    if(math::abs(x) < k_small_drag_time)
    {
        return time * (1.0f - x * (1.0f / 2.0f - x * (1.0f / 6.0f - x * (1.0f / 24.0f - x * (1.0f / 120.0f)))));
    }
    return (1.0f - std::exp(-x)) / drag;
}

auto drag_gamma(float drag, float time, float lambda) -> float
{
    const float x = drag * time;
    if(math::abs(x) < k_small_drag_time)
    {
        return time * time * (1.0f / 2.0f - x * (1.0f / 6.0f - x * (1.0f / 24.0f - x * (1.0f / 120.0f - x * (1.0f / 720.0f)))));
    }
    return (time - lambda) / drag;
}

auto evaluate_particle(const particle_record& record, const emitter_eval_params& params, const lut_set& luts)
    -> particle_state
{
    particle_state state;
    const uint32_t age_ticks = params.now - record.birth;
    state.is_alive = age_ticks < record.lifespan;
    const float lifespan = math::max(float(record.lifespan), 1.0f) / float(ticks_per_second);
    state.age = float(age_ticks) / float(ticks_per_second);
    state.life = math::clamp(state.age / lifespan, 0.0f, 1.0f);
    // A bounce starts a new segment of the path motion_offset ticks into the life; the curves go on from there.
    const float segment_age = float(math::min(record.motion_offset, age_ticks)) / float(ticks_per_second);
    state.segment_age = segment_age;
    float time = state.age - segment_age;
    float time_rate = 1.0f;
    float progress = state.life;
    float segment_progress = math::clamp(segment_age / lifespan, 0.0f, 1.0f);
    if(has_flag(params.flags, emitter_feature::non_linear_ease))
    {
        const math::vec4 life_curves = luts.sample(lut_row_life, progress);
        const float segment_eased = luts.sample(lut_row_life, segment_progress).x;
        time = (life_curves.x - segment_eased) * lifespan;
        time_rate = life_curves.y;
        progress = life_curves.x;
        segment_progress = segment_eased;
    }
    const float lambda = drag_lambda(params.drag, time);
    const float gamma = drag_gamma(params.drag, time, lambda);
    math::vec3 displacement;
    math::vec3 velocity;
    const math::vec3 launch = record.get_velocity();
    if(has_flag(params.flags, emitter_feature::speed_curve))
    {
        const math::vec4 speed = luts.sample(lut_row_speed, progress);
        const float segment_integral = luts.sample(lut_row_speed, segment_progress).x;
        displacement = launch * ((speed.x - segment_integral) * lifespan);
        velocity = launch * speed.y;
    }
    else
    {
        displacement = launch * lambda;
        velocity = launch * std::exp(-params.drag * time);
    }
    math::vec3 position = record.get_start() + displacement + params.acceleration * gamma;
    velocity = (velocity + params.acceleration * lambda) * time_rate;
    if(has_flag(params.flags, emitter_feature::turbulence))
    {
        // The field at the particle's start, scrolled from its birth by its age (so a stream from one point sways
        // over time): the displacement is how far the scroll has moved it since its segment began, the velocity
        // that displacement's rate. The noise tiles, so only the coordinates' fractions matter.
        const particle_noise& noise = get_particle_noise();
        const float strength = params.turbulence.x * luts.sample(lut_row_turbulence_size_z, state.life).x;
        const math::vec3 scroll(turbulence_scroll_axis * params.turbulence.z);
        const float birth_time = float(record.birth) / float(ticks_per_second);
        const math::vec3 base =
            math::fract(record.get_start() * params.turbulence.y + params.noise_offset) + math::fract(scroll * birth_time);
        const math::vec3 origin = noise.sample(base + scroll * segment_age);
        const math::vec3 now = noise.sample(base + scroll * state.age);
        const math::vec3 later = noise.sample(base + scroll * (state.age + turbulence_velocity_step));
        position += (now - origin) * strength;
        velocity += (later - now) * (strength / turbulence_velocity_step);
    }
    if(has_flag(params.flags, emitter_feature::local_space))
    {
        position = math::vec3(params.local_to_world * math::vec4(position, 1.0f));
        velocity = math::mat3(params.local_to_world) * velocity;
    }
    state.position = position;
    state.velocity = velocity;
    return state;
}

auto evaluate_particle_size(const particle_record& record,
                            const particle_state& state,
                            const emitter_eval_params& params,
                            const lut_set& luts) -> float
{
    const math::vec4 life_curves = luts.sample(lut_row_life, state.life);
    const math::vec2 picks = unpack_size_picks(record.size_picks);
    const float place = math::mix(picks.x, picks.y, size_pick_blend(state.life, params.size_width_start, params.size_width_end));
    float size = (math::mix(life_curves.z, life_curves.w, place) * record.size_factor + record.size_offset) *
                 params.system_scale;
    if(has_flag(params.flags, emitter_feature::size_by_speed))
    {
        const float factor = speed_factor(math::length(state.velocity), params.size_by_speed.w, params.size_by_speed.z);
        size *= math::mix(params.size_by_speed.x, params.size_by_speed.y, factor);
    }
    return math::max(size, 0.0f);
}

auto evaluate_particle_color(const particle_record& record,
                             const particle_state& state,
                             const emitter_eval_params& params,
                             const lut_set& luts) -> math::vec4
{
    math::vec4 color = luts.sample(lut_row_color, state.life);
    if(has_flag(params.flags, emitter_feature::two_gradients))
    {
        color = math::mix(color, luts.sample(lut_row_color_b, state.life), particle_random(record.seed, salt_gradient));
    }
    if(has_flag(params.flags, emitter_feature::color_by_speed))
    {
        color *= luts.sample(lut_row_color_by_speed,
                             speed_factor(math::length(state.velocity), params.color_by_speed.y, params.color_by_speed.x));
    }
    color *= unpack_tint(record);
    float intensity = params.intensity;
    if(has_flag(params.flags, emitter_feature::intensity_curve))
    {
        intensity *= luts.sample(lut_row_shape, state.life).w;
    }
    color.a *= params.opacity;
    color.r *= intensity;
    color.g *= intensity;
    color.b *= intensity;
    return color;
}

auto evaluate_particle_quad(const particle_record& record,
                            const particle_state& state,
                            const emitter_eval_params& params,
                            const lut_set& luts) -> math::vec2
{
    const float size = evaluate_particle_size(record, state, params, luts);
    math::vec2 quad(size);
    if(params.render_mode == render_mode::stretched)
    {
        quad.y = size * params.stretch_length_scale + math::length(state.velocity) * params.stretch_speed_scale;
    }
    else if(params.render_mode == render_mode::trail)
    {
        // The trail reaches back along the path at most the fastest the particle moved over its length (its
        // launch at the speed curve's peak plus what the acceleration added, at the easing's fastest rate), plus
        // the turbulence's push on its past points. Drag and the speed curve can make it slower now than it was.
        math::vec3 launch = record.get_velocity();
        math::vec3 acceleration = params.acceleration;
        if(has_flag(params.flags, emitter_feature::local_space))
        {
            launch = math::mat3(params.local_to_world) * launch;
            acceleration = math::mat3(params.local_to_world) * acceleration;
        }
        const float top_speed = math::length(launch) * params.speed_bound.x +
                                math::length(acceleration) * state.age * params.speed_bound.y;
        quad.y = size + 2.0f * (top_speed * params.trail_length + params.turbulence.w);
    }
    if(has_flag(params.flags, emitter_feature::size_axis_curve))
    {
        const math::vec4 shape = luts.sample(lut_row_shape, state.life);
        quad *= math::vec2(shape.x, shape.y);
    }
    return quad;
}

auto get_particle_cull_radius(const math::vec2& quad, const emitter_eval_params& params) -> float
{
    return 0.5f * math::length(quad * math::vec2(params.size_axes.x, params.size_axes.y)) * params.pivot_pad +
           params.camera_offset + params.turbulence.w;
}

auto get_camera_stretch_reach(const emitter_eval_params& params, float camera_speed, float axis_y_multiplier) -> float
{
    if(params.render_mode != render_mode::stretched)
    {
        return 0.0f;
    }
    return 0.5f * camera_speed * params.stretch_camera_velocity_scale * params.stretch_speed_scale * params.size_axes.y *
           math::abs(axis_y_multiplier) * params.pivot_pad;
}

auto is_within_draw_distance(const math::vec3& position, float radius, const math::vec3& eye, const emitter_eval_params& params)
    -> bool
{
    if(!has_flag(params.flags, emitter_feature::draw_distance))
    {
        return true;
    }
    return math::distance(position, eye) - radius <= params.draw_distance.x;
}

auto evaluate_particle_angle(const particle_record& record,
                             const particle_state& state,
                             const emitter_eval_params& params,
                             const lut_set& luts) -> float
{
    float speed = math::mix(params.spin.x, params.spin.y, particle_random(record.seed, salt_spin));
    if(has_flag(params.flags, emitter_feature::random_spin_direction) &&
       particle_random(record.seed, salt_spin_direction) < 0.5f)
    {
        speed = -speed;
    }
    // A spin curve turns by the curve's integral over the life lived (in lifetimes) times the lifetime.
    float spun_time = state.age;
    if(has_flag(params.flags, emitter_feature::spin_curve))
    {
        const float lifespan = math::max(float(record.lifespan), 1.0f) / float(ticks_per_second);
        spun_time = luts.sample(lut_row_shape, state.life).z * lifespan;
    }
    return math::mix(params.rotation.x, params.rotation.y, particle_random(record.seed, salt_rotation)) + speed * spun_time;
}

} // namespace ps_soa
} // namespace unravel
