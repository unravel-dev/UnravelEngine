/*
 * GTAO upsample: writes the full-resolution "GTAO" texture from the AO-resolution result.
 * At full resolution this is a copy; at reduced resolution each pixel takes the bilinear
 * footprint's four AO texels weighted by how close their view depth is to the depth the
 * pixel's own surface plane predicts at the texel (the same joint-bilateral idea the GI
 * upsample uses, with the plane term so a grazing surface does not reject its own
 * neighbours). An AO texel holds the top-left pixel of its block, and the footprint is
 * aligned to that pixel, not to the block centre.
 *
 * Taps are also weighted by their shading normal's agreement with the pixel's, since a convex
 * edge is depth-continuous and the neighbouring face's texels must not lend their bent cone.
 * Where no tap shares the pixel's surface - a silhouette row whose block texels are all sky,
 * or a thin structure the reduced grid stepped over - the nearest tap BY DEPTH stands in, and
 * with no surface tap at all the pixel is left unoccluded. A sky texel never stands in: its
 * bent normal is the cleared G-buffer's, and the detail term facing that reads as black.
 *
 * NORMAL-MAP DETAIL (u_gtao_params3): where the main pass did not see the shading normal at
 * every pixel - reduced resolution, or the geometric normal source - the visibility is
 * re-modulated here by how the pixel's shading normal faces the AO texel's bent cone
 * RELATIVE to the normal the AO was computed with (a bump facing away from the open
 * direction darkens; a flat pixel of an occluded texel is left alone), and the bent normal
 * is tilted by the map's perturbation. This is what keeps mortar lines and cobble cracks at
 * half resolution. The term only acts where the shading normal is a bump on the AO normal
 * and the cone faces that normal; a rim's chord-reconstructed normal fades it out, and a
 * wire or corner (two depth-continuous sides or fewer) gets none.
 */

#include "../bgfx_compute.sh"
#include "../common.sh"
#include "gtao_common.sh"

SAMPLER2D(s_gtao_input, 0);
SAMPLER2D(s_gtao_depth_mips, 1);
/// Full-resolution G-buffer depth.
SAMPLER2D(s_gtao_depth, 2);
IMAGE2D_WO(i_gtao_out, rgba8, 3);
/// Full-resolution G-buffer normal / metalness / roughness (the shading normal).
SAMPLER2D(s_gtao_normal, 4);

/// Deviation from the pixel's own depth beyond which a tap cannot stand in for it.
#define GTAO_UPSAMPLE_NO_TAP 1.0e30
/// Cosine between the pixel's shading normal and a tap's above which the tap weighs in
/// full; it fades to nothing at 0. A convex edge is depth-continuous, so depth alone lets a
/// pixel blend the texels of the neighbouring face: their bent normals then tilt the cone
/// away from the pixel's own normal and the detail term reads that as a bump facing away -
/// a line along every edge. Bumps of a normal map stay well inside this cosine.
#define GTAO_UPSAMPLE_NORMAL_COSINE 0.6
/// Floor on the AO normal's own facing of the bent cone in the detail ratio.
#define GTAO_DETAIL_MIN_FACING 0.1
/// Cosine between the pixel's shading normal and the AO normal above which the detail term
/// acts in full: a bump, however steep, keeps it; a rim whose depth-reconstructed normal is a
/// chord across the surface (nearly perpendicular to the shading normal) fades it out.
#define GTAO_DETAIL_BUMP_FACING 0.25
/// Cosine between the AO normal and the bent cone below which the cone no longer describes
/// that normal's hemisphere (a silhouette texel's reconstructed normal): the term fades out.
#define GTAO_DETAIL_CONE_FACING 0.3
/// Depth-continuous sides (of four) a pixel needs for the detail term: a rim has three; a
/// wire or a corner has two, and there neither the reconstructed normal nor the AO texel
/// describes the pixel's surface.
#define GTAO_DETAIL_MIN_CONTINUOUS_SIDES 2.5

/// The normal-map detail term. ao_normal is the normal the AO texel was computed with
/// (the geometric one, or the texel's shading normal); scale is the pixel's eligibility
/// (0 on wires and corners).
vec4 GtaoApplyDetail(vec4 value, vec3 pixel_normal, vec3 ao_normal, float scale)
{
	if(u_gtao_detail_enabled < 0.5)
	{
		return value;
	}
	vec3 bent = GtaoDecodeNormal(value);
	float pixel_facing = saturate(dot(pixel_normal, bent));
	float ao_facing = max(dot(ao_normal, bent), GTAO_DETAIL_MIN_FACING);
	float facing = saturate(pixel_facing / ao_facing);
	// The term is a first-order bump correction, so it acts only where its inputs are a bump's:
	// the pixel's normal a perturbation of the AO normal (not a rim's chord-reconstructed one)
	// and the cone facing that normal. Another surface's tap (a roof rim over a wall texel)
	// needs no gate: its cone is measured against the pixel's own AO normal, so an unbumped
	// pixel keeps the ratio at 1 whatever the cone.
	float bump_scale = smoothstep(0.0, GTAO_DETAIL_BUMP_FACING, dot(pixel_normal, ao_normal));
	float cone_valid = smoothstep(0.0, GTAO_DETAIL_CONE_FACING, dot(ao_normal, bent));
	float strength = u_gtao_detail_strength * bump_scale * cone_valid * scale;
	float visibility = value.a * mix(1.0, facing, strength);
	vec3 tilted = bent + (pixel_normal - ao_normal) * strength;
	vec3 bent_out = dot(tilted, tilted) > 1e-8 ? normalize(tilted) : bent;
	return GtaoEncode(bent_out, visibility);
}

