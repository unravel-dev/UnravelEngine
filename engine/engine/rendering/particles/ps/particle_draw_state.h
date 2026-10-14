#pragma once

#include "particle_gpu_schema.h"
#include "particle_types.h"

#include <cstdint>

namespace unravel
{
namespace ps_soa
{

/// The order an emitter's particles sort in for @p mode; Auto sorts back to front only where the blend depends on
/// order (normal, premultiplied).
auto to_sort_kind(sort_mode mode, blend_mode blend) -> gpu_sort_kind;
/// The bgfx blend state of @p mode.
auto blend_state(blend_mode mode) -> uint64_t;
/// Blend into a target that holds premultiplied light in rgb and the transmittance in alpha
/// (render_view_params::is_transmittance_target): alpha only ever multiplies by 1 - coverage.
auto transmittance_blend_state(blend_mode mode) -> uint64_t;
/// The blend factor (BGFX_STATE_BLEND_FACTOR) @p weight, 0..1, in all four channels.
auto get_blend_factor_rgba(float weight) -> uint32_t;

} // namespace ps_soa
} // namespace unravel
