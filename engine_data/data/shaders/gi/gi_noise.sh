#ifndef __GI_NOISE_SH__
#define __GI_NOISE_SH__

/*
 * Shared 2D jitter pattern for the GI stochastic kernels (gather cone jitter, integrate
 * bracket jitter, reflection VNDF sample). Two INDEPENDENT interleaved-gradient
 * evaluations: deriving the second channel from the first puts every 2D point on a 1D
 * curve through the unit square, so half the domain is never covered and high-contrast
 * content cannot converge (measured, reflections round 13). Callers add their own R2
 * temporal advance in value space - fract(pattern + R2(frame)) - which is what carries
 * the per-pixel convergence.
 *
 * HISTORY (2026-08-26): a 32x32 blue-noise tile lived here briefly (256-vec4 uniform
 * array + CPU void-and-cluster generator + macro-tile scramble + a settings toggle) and
 * was REMOVED after measurement: under this pipeline's temporal chain (probe-space mean,
 * dual-rate pixel temporal, denoise) it was visually indistinguishable from IGN and
 * perf-neutral, while costing a generator, 4 KB of uniforms per dispatch, and a
 * correlated-tiling defect that needed its own fix (pixels one tile apart shared xi, so
 * rare-event hits on a small emitter flashed on a lattice). Do not re-add a noise
 * texture/tile here without a measured visual win; a shared-memory staging of the tile
 * was ALSO tried and measured SLOWER (+0.1 ms FHD - occupancy beat the replay theory).
 */

/// The jitter pattern at a pixel, both channels independent, in [0, 1).
vec2 GiIgnNoise(ivec2 pixel)
{
	vec2 p = vec2(pixel);
	float a = fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
	float b = fract(52.9829189 * fract(0.06711056 * (p.y + 17.0) + 0.00583715 * (p.x + 31.0)));
	return vec2(a, b);
}

#endif // __GI_NOISE_SH__