/// View depth of a full-resolution pixel, clamped into the buffer.
float GtaoFullViewDepth(ivec2 full_texel, ivec2 full_max)
{
	return GtaoViewDepthFromDevice(texelFetch(s_gtao_depth, clamp(full_texel, ivec2(0, 0), full_max), 0).x);
}

/// The smaller of two one-sided depth differences: the surface's slope along the axis,
/// unless a depth edge sits on that side (then the other side speaks for the surface).
float GtaoSmallerDifference(float forward, float backward)
{
	return abs(forward) < abs(backward) ? forward : backward;
}

/// World-space geometric normal of a full-resolution pixel from its depth neighbourhood
/// (edge-aware and guided by the shading normal, as the main pass reconstructs it); the
/// shading normal (the fallback) where the neighbourhood is all edges.
vec3 GtaoPixelGeometricNormal(ivec2 texel, float view_depth, vec4 edges, float z_l, float z_r, float z_t, float z_b, vec3 fallback)
{
	vec2 uv = (vec2(texel) + vec2_splat(0.5)) * u_gtao_full_size.zw;
	vec2 uv_l = (vec2(texel + ivec2(-1, 0)) + vec2_splat(0.5)) * u_gtao_full_size.zw;
	vec2 uv_r = (vec2(texel + ivec2(1, 0)) + vec2_splat(0.5)) * u_gtao_full_size.zw;
	vec2 uv_t = (vec2(texel + ivec2(0, -1)) + vec2_splat(0.5)) * u_gtao_full_size.zw;
	vec2 uv_b = (vec2(texel + ivec2(0, 1)) + vec2_splat(0.5)) * u_gtao_full_size.zw;
	vec3 position = GtaoViewPosition(uv, view_depth);
	vec3 guide = normalize(mul(u_view, vec4(fallback, 0.0)).xyz);
	vec3 geometric = GtaoNormalFromDepth(edges, position,
	                                     GtaoViewPosition(uv_l, z_l), GtaoViewPosition(uv_r, z_r),
	                                     GtaoViewPosition(uv_t, z_t), GtaoViewPosition(uv_b, z_b),
	                                     guide);
	if(dot(geometric, -position) < 0.0)
	{
		geometric = -geometric;
	}
	if(dot(edges, vec4_splat(1.0)) < 0.5 || dot(geometric, geometric) < 0.5)
	{
		return fallback;
	}
	return normalize(mul(u_invView, vec4(geometric, 0.0)).xyz);
}

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
	ivec2 full_size = ivec2(u_gtao_full_size.xy);
	if(any(greaterThanEqual(texel, full_size)))
	{
		return;
	}
	ivec2 ao_size = ivec2(u_gtao_size.xy);
	ivec2 full_max = full_size - ivec2(1, 1);
	vec3 pixel_normal = GtaoWorldNormalTexel(s_gtao_normal, texel);
	float view_depth = GtaoFullViewDepth(texel, full_max);
	if(view_depth >= GTAO_SKY_DEPTH * 0.5)
	{
		imageStore(i_gtao_out, texel, GtaoEncode(pixel_normal, 1.0));
		return;
	}
	float z_l = GtaoFullViewDepth(texel + ivec2(-1, 0), full_max);
	float z_r = GtaoFullViewDepth(texel + ivec2(1, 0), full_max);
	float z_t = GtaoFullViewDepth(texel + ivec2(0, -1), full_max);
	float z_b = GtaoFullViewDepth(texel + ivec2(0, 1), full_max);
	// The pixel's depth edges: the detail term needs a surface around the pixel (a rim keeps
	// three continuous sides; a wire or a corner has two and gets none).
	vec4 edges = GtaoCalculateEdges(view_depth, z_l, z_r, z_t, z_b);
	float detail_scale = dot(edges, vec4_splat(1.0)) >= GTAO_DETAIL_MIN_CONTINUOUS_SIDES ? 1.0 : 0.0;
	// The normal the AO was computed with: the geometric one under the generated-normals
	// source (reconstructed here from the same depth), otherwise the texel's shading normal.
	bool geometric_source = u_gtao_normal_source > 0.5;
	vec3 geometric_normal = geometric_source ? GtaoPixelGeometricNormal(texel, view_depth, edges, z_l, z_r, z_t, z_b, pixel_normal) : pixel_normal;
	if(all(equal(ao_size, full_size)))
	{
		vec4 same = texelFetch(s_gtao_input, texel, 0);
		// At full resolution the AO texel IS the pixel: the detail term only applies for the
		// geometric source (the bent normal then lacks the map).
		same = GtaoApplyDetail(same, pixel_normal, geometric_normal, detail_scale);
		imageStore(i_gtao_out, texel, vec4(same.xyz, saturate(same.a * GTAO_OCCLUSION_TERM_SCALE)));
		return;
	}
	// The pixel's depth slope per full-resolution pixel: what its surface plane predicts at
	// each tap, so a grazing surface keeps its own taps and only another surface is rejected.
	float dz_dx = GtaoSmallerDifference(z_r - view_depth, view_depth - z_l);
	float dz_dy = GtaoSmallerDifference(z_b - view_depth, view_depth - z_t);
	// The pixel in AO texel space: AO texel i holds full pixel i * divisor, so the footprint
	// is anchored on that pixel and the bilinear weights follow the pixel's offset in its block.
	ivec2 divisor = ivec2(u_gtao_full_size.xy / u_gtao_size.xy + vec2_splat(0.5));
	vec2 ao_pos = vec2(texel) / vec2(divisor);
	vec2 base = floor(ao_pos);
	vec2 frac_part = ao_pos - base;
	ivec2 base_texel = ivec2(base);
	float depth_sigma = max(view_depth * u_gtao_depth_sigma, 1e-4);
	vec4 sum = vec4_splat(0.0);
	float w_sum = 0.0;
	vec4 nearest = vec4_splat(0.0);
	float nearest_deviation = GTAO_UPSAMPLE_NO_TAP;
	vec3 texel_normal_sum = vec3_splat(0.0);
	vec3 nearest_normal = pixel_normal;
	LOOP
	for(int i = 0; i < 4; ++i)
	{
		ivec2 offset = ivec2(i & 1, (i >> 1) & 1);
		ivec2 tap = clamp(base_texel + offset, ivec2(0, 0), ao_size - ivec2(1, 1));
		float tap_depth = texelFetch(s_gtao_depth_mips, tap, 0).x;
		// A sky tap holds no surface (and, under DXBC, a loop-level continue is avoided).
		if(tap_depth < GTAO_SKY_DEPTH * 0.5)
		{
			vec2 bilinear = mix(vec2_splat(1.0) - frac_part, frac_part, vec2(offset));
			float w_bilinear = bilinear.x * bilinear.y;
			vec2 delta = vec2(GtaoFullTexel(tap) - texel);
			float expected_depth = view_depth + dz_dx * delta.x + dz_dy * delta.y;
			float w_depth = exp(-abs(tap_depth - expected_depth) / depth_sigma);
			vec4 value = texelFetch(s_gtao_input, tap, 0);
			vec3 texel_normal = GtaoWorldNormal(s_gtao_normal, tap);
			float w_normal = smoothstep(0.0, GTAO_UPSAMPLE_NORMAL_COSINE, dot(pixel_normal, texel_normal));
			float w = w_bilinear * w_depth * w_normal;
			sum += value * w;
			texel_normal_sum += texel_normal * w;
			w_sum += w;
			// The stand-in ranks by depth, and a tap of another face (normal cosine below the
			// fade) only when no tap of this face exists.
			float deviation = abs(tap_depth - view_depth) + (w_normal > 0.0 ? 0.0 : 0.5 * GTAO_UPSAMPLE_NO_TAP);
			if(deviation < nearest_deviation)
			{
				nearest_deviation = deviation;
				nearest = value;
				nearest_normal = texel_normal;
			}
		}
	}
	if(nearest_deviation >= GTAO_UPSAMPLE_NO_TAP)
	{
		// Every tap is sky: a structure thinner than the AO grid. Nothing measured it, so it
		// stays open rather than borrowing the sky's (meaningless) bent normal.
		imageStore(i_gtao_out, texel, GtaoEncode(pixel_normal, 1.0));
		return;
	}
	vec4 result = w_sum > 1e-4 ? sum / w_sum : nearest;
	vec3 texel_normal = w_sum > 1e-4 ? texel_normal_sum / w_sum : nearest_normal;
	texel_normal = dot(texel_normal, texel_normal) > 1e-8 ? normalize(texel_normal) : pixel_normal;
	vec3 bent = result.xyz * 2.0 - vec3_splat(1.0);
	vec3 bent_out = dot(bent, bent) > 1e-8 ? normalize(bent) : GtaoDecodeNormal(nearest);
	vec3 ao_normal = geometric_source ? geometric_normal : texel_normal;
	vec4 detailed = GtaoApplyDetail(GtaoEncode(bent_out, result.a), pixel_normal, ao_normal, detail_scale);
	// Back from the stored occlusion-term scale to the [0, 1] the lighting consumes.
	imageStore(i_gtao_out, texel, vec4(detailed.xyz, saturate(detailed.a * GTAO_OCCLUSION_TERM_SCALE)));
}
