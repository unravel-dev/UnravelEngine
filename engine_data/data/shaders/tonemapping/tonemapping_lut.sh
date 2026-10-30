#ifndef TONEMAPPING_LUT_SH_HEADER_GUARD
#define TONEMAPPING_LUT_SH_HEADER_GUARD

/*
 * The table the tone curve, its colour grading and the display encoding are baked into (fs_tonemapping_lut.sc
 * writes it, fs_tonemapping.sc reads it): a TONEMAPPING_LUT_SIZE^3 cube stored as TONEMAPPING_LUT_SIZE slices side
 * by side, red along x inside a slice, green along y, blue across the slices.
 *
 * Each axis maps linear post-exposure colour through a shaper: entry 0 is exactly 0, so black stays black, and
 * entries 1 to SIZE - 1 are log2 spaced from 2^LOG2_MIN to 2^LOG2_MAX (below 2^LOG2_MIN the shaper is linear down to
 * 0). Log spacing keeps an entry every 0.4 stops, where the curves change, so the interpolation stays within about
 * half an 8-bit step of the curve evaluated per pixel.
 */

/// Entries per axis (tonemapping_pass::lut_size mirrors it). With the range below, entry 31 is exactly 1.0: the curves
/// that clip at white ("none") have their kink there, and between two entries a kink costs several 8-bit steps.
#define TONEMAPPING_LUT_SIZE 52
/// The colour range the log2 entries cover: below 2^-12 a straight segment to 0 follows the curves within half an 8-bit
/// step, above 2^8 they are white within half a step (Reinhard's 256 / 257).
#define TONEMAPPING_LUT_LOG2_MIN -12.0
#define TONEMAPPING_LUT_LOG2_MAX 8.0

/// The linear colour at fractional table @p index (0 to SIZE - 1) per channel: the inverse of tonemapping_lut_index.
vec3 tonemapping_lut_color(vec3 index)
{
	float log_steps = float(TONEMAPPING_LUT_SIZE - 2);
	vec3 log_color = exp2(mix(vec3_splat(TONEMAPPING_LUT_LOG2_MIN), vec3_splat(TONEMAPPING_LUT_LOG2_MAX),
	                          saturate((index - 1.0) / log_steps)));
	vec3 linear_color = index * exp2(TONEMAPPING_LUT_LOG2_MIN);
	return mix(linear_color, log_color, step(vec3_splat(1.0), index));
}

/// The fractional table index (0 to SIZE - 1) of linear post-exposure @p color per channel.
vec3 tonemapping_lut_index(vec3 color)
{
	float log_steps = float(TONEMAPPING_LUT_SIZE - 2);
	float smallest = exp2(TONEMAPPING_LUT_LOG2_MIN);
	vec3 log_position = (log2(max(color, vec3_splat(smallest))) - TONEMAPPING_LUT_LOG2_MIN) /
	                    (TONEMAPPING_LUT_LOG2_MAX - TONEMAPPING_LUT_LOG2_MIN);
	vec3 log_index = 1.0 + saturate(log_position) * log_steps;
	vec3 linear_index = saturate(color / smallest);
	return mix(linear_index, log_index, step(vec3_splat(smallest), color));
}

#endif // TONEMAPPING_LUT_SH_HEADER_GUARD
