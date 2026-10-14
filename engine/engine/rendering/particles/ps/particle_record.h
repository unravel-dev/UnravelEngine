#pragma once

#include <math/math.h>

#include <array>
#include <cstdint>

namespace unravel
{
namespace ps_soa
{

/// Particle ages are counted in ticks of 1/8192 s: sub-frame exact at any frame rate, and a 32-bit tick
/// count wraps after ~6 days, far beyond max_particle_lifetime.
constexpr double ticks_per_second = 8192.0;
constexpr uint32_t record_uints = 16;
constexpr uint32_t static_row_vec4s = 15;
constexpr uint32_t dynamic_row_uints = 28;
constexpr uint32_t lut_size = 256;
constexpr uint32_t lut_rows = 7;
constexpr uint32_t lut_set_vec4s = lut_size * lut_rows;

/// Slots of the emitter static row (PARTICLE_STATIC_* in particle_common.sh; the particle motion suite checks the two match).
enum static_slot : uint32_t
{
    /// Opacity, intensity, size pick widths at birth and at death.
    static_slot_appearance = 0,
    /// Size axes xyz, the curve set's first LUT vec4.
    static_slot_size_axes = 1,
    /// Size multiplier at the slow and fast ends, speed range scale and offset.
    static_slot_size_by_speed = 2,
    /// Color by speed range scale and offset, soft fade distance, render mode.
    static_slot_render = 3,
    /// Pivot xy, sheet tiles xy.
    static_slot_sheet = 4,
    /// Sheet cycles, feature flags, start rotation min and max (radians).
    static_slot_rotation = 5,
    /// Spin min and max (radians per second), pivot pad, sort mode.
    static_slot_spin = 6,
    /// Camera offset, camera fade start and end, max screen size.
    static_slot_view_shaping = 7,
    /// Stretch length and speed scales, TAA response, velocity alpha threshold.
    static_slot_stretch = 8,
    /// Strength, noise tiles per metre, per second, reach.
    static_slot_turbulence = 9,
    /// Noise offset xyz, receive shadows.
    static_slot_noise = 10,
    /// Max draw distance, its fade band, lighting wrap, cutout table + 1.
    static_slot_distance = 11,
    /// Trail length, trail width end, ribbon texture mode, ribbon repeats per second.
    static_slot_trail = 12,
    /// Facing axis xyz, share of the camera velocity a stretch takes off.
    static_slot_facing = 13,
    /// Fastest speed as multiples of the launch speed and of the acceleration's speed.
    static_slot_speed_bound = 14,
};

/// Slots of the emitter dynamic row (PARTICLE_DYNAMIC_* in particle_common.sh; the particle motion suite checks the two match).
enum dynamic_slot : uint32_t
{
    /// The emitter's local-to-world rows, 3 x 4 floats.
    dynamic_slot_local_to_world = 0,
    /// The emitter's rotation quaternion xyzw.
    dynamic_slot_rotation = 12,
    /// Acceleration xyz.
    dynamic_slot_acceleration = 16,
    /// Drag.
    dynamic_slot_drag = 19,
    /// The emitter's clock, ticks (uint).
    dynamic_slot_now = 20,
    /// The emitter's uniform size scale.
    dynamic_slot_system_scale = 21,
    /// Seconds the clock advanced since the last frame.
    dynamic_slot_frame_time = 22,
    /// A local emitter's origin motion since the last frame, xyz.
    dynamic_slot_origin_motion = 23,
};

enum lut_row : uint32_t
{
    lut_row_color = 0,
    lut_row_life = 1,
    lut_row_color_by_speed = 2,
    lut_row_speed = 3,
    /// (size x multiplier, size y multiplier, spin integral, intensity) over life.
    lut_row_shape = 4,
    /// The second color gradient (two_gradients).
    lut_row_color_b = 5,
    /// (turbulence strength over life, size z multiplier over life, 0, 0): the z multiplier sizes mesh particles.
    lut_row_turbulence_size_z = 6,
};

/**
 * @brief A particle as written once at its birth (64 bytes). Layout matches particle_record in
 * engine_data/data/shaders/particles/particle_common.sh.
 *
 * Everything a particle is at a later age follows from this record, its emitter's rows and the curve
 * set, so the record is never written again until the slot is reused.
 */
struct particle_record
{
    /// Spawn position and launch velocity, in simulation space (world, or the emitter's local space).
    /// Plain floats: math::vec3 is padded to 16 bytes in this build.
    float start[3] = {0.0f, 0.0f, 0.0f};
    /// Emitter time of birth, ticks (low 32 bits).
    uint32_t birth = 0;
    float velocity[3] = {0.0f, 0.0f, 0.0f};
    /// Lifetime, ticks.
    uint32_t lifespan = 0;
    uint32_t seed = 0;
    /// The size picks at birth and death, unorm16 each (start in the low half).
    uint32_t size_picks = 0;
    float size_factor = 1.0f;
    float size_offset = 0.0f;
    /// Linear tint, half floats: rg, ba.
    uint32_t tint_rg = 0;
    uint32_t tint_ba = 0;
    /// Emitter table row (low 16 bits).
    uint32_t emitter = 0;
    /// Age, ticks, at which the current motion segment started (0 = since birth).
    uint32_t motion_offset = 0;

    auto get_start() const -> math::vec3
    {
        return {start[0], start[1], start[2]};
    }

    void set_start(const math::vec3& value)
    {
        start[0] = value.x;
        start[1] = value.y;
        start[2] = value.z;
    }

    auto get_velocity() const -> math::vec3
    {
        return {velocity[0], velocity[1], velocity[2]};
    }

    void set_velocity(const math::vec3& value)
    {
        velocity[0] = value.x;
        velocity[1] = value.y;
        velocity[2] = value.z;
    }
};

static_assert(sizeof(particle_record) == record_uints * sizeof(uint32_t), "particle_record must be 64 bytes");

auto pack_size_picks(float start, float end) -> uint32_t;
auto unpack_size_picks(uint32_t picks) -> math::vec2;
void pack_tint(const math::vec4& tint, particle_record& record);
auto unpack_tint(const particle_record& record) -> math::vec4;

/// Seconds to ticks, rounded to the nearest tick.
inline auto seconds_to_ticks(double seconds) -> uint64_t
{
    return seconds <= 0.0 ? 0u : uint64_t(seconds * ticks_per_second + 0.5);
}

inline auto ticks_to_seconds(uint64_t ticks) -> double
{
    return double(ticks) / ticks_per_second;
}

} // namespace ps_soa
} // namespace unravel
