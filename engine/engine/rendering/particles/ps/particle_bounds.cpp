#include "particle_bounds.h"

#include "particle_eval.h"

namespace unravel
{
namespace ps_soa
{
namespace
{

/// Farthest the launch velocity carries a particle living @p lifespan seconds, per unit of speed.
auto launch_reach(float lifespan, const motion_bounds_params& params) -> float
{
    if(params.has_speed_curve)
    {
        return lifespan * params.ranges.speed_integral_max;
    }
    const float earliest = params.has_ease ? params.ranges.ease_min * lifespan : 0.0f;
    const float latest = params.has_ease ? params.ranges.ease_max * lifespan : lifespan;
    return math::max(math::abs(drag_lambda(params.drag, earliest)), math::abs(drag_lambda(params.drag, latest)));
}

/// Largest distance factor of the acceleration over a life (gamma is convex and 0 at birth).
auto acceleration_reach(float lifespan, const motion_bounds_params& params) -> float
{
    const float earliest = params.has_ease ? params.ranges.ease_min * lifespan : 0.0f;
    const float latest = params.has_ease ? params.ranges.ease_max * lifespan : lifespan;
    const auto gamma = [&](float time)
    {
        return drag_gamma(params.drag, time, drag_lambda(params.drag, time));
    };
    return math::max(gamma(earliest), gamma(latest));
}

/// Bounds of particles that bounce off surfaces the CPU does not know while curves reshape their motion: no further
/// from where each was spawned than the fastest it can move, sped up by what the curves add after a bounce
/// (lut_ranges::bounce_speed_gain), for its whole life, any way.
auto compute_unseen_bounce_bounds(const spawn_history& history, const math::bbox& spawn_region, const motion_bounds_params& params)
    -> math::bbox
{
    math::bbox bounds = spawn_region;
    const float gain = math::max(params.ranges.bounce_speed_gain, 1.0f);
    const float curve_speed = (params.has_speed_curve ? params.ranges.speed_factor_max : 1.0f) *
                              (params.has_ease ? params.ranges.ease_rate_max : 1.0f);
    const float acceleration = math::length(params.acceleration);
    float longest_reach = 0.0f;
    for(const auto& bucket : history.get_buckets())
    {
        const float top_speed = bucket.max_speed * curve_speed + acceleration * bucket.max_lifespan;
        const float reach = top_speed * bucket.max_lifespan * gain;
        bounds.add_point(bucket.region.min - math::vec3(reach));
        bounds.add_point(bucket.region.max + math::vec3(reach));
        longest_reach = math::max(longest_reach, reach);
    }
    // New particles reach as far from where they appear.
    bounds.add_point(spawn_region.min - math::vec3(longest_reach));
    bounds.add_point(spawn_region.max + math::vec3(longest_reach));
    return bounds;
}

} // namespace

void spawn_history::clear()
{
    buckets_.clear();
}

void spawn_history::add(double time, const math::vec3& position, float speed, float lifespan, float size)
{
    if(buckets_.empty() || time - buckets_.back().first_time >= bucket_seconds)
    {
        bucket fresh;
        fresh.first_time = time;
        fresh.region.reset();
        buckets_.push_back(fresh);
    }
    bucket& current = buckets_.back();
    current.last_time = math::max(current.last_time, time);
    current.region.add_point(position);
    current.max_speed = math::max(current.max_speed, speed);
    current.max_lifespan = math::max(current.max_lifespan, lifespan);
    current.max_size = math::max(current.max_size, size);
}

auto spawn_history::get_max_size() const -> float
{
    float size = 0.0f;
    for(const auto& bucket : buckets_)
    {
        size = math::max(size, bucket.max_size);
    }
    return size;
}

void spawn_history::expire(double now)
{
    // Buckets are in spawn order but not in death order (lifetimes differ), so check each.
    for(auto it = buckets_.begin(); it != buckets_.end();)
    {
        if(it->last_time + double(it->max_lifespan) <= now)
        {
            it = buckets_.erase(it);
            continue;
        }
        ++it;
    }
}

auto spawn_history::empty() const -> bool
{
    return buckets_.empty();
}

auto spawn_history::get_buckets() const -> const std::deque<bucket>&
{
    return buckets_;
}

auto compute_motion_bounds(const spawn_history& history, const math::bbox& spawn_region, const motion_bounds_params& params)
    -> math::bbox
{
    // Curves that reshape the motion go on after a bounce the CPU did not see: the path's own bounds do not hold it.
    if(params.bounces_unseen && (params.has_ease || params.has_speed_curve))
    {
        return compute_unseen_bounce_bounds(history, spawn_region, params);
    }
    math::bbox bounds = spawn_region;
    float longest_life = 0.0f;
    for(const auto& bucket : history.get_buckets())
    {
        const float reach = bucket.max_speed * launch_reach(bucket.max_lifespan, params);
        bounds.add_point(bucket.region.min - math::vec3(reach));
        bounds.add_point(bucket.region.max + math::vec3(reach));
        longest_life = math::max(longest_life, bucket.max_lifespan);
    }
    const math::vec3 drift = params.acceleration * acceleration_reach(longest_life, params);
    if(params.bounces_unseen)
    {
        // A bounce turns what the acceleration gave a particle any way.
        const math::vec3 reach(math::length(drift));
        bounds.min -= reach;
        bounds.max += reach;
        return bounds;
    }
    // The acceleration moves every particle the same way: stretch the box toward where it carries them.
    bounds.min += math::min(drift, math::vec3(0.0f));
    bounds.max += math::max(drift, math::vec3(0.0f));
    return bounds;
}

} // namespace ps_soa
} // namespace unravel
