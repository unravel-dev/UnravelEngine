#ifndef __LUMEN_SHORT_RANGE_AO_TEMPORAL_SH__
#define __LUMEN_SHORT_RANGE_AO_TEMPORAL_SH__

/*
 * Lumen short-range AO accumulation, inside the gather's temporal as UE 5.8 runs it (ScreenProbeTemporalReprojectionCS,
 * LumenScreenProbeGatherTemporal.usf:288-583, its short-range AO path): per full-resolution pixel, its own AO sample at
 * Epic's full resolution, or at half resolution one sample drawn by UE's stochastic bilinear reconstruction
 * (StochasticLightingTileClassification.usf:279-380 ComputeUpsampleWeights, StochasticLightingUpsample.ush
 * GetStochasticBilinearOffset) - of the four texels around the pixel, each weighted by the triangle filter over the
 * offset to the pixel it was searched from, that pixel's plane distance relative to this pixel's depth and the angle
 * between their normals, one is drawn in proportion to its weight, so the AO never crosses a depth or normal edge and
 * averages to a bilinear upsample over frames - blended with last frame's AO over the gather's 2x2 reprojection taps
 * with the gather's weight 1 / (1 + N) (N after the gather's fast update), after clamping the history into the drawn
 * sample's 3 x 3 neighbourhood mean +- LUMEN_SHORT_RANGE_AO_NEIGHBORHOOD_CLAMP_SCALE deviations (:234-284, :513-519).
 * A pixel no sample reconstructs leans on its history (weight 1 / (1 + LUMEN_TEMPORAL_INVALID_CURRENT_WEIGHT N),
 * :524-528).
 *
 * The AO is carried as (unit bent normal, visibility). The includer declares s_lumen_depth, s_lumen_normal,
 * s_lumen_short_range_ao (this frame's search, cs_lumen_short_range_ao.sc) and s_lumen_short_range_ao_history (last
 * frame's accumulation), and includes lumen_history.sh.
 */

#include "lumen/lumen_short_range_ao.sh"

#define u_lumen_short_range_ao_has_history (u_lumen_short_range_ao.x > 0.0)

/// The weight half-resolution texel @p texel reconstructs the pixel with (UE ComputeUpsampleWeights).
float LumenShortRangeAOSampleWeight(ivec2 texel, ivec2 pixel, vec3 position, vec3 normal, float scene_depth)
{
	ivec2 size = textureSize(s_lumen_short_range_ao, 0);
	if(any(lessThan(texel, ivec2(0, 0))) || any(greaterThanEqual(texel, size)))
	{
		return 0.0;
	}
	ivec2 source = LumenShortRangeAOPixel(texel);
	vec2 offset = abs(vec2(source - pixel));
	float filter_weight = max(2.0 - offset.x, 0.0) * max(2.0 - offset.y, 0.0);
	float source_depth01 = texelFetch(s_lumen_depth, source, 0).x;
	if(filter_weight <= 0.0 || source_depth01 >= 1.0)
	{
		return 0.0;
	}
	vec3 source_position = LumenWorldFromDepth(LumenPixelUv(source), source_depth01);
	float relative_distance = abs(dot(source_position - position, normal)) / scene_depth;
	float depth_weight = exp2(-LUMEN_SHORT_RANGE_AO_UPSAMPLE_DEPTH_WEIGHT * relative_distance * relative_distance);
	vec3 source_normal = decodeNormalOctahedron(texelFetch(s_lumen_normal, source, 0).xy);
	float normal_weight = 1.0 - saturate(acos(saturate(dot(source_normal, normal))));
	return filter_weight * depth_weight * normal_weight * normal_weight;
}

