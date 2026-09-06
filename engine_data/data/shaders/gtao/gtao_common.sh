#ifndef GTAO_COMMON_SH_HEADER_GUARD
#define GTAO_COMMON_SH_HEADER_GUARD

/*
 * Ground Truth Ambient Occlusion (Jimenez et al. 2016, in the shape of Intel's XeGTAO):
 * shared constants, uniforms and helpers for the prefilter, main, denoise, temporal and
 * upsample passes. Every pass runs at the AO resolution (full or half of the G-buffer);
 * the upsample writes the full-resolution result the lighting consumes.
 *
 * OUTPUT CONTRACT (the "GTAO" texture, RGBA8): rgb = world-space bent normal encoded
 * * 0.5 + 0.5, a = cosine-weighted visibility in [0, 1] (1 = unoccluded). Sky pixels
 * store the G-buffer normal and visibility 1.
 *
 * VIEW-SPACE CONVENTION: positions come from computeViewSpacePosition (shaderlib.sh), which
 * routes through clipTransform / toClipSpaceDepth, so the backend's y flip and depth range
 * are handled there and nothing here assumes a screen axis orientation. A slice direction
 * is derived by reconstructing a second view position at a small uv offset (same depth),
 * which keeps the screen-to-view mapping exact for every backend.
 */

#include "../lighting.sh"

#define GTAO_DEPTH_MIP_LEVELS 5
#define GTAO_PI 3.14159265358979
#define GTAO_HALF_PI 1.5707963267949
/// View-space depth stored for sky / no-geometry texels (beyond any effect radius).
#define GTAO_SKY_DEPTH 1.0e6
/// Device depth at or beyond this is the far plane (nothing was drawn).
#define GTAO_SKY_DEVICE_DEPTH 0.999999
/// Sample offsets shorter than this many pixels read the centre texel and are skipped.
#define GTAO_PIXEL_TOO_CLOSE 1.3
/// Sectors of the visibility bitmask per slice: the projected normal's hemisphere split into
/// this many equal angles, one bit each (32 = the machine word; the paper's choice).
#define GTAO_SECTOR_COUNT 32

/// acos to within 0.001 rad without the transcendental (the usual polynomial), for the
/// four horizon angles every sample of the bitmask needs.
float GtaoFastAcos(float x)
{
	float ax = abs(x);
	float result = ((-0.0187293 * ax + 0.0742610) * ax - 0.2121144) * ax + 1.5707288;
	result *= sqrt(saturate(1.0 - ax));
	return x >= 0.0 ? result : GTAO_PI - result;
}
/// Lower clamp on the visibility (XeGTAO): a fully black cavity reads as no surface.
#define GTAO_MIN_VISIBILITY 0.03
/// The raw per-frame visibility can overshoot 1 (noise); it is stored divided by this scale
/// so the denoise and temporal average the overshoot out instead of clipping it dark, and
/// the upsample multiplies it back (XeGTAO's XE_GTAO_OCCLUSION_TERM_SCALE).
#define GTAO_OCCLUSION_TERM_SCALE 1.5
/// The receiver is pulled this fraction toward the camera before the horizon search, so
/// its own surface's depth quantisation never reads as an occluder (XeGTAO, FP32 depth).
#define GTAO_DEPTH_BIAS 0.99999
/// The receiver normal source rides u_gtao_params3.x (settings.generate_normals): 1 = the
/// geometric normal reconstructed from depth (the default: the horizon integral then matches
/// the depth it marches and cannot self-occlude on a normal map; the map's crevices come from
/// the upsample's detail term); 0 = the G-buffer shading normal, normal map included, which
/// is what Intel recommends when a G-buffer normal exists and gives the bump response
/// inside the integral itself.
#define GTAO_NORMAL_SOURCE_GBUFFER 0.0
#define GTAO_NORMAL_SOURCE_GEOMETRIC 1.0
/// Width of the procedural Hilbert curve driving the R2 noise (XeGTAO: level 6).
#define GTAO_HILBERT_WIDTH 64

