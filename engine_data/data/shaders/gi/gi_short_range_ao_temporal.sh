#ifndef __GI_SHORT_RANGE_AO_TEMPORAL_SH__
#define __GI_SHORT_RANGE_AO_TEMPORAL_SH__

/*
 * Short-range AO accumulation, inside the gather's temporal: per full-resolution pixel, its own AO sample at full
 * resolution (the Epic tier), or at half resolution one sample drawn by a stochastic bilinear reconstruction - of the
 * four texels around the pixel, each weighted by the triangle filter over the offset to the pixel it was searched
 * from, that pixel's plane distance relative to this pixel's depth and the angle between their normals, one is drawn
 * in proportion to its weight, so the AO never crosses a depth or normal edge and averages to a bilinear upsample over
 * frames - blended with last frame's AO over the gather's 2x2 reprojection taps with the gather's weight 1 / (1 + N)
 * (N after the gather's fast update), after clamping the history into the drawn sample's 3 x 3 neighbourhood mean +-
 * GI_SHORT_RANGE_AO_NEIGHBORHOOD_CLAMP_SCALE deviations. A pixel no sample reconstructs leans on its history
 * (weight 1 / (1 + GI_TEMPORAL_INVALID_CURRENT_WEIGHT N)).
 *
 * The AO is carried as (unit bent normal, visibility) and stored packed (GiPackShortRangeAO). The includer declares
 * s_gi_depth, s_gi_normal, s_gi_short_range_ao (this frame's search, cs_gi_short_range_ao.sc) and
 * s_gi_short_range_ao_history (last frame's accumulation) as uint samplers, and includes gi_history.sh.
 */

#include "gi/gi_short_range_ao.sh"

#define u_gi_short_range_ao_has_history (u_gi_short_range_ao.x > 0.0)

/// The group's tile of this frame's AO search, loaded once per 8 x 8 group (GiLoadShortRangeAOTile) instead of
/// fetched per pixel. At full resolution: the group's pixels with a one-texel border, every pixel's 3 x 3 clamp
/// neighbourhood. At half resolution: the 8 x 8 texels from GI_SHORT_RANGE_AO_HALF_TILE_BORDER before the group's
/// first texel, which hold every texel its pixels draw from with their clamp neighbourhoods, and the position and
/// normal of the pixel each texel was searched from, so the reconstruction weights read them from group-shared memory.
#define GI_SHORT_RANGE_AO_GROUP_EDGE 8
#define GI_SHORT_RANGE_AO_TILE_EDGE (GI_SHORT_RANGE_AO_GROUP_EDGE + 2)
#define GI_SHORT_RANGE_AO_TILE_TEXELS (GI_SHORT_RANGE_AO_TILE_EDGE * GI_SHORT_RANGE_AO_TILE_EDGE)
/// One half-resolution texel per thread of the group.
#define GI_SHORT_RANGE_AO_HALF_TILE_EDGE GI_SHORT_RANGE_AO_GROUP_EDGE
#define GI_SHORT_RANGE_AO_HALF_TILE_BORDER 2
#define GI_SHORT_RANGE_AO_HALF_TILE_TEXELS (GI_SHORT_RANGE_AO_HALF_TILE_EDGE * GI_SHORT_RANGE_AO_HALF_TILE_EDGE)
SHARED vec4 s_short_range_ao_tile[GI_SHORT_RANGE_AO_TILE_TEXELS];
/// Half resolution: xyz = the world position of the pixel the texel was searched from, w = 0 when the texel lies
/// outside the search or that pixel is not a surface, else 1 + the pixel's offset in its 2 x 2 block (x + 2 y).
SHARED vec4 s_short_range_ao_source_position[GI_SHORT_RANGE_AO_HALF_TILE_TEXELS];
SHARED vec3 s_short_range_ao_source_normal[GI_SHORT_RANGE_AO_HALF_TILE_TEXELS];

/// The half-resolution tile's index of texel @p texel for the group whose first pixel is @p group_origin.
int GiShortRangeAOHalfTileIndex(ivec2 texel, ivec2 group_origin)
{
	ivec2 local = texel - group_origin / 2 +
	              ivec2(GI_SHORT_RANGE_AO_HALF_TILE_BORDER, GI_SHORT_RANGE_AO_HALF_TILE_BORDER);
	return local.y * GI_SHORT_RANGE_AO_HALF_TILE_EDGE + local.x;
}

