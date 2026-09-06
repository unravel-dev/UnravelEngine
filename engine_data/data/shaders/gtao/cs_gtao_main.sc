/*
 * GTAO main pass: per AO-resolution texel, the cosine-weighted visibility integral over
 * `slice_count` slices through the view vector (Jimenez et al. 2016, "Practical Realtime
 * Strategies for Accurate Indirect Occlusion"; structure after Intel's XeGTAO), each slice
 * searched by `steps_per_slice` samples of the prefiltered
 * view-depth mips along +/- the slice direction. Two integrations of one search:
 *  - the two-horizon closed form (default): the largest horizon angle on
 *    each side bounds the visible arc; the bent normal - the mean unoccluded direction the
 *    lighting steers its lookups and specular occlusion by - comes from the arc's moments;
 *  - the VISIBILITY BITMASK (Therrien, Levesque, Gilet 2023; u_gtao_bitmask): the projected
 *    normal's hemisphere split into GTAO_SECTOR_COUNT sectors, each sample marking the
 *    sectors between its front face and the face one slab depth behind it, so what lies
 *    beyond a thin occluder stays visible; the open sectors give visibility and bent normal.
 * The search extent is a world radius with a distance falloff, capped on screen (XeGTAO).
 *
 * Output (RGBA8): rgb = world-space bent normal * 0.5 + 0.5, a = visibility.
 */

#include "../bgfx_compute.sh"
#include "../common.sh"
#include "gtao_common.sh"

/// Prefiltered view-space depth, GTAO_DEPTH_MIP_LEVELS mips, point sampled.
SAMPLER2D(s_gtao_depth_mips, 0);
/// Full-resolution G-buffer normal / metalness / roughness.
SAMPLER2D(s_gtao_normal, 1);
IMAGE2D_WO(i_gtao_out, rgba8, 2);

/// The slice integral's arc term (the closed form of the cosine-weighted visibility between
/// the normal-relative angles h and n) and the bent-normal moments, as derived in the GTAO
/// paper's supplemental material: the fast path for a slice no sample touched.
float GtaoArcIntegral(float h, float n, float cos_n)
{
	return 0.25 * (cos_n + 2.0 * h * sin(n) - cos(2.0 * h - n));
}