/// xy = AO-resolution size in texels, zw = 1 / size.
uniform vec4 u_gtao_size;
/// xy = full-resolution (G-buffer) size in texels, zw = 1 / size.
uniform vec4 u_gtao_full_size;
/// x = effect radius (view units), y = falloff range as a fraction of the radius,
/// z = final visibility power, w = thin occluder compensation (0 = off).
uniform vec4 u_gtao_params0;
/// x = slice count, y = steps per slice, z = temporal noise index, w = depth mip sampling
/// offset (log2 pixels below which mip 0 is read).
uniform vec4 u_gtao_params1;
/// x = denoise depth sigma (fraction of view depth), y = denoise normal power,
/// z = sample distribution power, w = maximum screen radius as a fraction of the height.
uniform vec4 u_gtao_params2;
/// x = normal source (GTAO_NORMAL_SOURCE_*), y = normal-map detail strength of the upsample,
/// z = 1 when the upsample applies that detail (reduced resolution, or the geometric source),
/// w unused.
uniform vec4 u_gtao_params3;

#define u_gtao_radius             u_gtao_params0.x
#define u_gtao_falloff_range      u_gtao_params0.y
#define u_gtao_final_power        u_gtao_params0.z
#define u_gtao_occluder_thickness u_gtao_params0.w
#define u_gtao_slice_count        u_gtao_params1.x
#define u_gtao_steps_per_slice    u_gtao_params1.y
#define u_gtao_noise_index        u_gtao_params1.z
#define u_gtao_mip_offset         u_gtao_params1.w
#define u_gtao_depth_sigma        u_gtao_params2.x
#define u_gtao_normal_power       u_gtao_params2.y
#define u_gtao_distribution_power u_gtao_params2.z
#define u_gtao_max_screen_radius  u_gtao_params2.w
#define u_gtao_normal_source      u_gtao_params3.x
#define u_gtao_detail_strength    u_gtao_params3.y
#define u_gtao_detail_enabled     u_gtao_params3.z
/// The denoise pass's axis: 0 = along x, 1 = along y (the 5x5 blur is run separably).
#define u_gtao_denoise_axis       u_gtao_params3.w
/// x = 1 for the visibility bitmask (else the two-horizon integral), yzw unused.
uniform vec4 u_gtao_params4;
#define u_gtao_bitmask            u_gtao_params4.x

/// View-space depth (positive distance along the view axis) of a device depth value.
float GtaoViewDepthFromDevice(float device_depth)
{
	if(device_depth >= GTAO_SKY_DEVICE_DEPTH)
	{
		return GTAO_SKY_DEPTH;
	}
	return abs(computeViewSpacePosition(vec2(0.5, 0.5), device_depth).z);
}

/// View-space position of the point seen at uv at the given view depth: the pixel's view
/// ray (through any depth) scaled to the requested depth.
vec3 GtaoViewPosition(vec2 uv, float view_depth)
{
	vec3 ray = computeViewSpacePosition(uv, 0.5);
	return ray * (view_depth / max(abs(ray.z), 1e-6));
}

/// The uv of the full-resolution pixel an AO-resolution uv stands for. An AO texel's depth
/// is its block's top-left pixel's, so its position must lie on THAT pixel's ray, not on the
/// ray through the block centre (the texture uv). On a flat surface the mismatch shifts
/// every point by the same slope-proportional amount and cancels; where the slope changes,
/// a convex edge, the two faces shift by different amounts and the near face's points rise
/// above the other face's plane - a line of false occlusion along every edge at reduced
/// resolution. Identity at full resolution.
vec2 GtaoRayUv(vec2 ao_uv)
{
	vec2 divisor = floor(u_gtao_full_size.xy / u_gtao_size.xy + vec2_splat(0.5));
	return ao_uv - (divisor * 0.5 - vec2_splat(0.5)) * u_gtao_full_size.zw;
}

/// The inverse of GtaoRayUv: the AO-texture uv whose texel holds the pixel at ray_uv.
vec2 GtaoTexelUvFromRay(vec2 ray_uv)
{
	vec2 divisor = floor(u_gtao_full_size.xy / u_gtao_size.xy + vec2_splat(0.5));
	return ray_uv + (divisor * 0.5 - vec2_splat(0.5)) * u_gtao_full_size.zw;
}

/// View-space position of an AO-resolution texture uv at the given view depth: the depth
/// chain's sample for that uv lies on its top-left pixel's ray (see GtaoRayUv).
vec3 GtaoAoViewPosition(vec2 ao_uv, float view_depth)
{
	return GtaoViewPosition(GtaoRayUv(ao_uv), view_depth);
}

