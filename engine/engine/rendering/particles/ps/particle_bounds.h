#pragma once

#include "particle_eval.h"

#include <math/bbox.h>
#include <math/math.h>

#include <deque>

namespace unravel
{
namespace ps_soa
{

/**
 * @brief Where an emitter's live particles were spawned, in time buckets: the region, the fastest launch
 * and the longest life of each bucket. Together with the motion the emitter applies, it bounds every
 * live particle without visiting one.
 */
class spawn_history
{
public:
    /// Emitter seconds one bucket covers.
    static constexpr double bucket_seconds = 0.1;

    void clear();
    /// Records a particle spawned at @p time (emitter seconds) at @p position (simulation space), launched
    /// at @p speed, living @p lifespan seconds, at most @p size large (before the emitter's scale).
    void add(double time, const math::vec3& position, float speed, float lifespan, float size);
    /// Largest particle of the live buckets (0 when empty).
    auto get_max_size() const -> float;
    /// Drops the buckets whose particles are all dead at @p now.
    void expire(double now);
    auto empty() const -> bool;

    struct bucket
    {
        double first_time = 0.0;
        double last_time = 0.0;
        math::bbox region;
        float max_speed = 0.0f;
        float max_lifespan = 0.0f;
        float max_size = 0.0f;
    };

    auto get_buckets() const -> const std::deque<bucket>&;

private:
    std::deque<bucket> buckets_;
};

struct motion_bounds_params
{
    /// Simulation-space acceleration and drag (particle_eval.h, emitter_eval_params).
    math::vec3 acceleration{0.0f};
    float drag = 0.0f;
    lut_ranges ranges{};
    bool has_ease = false;
    bool has_speed_curve = false;
};

/**
 * @brief Bounds, in simulation space, of every particle spawned in @p history (with @p spawn_region,
 * where new ones appear, added). Sizes are not included.
 */
auto compute_motion_bounds(const spawn_history& history, const math::bbox& spawn_region, const motion_bounds_params& params)
    -> math::bbox;

} // namespace ps_soa
} // namespace unravel