/// The half-resolution texel the pixel reconstructs from this frame (xy) and whether any reaches
/// LUMEN_SHORT_RANGE_AO_RECONSTRUCT_MIN_WEIGHT (z > 0), drawn with @p random in [0, 1).
ivec3 LumenShortRangeAODrawSample(ivec2 pixel, vec3 position, vec3 normal, float scene_depth, float random)
{
	ivec2 base = ivec2(floor((vec2(pixel) + vec2_splat(0.5)) * 0.5 - vec2_splat(0.5)));
	float w00 = LumenShortRangeAOSampleWeight(base, pixel, position, normal, scene_depth);
	float w10 = LumenShortRangeAOSampleWeight(base + ivec2(1, 0), pixel, position, normal, scene_depth);
	float w01 = LumenShortRangeAOSampleWeight(base + ivec2(0, 1), pixel, position, normal, scene_depth);
	float w11 = LumenShortRangeAOSampleWeight(base + ivec2(1, 1), pixel, position, normal, scene_depth);
	float total = w00 + w10 + w01 + w11;
	float pick = random * total;
	ivec2 offset = ivec2(1, 1);
	if(pick <= w00 && w00 > 0.0)
	{
		offset = ivec2(0, 0);
	}
	else if(pick <= w00 + w10 && w10 > 0.0)
	{
		offset = ivec2(1, 0);
	}
	else if(pick <= w00 + w10 + w01 && w01 > 0.0)
	{
		offset = ivec2(0, 1);
	}
	ivec2 size = textureSize(s_lumen_short_range_ao, 0);
	ivec2 texel = clamp(base + offset, ivec2(0, 0), size - ivec2(1, 1));
	return ivec3(texel, total >= LUMEN_SHORT_RANGE_AO_RECONSTRUCT_MIN_WEIGHT ? 1 : 0);
}

/// The history clamped into the current AO's 3 x 3 neighbourhood mean +- the clamp scale x its deviation.
vec4 LumenClampShortRangeAOHistory(vec4 history, ivec2 center, vec4 current)
{
	ivec2 size = textureSize(s_lumen_short_range_ao, 0);
	vec4 sum = current;
	vec4 square_sum = current * current;
	float count = 1.0;
	for(int y = -1; y <= 1; ++y)
	{
		for(int x = -1; x <= 1; ++x)
		{
			ivec2 texel = center + ivec2(x, y);
			bool is_center = x == 0 && y == 0;
			if(is_center || any(lessThan(texel, ivec2(0, 0))) || any(greaterThanEqual(texel, size)))
			{
				continue;
			}
			vec4 neighbour = texelFetch(s_lumen_short_range_ao, texel, 0);
			sum += neighbour;
			square_sum += neighbour * neighbour;
			count += 1.0;
		}
	}
	vec4 mean = sum / count;
	vec4 deviation = sqrt(max(square_sum / count - mean * mean, vec4_splat(0.0)));
	vec4 extent = LUMEN_SHORT_RANGE_AO_NEIGHBORHOOD_CLAMP_SCALE * deviation;
	return clamp(history, mean - extent, mean + extent);
}

/// The pixel's accumulated AO (bent normal, visibility) at the gather's reprojection @p taps and history length
/// @p frames.
vec4 LumenAccumulateShortRangeAO(ivec2 pixel, vec3 position, vec3 normal, float depth, LumenHistoryTaps taps, float frames)
{
#if LUMEN_SHORT_RANGE_AO_DOWNSAMPLE_FACTOR == 1
	ivec2 center = pixel;
	bool is_reconstructed = true;
#else
	float random = LumenShortRangeAONoise(pixel, 1.0).x;
	ivec3 drawn = LumenShortRangeAODrawSample(pixel, position, normal, depth, random);
	ivec2 center = drawn.xy;
	bool is_reconstructed = drawn.z > 0;
#endif
	vec4 current = texelFetch(s_lumen_short_range_ao, center, 0);
	float weight_sum = dot(taps.weights, vec4_splat(1.0));
	BRANCH
	if(!u_lumen_short_range_ao_has_history || weight_sum <= 0.0)
	{
		return current;
	}
	vec4 history = vec4_splat(0.0);
	for(int tap = 0; tap < 4; ++tap)
	{
		history += LumenHistoryTapWeight(taps, tap) *
		           texelFetch(s_lumen_short_range_ao_history, LumenHistoryTapTexel(taps, tap), 0);
	}
	history = LumenClampShortRangeAOHistory(history / weight_sum, center, current);
	float current_weight = is_reconstructed ? 1.0 / (1.0 + frames)
	                                        : 1.0 / (1.0 + LUMEN_TEMPORAL_INVALID_CURRENT_WEIGHT * frames);
	return mix(history, current, current_weight);
}

/// The composite's screen AO texel of an accumulated AO (rgb = bent normal x 0.5 + 0.5, a = visibility;
/// fs_pbr_lighting.sh s_screen_ao).
vec4 LumenShortRangeAOScreen(vec4 accumulated, vec3 normal)
{
	float bent_length = length(accumulated.xyz);
	vec3 bent = bent_length > 1e-6 ? accumulated.xyz / bent_length : normal;
	return vec4(bent * 0.5 + 0.5, saturate(accumulated.w));
}

#endif // __LUMEN_SHORT_RANGE_AO_TEMPORAL_SH__
