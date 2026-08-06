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
 * Layout, shared by all three probe passes and mirrored by gi_resolve_pass.cpp:
 *  - Radiance atlas: a 2D RGBA16F image, one GI_PROBE_DIR_EDGE^2 octahedral tile per probe,
 *    tiles packed in probe-grid order. rgb = radiance (intensity applied), a = the cone's
 *    encoded PROXIMITY (see GI_PROBE_PROXIMITY_SKY): what the cone saw and roughly how far
 *    away, 0 where the texel measured nothing.
 *  - Probe buffer: GI_PROBE_STRIDE vec4s per probe, probe-major:
 *      [9]     xyz = anchor world position, w = 1 when the probe anchors on geometry
 *      [10]    xyz = anchor world normal, w = anchor view distance
 *      [11]    x = accumulated history count, y = this frame's blend weight
 *    (slots [0..8] held an SH2 projection in an earlier design and are currently unused.)
 */

#define GI_PROBE_DIR_EDGE   8
#define GI_PROBE_DIR_COUNT  64
#define GI_PROBE_STRIDE     12
#define GI_PROBE_META       9
#define GI_PROBE_META2      10
/// Single layer: the v2 gather anchors one probe per tile (Phase 8 removed the v1
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

/// x = history cap in frames (1 disables history, also sent while the buffers hold garbage),
/// y = debug view mode (consumed by the integrate pass),
/// z = WRITE half offset into the probe buffer, w = READ half offset -- both in PROBES.
///
/// The probe buffer is double buffered because history is REPROJECTED: this frame's probe needs
/// last frame's meta and radiance for the probe that covered its anchor's world position THEN,
/// which is generally a different probe index. Without both halves resident there is nothing to
/// reproject from.
uniform vec4 u_gi_probe_temporal;
#define u_gi_probe_history_cap   u_gi_probe_temporal.x
#define u_gi_probe_debug_mode    int(u_gi_probe_temporal.y)
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

#endif // __GI_PROBE_COMMON_SH__
