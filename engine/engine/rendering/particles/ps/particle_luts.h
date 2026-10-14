#pragma once

#include "emitter_desc.h"
#include "particle_eval.h"

#include <cstdint>

namespace unravel
{
namespace ps_soa
{

/**
 * @brief Identifies the inputs of an emitter's curve set: two emitters with equal keys share one baked
 * set, and an emitter re-bakes only when its key changes. Cheap enough to compute every frame.
 */
auto compute_lut_key(const emitter_desc& desc, emitter_feature features) -> uint64_t;

/**
 * @brief Bakes the emitter's curve rows (particle_common.sh, lut_row): color, (eased life, its rate, size
 * min, size max), color by speed, (speed integral, speed factor, erosion, sheet frame), (size x, size y,
 * spin integral, intensity), the second color, and their ranges.
 */
void bake_lut_set(const emitter_desc& desc, emitter_feature features, lut_set& out_luts);

} // namespace ps_soa
} // namespace unravel
