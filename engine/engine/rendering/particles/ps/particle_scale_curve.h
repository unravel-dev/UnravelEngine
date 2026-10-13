#pragma once

#include <math/gradient.h>
#include <math/math.h>

namespace unravel
{
namespace ps_soa
{

/**
 * @brief Where one particle sits on its emitter's size curve.
 *
 * The size curve is a range (min..max) keyed over the particle's life. A particle picks its place
 * in that range once for its birth and once for its death and drifts from the first pick to the
 * second as it ages, so a curve with two keys keeps the independent start and end sizes it has
 * always had. A burst's size override scales the curve through `factor`, or replaces a curve that
 * is zero throughout through `offset`.
 */
struct particle_scale_pick
{
    /// Place in the range at birth: 0 = min, 1 = max.
    float start = 0.0f;
    /// Place in the range at death.
    float end = 0.0f;
    float factor = 1.0f;
    float offset = 0.0f;
};

/**
 * @brief Widths (max - min) of the size range at birth and at death. They weigh how a particle
 * drifts between its two picks (see get_scale_pick_blend).
 */
struct scale_curve_widths
{
    float start = 0.0f;
    float end = 0.0f;
};

auto get_scale_curve_widths(const math::gradient<frange_t>& curve) -> scale_curve_widths;

/**
 * @brief How far a particle's place in the size range has moved from its start pick toward its end
 * pick at @p life (0..1).
 *
 * Weighted by the range widths so that a curve with linear keys at 0 and 1 gives exactly
 * mix(size at birth, size at death, life), whatever the two widths are.
 */
auto get_scale_pick_blend(float life, const scale_curve_widths& widths) -> float;

/**
 * @brief A particle's size at @p life (0..1), before the emitter's own scale.
 */
auto sample_particle_scale(const math::gradient<frange_t>& curve,
                           const scale_curve_widths& widths,
                           const particle_scale_pick& pick,
                           float life) -> float;

/**
 * @brief The largest size the curve reaches over a life, for culling bounds. Checks every key and
 * a fixed set of points between them, so a smooth key's overshoot counts too.
 */
auto get_scale_curve_peak(const math::gradient<frange_t>& curve) -> float;

/**
 * @brief Sets @p pick's factor and offset so the particle is @p size at birth, the rest of its
 * curve scaled with it. A particle whose curve starts at 0 is scaled to peak at @p size instead,
 * and one whose curve is 0 throughout holds @p size.
 */
void fit_scale_pick_to_size(const math::gradient<frange_t>& curve,
                            const scale_curve_widths& widths,
                            float size,
                            particle_scale_pick& pick);

} // namespace ps_soa
} // namespace unravel