/// Loads the tile of the group whose first pixel is @p group_origin; thread @p local_index of the 8 x 8 group. Every
/// thread calls it before any returns, and a barrier follows. Texels past the view's edge read the clamped edge; the
/// clamp skips them by position and the reconstruction by their validity.
void GiLoadShortRangeAOTile(ivec2 group_origin, int local_index)
{
	ivec2 size = textureSize(s_gi_short_range_ao, 0);
	BRANCH
	if(u_gi_short_range_ao_is_full_res)
	{
		int group_threads = GI_SHORT_RANGE_AO_GROUP_EDGE * GI_SHORT_RANGE_AO_GROUP_EDGE;
		for(int i = local_index; i < GI_SHORT_RANGE_AO_TILE_TEXELS; i += group_threads)
		{
			ivec2 offset = ivec2(i % GI_SHORT_RANGE_AO_TILE_EDGE, i / GI_SHORT_RANGE_AO_TILE_EDGE) - ivec2(1, 1);
			ivec2 texel = clamp(group_origin + offset, ivec2(0, 0), size - ivec2(1, 1));
			s_short_range_ao_tile[i] = GiUnpackShortRangeAO(texelFetch(s_gi_short_range_ao, texel, 0).x);
		}
		return;
	}
	ivec2 local = ivec2(local_index % GI_SHORT_RANGE_AO_HALF_TILE_EDGE,
	                    local_index / GI_SHORT_RANGE_AO_HALF_TILE_EDGE);
	ivec2 texel = group_origin / 2 + local -
	              ivec2(GI_SHORT_RANGE_AO_HALF_TILE_BORDER, GI_SHORT_RANGE_AO_HALF_TILE_BORDER);
	bool is_inside = all(greaterThanEqual(texel, ivec2(0, 0))) && all(lessThan(texel, size));
	ivec2 clamped = clamp(texel, ivec2(0, 0), size - ivec2(1, 1));
	s_short_range_ao_tile[local_index] = GiUnpackShortRangeAO(texelFetch(s_gi_short_range_ao, clamped, 0).x);
	ivec2 source = GiShortRangeAOPixel(clamped);
	float source_depth01 = texelFetch(s_gi_depth, source, 0).x;
	bool is_valid = is_inside && source_depth01 < 1.0;
	vec3 source_position = is_valid ? GiWorldFromDepth(GiPixelUv(source), source_depth01) : vec3_splat(0.0);
	ivec2 block_offset = source - clamped * 2;
	float source_code = 1.0 + float(block_offset.x + 2 * block_offset.y);
	s_short_range_ao_source_position[local_index] = vec4(source_position, is_valid ? source_code : 0.0);
	s_short_range_ao_source_normal[local_index] = decodeNormalOctahedron(texelFetch(s_gi_normal, source, 0).xy);
}

/// The weight half-resolution texel @p texel reconstructs the pixel with, from the tile of the group whose first pixel
/// is @p group_origin; @p inv_scene_depth = 1 / the pixel's linear depth.
float GiShortRangeAOSampleWeight(ivec2 texel, ivec2 pixel, vec3 position, vec3 normal, float inv_scene_depth,
                                 ivec2 group_origin)
{
	int index = GiShortRangeAOHalfTileIndex(texel, group_origin);
	vec4 source_position = s_short_range_ao_source_position[index];
	if(source_position.w <= 0.0)
	{
		return 0.0;
	}
	int block_code = int(source_position.w) - 1;
	ivec2 source = texel * 2 + ivec2(block_code & 1, block_code >> 1);
	vec2 offset = abs(vec2(source - pixel));
	float filter_weight = max(2.0 - offset.x, 0.0) * max(2.0 - offset.y, 0.0);
	float relative_distance = abs(dot(source_position.xyz - position, normal)) * inv_scene_depth;
	float depth_weight = exp2(-GI_SHORT_RANGE_AO_UPSAMPLE_DEPTH_WEIGHT * relative_distance * relative_distance);
	vec3 source_normal = s_short_range_ao_source_normal[index];
	float normal_weight = 1.0 - saturate(GiAcosFast(saturate(dot(source_normal, normal))));
	return filter_weight * depth_weight * normal_weight * normal_weight;
}

