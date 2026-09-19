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
 * HISTORY: a 32x32 blue-noise tile lived here briefly (256-vec4 uniform
 * array + CPU void-and-cluster generator + macro-tile scramble + a settings toggle) and
 * was REMOVED after measurement: under this pipeline's temporal chain (probe-space mean,
 * dual-rate pixel temporal, denoise) it was visually indistinguishable from IGN and
 * perf-neutral, while costing a generator, 4 KB of uniforms per dispatch, and a
 * correlated-tiling defect that needed its own fix (pixels one tile apart shared xi, so
 * rare-event hits on a small emitter flashed on a lattice). Do not re-add a noise
 * texture/tile here without a measured visual win; a shared-memory staging of the tile
 * was ALSO tried and measured SLOWER (+0.1 ms FHD - occupancy beat the replay theory).
 */

/// The R2 low-discrepancy sequence's per-index advance (1 / plastic number and its square).
#define GI_R2_ADVANCE vec2(0.754877666, 0.569840291)

/// The jitter pattern at a pixel, both channels independent, in [0, 1).
vec2 GiIgnNoise(ivec2 pixel)
{
	vec2 p = vec2(pixel);
	float a = fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
	float b = fract(52.9829189 * fract(0.06711056 * (p.y + 17.0) + 0.00583715 * (p.x + 31.0)));
	return vec2(a, b);
}

/*
 * The jitter pattern of one direction CELL of an octahedral probe tile: the IGN of the probe's
 * LATTICE coordinate, rotated per cell along R2. IGN spreads its values evenly over any 3x3
 * block of ADJACENT coordinates - sampled at the atlas stride (one tile = 8 texels) that
 * property is gone and what is left is a plane wave: the same cell of neighbouring probes
 * stepped through the unit square in regular stripes. With the gather's jitter on a short frame
 * cycle every cell is a fixed few-point quadrature of its cone, its error a function of that
 * offset, so the stripes printed themselves on the floor as diagonal waves and a sawtooth along
 * soft shadow edges - fixed to the SCREEN lattice, sliding over the world in a camera turn
 * (measured 2026-09-18, Sponza gallery, probe spacing 4 / 8 / 16: fringe period proportional to
 * the spacing; view-dependent band residual under a 3 degree turn 8.6 levels -> 4.2 with this
 * pattern at the same 8-frame cycle, rest std p95 1.03 -> 0.71). On the probe lattice the 3x3
 * probe filter and the integrate's bracket see well-spread offsets again; the per-cell rotation
 * is the same for every probe and only decorrelates the cells of one tile.
 */
vec2 GiProbeCellNoise(ivec2 probe, int cell_index)
{
	return fract(GiIgnNoise(probe) + GI_R2_ADVANCE * float(cell_index));
}

#endif // __GI_NOISE_SH__
