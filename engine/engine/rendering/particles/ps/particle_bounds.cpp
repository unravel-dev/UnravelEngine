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

/**
 * @brief Widens @p bounds, which hold the particles' own paths, to where the emitter's forces carry them over a life of
 * @p longest_life seconds at most: the box of the cylinder about the force axis that holds them, and the target they
 * converge on, since a particle on its way lies between where it would be and the target.
 *
 * An orbit turns a particle all the way round the axis. The radial force moves it along the direction its path
 * segment started in: outward by its reach, and toward the axis no further than the segment started from it, which
 * is within the cylinder of the paths on whichever side of the axis the path has gone to since.
 */
auto widen_by_forces(const math::bbox& bounds, float longest_life, const motion_bounds_params& params) -> math::bbox
{
    math::bbox widened = bounds;
    if(params.orbits || params.moves_radially)
    {
        float radius = 0.0f;
        float lowest = 0.0f;
        float highest = 0.0f;
        for(uint32_t corner = 0; corner < 8; ++corner)
        {
            const math::vec3 point((corner & 1u) != 0u ? bounds.max.x : bounds.min.x,
                                   (corner & 2u) != 0u ? bounds.max.y : bounds.min.y,
                                   (corner & 4u) != 0u ? bounds.max.z : bounds.min.z);
            const math::vec3 offset = point - params.force_origin;
            const float height = math::dot(offset, params.force_axis);
            radius = math::max(radius, math::length(offset - params.force_axis * height));
            lowest = corner == 0 ? height : math::min(lowest, height);
            highest = corner == 0 ? height : math::max(highest, height);
        }
        const float outward = params.radial_out_per_second * longest_life;
        const float inward = math::min(params.radial_in_per_second * longest_life, radius);
        radius += math::max(outward, inward);
        // A cylinder's box reaches its radius x the share of each direction that lies across its axis.
        const math::vec3 across = math::sqrt(math::max(math::vec3(1.0f) - params.force_axis * params.force_axis, math::vec3(0.0f)));
        const math::vec3 low_end = params.force_origin + params.force_axis * lowest;
        const math::vec3 high_end = params.force_origin + params.force_axis * highest;
        widened.min = math::min(low_end, high_end) - across * radius;
        widened.max = math::max(low_end, high_end) + across * radius;
    }
    if(params.converges)
    {
        widened.add_point(params.force_target);
    }
    return widened;
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
    float longest_life = 0.0f;
    for(const auto& bucket : history.get_buckets())
    {
        // A bounce the CPU did not see stores the velocity it leaves with less what the forces add there, so the
        // forces' speed joins what a path can start with.
        const float top_speed = bucket.max_speed * curve_speed + acceleration * bucket.max_lifespan + params.force_speed;
        const float reach = top_speed * bucket.max_lifespan * gain;
        bounds.add_point(bucket.region.min - math::vec3(reach));
        bounds.add_point(bucket.region.max + math::vec3(reach));
        longest_reach = math::max(longest_reach, reach);
        longest_life = math::max(longest_life, bucket.max_lifespan);
    }
    // New particles reach as far from where they appear.
    bounds.add_point(spawn_region.min - math::vec3(longest_reach));
    bounds.add_point(spawn_region.max + math::vec3(longest_reach));
    return widen_by_forces(bounds, longest_life, params);
}

} // namespace

void spawn_history::clear()
{
    buckets_.clear();
}

void spawn_history::add(double time, const math::vec3& position, float speed, float lifespan, float size, float whole_life)
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
    current.min_whole_life = current.min_whole_life > 0.0f ? math::min(current.min_whole_life, whole_life) : whole_life;
}

auto spawn_history::get_shortest_life() const -> float
{
    float shortest = 0.0f;
    for(const auto& bucket : buckets_)
    {
        if(bucket.min_whole_life > 0.0f)
        {
            shortest = shortest > 0.0f ? math::min(shortest, bucket.min_whole_life) : bucket.min_whole_life;
        }
    }
    return shortest;
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
        // A bounce turns what the acceleration gave a particle any way, and stores the velocity it leaves with less
        // what the forces add there.
        const math::vec3 reach(math::length(drift) + params.force_speed * longest_life);
        bounds.min -= reach;
        bounds.max += reach;
        return widen_by_forces(bounds, longest_life, params);
    }
    // The acceleration moves every particle the same way: stretch the box toward where it carries them.
    bounds.min += math::min(drift, math::vec3(0.0f));
    bounds.max += math::max(drift, math::vec3(0.0f));
    return widen_by_forces(bounds, longest_life, params);
}

} // namespace ps_soa
} // namespace unravel
