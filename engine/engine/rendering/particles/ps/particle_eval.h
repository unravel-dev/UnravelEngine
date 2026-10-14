#pragma once

#include "particle_record.h"
#include "particle_types.h"

#include <math/math.h>

#include <array>
#include <cstdint>
#include <vector>

namespace unravel
{
namespace ps_soa
{

/**
 * @brief What the GPU knows about an emitter, in named form. pack_static_row / pack_dynamic_row turn
 * it into the emitter table rows of particle_common.sh; the CPU evaluates particles from it directly.
 */
struct emitter_eval_params
{
    // Settings (static row).
    float opacity = 1.0f;
    float intensity = 1.0f;
    float size_width_start = 0.0f;
    float size_width_end = 0.0f;
    math::vec3 size_axes{1.0f};
    uint32_t lut_base = 0;
    math::vec4 size_by_speed{1.0f, 1.0f, 0.0f, 0.0f};
    math::vec2 color_by_speed{0.0f, 0.0f};
    float soft_fade_distance = 0.0f;
    render_mode render_mode = render_mode::billboard;
    math::vec2 pivot{0.5f, 0.5f};
    math::vec2 tiles{1.0f, 1.0f};
    float cycles = 0.0f;
    uint32_t flags = 0;
    math::vec2 rotation{0.0f, 0.0f};
    math::vec2 spin{0.0f, 0.0f};
    /// 1 + how far an off-centre pivot pushes the quad from its particle (cull radius factor).
    float pivot_pad = 1.0f;
    sort_mode sort_mode = sort_mode::automatic;
    float camera_offset = 0.0f;
    math::vec2 camera_fade{0.0f, 0.0f};
    float max_screen_size = 0.0f;
    float stretch_length_scale = 1.0f;
    float stretch_speed_scale = 0.0f;
    /// Stretched mode: the share of the view's camera velocity taken off the particle's velocity.
    float stretch_camera_velocity_scale = 0.0f;
    /// Fixed axis mode: the quad's axis in the emitter's space.
    math::vec3 facing_axis{0.0f, 1.0f, 0.0f};
    float taa_response = 0.0f;
    /// Velocity pass: texels below this opacity keep the scene's motion.
    float velocity_alpha_threshold = 0.5f;
    /// Turbulence: metres of displacement, noise tiles per metre, noise tiles per second of evolution, and the
    /// farthest it moves a particle (cull radius); the emitter's own place in the noise.
    math::vec4 turbulence{0.0f};
    math::vec3 noise_offset{0.0f};
    /// Max draw distance and the fade band before it, metres.
    math::vec2 draw_distance{0.0f};
    /// Lit particles: 1 when they darken in the sun's shadow; the surface lighting's wrap.
    float receive_shadows = 1.0f;
    float lighting_wrap = 0.5f;
    /// Cutout primitive: the emitter's cutout table's first vec4 + 1 (0 = none yet: the whole quad draws).
    float cutout_table = 0.0f;
    /// Trail mode: seconds of path, width at the tail (share of the size).
    float trail_length = 0.0f;
    float trail_width_end = 0.0f;
    /// Ribbon mode: the texture mode (ribbon_texture_mode) and the repeats per second of age (tile).
    ribbon_texture_mode ribbon_texture_mode = ribbon_texture_mode::age;
    float ribbon_tiles_per_second = 4.0f;
    /// Fastest a particle moves: x multiples of its launch speed (the speed curve's peak x the easing's fastest
    /// rate) plus y multiples of the speed the acceleration gave it (the easing's fastest rate).
    math::vec2 speed_bound{1.0f, 1.0f};
    // Per frame (dynamic row).
    math::mat4 local_to_world{1.0f};
    math::quat emitter_rotation{1.0f, 0.0f, 0.0f, 0.0f};
    math::vec3 acceleration{0.0f};
    float drag = 0.0f;
    uint32_t now = 0;
    float system_scale = 1.0f;
    /// Seconds the emitter's clock advanced since the last frame (motion vectors).
    float frame_time = 0.0f;
    /// World displacement of a local-space emitter's origin since the last frame (motion vectors).
    math::vec3 origin_motion{0.0f};
};

using static_row = std::array<math::vec4, static_row_vec4s>;
using dynamic_row = std::array<uint32_t, dynamic_row_uints>;

auto pack_static_row(const emitter_eval_params& params) -> static_row;
auto pack_dynamic_row(const emitter_eval_params& params) -> dynamic_row;

/// Extremes of a baked curve set that the analytic bounds need.
struct lut_ranges
{
    /// Range of the eased life (an overshooting easing leaves 0..1).
    float ease_min = 0.0f;
    float ease_max = 1.0f;
    /// Largest |integral of the speed curve| over a life, in lifetimes (1 for a constant speed).
    float speed_integral_max = 1.0f;
    /// Largest |speed curve factor| and |easing rate| over a life (1 without them): the fastest a particle moves
    /// is its launch speed times both.
    float speed_factor_max = 1.0f;
    float ease_rate_max = 1.0f;
    /// Largest size the size curve reaches over a life.
    float size_peak = 0.0f;
    /// Largest multiplier the per-axis size curves reach (1 without them).
    float size_axis_peak = 1.0f;
    /// Largest |turbulence strength multiplier| over a life.
    float turbulence_peak = 1.0f;
};

/**
 * @brief The curve rows an emitter's particles read (particle_common.sh, particle_lut; lut_row).
 */
struct lut_set
{
    std::array<math::vec4, lut_set_vec4s> data{};
    lut_ranges ranges{};

