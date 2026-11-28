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
    /// at @p speed, living @p lifespan seconds from then, at most @p size large (before the emitter's scale).
    /// @p whole_life is its lifetime from birth, which is longer than @p lifespan for a path that starts at a bounce.
    void add(double time, const math::vec3& position, float speed, float lifespan, float size, float whole_life);
    /// Largest particle of the live buckets (0 when empty).
    auto get_max_size() const -> float;
    /// Shortest lifetime, from birth, of the live buckets' particles (0 when empty).
    auto get_shortest_life() const -> float;
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
        /// Shortest lifetime from birth of its particles (0 while it holds none).
        float min_whole_life = 0.0f;
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
    /// The particles bounce off surfaces the CPU does not know (collision_source::scene_surfaces, unless the quality
    /// turns scene collision off): the speed the acceleration gives one can then carry it any way, not only along the
    /// acceleration, and a speed curve or an easing speeds a bounced particle up by as much as it rises after the
    /// bounce.
    bool bounces_unseen = false;
    /// The emitter's forces (emitter_eval_params), in simulation space: whether the particles turn about the axis
    /// through force_origin along force_axis, whether the radial force moves them, the metres per second of life it
    /// carries them away from the axis and toward it at most (lut_ranges::radial_rise and radial_fall x the radial
    /// speed), whether they converge on force_target, and the fastest the forces move one, metres per second.
    bool orbits = false;
    bool moves_radially = false;
    float radial_out_per_second = 0.0f;
    float radial_in_per_second = 0.0f;
    bool converges = false;
    math::vec3 force_origin{0.0f};
    math::vec3 force_axis{0.0f, 1.0f, 0.0f};
    math::vec3 force_target{0.0f};
    float force_speed = 0.0f;
};

/**
 * @brief Bounds, in simulation space, of every particle spawned in @p history (with @p spawn_region,
 * where new ones appear, added). Sizes are not included.
 */
auto compute_motion_bounds(const spawn_history& history, const math::bbox& spawn_region, const motion_bounds_params& params)
    -> math::bbox;

} // namespace ps_soa
} // namespace unravel
