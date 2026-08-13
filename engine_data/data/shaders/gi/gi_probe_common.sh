#ifndef __GI_PROBE_COMMON_SH__
#define __GI_PROBE_COMMON_SH__

/*
 * Screen-space radiance probes: shared layout and maths.
 *
 * The probe gather decouples RAYS from PIXELS. One probe per NxN pixel tile traces a fixed set
 * of octahedral directions once; every pixel then integrates the four probes around it. Ray
 * count stops scaling with resolution, ray origins are shared (coherent traces), and the
 * radiance is filtered IN PROBE SPACE before any pixel sees it -- the three structural
 * advantages a per-pixel gather cannot have at any tuning.
 *
 * Layout, shared by the probe passes and mirrored by gi_resolve_pass.cpp:
 *  - Radiance atlas: a 2D RGBA16F image, one GI_PROBE_DIR_EDGE^2 octahedral tile per probe,
 *    tiles packed in probe-grid order. rgb = radiance (intensity applied), a = the cone's
 *    encoded PROXIMITY (see GI_PROBE_PROXIMITY_SKY): what the cone saw and roughly how far
 *    away, 0 where the texel measured nothing.
 *  - Probe buffer: GI_PROBE_STRIDE vec4s per probe, probe-major:
 *      [0..3]  the 4x4 importance mip the filter writes for next frame's ray allocation
 *      [4]     xyz = lifted trace origin, w = shortened-ray range (placement pass)
 *      [5]     xy = anchor uv, z = anchor device depth, w unused (placement pass)
 *      [9]     xyz = anchor world position, w = the probe MODE: 0 = no geometry, 1 = traced,
 *              2 = interpolated from its even-lattice parents (adaptive gather). Consumers
 *              that only care about validity keep testing w > 0.5.
 *      [10]    xyz = anchor world normal, w = anchor view distance
 *      [11]    x = accumulated history count, y = this frame's blend weight
 */

#define GI_PROBE_DIR_EDGE   8
#define GI_PROBE_DIR_COUNT  64
#define GI_PROBE_STRIDE     12
#define GI_PROBE_ORIGIN     4
#define GI_PROBE_ANCHOR     5
#define GI_PROBE_META       9
#define GI_PROBE_META2      10
/// Single layer: the gather anchors one probe per tile (Phase 8 removed the v1
/// two-layer machinery); the record indexing keeps the parameter for layout stability.
#define GI_PROBE_LAYERS     1

/// x = probe count x, y = probe count y, z = probe spacing in TRACE-RESOLUTION pixels,
/// w = frame index.
uniform vec4 u_gi_probe_params;
#define u_gi_probe_count_x  int(u_gi_probe_params.x)
#define u_gi_probe_count_y  int(u_gi_probe_params.y)
#define u_gi_probe_spacing  u_gi_probe_params.z
#define u_gi_probe_frame    uint(u_gi_probe_params.w)

/// xy = trace-resolution target size in pixels, zw = 1 / that size.
uniform vec4 u_gi_probe_screen;

/// x = 1 when the record halves hold trusted data (gates importance reprojection),
/// y = probe-space temporal WINDOW in frames (1 = trace every octahedral texel this
///     frame, the A/B-off path; GI_SCREEN_PROBE_WINDOW = 4 traces a 16-ray Bayer
///     stratum and copies the rest from last frame's reprojected tile),
/// z = WRITE half offset into the probe buffer, w = READ half offset -- both in PROBES.
///
/// The probe buffer is double buffered because history is REPROJECTED: this frame's probe needs
/// last frame's meta and radiance for the probe that covered its anchor's world position THEN,
/// which is generally a different probe index. Without both halves resident there is nothing to
/// reproject from. The radiance atlas is ping-ponged for the same reason.
uniform vec4 u_gi_probe_temporal;
#define u_gi_probe_history_cap   u_gi_probe_temporal.x
#define u_gi_probe_window        uint(max(u_gi_probe_temporal.y, 1.0))
#define u_gi_probe_write_offset  uint(u_gi_probe_temporal.z)
#define u_gi_probe_read_offset   uint(u_gi_probe_temporal.w)

/**
 * Octahedral decode: maps a [0,1]^2 tile coordinate to a unit direction on the full sphere.
 * The standard equal-area-ish octahedral mapping; its inverse is not needed anywhere because
 * directions are only ever produced from texels, never searched for.
 */
