#include "particle_scale_curve.h"

namespace unravel
{
namespace ps_soa
{
namespace
{

/// Evenly spaced points of a life checked besides the keys, so a smooth key's overshoot counts.
constexpr int k_scale_scan_steps = 64;
/// Below this both range widths are empty and the drift between the two picks changes nothing.
constexpr float k_min_width_weight = 1.0e-6f;

/// Calls @p visit with every life (0..1) worth checking for the extremes of a curve: evenly spaced
/// points and the key positions of both channels.
template<typename Visitor>
void for_each_scan_position(const math::gradient<frange_t>& curve, Visitor&& visit)
{
    for(int step = 0; step <= k_scale_scan_steps; ++step)
    {
        visit(float(step) / float(k_scale_scan_steps));
    }
    for(const auto& channel : curve.get_channels())
    {
        for(const auto& key : channel.get_keys())
        {
            visit(math::clamp(key.position, 0.0f, 1.0f));
        }
    }
}

} // namespace

auto get_scale_curve_widths(const math::gradient<frange_t>& curve) -> scale_curve_widths
{
    const frange_t start = curve.sample(0.0f);
    const frange_t end = curve.sample(1.0f);
    return {math::abs(start.max - start.min), math::abs(end.max - end.min)};
}

auto get_scale_pick_blend(float life, const scale_curve_widths& widths) -> float
{
    const float start_weight = (1.0f - life) * widths.start;
    const float end_weight = life * widths.end;
    const float total_weight = start_weight + end_weight;
    return total_weight > k_min_width_weight ? end_weight / total_weight : life;
}

auto sample_particle_scale(const math::gradient<frange_t>& curve,
                           const scale_curve_widths& widths,
                           const particle_scale_pick& pick,
                           float life) -> float
{
    const frange_t range = curve.sample(life);
    const float place = math::mix(pick.start, pick.end, get_scale_pick_blend(life, widths));
    return math::mix(range.min, range.max, place) * pick.factor + pick.offset;
}

auto get_scale_curve_peak(const math::gradient<frange_t>& curve) -> float
{
    float peak = 0.0f;
    for_each_scan_position(curve,
                           [&](float life)
                           {
                               const frange_t range = curve.sample(life);
                               peak = math::max(peak, math::max(range.min, range.max));
                           });
    return peak;
}

void fit_scale_pick_to_size(const math::gradient<frange_t>& curve,
                            const scale_curve_widths& widths,
                            float size,
                            particle_scale_pick& pick)
{
    pick.factor = 1.0f;
    pick.offset = 0.0f;
    const float birth_size = sample_particle_scale(curve, widths, pick, 0.0f);
    if(birth_size > 0.0f)
    {
        pick.factor = size / birth_size;
        return;
    }
    float peak_size = 0.0f;
    for_each_scan_position(curve,
                           [&](float life)
                           {
                               peak_size = math::max(peak_size, sample_particle_scale(curve, widths, pick, life));
                           });
    if(peak_size > 0.0f)
    {
        pick.factor = size / peak_size;
        return;
    }
    pick.factor = 0.0f;
    pick.offset = size;
}

} // namespace ps_soa
} // namespace unravel