    auto sample(lut_row row, float t) const -> math::vec4;
};

/// A particle at the emitter's current time.
struct particle_state
{
    math::vec3 position{};
    math::vec3 velocity{};
    float age = 0.0f;
    float life = 0.0f;
    /// Seconds into its life the particle's current path segment began (its last bounce; 0 before any).
    float segment_age = 0.0f;
    bool is_alive = false;
};

/// Distance an initial velocity carries a particle in @p time seconds under linear drag.
auto drag_lambda(float drag, float time) -> float;
/// Distance a constant acceleration carries a particle in @p time seconds under linear drag.
auto drag_gamma(float drag, float time, float lambda) -> float;

auto evaluate_particle(const particle_record& record, const emitter_eval_params& params, const lut_set& luts)
    -> particle_state;
auto evaluate_particle_size(const particle_record& record,
                            const particle_state& state,
                            const emitter_eval_params& params,
                            const lut_set& luts) -> float;
auto evaluate_particle_color(const particle_record& record,
                             const particle_state& state,
                             const emitter_eval_params& params,
                             const lut_set& luts) -> math::vec4;
auto evaluate_particle_angle(const particle_record& record,
                             const particle_state& state,
                             const emitter_eval_params& params,
                             const lut_set& luts) -> float;
/// The particle's quad, width and height, before the emitter's 3D scale: square, or stretched along its
/// motion in stretched mode.
auto evaluate_particle_quad(const particle_record& record,
                            const particle_state& state,
                            const emitter_eval_params& params,
                            const lut_set& luts) -> math::vec2;
/// Radius around the particle's position its quad stays inside, whatever its spin, camera offset and turbulence.
auto get_particle_cull_radius(const math::vec2& quad, const emitter_eval_params& params) -> float;
/// How much further a stretched quad may reach when the view's camera moves at @p camera_speed (m/s) and the
/// emitter stretches against the camera's velocity; 0 otherwise.
auto get_camera_stretch_reach(const emitter_eval_params& params, float camera_speed, float axis_y_multiplier) -> float;

/// False when the particle (its cull sphere) lies beyond its emitter's max draw distance from @p eye.
auto is_within_draw_distance(const math::vec3& position, float radius, const math::vec3& eye, const emitter_eval_params& params)
    -> bool;

/// Seconds between the two noise samples the turbulence's velocity is taken from.
constexpr float turbulence_velocity_step = 1.0f / 30.0f;
/// The direction the noise scrolls along as it evolves (a unit diagonal).
constexpr float turbulence_scroll_axis = 0.57735027f;

/// A number in [0, 1) drawn from a particle's seed; each salt gives an independent pick.
auto particle_random(uint32_t seed, uint32_t salt) -> float;
auto particle_hash(uint32_t value) -> uint32_t;

constexpr uint32_t salt_rotation = 1;
constexpr uint32_t salt_spin = 2;
constexpr uint32_t salt_spin_direction = 3;
constexpr uint32_t salt_sheet = 4;
constexpr uint32_t salt_gradient = 5;
constexpr uint32_t salt_sheet_row = 6;
/// The axis a mesh particle turns about: its height on the unit sphere and its turn around the vertical.
constexpr uint32_t salt_mesh_axis_height = 7;
constexpr uint32_t salt_mesh_axis_turn = 8;

} // namespace ps_soa
} // namespace unravel