vec3 GiOctDecode(vec2 tile_uv)
{
	vec2 f = tile_uv * 2.0 - 1.0;
	vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
	float t = saturate(-n.z);
	n.x += n.x >= 0.0 ? -t : t;
	n.y += n.y >= 0.0 ? -t : t;
	return normalize(n);
}

/// Octahedral encode: inverse of GiOctDecode, mapping a unit direction to [0,1]^2 tile space.
vec2 GiOctEncode(vec3 d)
{
	d /= (abs(d.x) + abs(d.y) + abs(d.z));
	vec2 e = d.xy;
	if(d.z < 0.0)
	{
		vec2 sign_not_zero = vec2(d.x >= 0.0 ? 1.0 : -1.0, d.y >= 0.0 ? 1.0 : -1.0);
		e = (vec2_splat(1.0) - abs(d.yx)) * sign_not_zero;
	}
	return e * 0.5 + 0.5;
}

/// Wraps a texel coordinate outside an octahedral tile back inside it. Crossing an octahedral
/// edge lands on the opposite half of the sphere with the transverse axis mirrored -- this is
/// what makes bilinear taps near tile edges sample the CORRECT neighbouring direction instead
/// of an adjacent probe's tile.
ivec2 GiOctWrapTexel(ivec2 texel)
{
	if(texel.x < 0)
	{
		texel.x = -1 - texel.x;
		texel.y = GI_PROBE_DIR_EDGE - 1 - texel.y;
	}
	if(texel.x >= GI_PROBE_DIR_EDGE)
	{
		texel.x = 2 * GI_PROBE_DIR_EDGE - 1 - texel.x;
		texel.y = GI_PROBE_DIR_EDGE - 1 - texel.y;
	}
	if(texel.y < 0)
	{
		texel.y = -1 - texel.y;
		texel.x = GI_PROBE_DIR_EDGE - 1 - texel.x;
	}
	if(texel.y >= GI_PROBE_DIR_EDGE)
	{
		texel.y = 2 * GI_PROBE_DIR_EDGE - 1 - texel.y;
		texel.x = GI_PROBE_DIR_EDGE - 1 - texel.x;
	}
	return texel;
}

/// The nine SH2 basis functions at a direction, in the order the probe buffer stores them.
void GiShBasis(vec3 d, out float basis[9])
{
	basis[0] = 0.282095;
	basis[1] = 0.488603 * d.y;
	basis[2] = 0.488603 * d.z;
	basis[3] = 0.488603 * d.x;
	basis[4] = 1.092548 * d.x * d.y;
	basis[5] = 1.092548 * d.y * d.z;
	basis[6] = 0.315392 * (3.0 * d.z * d.z - 1.0);
	basis[7] = 1.092548 * d.x * d.z;
	basis[8] = 0.546274 * (d.x * d.x - d.y * d.y);
}

/// Cosine-lobe convolution weights per band, divided by PI so the result of the irradiance
/// evaluation is directly in the output's radiance-mean units (E / PI).
void GiShIrradianceWeights(out float weights[9])
{
	// A0 = pi, A1 = 2pi/3, A2 = pi/4, all over pi.
	weights[0] = 1.0;
	weights[1] = 2.0 / 3.0;
	weights[2] = 2.0 / 3.0;
	weights[3] = 2.0 / 3.0;
	weights[4] = 0.25;
	weights[5] = 0.25;
	weights[6] = 0.25;
	weights[7] = 0.25;
	weights[8] = 0.25;
}

/// 2,3-Halton point of an 8-cycle, the placement jitter within the probe tile. A short cycle on
/// purpose: the temporal filter accumulates a bounded window, so the cycle must fit inside it.
vec2 GiHalton8(uint frame)
{
	uint index = frame % 8u;
	float h2 = 0.0;
	float f2 = 0.5;
	uint n2 = index + 1u;
	for(int i = 0; i < 4 && n2 > 0u; ++i)
	{
		h2 += f2 * float(n2 % 2u);
		n2 /= 2u;
		f2 *= 0.5;
	}
	float h3 = 0.0;
	float f3 = 1.0 / 3.0;
	uint n3 = index + 1u;
	for(int i = 0; i < 3 && n3 > 0u; ++i)
	{
		h3 += f3 * float(n3 % 3u);
		n3 /= 3u;
		f3 /= 3.0;
	}
	return vec2(h2, h3);
}

