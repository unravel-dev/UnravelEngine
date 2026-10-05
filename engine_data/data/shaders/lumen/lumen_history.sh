#ifndef __LUMEN_HISTORY_SH__
#define __LUMEN_HISTORY_SH__

/*
 * The per-pixel history of the Lumen gather's temporal (UE 5.8 LumenScreenProbeGatherTemporal.usf:288-583,
 * history validity from StochasticLightingTileClassification.usf:884-1085): last frame's result at a
 * point's reprojection, over the 2x2 bilinear taps whose stored depth agrees with the reprojected depth.
 * The diffuse and the rough specular histories share the taps, as UE's temporal reprojects both with one
 * set of weights. A surface the velocity buffer marks as moving reprojects from where it was last frame
 * (lumen_motion.sh), so its history follows it and its depth test holds.
 *
 * The includer declares s_lumen_history (rgb = E / pi, a = LumenEncodeHistoryAlpha) and s_lumen_prev_depth (last
 * frame's device depth), defines LUMEN_VELOCITY_STAGE for moving surfaces, and includes lumen_common.sh and
 * pre_exposure.sh.
 */

#include "lumen/lumen_motion.sh"

/// A point's 2x2 history taps: the top-left texel and each tap's weight (bilinear, 0 where the stored depth
/// disagrees); all weights 0 when the point reprojects outside last frame's view.
struct LumenHistoryTaps
{
	ivec2 origin;
	vec4 weights;
};

LumenHistoryTaps LumenHistoryReprojection(ivec2 pixel, vec3 position, vec3 normal)
{
	LumenHistoryTaps taps;
	taps.origin = ivec2(0, 0);
	taps.weights = vec4_splat(0.0);
	vec3 prev_position = LumenPrevWorldPosition(LumenPixelUv(pixel), position);
	vec4 prev_clip = mul(u_lumen_prev_view_proj, vec4(prev_position, 1.0));
	if(prev_clip.w <= 0.0)
	{
		return taps;
	}
	vec2 history_uv = clipToUv((prev_clip.xy / prev_clip.w) * 0.5 + 0.5);
	if(any(lessThan(history_uv, vec2_splat(0.0))) || any(greaterThan(history_uv, vec2_splat(1.0))))
	{
		return taps;
	}
	vec2 history_size = vec2(textureSize(s_lumen_history, 0));
	vec2 guard = LUMEN_TEMPORAL_HISTORY_GUARD / history_size;
	history_uv = clamp(history_uv, guard, vec2_splat(1.0) - guard);
	vec2 coord = history_uv * history_size - 0.5;
	vec2 origin = floor(coord);
	vec2 fraction = coord - origin;
	float reprojected_depth = prev_clip.w;
	vec3 camera = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	float n_dot_v = saturate(dot(normal, normalize(camera - position)));
	float noise = BlueNoise2D(vec2(pixel), mod(u_lumen_frame_index + 0.5 * float(LUMEN_INTEGRATE_NOISE_PERIOD),
	                                            float(LUMEN_INTEGRATE_NOISE_PERIOD))).x;
	float threshold = reprojected_depth * LUMEN_TEMPORAL_DEPTH_THRESHOLD *
	                  mix(LUMEN_TEMPORAL_DEPTH_NOISE_MIN, LUMEN_TEMPORAL_DEPTH_NOISE_MAX, noise) /
	                  mix(LUMEN_TEMPORAL_MIN_NOV, 1.0, n_dot_v);
	taps.origin = ivec2(origin);
	vec4 weights = vec4_splat(0.0);
	for(int tap = 0; tap < 4; ++tap)
	{
		ivec2 step_xy = ivec2(tap & 1, tap >> 1);
		vec2 axis_weight = mix(vec2_splat(1.0) - fraction, fraction, vec2(step_xy));
		float bilinear = axis_weight.x * axis_weight.y;
		ivec2 texel = clamp(taps.origin + step_xy, ivec2(0, 0), ivec2(history_size) - ivec2(1, 1));
		float tap_depth = LumenLinearDepth(texelFetch(s_lumen_prev_depth, texel, 0).x);
		bool is_valid = bilinear > LUMEN_TEMPORAL_MIN_TAP_WEIGHT && abs(tap_depth - reprojected_depth) < threshold;
		float weight = is_valid ? bilinear : 0.0;
		weights += weight * vec4(tap == 0 ? 1.0 : 0.0, tap == 1 ? 1.0 : 0.0, tap == 2 ? 1.0 : 0.0, tap == 3 ? 1.0 : 0.0);
	}
	taps.weights = weights;
	return taps;
}

