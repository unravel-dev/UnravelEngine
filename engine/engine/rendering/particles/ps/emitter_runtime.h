#pragma once

#include "particle_types.h"

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
 * @brief Mutable emission bookkeeping owned by the emitter instance.
 */
struct emitter_sim_state
{
    static constexpr size_t temporal_buffer_size = 8;

    /// Time since the last rate-spawned particle, seconds (always below one particle interval).
    float emission_time_accum = 0.0f;
    /// Metres the emitter moved since its last distance-spawned particle (always below one spacing).
    float distance_accum = 0.0f;
    float start_delay_elapsed = 0.0f;
    /// Time into the current emission cycle, seconds.
    float cycle_time = 0.0f;
    /// A one-shot emitter has emitted its whole cycle.
    bool emission_finished = false;
    bool first_update = true;
    /// Cleared when the emitter first plays after it is created or reset; a prewarm runs then.
    bool prewarm_pending = true;
    bool playing = true;
    /// Simulation time skipped while culling froze the emitter, caught up when it wakes.
    float frozen_time = 0.0f;
    emitter_feature features = emitter_feature::none;

    std::array<math::vec3, temporal_buffer_size> temporal_positions{};
    std::array<float, temporal_buffer_size> temporal_dts{};
    uint32_t temporal_count = 0;

    math::bbox world_bounds{math::vec3(-1.0f), math::vec3(1.0f)};

    void reset();
    void push_temporal_sample(const math::vec3& position, float dt);
    auto calculate_smoothed_emitter_speed(float max_speed) const -> float;
};

} // namespace ps_soa
} // namespace unravel
