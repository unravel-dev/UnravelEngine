#pragma once

#include "particle_types.h"

#include <bx/easing.h>
#include <math/bbox.h>
#include <math/math.h>
#include <math/transform.hpp>

#include <array>
#include <cstdint>

namespace unravel
{
namespace ps_soa
{

/**
 * @brief Per-frame transform inputs. Authoring desc never owns these.
 */
struct emitter_transform_state
{
    math::transform current{};
    math::transform previous{};
};

/**
 * @brief Mutable simulation bookkeeping owned by the soa emitter instance.
 */
struct emitter_sim_state
{
    static constexpr size_t temporal_buffer_size = 8;

    float emission_time_accum = 0.0f;
    float start_delay_elapsed = 0.0f;
    uint32_t total_particles_spawned = 0;
    bool first_update = true;
    bool playing = true;
    bool loop = true;
    emitter_feature features = emitter_feature::none;

    std::array<math::vec3, temporal_buffer_size> temporal_positions{};
    std::array<float, temporal_buffer_size> temporal_dts{};
    uint32_t temporal_count = 0;

    math::bbox world_bounds{math::vec3(-1.0f), math::vec3(1.0f)};

    void reset();
    void push_temporal_sample(const math::vec3& position, float dt);
    auto calculate_smoothed_emitter_speed(float max_speed) const -> float;
};

/**
 * @brief Hot-loop constants baked from desc (CPU now, CB-shaped for GPU later).
 */
struct emitter_sim_constants
{
    emitter_feature features = emitter_feature::none;
    simulation_space space = simulation_space::world;
    float opacity = 1.0f;
    float color_intensity = 1.0f;
    float avg_system_scale = 1.0f;
    math::vec3 particle_scale_3d{1.0f, 1.0f, 1.0f};
    math::vec2 pivot{0.5f, 0.5f};
    render_mode render_mode = render_mode::billboard;
    blend_mode blend_mode = blend_mode::normal;
    texture_mode texture_mode = texture_mode::multi_channel;
    math::vec2 tex_sheet_tiles{1.0f, 1.0f};
    float tex_sheet_cycles = 0.0f;
    bool tex_sheet_randomize = false;
    frange_t size_by_speed_range{1.0f, 1.0f};
    frange_t size_by_speed_velocity_range{0.0f, 10.0f};
    frange_t color_by_speed_velocity_range{0.0f, 10.0f};
    float inv_size_by_speed_velocity_span = 0.0f;
    float inv_color_by_speed_velocity_span = 0.0f;
    bx::EaseFn ease_pos = nullptr;
    math::mat4 local_to_world{1.0f};
    /// Emitter world rotation used for constrained billboards in local simulation.
    /// Stored with non-negative w so xyz can be packed into instance data.
    math::quat emitter_rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

} // namespace ps_soa
} // namespace unravel
