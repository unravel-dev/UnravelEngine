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

/**
 * @brief Emission rate, shape, and lifetime cycle authoring.
 */
struct emitter_emission_desc
{
    float particles_per_second = 50.0f;
    float emission_lifetime = 2.0f;
    float start_delay = 0.0f;
    bool loop = true;
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
    bool align_to_direction = false;
    math::vec2 pivot{0.5f, 0.5f};
    math::vec2 tex_sheet_tiles{1.0f, 1.0f};
    float tex_sheet_cycles = 0.0f;
    bool tex_sheet_randomize = false;

    void reset();
};

/**
 * @brief Playback gates (playing / paused).
 */
struct emitter_playback_desc
{
    bool playing = true;
    bool paused = false;

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