/// The full-resolution pixel an AO texel stands for: the top-left pixel of its block. Depth
/// AND normal are read from this one pixel - a texel straddling a silhouette must describe
/// one surface, never a depth from one side with a normal from the other (that mismatch
/// tilts the tangent plane and darkens every silhouette into an outline).
ivec2 GtaoFullTexel(ivec2 ao_texel)
{
	ivec2 divisor = ivec2(u_gtao_full_size.xy / u_gtao_size.xy + vec2_splat(0.5));
	return clamp(ao_texel * divisor, ivec2(0, 0), ivec2(u_gtao_full_size.xy) - ivec2(1, 1));
}

/// World-space G-buffer normal of one full-resolution pixel (octahedral in the
/// normal/metal/rough attachment), point-fetched: bilinear filtering of an octahedral
/// encoding across an edge is not a normal of either surface.
vec3 GtaoWorldNormalTexel(sampler2D normal_tex, ivec2 full_texel)
{
	vec4 data1 = texelFetch(normal_tex, full_texel, 0);
	return normalize(decodeNormalOctahedron(data1.xy));
}

/// World-space G-buffer normal of an AO texel (the pixel GtaoFullTexel selects).
vec3 GtaoWorldNormal(sampler2D normal_tex, ivec2 ao_texel)
{
	return GtaoWorldNormalTexel(normal_tex, GtaoFullTexel(ao_texel));
}

/// Index of a pixel along a GTAO_HILBERT_WIDTH x GTAO_HILBERT_WIDTH Hilbert curve
/// (XeGTAO's HilbertIndex, computed instead of read from a lookup texture).
uint GtaoHilbertIndex(uint pos_x, uint pos_y)
{
	uint index = 0u;
	for(uint level = uint(GTAO_HILBERT_WIDTH) / 2u; level > 0u; level /= 2u)
	{
		uint region_x = (pos_x & level) > 0u ? 1u : 0u;
		uint region_y = (pos_y & level) > 0u ? 1u : 0u;
		index += level * level * ((3u * region_x) ^ region_y);
		if(region_y == 0u)
		{
			if(region_x == 1u)
			{
				pos_x = uint(GTAO_HILBERT_WIDTH - 1) - pos_x;
				pos_y = uint(GTAO_HILBERT_WIDTH - 1) - pos_y;
			}
			uint temp = pos_x;
			pos_x = pos_y;
			pos_y = temp;
		}
	}
	return index;
}

/// XeGTAO's spatiotemporal noise: the R2 sequence driven by the Hilbert index of the pixel,
/// advanced per frame - a blue-noise-like distribution over both screen and time, with two
/// decorrelated lanes (slice angle, step offset).
vec2 GtaoSpatioTemporalNoise(ivec2 pixel, float temporal_index)
{
	uint index = GtaoHilbertIndex(uint(pixel.x) % uint(GTAO_HILBERT_WIDTH), uint(pixel.y) % uint(GTAO_HILBERT_WIDTH));
	index += 288u * (uint(temporal_index) % 64u);
	return fract(vec2_splat(0.5) + float(index) * vec2(0.75487766624669276, 0.5698402909980532));
}

/// XeGTAO's depth edges of a pixel against its four neighbours (1 = continuous, 0 = edge),
/// slope-adjusted so a plane seen at a grazing angle is not an edge.
vec4 GtaoCalculateEdges(float center_z, float left_z, float right_z, float top_z, float bottom_z)
{
	vec4 edges = vec4(left_z, right_z, top_z, bottom_z) - vec4_splat(center_z);
	float slope_lr = (edges.y - edges.x) * 0.5;
	float slope_tb = (edges.w - edges.z) * 0.5;
	vec4 slope_adjusted = edges + vec4(slope_lr, -slope_lr, slope_tb, -slope_tb);
	edges = min(abs(edges), abs(slope_adjusted));
	return saturate(vec4_splat(1.25) - edges / (center_z * 0.011));
}

/// Sharpness of the shading-normal guidance in GtaoNormalFromDepth: a quadrant whose plane
/// straddles a 90-degree edge (45 degrees off the guide) keeps 0.71^16 = 0.4% of its weight.
#define GTAO_NORMAL_GUIDE_POWER 16.0