/// The sectors one sample occludes: the slab between the sample (its front face) and the
/// point `thickness` behind it along the view direction (its back face), as an angle range
/// about the view vector, signed by the slice side, mapped onto the projected normal's
/// hemisphere [hemisphere_start, hemisphere_start + pi). The falloff weight pulls both faces
/// to the hemisphere edge on that side, so a sample at the radius marks nothing (GTAO's
/// horizon blend, in angle). A sample below the tangent plane maps past the last sector
/// and marks nothing either.
uint GtaoSlabSectors(vec3 delta, float dist, vec3 view_vec, float thickness, float weight,
                     float hemisphere_start, float side, float n)
{
	vec3 back = delta - view_vec * thickness;
	float cos_front = dot(delta, view_vec) / max(dist, 1e-5);
	float cos_back = dot(back, view_vec) / max(length(back), 1e-5);
	float theta_front = side * GtaoFastAcos(clamp(cos_front, -1.0, 1.0));
	float theta_back = side * GtaoFastAcos(clamp(cos_back, -1.0, 1.0));
	float bound = n + side * GTAO_HALF_PI;
	theta_front = mix(bound, theta_front, weight);
	theta_back = mix(bound, theta_back, weight);
	float sector_angle = GTAO_PI / float(GTAO_SECTOR_COUNT);
	float lo = (min(theta_front, theta_back) - hemisphere_start) / sector_angle;
	float hi = (max(theta_front, theta_back) - hemisphere_start) / sector_angle;
	uint first = uint(clamp(floor(lo), 0.0, float(GTAO_SECTOR_COUNT)));
	uint last = uint(clamp(ceil(hi), 0.0, float(GTAO_SECTOR_COUNT)));
	if(last <= first)
	{
		return 0u;
	}
	uint count = last - first;
	uint bits = count >= 32u ? 0xFFFFFFFFu : ((1u << count) - 1u);
	return bits << first;
}

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = ivec2(u_gtao_size.xy);
	if(any(greaterThanEqual(texel, size)))
	{
		return;
	}
	vec2 uv = (vec2(texel) + vec2_splat(0.5)) * u_gtao_size.zw;
	float view_depth = texelFetch(s_gtao_depth_mips, texel, 0).x;
	vec3 world_normal = GtaoWorldNormal(s_gtao_normal, texel);
	if(view_depth >= GTAO_SKY_DEPTH * 0.5)
	{
		imageStore(i_gtao_out, texel, GtaoEncode(world_normal, 1.0));
		return;
	}
	// The four neighbours' depths: the depth edges (also what the denoiser weights by) and,
	// with GTAO_NORMALS_FROM_DEPTH, the geometric normal of this texel.
	ivec2 size_max = size - ivec2(1, 1);
	vec2 uv_l = (vec2(texel + ivec2(-1, 0)) + vec2_splat(0.5)) * u_gtao_size.zw;
	vec2 uv_r = (vec2(texel + ivec2(1, 0)) + vec2_splat(0.5)) * u_gtao_size.zw;
	vec2 uv_t = (vec2(texel + ivec2(0, -1)) + vec2_splat(0.5)) * u_gtao_size.zw;
	vec2 uv_b = (vec2(texel + ivec2(0, 1)) + vec2_splat(0.5)) * u_gtao_size.zw;
	float z_l = texelFetch(s_gtao_depth_mips, clamp(texel + ivec2(-1, 0), ivec2(0, 0), size_max), 0).x;
	float z_r = texelFetch(s_gtao_depth_mips, clamp(texel + ivec2(1, 0), ivec2(0, 0), size_max), 0).x;
	float z_t = texelFetch(s_gtao_depth_mips, clamp(texel + ivec2(0, -1), ivec2(0, 0), size_max), 0).x;
	float z_b = texelFetch(s_gtao_depth_mips, clamp(texel + ivec2(0, 1), ivec2(0, 0), size_max), 0).x;
	vec4 edges = GtaoCalculateEdges(view_depth, z_l, z_r, z_t, z_b);
	// Pull the receiver toward the camera by a hair: its own surface's depth noise must never
	// register as an occluder (XeGTAO; the value for an FP32 depth buffer).
	view_depth *= GTAO_DEPTH_BIAS;
	vec3 position = GtaoAoViewPosition(uv, view_depth);
	vec3 view_vec = normalize(-position);
	// The receiver normal (see u_gtao_params3): the G-buffer shading normal carries the
	// normal map and gives the bump-scale response; the geometric one is reconstructed from
	// the four neighbours, edge-aware and guided by the shading normal to the pixel's own
	// face, and falls back to the G-buffer where the neighbourhood is all edges.
	vec3 gbuffer_normal = normalize(mul(u_view, vec4(world_normal, 0.0)).xyz);
	vec3 normal = gbuffer_normal;
	if(u_gtao_normal_source > 0.5)
	{
		vec3 geometric = GtaoNormalFromDepth(edges, position,
		                                     GtaoAoViewPosition(uv_l, z_l), GtaoAoViewPosition(uv_r, z_r),
		                                     GtaoAoViewPosition(uv_t, z_t), GtaoAoViewPosition(uv_b, z_b),
		                                     gbuffer_normal);
		if(dot(geometric, view_vec) < 0.0)
		{
			geometric = -geometric;
		}
		if(dot(edges, vec4_splat(1.0)) >= 0.5 && dot(geometric, geometric) > 0.5)
		{
			normal = geometric;
		}
	}
	// The radius in AO-resolution pixels: the projection's y scale over the view depth.
	float pixels_per_unit = 0.5 * u_gtao_size.y * u_proj[1][1] / max(view_depth, 1e-4);
	float radius = u_gtao_radius;
	float screen_radius = radius * pixels_per_unit;
	float max_screen_radius = u_gtao_max_screen_radius * u_gtao_size.y;
	if(screen_radius > max_screen_radius)
	{
		// Very close geometry: shrink the world radius so the march stays on screen.
		radius *= max_screen_radius / screen_radius;
		screen_radius = max_screen_radius;
	}
	// Fade toward unoccluded for tiny screen radii (XeGTAO), then stop below a pixel.
	float visibility = saturate((10.0 - screen_radius) / 100.0) * 0.5;
	if(screen_radius < GTAO_PIXEL_TOO_CLOSE)
	{
		imageStore(i_gtao_out, texel, GtaoEncode(world_normal, saturate(1.0 / GTAO_OCCLUSION_TERM_SCALE)));
		return;
	}
	// An occluder's influence fades over the outer falloff_range of the radius.
	float falloff_range = u_gtao_falloff_range * radius;
	float falloff_from = radius * (1.0 - u_gtao_falloff_range);
	float falloff_mul = -1.0 / max(falloff_range, 1e-5);
	float falloff_add = falloff_from / max(falloff_range, 1e-5) + 1.0;
	// Noise: Hilbert-curve R2, advanced per frame (XeGTAO's spatiotemporal noise); the slice
	// angle and the step offset use the two lanes.
	vec2 noise = GtaoSpatioTemporalNoise(texel, u_gtao_noise_index);
	float noise_slice = noise.x;
	float noise_sample = noise.y;
	int slice_count = int(u_gtao_slice_count + 0.5);
	int steps_per_slice = int(u_gtao_steps_per_slice + 0.5);
	float min_s = GTAO_PIXEL_TOO_CLOSE / screen_radius;
	bool use_bitmask = u_gtao_bitmask > 0.5;
	// The bitmask's slab depth as a fraction of the (possibly screen-clamped) radius, so a
	// scene keeps its look when the radius changes.
	float occluder_thickness = u_gtao_occluder_thickness * radius;
	float sector_angle = GTAO_PI / float(GTAO_SECTOR_COUNT);
	vec3 bent_normal = vec3_splat(0.0);
	LOOP
	for(int slice = 0; slice < slice_count; ++slice)
	{
		float slice_k = (float(slice) + noise_slice) / float(slice_count);
		float phi = slice_k * GTAO_PI;
		vec2 omega = vec2(cos(phi), sin(phi));
		// The slice direction in view space: the offset between two view positions at the
		// SAME depth one texel apart along omega (exact for the backend's screen mapping).
		vec3 direction_vec = normalize(GtaoAoViewPosition(uv + omega * u_gtao_size.zw, view_depth) - position);
		vec3 ortho_direction = direction_vec - dot(direction_vec, view_vec) * view_vec;
		vec3 axis = normalize(cross(ortho_direction, view_vec));
		vec3 projected_normal = normal - axis * dot(normal, axis);
		float projected_length = length(projected_normal);
		if(projected_length < 1e-4)
		{
			continue;
		}
		float sign_n = sign(dot(ortho_direction, projected_normal));
		float cos_n = saturate(dot(projected_normal, view_vec) / projected_length);
		float n = sign_n * acos(cos_n);
		// Horizons start at the normal-relative hemisphere bounds; the sectors cover that
		// hemisphere and nothing is occluded yet.
		float low_horizon_cos0 = cos(n + GTAO_HALF_PI);
		float low_horizon_cos1 = cos(n - GTAO_HALF_PI);
		float horizon_cos0 = low_horizon_cos0;
		float horizon_cos1 = low_horizon_cos1;
		float hemisphere_start = n - GTAO_HALF_PI;
		uint occluded = 0u;
		LOOP
		for(int step = 0; step < steps_per_slice; ++step)
		{
			// Progressive per-step noise (R2 over slice x step) on top of the per-pixel lane.
			float step_noise = fract(noise_sample + float(slice + step * steps_per_slice) * 0.6180339887498948);
			float s = (float(step) + step_noise) / float(steps_per_slice);
			s = pow(s, u_gtao_distribution_power) + min_s;
			vec2 sample_offset_px = omega * (s * screen_radius);
			// Snap to texel centres so the depth read is the texel's own, then read the mip
			// whose footprint matches the step spacing.
			sample_offset_px = round(sample_offset_px);
			float sample_length = length(sample_offset_px);
			float mip = clamp(log2(max(sample_length, 1.0)) - u_gtao_mip_offset, 0.0, float(GTAO_DEPTH_MIP_LEVELS - 1));
			vec2 sample_offset_uv = sample_offset_px * u_gtao_size.zw;
			vec2 uv0 = uv + sample_offset_uv;
			vec2 uv1 = uv - sample_offset_uv;
			float depth0 = texture2DLod(s_gtao_depth_mips, uv0, mip).x;
			float depth1 = texture2DLod(s_gtao_depth_mips, uv1, mip).x;
			vec3 delta0 = GtaoAoViewPosition(uv0, depth0) - position;
			vec3 delta1 = GtaoAoViewPosition(uv1, depth1) - position;
			float dist0 = length(delta0);
			float dist1 = length(delta1);
			// The sample's weight over the radius: 1 counts in full, 0 leaves the horizon /
			// sectors untouched.
			float weight0 = saturate(dist0 * falloff_mul + falloff_add);
			float weight1 = saturate(dist1 * falloff_mul + falloff_add);
			if(use_bitmask)
			{
				occluded |= GtaoSlabSectors(delta0, dist0, view_vec, occluder_thickness, weight0, hemisphere_start, 1.0, n);
				occluded |= GtaoSlabSectors(delta1, dist1, view_vec, occluder_thickness, weight1, hemisphere_start, -1.0, n);
			}
			else
			{
				float shc0 = dot(delta0, view_vec) / max(dist0, 1e-5);
				float shc1 = dot(delta1, view_vec) / max(dist1, 1e-5);
				shc0 = mix(low_horizon_cos0, shc0, weight0);
				shc1 = mix(low_horizon_cos1, shc1, weight1);
				horizon_cos0 = max(horizon_cos0, shc0);
				horizon_cos1 = max(horizon_cos1, shc1);
			}
		}
		// XeGTAO's fudge for a slight over-darkening on steep slopes.
		projected_length = mix(projected_length, 1.0, 0.05);
		vec3 slice_dir = normalize(ortho_direction);
		float sin_n = sin(n);
		float slice_visibility = 0.0;
		// Bent-normal moments in the slice frame: t0 along the slice direction, t1 along the
		// view vector.
		float t0 = 0.0;
		float t1 = 0.0;
		if(!use_bitmask || occluded == 0u)
		{
			// The closed-form arc between the two horizons (the whole hemisphere for a bitmask
			// slice no sample touched); the +omega side (horizon 0) bounds the positive arc.
			float h0 = use_bitmask ? n - GTAO_HALF_PI : -acos(clamp(horizon_cos1, -1.0, 1.0));
			float h1 = use_bitmask ? n + GTAO_HALF_PI : acos(clamp(horizon_cos0, -1.0, 1.0));
			h0 = n + clamp(h0 - n, -GTAO_HALF_PI, GTAO_HALF_PI);
			h1 = n + clamp(h1 - n, -GTAO_HALF_PI, GTAO_HALF_PI);
			slice_visibility = GtaoArcIntegral(h0, n, cos_n) + GtaoArcIntegral(h1, n, cos_n);
			t0 = (6.0 * sin(h0 - n) - sin(3.0 * h0 - n) + 6.0 * sin(h1 - n) - sin(3.0 * h1 - n) +
			      16.0 * sin_n - 3.0 * (sin(h0 + n) + sin(h1 + n))) / 12.0;
			t1 = (-cos(3.0 * h0 - n) - cos(3.0 * h1 - n) + 8.0 * cos_n - 3.0 * (cos(h0 + n) + cos(h1 + n))) / 12.0;
		}
		else if(occluded != 0xFFFFFFFFu)
		{
			// The open sectors, each with GTAO's integrand cos(theta - n) |sin theta| over its
			// angle (the midpoint rule). With theta = n + alpha and alpha the sector's constant
			// offset from the normal, cos(theta - n) = cos(alpha) and sin / cos(theta) are angle
			// sums of the per-slice sin n / cos n: no trig per sector.
			UNROLL
			for(int sector = 0; sector < GTAO_SECTOR_COUNT; ++sector)
			{
				float alpha = (float(sector) + 0.5) * sector_angle - GTAO_HALF_PI;
				float cos_alpha = cos(alpha);
				float sin_alpha = sin(alpha);
				float open = ((occluded >> uint(sector)) & 1u) == 0u ? 1.0 : 0.0;
				float sin_theta = sin_n * cos_alpha + cos_n * sin_alpha;
				float cos_theta = cos_n * cos_alpha - sin_n * sin_alpha;
				float sector_weight = open * cos_alpha * abs(sin_theta) * sector_angle;
				slice_visibility += sector_weight;
				t0 += sector_weight * sin_theta;
				t1 += sector_weight * cos_theta;
			}
		}
		visibility += projected_length * slice_visibility;
		bent_normal += (slice_dir * t0 + view_vec * t1) * projected_length;
	}
	visibility /= float(slice_count);
	// Not saturated before the power: the raw term overshoots 1 under noise and the stored
	// scale below lets the denoise and temporal average that out instead of clipping it.
	visibility = pow(max(visibility, 0.0), u_gtao_final_power);
	visibility = max(GTAO_MIN_VISIBILITY, visibility);
	vec3 bent_view = normal;
	if(dot(bent_normal, bent_normal) > 1e-8)
	{
		bent_view = normalize(bent_normal);
	}
	vec3 bent_world = normalize(mul(u_invView, vec4(bent_view, 0.0)).xyz);
	imageStore(i_gtao_out, texel, GtaoEncode(bent_world, saturate(visibility / GTAO_OCCLUSION_TERM_SCALE)));
}
