#ifndef __GI_ADAPTIVE_PROBES_SH__
#define __GI_ADAPTIVE_PROBES_SH__

/*
 * Adaptive screen probes. Each uniform tile tests the tier's grid of candidate pixels
 * (u_gi_adaptive_samples_x x _y); a candidate the uniform probes cannot interpolate (weights summing below
 * GI_INTERP_MIN_WEIGHT) and no lower-numbered candidate around it covers becomes a probe. Adaptive probe i lives in
 * the probe atlas at (i % probes_x, probes_y + i / probes_x) and is listed in its uniform tile; every per-probe pass
 * treats it as a probe at that atlas tile.
 *
 * The adaptive state buffer (uint): [GI_ADAPTIVE_COUNTER] the probes spawned this frame (may pass the capacity:
 * those past it are dropped), then one record of GI_ADAPTIVE_TILE_STRIDE per uniform tile (tile = y x probes_x +
 * x): the tile's adaptive probe count, the indices of its adaptive probes (GI_ADAPTIVE_MAX_SAMPLES at most) and the
 * mask of its candidates the uniform probes cannot interpolate (one bit each). Sized for the view's uniform tiles
 * (gi_adaptive_probes.cpp mirrors the layout).
 *
 * The includer includes gi_common.sh.
 */

/// x = the adaptive probe capacity (trunc(uniform probes x GI_ADAPTIVE_ALLOCATION_FRACTION)), y = the texels per
/// axis of a probe's bordered radiance (cs_gi_adaptive_args.sc), z / w = the candidates per uniform tile along x / y,
/// the quality tier's (8 = GI_ADAPTIVE_SAMPLES_X x _Y at Epic, 16 = 4 x 4 at High).
uniform vec4 u_gi_adaptive;

#define u_gi_adaptive_capacity uint(u_gi_adaptive.x)
#define u_gi_adaptive_border_res uint(u_gi_adaptive.y)
#define u_gi_adaptive_samples_x int(u_gi_adaptive.z)
#define u_gi_adaptive_samples_y int(u_gi_adaptive.w)
#define u_gi_adaptive_samples (u_gi_adaptive_samples_x * u_gi_adaptive_samples_y)

/// The most candidates per uniform tile (4 x 4): a tile's probe list and placement mask hold this many.
#define GI_ADAPTIVE_MAX_SAMPLES 16
/// The most uniform tiles an 8 x 8 group of candidates covers (8 candidates per tile at the fewest).
#define GI_ADAPTIVE_MAX_GROUP_TILES 8
#define GI_ADAPTIVE_COUNTER 0
#define GI_ADAPTIVE_TILE_STRIDE (GI_ADAPTIVE_MAX_SAMPLES + 2)

int GiAdaptiveTileIndex(ivec2 tile)
{
	return tile.y * u_gi_probe_count.x + tile.x;
}

/// The state entries of uniform tile @p tile_index: its adaptive probe count, the index of its @p k-th adaptive
/// probe, its placement mask.
int GiAdaptiveCountEntry(int tile_index)
{
	return 1 + tile_index * GI_ADAPTIVE_TILE_STRIDE;
}

int GiAdaptiveProbeEntry(int tile_index, int k)
{
	return GiAdaptiveCountEntry(tile_index) + 1 + k;
}

int GiAdaptiveMaskEntry(int tile_index)
{
	return GiAdaptiveCountEntry(tile_index) + 1 + GI_ADAPTIVE_MAX_SAMPLES;
}

/// The atlas tile of adaptive probe @p index.
ivec2 GiAdaptiveAtlasTile(int index)
{
	int row = index / u_gi_probe_count.x;
	return ivec2(index - row * u_gi_probe_count.x, u_gi_probe_count.y + row);
}

/// Candidate @p sample_index of uniform tile @p tile: the uniform probe's pixel plus a
/// Hammersley point over the tile, scrambled per tile and frame.
ivec2 GiAdaptiveSamplePixel(ivec2 tile, int sample_index)
{
	ivec2 uniform_pixel = tile * int(u_gi_downsample) + ivec2(u_gi_placement_jitter);
	uvec2 seed = Rand3DPCG16(ivec3(tile, int(u_gi_frame_mod))).xy;
	vec2 offset = clamp(Hammersley16(uint(sample_index), uint(u_gi_adaptive_samples), seed) * u_gi_downsample,
	                    vec2_splat(0.0),
	                    vec2_splat(u_gi_downsample - 1.0));
	return uniform_pixel + ivec2(offset);
}

/// The interpolation weights (x = primary, y = fallback) of the adaptive probe at @p probe_pixel (device depth
/// @p probe_depth01) for @p pixel on the plane (@p position, @p normal) at view depth @p depth: the plane weights times
/// a falloff over one tile of the nearer axis distance.
vec2 GiAdaptiveProbeWeights(ivec2 pixel, vec3 position, vec3 normal, float depth, ivec2 probe_pixel, float probe_depth01)
{
	ivec2 uv_pixel = min(probe_pixel, ivec2(u_gi_view_size) - ivec2(1, 1));
	vec3 probe_position = GiWorldFromDepth(GiPixelUv(uv_pixel), probe_depth01);
	float plane_distance = abs(dot(probe_position - position, normal)) / depth;
	float r2 = plane_distance * plane_distance;
	vec2 distance = abs(vec2(probe_pixel - pixel));
	float falloff = 1.0 - saturate(min(distance.x, distance.y) / u_gi_downsample);
	return falloff * vec2(exp2(-GI_INTERP_DEPTH_WEIGHT * r2), exp2(-GI_INTERP_FALLBACK_DEPTH_WEIGHT * r2));
}

#endif // __GI_ADAPTIVE_PROBES_SH__