/// One quadrant's unit plane normal from two neighbour directions, facing the viewer.
vec3 GtaoQuadrantNormal(vec3 first, vec3 second, vec3 view_vec)
{
	vec3 n = cross(first, second);
	n = dot(n, n) > 1e-12 ? normalize(n) : vec3_splat(0.0);
	return dot(n, view_vec) < 0.0 ? -n : n;
}

/// Weight of one quadrant: its two depth edges, and its agreement with the shading normal.
float GtaoQuadrantWeight(float accepted, vec3 quadrant_normal, vec3 guide)
{
	return accepted * pow(saturate(dot(quadrant_normal, guide)), GTAO_NORMAL_GUIDE_POWER);
}

/// XeGTAO's geometric normal from the four neighbours' view positions, each quadrant
/// weighted by its two edges so a silhouette neighbour never tilts the plane, and by its
/// agreement with the G-buffer shading normal (view space). The depth alone cannot tell
/// which face a pixel on a convex edge belongs to, and XeGTAO's plain quadrant mean gives
/// such a pixel a normal halfway between the two faces - a line of false occlusion along
/// every edge and every facet seam. The shading normal knows the face; the depth still
/// supplies the flat plane, free of the normal map. Zero where no accepted quadrant faces
/// the shading normal (the caller falls back to the shading normal).
vec3 GtaoNormalFromDepth(vec4 edges, vec3 center, vec3 left, vec3 right, vec3 top, vec3 bottom, vec3 guide)
{
	vec4 accepted = saturate(vec4(edges.x * edges.z, edges.z * edges.y, edges.y * edges.w, edges.w * edges.x) + vec4_splat(0.01));
	vec3 view_vec = normalize(-center);
	vec3 l = normalize(left - center);
	vec3 r = normalize(right - center);
	vec3 t = normalize(top - center);
	vec3 b = normalize(bottom - center);
	vec3 n_lt = GtaoQuadrantNormal(l, t, view_vec);
	vec3 n_tr = GtaoQuadrantNormal(t, r, view_vec);
	vec3 n_rb = GtaoQuadrantNormal(r, b, view_vec);
	vec3 n_bl = GtaoQuadrantNormal(b, l, view_vec);
	vec3 n = GtaoQuadrantWeight(accepted.x, n_lt, guide) * n_lt +
	         GtaoQuadrantWeight(accepted.y, n_tr, guide) * n_tr +
	         GtaoQuadrantWeight(accepted.z, n_rb, guide) * n_rb +
	         GtaoQuadrantWeight(accepted.w, n_bl, guide) * n_bl;
	return dot(n, n) > 1e-12 ? normalize(n) : vec3_splat(0.0);
}

vec4 GtaoEncode(vec3 world_bent_normal, float visibility)
{
	return vec4(world_bent_normal * 0.5 + vec3_splat(0.5), visibility);
}

vec3 GtaoDecodeNormal(vec4 encoded)
{
	return normalize(encoded.xyz * 2.0 - vec3_splat(1.0));
}

/// XeGTAO's depth MIP filter: a weighted mean in which the depths within the effect's
/// falloff range of the FARTHEST of the four keep their weight and a lone nearer outlier
/// drops out. A coarse mip therefore never turns a thin near occluder into a wide false
/// occluder for the receivers behind it (the halo the plain average or a min filter casts);
/// the outlier is still found at the finer mips the near steps read.
float GtaoDepthMipFilter(float d0, float d1, float d2, float d3)
{
	float max_depth = max(max(d0, d1), max(d2, d3));
	float radius = u_gtao_radius;
	float falloff_range = u_gtao_falloff_range * radius;
	float falloff_from = radius * (1.0 - u_gtao_falloff_range);
	float falloff_mul = -1.0 / max(falloff_range, 1e-5);
	float falloff_add = falloff_from / max(falloff_range, 1e-5) + 1.0;
	float w0 = saturate((max_depth - d0) * falloff_mul + falloff_add);
	float w1 = saturate((max_depth - d1) * falloff_mul + falloff_add);
	float w2 = saturate((max_depth - d2) * falloff_mul + falloff_add);
	float w3 = saturate((max_depth - d3) * falloff_mul + falloff_add);
	float w_sum = w0 + w1 + w2 + w3;
	return (d0 * w0 + d1 * w1 + d2 * w2 + d3 * w3) / max(w_sum, 1e-5);
}

#endif // GTAO_COMMON_SH_HEADER_GUARD