/// The half-resolution texel the pixel reconstructs from this frame (xy) and whether any reaches
/// GI_SHORT_RANGE_AO_RECONSTRUCT_MIN_WEIGHT (z > 0), drawn with @p random in [0, 1); the pixel belongs to the group
/// whose first pixel is @p group_origin.
ivec3 GiShortRangeAODrawSample(ivec2 pixel, vec3 position, vec3 normal, float scene_depth, float random,
                               ivec2 group_origin)
{
	ivec2 base = ivec2(floor((vec2(pixel) + vec2_splat(0.5)) * 0.5 - vec2_splat(0.5)));
	float inv_depth = 1.0 / scene_depth;
	float w00 = GiShortRangeAOSampleWeight(base, pixel, position, normal, inv_depth, group_origin);
	float w10 = GiShortRangeAOSampleWeight(base + ivec2(1, 0), pixel, position, normal, inv_depth, group_origin);
	float w01 = GiShortRangeAOSampleWeight(base + ivec2(0, 1), pixel, position, normal, inv_depth, group_origin);
	float w11 = GiShortRangeAOSampleWeight(base + ivec2(1, 1), pixel, position, normal, inv_depth, group_origin);
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
	ivec2 size = textureSize(s_gi_short_range_ao, 0);
	ivec2 texel = clamp(base + offset, ivec2(0, 0), size - ivec2(1, 1));
	return ivec3(texel, total >= GI_SHORT_RANGE_AO_RECONSTRUCT_MIN_WEIGHT ? 1 : 0);
}

/// This frame's AO search at @p texel, a neighbour of the group whose first pixel is @p group_origin.
vec4 GiShortRangeAONeighbour(ivec2 texel, ivec2 group_origin)
{
	BRANCH
	if(u_gi_short_range_ao_is_full_res)
	{
		ivec2 local = texel - group_origin + ivec2(1, 1);
		return s_short_range_ao_tile[local.y * GI_SHORT_RANGE_AO_TILE_EDGE + local.x];
	}
	return s_short_range_ao_tile[GiShortRangeAOHalfTileIndex(texel, group_origin)];
}

/// The history clamped into the current AO's 3 x 3 neighbourhood mean +- the clamp scale x its deviation; the
/// neighbourhood of @p center, a pixel of the group whose first pixel is @p group_origin.
vec4 GiClampShortRangeAOHistory(vec4 history, ivec2 center, vec4 current, ivec2 group_origin)
{
	ivec2 size = textureSize(s_gi_short_range_ao, 0);
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
			vec4 neighbour = GiShortRangeAONeighbour(texel, group_origin);
			sum += neighbour;
			square_sum += neighbour * neighbour;
			count += 1.0;
		}
	}
	vec4 mean = sum / count;
	vec4 deviation = sqrt(max(square_sum / count - mean * mean, vec4_splat(0.0)));
	vec4 extent = GI_SHORT_RANGE_AO_NEIGHBORHOOD_CLAMP_SCALE * deviation;
	return clamp(history, mean - extent, mean + extent);
}

/// The pixel's accumulated AO (bent normal, visibility) at the gather's reprojection @p taps and history length
/// @p frames; the pixel belongs to the group whose first pixel is @p group_origin.
vec4 GiAccumulateShortRangeAO(ivec2 pixel,
                              vec3 position,
                              vec3 normal,
                              float depth,
                              GiHistoryTaps taps,
                              float frames,
                              ivec2 group_origin)
{
	ivec2 center = pixel;
	bool is_reconstructed = true;
	BRANCH
	if(!u_gi_short_range_ao_is_full_res)
	{
		float random = GiShortRangeAONoise(pixel, 1.0).x;
		ivec3 drawn = GiShortRangeAODrawSample(pixel, position, normal, depth, random, group_origin);
		center = drawn.xy;
		is_reconstructed = drawn.z > 0;
	}
	vec4 current = GiShortRangeAONeighbour(center, group_origin);
	float weight_sum = dot(taps.weights, vec4_splat(1.0));
	BRANCH
	if(!u_gi_short_range_ao_has_history || weight_sum <= 0.0)
	{
		return current;
	}
	vec4 history = vec4_splat(0.0);
	for(int tap = 0; tap < 4; ++tap)
	{
		uint packed_history = texelFetch(s_gi_short_range_ao_history, GiHistoryTapTexel(taps, tap), 0).x;
		history += GiWeightedHistoryTap(GiHistoryTapWeight(taps, tap), GiUnpackShortRangeAO(packed_history));
	}
	history = GiClampShortRangeAOHistory(history / weight_sum, center, current, group_origin);
	float current_weight = is_reconstructed ? 1.0 / (1.0 + frames)
	                                        : 1.0 / (1.0 + GI_TEMPORAL_INVALID_CURRENT_WEIGHT * frames);
	return mix(history, current, current_weight);
}

/// The composite's screen AO texel of an accumulated AO (rgb = bent normal x 0.5 + 0.5, a = visibility;
/// fs_pbr_lighting.sh s_screen_ao).
vec4 GiShortRangeAOScreen(vec4 accumulated, vec3 normal)
{
	float bent_length = length(accumulated.xyz);
	vec3 bent = bent_length > 1e-6 ? accumulated.xyz / bent_length : normal;
	return vec4(bent * 0.5 + 0.5, saturate(accumulated.w));
}

#endif // __GI_SHORT_RANGE_AO_TEMPORAL_SH__
