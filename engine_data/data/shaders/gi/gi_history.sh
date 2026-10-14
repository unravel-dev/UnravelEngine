#ifndef __GI_HISTORY_SH__
#define __GI_HISTORY_SH__

/*
 * The per-pixel history of the GI gather's temporal filter: last frame's result at a point's reprojection, over
 * the 2x2 bilinear taps whose stored depth agrees with the reprojected depth.
 * The diffuse and the rough specular histories share the taps: the temporal filter reprojects both with one
 * set of weights. A surface the velocity buffer marks as moving reprojects from where it was last frame
 * (gi_motion.sh), so its history follows it and its depth test holds.
 *
 * The includer declares s_gi_history (rgb = E / pi, a = GiEncodeHistoryAlpha) and s_gi_prev_depth (last
 * frame's device depth), defines GI_VELOCITY_STAGE for moving surfaces, and includes gi_common.sh and
 * pre_exposure.sh.
 */

#include "gi/gi_motion.sh"

/// A point's 2x2 history taps: the top-left texel and each tap's weight (bilinear, 0 where the stored depth
/// disagrees); all weights 0 when the point reprojects outside last frame's view.
struct GiHistoryTaps
{
	ivec2 origin;
	vec4 weights;
};

GiHistoryTaps GiHistoryReprojection(ivec2 pixel, vec3 position, vec3 normal)
{
	GiHistoryTaps taps;
	taps.origin = ivec2(0, 0);
	taps.weights = vec4_splat(0.0);
	vec3 prev_position = GiPrevWorldPosition(GiPixelUv(pixel), position);
	vec4 prev_clip = mul(u_gi_prev_view_proj, vec4(prev_position, 1.0));
	if(prev_clip.w <= 0.0)
	{
		return taps;
	}
	vec2 history_uv = clipToUv((prev_clip.xy / prev_clip.w) * 0.5 + 0.5);
	if(any(lessThan(history_uv, vec2_splat(0.0))) || any(greaterThan(history_uv, vec2_splat(1.0))))
	{
		return taps;
	}
	vec2 history_size = vec2(textureSize(s_gi_history, 0));
	vec2 guard = GI_TEMPORAL_HISTORY_GUARD / history_size;
	history_uv = clamp(history_uv, guard, vec2_splat(1.0) - guard);
	vec2 coord = history_uv * history_size - 0.5;
	vec2 origin = floor(coord);
	vec2 fraction = coord - origin;
	float reprojected_depth = prev_clip.w;
	vec3 camera = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	float n_dot_v = saturate(dot(normal, normalize(camera - position)));
	float noise = BlueNoise2D(vec2(pixel), mod(u_gi_frame_index + 0.5 * float(GI_INTEGRATE_NOISE_PERIOD),
	                                            float(GI_INTEGRATE_NOISE_PERIOD))).x;
	float threshold = reprojected_depth * GI_TEMPORAL_DEPTH_THRESHOLD *
	                  mix(GI_TEMPORAL_DEPTH_NOISE_MIN, GI_TEMPORAL_DEPTH_NOISE_MAX, noise) /
	                  mix(GI_TEMPORAL_MIN_NOV, 1.0, n_dot_v);
	taps.origin = ivec2(origin);
	vec4 weights = vec4_splat(0.0);
	for(int tap = 0; tap < 4; ++tap)
	{
		ivec2 step_xy = ivec2(tap & 1, tap >> 1);
		vec2 axis_weight = mix(vec2_splat(1.0) - fraction, fraction, vec2(step_xy));
		float bilinear = axis_weight.x * axis_weight.y;
		ivec2 texel = clamp(taps.origin + step_xy, ivec2(0, 0), ivec2(history_size) - ivec2(1, 1));
		float tap_depth = GiLinearDepth(texelFetch(s_gi_prev_depth, texel, 0).x);
		bool is_valid = bilinear > GI_TEMPORAL_MIN_TAP_WEIGHT && abs(tap_depth - reprojected_depth) < threshold;
		float weight = is_valid ? bilinear : 0.0;
		weights += weight * vec4(tap == 0 ? 1.0 : 0.0, tap == 1 ? 1.0 : 0.0, tap == 2 ? 1.0 : 0.0, tap == 3 ? 1.0 : 0.0);
	}
	taps.weights = weights;
	return taps;
}