/// The texel of tap @p tap (0-3, row-major over the 2x2).
ivec2 LumenHistoryTapTexel(LumenHistoryTaps taps, int tap)
{
	ivec2 size = textureSize(s_lumen_history, 0);
	return clamp(taps.origin + ivec2(tap & 1, tap >> 1), ivec2(0, 0), size - ivec2(1, 1));
}

/// The weight of tap @p tap (0-3).
float LumenHistoryTapWeight(LumenHistoryTaps taps, int tap)
{
	return dot(taps.weights, vec4(tap == 0 ? 1.0 : 0.0, tap == 1 ? 1.0 : 0.0, tap == 2 ? 1.0 : 0.0, tap == 3 ? 1.0 : 0.0));
}

/// Last frame's result over the taps, corrected into this frame's pre-exposure (rgb), and the frame count it
/// continues (a; 0 when no tap is valid).
vec4 LumenReadHistoryTaps(LumenHistoryTaps taps)
{
	float weight_sum = dot(taps.weights, vec4_splat(1.0));
	if(weight_sum <= 0.0)
	{
		return vec4_splat(0.0);
	}
	vec3 color = vec3_splat(0.0);
	float count = 0.0;
	for(int tap = 0; tap < 4; ++tap)
	{
		float weight = LumenHistoryTapWeight(taps, tap);
		vec4 history = texelFetch(s_lumen_history, LumenHistoryTapTexel(taps, tap), 0);
		color += weight * history.xyz;
		count += weight * (LumenHistoryAlphaFrames(history.w, u_lumen_temporal_max_frames) + 1.0);
	}
	float frames = min(count / max(weight_sum, 1e-5), u_lumen_temporal_max_frames);
	return vec4(color / max(weight_sum, 1e-5) * u_history_pre_exposure_correction, frames);
}

/// Last frame's fast update amount over the taps (UE FastUpdateModeHistoryValue); 0 when no tap is valid.
float LumenReadHistoryFastUpdate(LumenHistoryTaps taps)
{
	float weight_sum = dot(taps.weights, vec4_splat(1.0));
	if(weight_sum <= 0.0)
	{
		return 0.0;
	}
	float amount = 0.0;
	for(int tap = 0; tap < 4; ++tap)
	{
		float alpha = texelFetch(s_lumen_history, LumenHistoryTapTexel(taps, tap), 0).w;
		amount += LumenHistoryTapWeight(taps, tap) * LumenHistoryAlphaFastUpdate(alpha);
	}
	return amount / weight_sum;
}

/// LumenReadHistoryTaps at the pixel's own reprojection.
vec4 LumenReadHistory(ivec2 pixel, vec3 position, vec3 normal)
{
	return LumenReadHistoryTaps(LumenHistoryReprojection(pixel, position, normal));
}

/// The frame count as the 4-bit history stores it: multiples of the temporal's maximum frame count / 15.
float LumenQuantizeFrames(float frames)
{
	float levels = float(LUMEN_TEMPORAL_COUNT_LEVELS);
	return floor(saturate(frames / u_lumen_temporal_max_frames) * levels + 0.5) / levels *
	       u_lumen_temporal_max_frames;
}

#endif // __LUMEN_HISTORY_SH__
