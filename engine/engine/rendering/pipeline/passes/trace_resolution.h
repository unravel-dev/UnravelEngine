#pragma once

#include <base/basetypes.hpp>
#include <algorithm>
#include <cstdint>

namespace unravel
{

/// Integer downscale divisor applied to the full-resolution G-buffer when running
/// screen-space trace passes (SSR, SSIL). The numeric value equals the divisor, which
/// lets the shaders consume it directly as `full_res / trace_res`.
///
/// Note: SSR is capped at `half` at runtime regardless of the configured value, because
/// sub-half-resolution tracing causes severe artifacts in Hi-Z traversal, temporal
/// reprojection, and denoising (see ssr_pass::run_ssr_trace). SSIL tolerates `quarter`
/// because indirect lighting is inherently low-frequency.
enum class trace_resolution : uint8_t
{
    full    = 1,
    half    = 2,
    quarter = 4,
    eighth  = 8,
};

/// Returns the integer divisor backing the enum value (never zero).
inline auto get_divisor(trace_resolution res) -> uint32_t
{
    return static_cast<uint32_t>(res);
}

/// Computes the trace-target size from a full-resolution reference, clamped to 1x1 min.
inline auto compute_trace_size(const usize32_t& ref, trace_resolution res) -> usize32_t
{
    const uint32_t d = get_divisor(res);
    return {std::max(1u, ref.width / d), std::max(1u, ref.height / d)};
}


} // namespace unravel
