#ifndef __LUMEN_ADAPTIVE_PROBES_SH__
#define __LUMEN_ADAPTIVE_PROBES_SH__

/*
 * Adaptive screen probes (UE 5.8 ScreenProbeAdaptivePlacementMarkCS and SpawnCS, LumenScreenProbeGather.usf:1721-1952;
 * CalculateUpsampleInterpolationWeights, :216-310). Each uniform tile tests LUMEN_ADAPTIVE_SAMPLES_X x
 * LUMEN_ADAPTIVE_SAMPLES_Y candidate pixels; a candidate the uniform probes cannot interpolate (weights summing below
 * LUMEN_INTERP_MIN_WEIGHT) and no lower-numbered candidate around it covers becomes a probe. Adaptive probe i lives in
 * the probe atlas at (i % probes_x, probes_y + i / probes_x) and is listed in its uniform tile; every per-probe pass
 * treats it as a probe at that atlas tile.
 *
 * The adaptive state buffer (uint), tile = y x probes_x + x over the uniform tiles:
 *  [LUMEN_ADAPTIVE_COUNTER] the probes spawned this frame (may pass the capacity: those past it are dropped);
 *  [LUMEN_ADAPTIVE_HEADER + tile] the tile's adaptive probes;
 *  [LUMEN_ADAPTIVE_INDICES + tile x LUMEN_ADAPTIVE_SAMPLES + k] the index of the tile's k-th adaptive probe;
 *  [LUMEN_ADAPTIVE_MASK + tile] the tile's candidates the uniform probes cannot interpolate (one bit each).
 * Sized for LUMEN_PROBE_PIXEL_STRIDE^2 views (lumen_adaptive_probes.cpp mirrors the layout).
 *
 * The includer includes lumen_common.sh.
 */

/// x = the adaptive probe capacity (trunc(uniform probes x LUMEN_ADAPTIVE_ALLOCATION_FRACTION)).
uniform vec4 u_lumen_adaptive;

#define u_lumen_adaptive_capacity uint(u_lumen_adaptive.x)

#define LUMEN_ADAPTIVE_SAMPLES (LUMEN_ADAPTIVE_SAMPLES_X * LUMEN_ADAPTIVE_SAMPLES_Y)
#define LUMEN_ADAPTIVE_MAX_TILES ((LUMEN_PROBE_PIXEL_STRIDE / LUMEN_PROBE_DOWNSAMPLE_FACTOR) * (LUMEN_PROBE_PIXEL_STRIDE / LUMEN_PROBE_DOWNSAMPLE_FACTOR))
#define LUMEN_ADAPTIVE_COUNTER 0
#define LUMEN_ADAPTIVE_HEADER 1
#define LUMEN_ADAPTIVE_INDICES (LUMEN_ADAPTIVE_HEADER + LUMEN_ADAPTIVE_MAX_TILES)
#define LUMEN_ADAPTIVE_MASK (LUMEN_ADAPTIVE_INDICES + LUMEN_ADAPTIVE_MAX_TILES * LUMEN_ADAPTIVE_SAMPLES)

int LumenAdaptiveTileIndex(ivec2 tile)
{
	return tile.y * u_lumen_probe_count.x + tile.x;
}

/// The atlas tile of adaptive probe @p index.
ivec2 LumenAdaptiveAtlasTile(int index)
{
	int row = index / u_lumen_probe_count.x;
	return ivec2(index - row * u_lumen_probe_count.x, u_lumen_probe_count.y + row);
}

/// Candidate @p sample_index of uniform tile @p tile (UE GetAdaptiveSampleCoord): the uniform probe's pixel plus a
/// Hammersley point over the tile, scrambled per tile and frame.
ivec2 LumenAdaptiveSamplePixel(ivec2 tile, int sample_index)
{
	ivec2 uniform_pixel = tile * int(u_lumen_downsample) + ivec2(u_lumen_placement_jitter);
	uvec2 seed = Rand3DPCG16(ivec3(tile, int(u_lumen_frame_mod))).xy;
	vec2 offset = clamp(Hammersley16(uint(sample_index), uint(LUMEN_ADAPTIVE_SAMPLES), seed) * u_lumen_downsample,
	                    vec2_splat(0.0),
	                    vec2_splat(u_lumen_downsample - 1.0));
	return uniform_pixel + ivec2(offset);
}

/// The interpolation weights (x = primary, y = fallback) of the adaptive probe at @p probe_pixel (device depth
/// @p probe_depth01) for @p pixel on the plane (@p position, @p normal) at view depth @p depth (UE
/// GetAdaptiveProbeInterpolationWeight): the plane weights times a falloff over one tile of the nearer axis distance.
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