/// The texel of tap @p tap (0-3, row-major over the 2x2).
ivec2 GiHistoryTapTexel(GiHistoryTaps taps, int tap)
{
	ivec2 size = textureSize(s_gi_history, 0);
	return clamp(taps.origin + ivec2(tap & 1, tap >> 1), ivec2(0, 0), size - ivec2(1, 1));
}

/// The weight of tap @p tap (0-3).
float GiHistoryTapWeight(GiHistoryTaps taps, int tap)
{
	return dot(taps.weights, vec4(tap == 0 ? 1.0 : 0.0, tap == 1 ? 1.0 : 0.0, tap == 2 ? 1.0 : 0.0, tap == 3 ? 1.0 : 0.0));
}

/// @p value weighted by @p weight, selected rather than multiplied: a tap of weight 0 can be a texel the gather never
/// wrote (it skips sky pixels), whose content must not reach the sum even as 0 x NaN.
vec4 GiWeightedHistoryTap(float weight, vec4 value)
{
	return weight > 0.0 ? weight * value : vec4_splat(0.0);
}

/// Last frame's history over the taps, from one fetch per tap.
struct GiHistorySample
{
	/// rgb = the result corrected into this frame's pre-exposure, a = the frame count it continues (0 when no tap is
	/// valid).
	vec4 color_frames;
	/// The fast update amount it carries; 0 when no tap is valid.
	float fast_update;
};

GiHistorySample GiReadHistorySample(GiHistoryTaps taps)
{
	GiHistorySample result;
	result.color_frames = vec4_splat(0.0);
	result.fast_update = 0.0;
	float weight_sum = dot(taps.weights, vec4_splat(1.0));
	if(weight_sum <= 0.0)
	{
		return result;
	}
	vec3 color = vec3_splat(0.0);
	float count = 0.0;
	float fast_update = 0.0;
	for(int tap = 0; tap < 4; ++tap)
	{
		float weight = GiHistoryTapWeight(taps, tap);
		vec4 history = texelFetch(s_gi_history, GiHistoryTapTexel(taps, tap), 0);
		vec4 tap_frames = vec4(history.xyz, GiHistoryAlphaFrames(history.w, u_gi_temporal_max_frames) + 1.0);
		vec4 weighted = GiWeightedHistoryTap(weight, tap_frames);
		color += weighted.xyz;
		count += weighted.w;
		fast_update += GiWeightedHistoryTap(weight, vec4_splat(GiHistoryAlphaFastUpdate(history.w))).x;
	}
	float frames = min(count / max(weight_sum, 1e-5), u_gi_temporal_max_frames);
	result.color_frames = vec4(color / max(weight_sum, 1e-5) * u_history_pre_exposure_correction, frames);
	result.fast_update = fast_update / weight_sum;
	return result;
}

/// Last frame's result over the taps, corrected into this frame's pre-exposure (rgb), and the frame count it
/// continues (a; 0 when no tap is valid).
vec4 GiReadHistoryTaps(GiHistoryTaps taps)
{
	return GiReadHistorySample(taps).color_frames;
}

/// GiReadHistoryTaps at the pixel's own reprojection.
vec4 GiReadHistory(ivec2 pixel, vec3 position, vec3 normal)
{
	return GiReadHistoryTaps(GiHistoryReprojection(pixel, position, normal));
}

/// The frame count as the 4-bit history stores it: multiples of the temporal's maximum frame count / 15.
float GiQuantizeFrames(float frames)
{
	float levels = float(GI_TEMPORAL_COUNT_LEVELS);
	return floor(saturate(frames / u_gi_temporal_max_frames) * levels + 0.5) / levels *
	       u_gi_temporal_max_frames;
}

#endif // __GI_HISTORY_SH__
