#pragma once

#include "particle_types.h"

#include <base/basetypes.hpp>
#include <bx/easing.h>
#include <math/gradient.h>
#include <math/math.h>

namespace unravel
{
namespace ps_soa
{

/// Resolution the emitter's curves are pre-sampled at. Every particle reads them per frame, so
/// they are baked once per edit rather than evaluated key by key.
constexpr size_t curve_bake_resolution = 256;
/// The lifetime curve is read against a speed rather than a 0..1 lifetime, so it is baked finer.
constexpr size_t lifetime_by_emitter_speed_bake_resolution = 1024;

/**
 * @brief Emission rate, shape, and lifetime cycle authoring.
 */
struct emitter_emission_desc
{
    float particles_per_second = 50.0f;
    float emission_lifetime = 2.0f;
    float start_delay = 0.0f;
    bool loop = true;
    /// A looping emitter starts full, as if it had been running for a whole particle lifetime, when it
    /// first plays after it is created or reset.
    bool prewarm = false;
    spawn_location spawn_location = spawn_location::inside;
    math::vec3 shape_position{0.0f, 0.0f, 0.0f};
    math::vec3 shape_scale{1.0f, 1.0f, 1.0f};

    void reset();
};

/**
 * @brief Motion / forces / trajectory authoring.
 */
struct emitter_motion_desc
{
    simulation_space space = simulation_space::world;
    float lifetime = 1.0f;
    float gravity_scale = 0.0f;
    float temporal_motion = 1.0f;
    float velocity_damping = 0.0f;
    math::vec3 force_over_lifetime{0.0f, 0.0f, 0.0f};
    math::gradient<frange_t> velocity_gradient;
    bx::Easing::Enum position_easing = bx::Easing::Linear;
    math::gradient<float> lifetime_by_emitter_speed_gradient;
    frange_t lifetime_by_emitter_speed_range{0.0f, 10.0f};

    void reset();
};

/**
 * @brief Color / scale / speed-based appearance authoring.
 */
struct emitter_appearance_desc
{
    math::gradient<math::color> color_gradient;
    math::gradient<frange_t> scale_gradient;
    math::vec3 initial_scale_3d{1.0f, 1.0f, 1.0f};
    float opacity = 1.0f;
    float color_intensity = 1.0f;
    frange_t size_by_speed_range{1.0f, 1.0f};
    frange_t size_by_speed_velocity_range{0.0f, 10.0f};
    math::gradient<math::color> color_by_speed_gradient;
    frange_t color_by_speed_velocity_range{0.0f, 10.0f};

    void reset();
};

/**
 * @brief Render / texture-sheet authoring (constant per emitter).
 */
struct emitter_render_desc
{
    texture_mode texture_mode = texture_mode::multi_channel;
    render_mode render_mode = render_mode::billboard;
    blend_mode blend_mode = blend_mode::normal;
    /// Particles turn so the texture's top points along their motion.
    bool align_to_direction = false;
    math::vec2 pivot{0.5f, 0.5f};
    math::vec2 tex_sheet_tiles{1.0f, 1.0f};
    float tex_sheet_cycles = 0.0f;
    bool tex_sheet_randomize = false;
    /// Crossfade from each texture sheet frame into the next instead of stepping between them.
    bool frame_blending = false;
    /// Start angle of a particle in its quad's plane, degrees, random between min and max.
    frange_t rotation{0.0f, 0.0f};
    /// Spin in degrees per second, random between min and max.
    frange_t rotation_speed{0.0f, 0.0f};
    /// Half the particles spin the other way.
    bool random_rotation_direction = false;
    /// Metres over which a particle fades out where it meets the scene behind it (soft particles);
    /// 0 keeps the hard edge.
    float soft_fade_distance = 0.0f;
    /// Draw order against other emitters, whatever their blend mode: lower values draw first and
    /// higher ones composite on top. Emitters of equal priority draw back to front by distance.
    int32_t sort_priority = 0;

    void reset();
};

/**
 * @brief Playback gates (playing / paused).
 */
struct emitter_playback_desc
{
    bool playing = true;
    bool paused = false;
    /// Whether the emitter plays from the moment it is created (scene load, play start, instantiation);
    /// off leaves it stopped until it is played.
    bool auto_play = true;

    void reset();
};

/**
 * @brief Full authoring description for an emitter (no transforms / runtime).
 */
struct emitter_desc
{
    emitter_emission_desc emission;
    emitter_motion_desc motion;
    emitter_appearance_desc appearance;
    emitter_render_desc render;
    emitter_playback_desc playback;

    void reset();
    /**
     * @brief Derive feature mask used to specialize update/render kernels.
     */
    auto bake_features() const -> emitter_feature;
};

} // namespace ps_soa
} // namespace unravel
