#pragma once

#include <math/math.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace unravel
{
namespace ps_soa
{

/// Particle ages are counted in ticks of 1/8192 s: sub-frame exact at any frame rate, and a 32-bit tick
/// count wraps after ~6 days, far beyond max_particle_lifetime.
constexpr double ticks_per_second = 8192.0;
constexpr uint32_t record_uints = 16;
constexpr uint32_t static_row_vec4s = 17;
constexpr uint32_t dynamic_row_uints = 36;
constexpr uint32_t lut_size = 256;
constexpr uint32_t lut_rows = 9;
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
    /// Ribbon facing (ribbon_facing), the ribbons the emitter's live particles are of, 1 when particles at rest lie
    /// flat on their surface, unused w.
    static_slot_ribbon = 15,
    /// Forces: radians per second about the force axis, metres per second away from it, 1 when the particles converge
    /// on the target, unused w.
    static_slot_forces = 16,
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
    /// The forces' axis, unit, in simulation space, xyz.
    dynamic_slot_force_axis = 26,
    /// The point the particles converge on, in simulation space, xyz.
    dynamic_slot_force_target = 29,
    /// The fastest the forces move a particle, metres per second in world space. The three uints after it are unused.
    dynamic_slot_force_speed = 32,
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
    /// (turbulence strength, size z multiplier, Custom Data X, Custom Data Y) over life: the z multiplier sizes mesh
    /// particles, the custom data is what the emitter's material reads.
    lut_row_turbulence_size_z = 6,
    /// The forces over life: the integrals of the orbit and of the radial multiplier (in lifetimes), the converge
    /// blend (0..1), unused w.
    lut_row_forces = 7,
    /// What lut_row_forces changes by: the orbit and the radial multiplier, the converge blend's slope per lifetime,
    /// unused w.
    lut_row_force_rates = 8,
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
    /// Emitter table row, and the ribbon of its emitter the particle is of (ribbon render mode): the low and the
    /// high half of one uint of the record.
    uint16_t emitter = 0;
    uint16_t ribbon = 0;
    /// Age, ticks, at which the current motion segment started (0 = since birth). With motion_offset_rest_bit the
    /// particle rests at start since that age, and velocity holds the unit normal of the surface it rests on.
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

/// The bit of particle_record::motion_offset that marks a particle at rest (PARTICLE_MOTION_REST_BIT in
/// particle_common.sh). An age never reaches it: lifetimes are far shorter than 2^31 ticks.
constexpr uint32_t motion_offset_rest_bit = 0x80000000u;

/// Metres a bounced particle starts off its surface, so its next path does not cross it at once, and metres above
/// its surface a particle at rest lies when it lies flat on it (PARTICLE_CONTACT_LIFT and PARTICLE_REST_LIFT in
/// cs_particle_collide.eshader).
constexpr float contact_lift = 1.0e-3f;
constexpr float rest_lift = 5.0e-3f;
/// Metres from the forces' axis below which a particle is on it and has no direction to turn or move along, and the
/// least the converge blend has left to go at a path segment's start for the segment to blend on: with less the
/// particle is at the target (PARTICLE_MIN_FORCE_RADIUS and PARTICLE_MIN_CONVERGE_SPAN in particle_common.sh).
constexpr float min_force_radius = 1.0e-5f;
constexpr float min_converge_span = 1.0e-4f;
/// Smallest speed factor (speed curve times easing rate) a bounce divides by when it carries a particle's curves on
/// past its contact; below it the bounced velocity is kept as it is (PARTICLE_MIN_SEGMENT_FACTOR).
constexpr float min_segment_factor = 1.0e-2f;

/// A record in the GPU's pool also counts its particle's bounces off scene surfaces (cs_particle_collide.eshader)
/// in motion_offset: motion_offset_bounce_mask at motion_offset_bounce_shift, above the ages (every age fits
/// motion_offset_age_mask) and below the rest bit. The CPU's records never hold a count
/// (PARTICLE_MOTION_BOUNCE_SHIFT, PARTICLE_MOTION_BOUNCE_MASK and PARTICLE_MOTION_AGE_MASK in particle_common.sh).
constexpr uint32_t motion_offset_bounce_shift = 25u;
constexpr uint32_t motion_offset_bounce_mask = 0x3fu;
constexpr uint32_t motion_offset_age_mask = (1u << motion_offset_bounce_shift) - 1u;
static_assert(((motion_offset_bounce_mask << motion_offset_bounce_shift) & (motion_offset_rest_bit | motion_offset_age_mask)) == 0u,
              "the bounce count lies between the age and the rest bit");

/// Whether the particle rests where it is for the rest of its life (collision_mode::stick, or a bounce slower than the
/// emitter's rest speed).
inline auto is_at_rest(const particle_record& record) -> bool
{
    return (record.motion_offset & motion_offset_rest_bit) != 0u;
}

/// The age, ticks, at which a particle at rest came to rest.
inline auto get_rest_age_ticks(const particle_record& record) -> uint32_t
{
    return record.motion_offset & ~motion_offset_rest_bit;
}
// particle_common.sh reads the emitter as the low half and the ribbon as the high half of the record's uint 14.
static_assert(offsetof(particle_record, emitter) == 14 * sizeof(uint32_t), "the emitter row is the low half of uint 14");
static_assert(offsetof(particle_record, ribbon) == 14 * sizeof(uint32_t) + sizeof(uint16_t), "the ribbon is the high half of uint 14");

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
