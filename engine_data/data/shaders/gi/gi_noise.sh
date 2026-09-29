#ifndef __GI_NOISE_SH__
#define __GI_NOISE_SH__

/*
 * Shared 2D jitter pattern for the GI stochastic kernels (gather cone jitter, integrate
 * bracket jitter, reflection VNDF sample). Two INDEPENDENT interleaved-gradient
 * evaluations: deriving the second channel from the first puts every 2D point on a 1D
 * curve through the unit square, so half the domain is never covered and high-contrast
 * content cannot converge. Callers add their own R2 temporal advance in value space -
 * fract(pattern + R2(frame)) - which is what carries the per-pixel convergence.
 *
 * Plain IGN rather than a blue-noise tile: the temporal chain (probe-space mean, dual-rate
 * pixel temporal, denoise) hides the difference, and a tile costs a generator and uniforms
 * per dispatch and correlates pixels one tile apart (rare-event hits on a small emitter
 * flash on a lattice).
 */

/// The R2 low-discrepancy sequence's per-index advance (1 / plastic number and its square).
#define GI_R2_ADVANCE vec2(0.754877666, 0.569840291)
/// The R3 sequence's per-index advance (1 / phi3 and its square and cube, phi3 the real root of
/// x^4 = x + 1): successive indices spread evenly through the unit cube.
#define GI_R3_ADVANCE vec3(0.819172513, 0.671043607, 0.549700478)

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
 * steps through the unit square in regular stripes. With the gather's jitter on a short frame
 * cycle every cell is a fixed few-point quadrature of its cone, its error a function of that
 * offset, so the stripes would print on the floor as diagonal waves and a sawtooth along soft
 * shadow edges - fixed to the SCREEN lattice, sliding over the world in a camera turn. On the
 * probe lattice the 3x3 probe filter and the integrate's bracket see well-spread offsets; the
 * per-cell rotation is the same for every probe and only decorrelates the cells of one tile.
 */
vec2 GiProbeCellNoise(ivec2 probe, int cell_index)
{
	return fract(GiIgnNoise(probe) + GI_R2_ADVANCE * float(cell_index));
}

#endif // __GI_NOISE_SH__