/// The even-lattice PARENTS of a probe for the adaptive gather: the up-to-four probes at even
/// coordinates bracketing it along each odd axis. A non-straddled (even) axis and a lattice
/// edge both DUPLICATE a parent rather than shrink the set, so four uniform 0.25 taps always
/// normalise correctly - and the clamp falls BACK to the low parent, never onto an odd
/// coordinate, because odd probes may themselves be interpolated and must never be read as a
/// source (the interp pass relies on parents being trace-written this same frame).
void GiProbeParents(ivec2 probe, out ivec2 parents[4])
{
	int x0 = probe.x & (~1);
	int y0 = probe.y & (~1);
	int x1 = (probe.x & 1) != 0 ? x0 + 2 : x0;
	int y1 = (probe.y & 1) != 0 ? y0 + 2 : y0;
	if(x1 >= u_gi_probe_count_x)
	{
		x1 = x0;
	}
	if(y1 >= u_gi_probe_count_y)
	{
		y1 = y0;
	}
	parents[0] = ivec2(x0, y0);
	parents[1] = ivec2(x1, y0);
	parents[2] = ivec2(x0, y1);
	parents[3] = ivec2(x1, y1);
}

/// First vec4 of the traced-list region in the probe buffer: past both record halves. The
/// compacted probe coordinates live THERE - one per vec4, bit-cast into .x - because the
/// trace has no spare binding stage for a second buffer (all sixteen carry world structures)
/// while stage 7 is already bound everywhere. Float buffers pass raw bits through exactly,
/// so the cast is lossless; one WHOLE vec4 per coordinate because the D3D path binds this as
/// a typed UAV, and typed UAV stores must write every component - four-to-a-vec4 component
/// stores do not compile there.
uint GiProbeTracedListBase()
{
	return 2u * uint(GI_PROBE_LAYERS) * uint(u_gi_probe_count_x * u_gi_probe_count_y) *
	       uint(GI_PROBE_STRIDE);
}

uint GiProbeIndex(int px, int py)
{
	return uint(py) * uint(u_gi_probe_count_x) + uint(px);
}

/// Record index of a probe within one buffer half: layers are stored as consecutive full
/// lattices, so layer L of tile (x, y) sits at L * lattice + index.
uint GiProbeRecord(int px, int py, int layer)
{
	return uint(layer) * uint(u_gi_probe_count_x * u_gi_probe_count_y) + GiProbeIndex(px, py);
}

/// Top-left texel of a probe's octahedral tile in the radiance atlas: layers stack VERTICALLY,
/// each occupying a full lattice of tiles.
ivec2 GiProbeAtlasBase(int px, int py, int layer)
{
	return ivec2(px, py + layer * u_gi_probe_count_y) * GI_PROBE_DIR_EDGE;
}

/**
 * Whether octahedral texel @p local is in this frame's traced stratum.
 *
 * Window 1 traces every texel (the A/B-off path, identical to the pre-temporal gather).
 * Window 4 is a 2x2 Bayer phase: 16 of 64 texels, exhaustive over GI_SCREEN_PROBE_WINDOW
 * frames, spatially distributed so the probe-space filter always has a nearby fresh sample.
 */
bool GiScreenProbeInStratum(ivec2 local, uint frame, uint window)
{
	if(window <= 1u)
	{
		return true;
	}
	uint phase = frame % window;
	return (uint(local.x) & 1u) == (phase & 1u) &&
	       (uint(local.y) & 1u) == ((phase >> 1u) & 1u);
}

/**
 * Compacted-trace inverse of GiScreenProbeInStratum: thread t in
 * [0, GI_SCREEN_PROBE_RAYS_PER_FRAME) plus Bayer phase -> octahedral texel.
 * The 16 threads are a 4x4 coarse grid; phase selects which of the 2x2 sub-texels
 * each coarse cell traces this frame. Window 1 walks phase 0..3 so the same
 * 16-thread group still covers the whole atlas.
 */
ivec2 GiScreenProbeStratumLocal(int thread, uint phase)
{
	int coarse = GI_PROBE_DIR_EDGE / 2;
	int cx = (thread % coarse) * 2;
	int cy = (thread / coarse) * 2;
	return ivec2(cx + int(phase & 1u), cy + int((phase >> 1u) & 1u));
}

#endif // __GI_PROBE_COMMON_SH__
