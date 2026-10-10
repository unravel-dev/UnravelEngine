#ifndef __LUMEN_ADAPTIVE_PROBES_SH__
#define __LUMEN_ADAPTIVE_PROBES_SH__

/*
 * Adaptive screen probes. Each uniform tile tests the tier's grid of candidate pixels
 * (u_lumen_adaptive_samples_x x _y); a candidate the uniform probes cannot interpolate (weights summing below
 * LUMEN_INTERP_MIN_WEIGHT) and no lower-numbered candidate around it covers becomes a probe. Adaptive probe i lives in
 * the probe atlas at (i % probes_x, probes_y + i / probes_x) and is listed in its uniform tile; every per-probe pass
 * treats it as a probe at that atlas tile.
 *
 * The adaptive state buffer (uint): [LUMEN_ADAPTIVE_COUNTER] the probes spawned this frame (may pass the capacity:
 * those past it are dropped), then one record of LUMEN_ADAPTIVE_TILE_STRIDE per uniform tile (tile = y x probes_x +
 * x): the tile's adaptive probe count, the indices of its adaptive probes (LUMEN_ADAPTIVE_MAX_SAMPLES at most) and the
 * mask of its candidates the uniform probes cannot interpolate (one bit each). Sized for the view's uniform tiles
 * (lumen_adaptive_probes.cpp mirrors the layout).
 *
 * The includer includes lumen_common.sh.
 */

/// x = the adaptive probe capacity (trunc(uniform probes x LUMEN_ADAPTIVE_ALLOCATION_FRACTION)), y = the texels per
/// axis of a probe's bordered radiance (cs_lumen_adaptive_args.sc), z / w = the candidates per uniform tile along x / y,
/// the quality tier's (8 = LUMEN_ADAPTIVE_SAMPLES_X x _Y at Epic, 16 = 4 x 4 at High).
uniform vec4 u_lumen_adaptive;

#define u_lumen_adaptive_capacity uint(u_lumen_adaptive.x)
#define u_lumen_adaptive_border_res uint(u_lumen_adaptive.y)
#define u_lumen_adaptive_samples_x int(u_lumen_adaptive.z)
#define u_lumen_adaptive_samples_y int(u_lumen_adaptive.w)
#define u_lumen_adaptive_samples (u_lumen_adaptive_samples_x * u_lumen_adaptive_samples_y)

/// The most candidates per uniform tile (4 x 4): a tile's probe list and placement mask hold this many.
#define LUMEN_ADAPTIVE_MAX_SAMPLES 16
/// The most uniform tiles an 8 x 8 group of candidates covers (8 candidates per tile at the fewest).
#define LUMEN_ADAPTIVE_MAX_GROUP_TILES 8
#define LUMEN_ADAPTIVE_COUNTER 0
#define LUMEN_ADAPTIVE_TILE_STRIDE (LUMEN_ADAPTIVE_MAX_SAMPLES + 2)

int LumenAdaptiveTileIndex(ivec2 tile)
{
	return tile.y * u_lumen_probe_count.x + tile.x;
}

/// The state entries of uniform tile @p tile_index: its adaptive probe count, the index of its @p k-th adaptive
/// probe, its placement mask.
int LumenAdaptiveCountEntry(int tile_index)
{
	return 1 + tile_index * LUMEN_ADAPTIVE_TILE_STRIDE;
}

int LumenAdaptiveProbeEntry(int tile_index, int k)
{
	return LumenAdaptiveCountEntry(tile_index) + 1 + k;
}

int LumenAdaptiveMaskEntry(int tile_index)
{
	return LumenAdaptiveCountEntry(tile_index) + 1 + LUMEN_ADAPTIVE_MAX_SAMPLES;
}

/// The atlas tile of adaptive probe @p index.
ivec2 LumenAdaptiveAtlasTile(int index)
{
	int row = index / u_lumen_probe_count.x;
	return ivec2(index - row * u_lumen_probe_count.x, u_lumen_probe_count.y + row);
}

/// Candidate @p sample_index of uniform tile @p tile: the uniform probe's pixel plus a
/// Hammersley point over the tile, scrambled per tile and frame.
ivec2 LumenAdaptiveSamplePixel(ivec2 tile, int sample_index)
{
	ivec2 uniform_pixel = tile * int(u_lumen_downsample) + ivec2(u_lumen_placement_jitter);
	uvec2 seed = Rand3DPCG16(ivec3(tile, int(u_lumen_frame_mod))).xy;
	vec2 offset = clamp(Hammersley16(uint(sample_index), uint(u_lumen_adaptive_samples), seed) * u_lumen_downsample,
	                    vec2_splat(0.0),
	                    vec2_splat(u_lumen_downsample - 1.0));
	return uniform_pixel + ivec2(offset);
}

/// The interpolation weights (x = primary, y = fallback) of the adaptive probe at @p probe_pixel (device depth
/// @p probe_depth01) for @p pixel on the plane (@p position, @p normal) at view depth @p depth: the plane weights times
/// a falloff over one tile of the nearer axis distance.
vec2 LumenAdaptiveProbeWeights(ivec2 pixel, vec3 position, vec3 normal, float depth, ivec2 probe_pixel, float probe_depth01)
{
	ivec2 uv_pixel = min(probe_pixel, ivec2(u_lumen_view_size) - ivec2(1, 1));
	vec3 probe_position = LumenWorldFromDepth(LumenPixelUv(uv_pixel), probe_depth01);
	float plane_distance = abs(dot(probe_position - position, normal)) / depth;
	float r2 = plane_distance * plane_distance;
	vec2 distance = abs(vec2(probe_pixel - pixel));
	float falloff = 1.0 - saturate(min(distance.x, distance.y) / u_lumen_downsample);
	return falloff * vec2(exp2(-LUMEN_INTERP_DEPTH_WEIGHT * r2), exp2(-LUMEN_INTERP_FALLBACK_DEPTH_WEIGHT * r2));
}

#endif // __LUMEN_ADAPTIVE_PROBES_SH__
