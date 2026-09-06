/*
 * GTAO main pass: per AO-resolution texel, the cosine-weighted visibility integral over
 * `slice_count` slices through the view vector, each slice's two horizons found by
 * `steps_per_slice` samples of the prefiltered view-depth mips along +/- the slice
 * direction (Jimenez et al. 2016, "Practical Realtime Strategies for Accurate Indirect
 * Occlusion"; structure after Intel's XeGTAO). Also integrates the bent normal - the
 * mean unoccluded direction - which the lighting uses to steer its diffuse lookups.
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

/// The slice integral's arc terms (the closed form of the cosine-weighted visibility
/// between the normal-relative angles h and n) and the bent-normal moments, as derived
/// in the GTAO paper's supplemental material.
float GtaoArcIntegral(float h, float n, float cos_n)
{
	return 0.25 * (cos_n + 2.0 * h * sin(n) - cos(2.0 * h - n));
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
	float thin_compensation = u_gtao_thin_compensation;
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
		// Horizons start at the normal-relative hemisphere bounds.
		float low_horizon_cos0 = cos(n + GTAO_HALF_PI);
		float low_horizon_cos1 = cos(n - GTAO_HALF_PI);
		float horizon_cos0 = low_horizon_cos0;
		float horizon_cos1 = low_horizon_cos1;
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
			float shc0 = dot(delta0, view_vec) / max(dist0, 1e-5);
			float shc1 = dot(delta1, view_vec) / max(dist1, 1e-5);
			// Falloff over the radius; XeGTAO's thickness heuristic stretches the depth axis
			// so samples well in front of (or behind) the receiver drop out sooner - a thin
			// occluder then shades less like a wall. 0 = the plain distance.
			float falloff0 = length(vec3(delta0.xy, delta0.z * (1.0 + thin_compensation)));
			float falloff1 = length(vec3(delta1.xy, delta1.z * (1.0 + thin_compensation)));
			float weight0 = saturate(falloff0 * falloff_mul + falloff_add);
			float weight1 = saturate(falloff1 * falloff_mul + falloff_add);
			shc0 = mix(low_horizon_cos0, shc0, weight0);
			shc1 = mix(low_horizon_cos1, shc1, weight1);
			horizon_cos0 = max(horizon_cos0, shc0);
			horizon_cos1 = max(horizon_cos1, shc1);
		}
		// XeGTAO's fudge for a slight over-darkening on steep slopes.
		projected_length = mix(projected_length, 1.0, 0.05);
		// The +omega side (horizon 0) bounds the positive arc, the -omega side the negative.
		float h0 = -acos(clamp(horizon_cos1, -1.0, 1.0));
		float h1 = acos(clamp(horizon_cos0, -1.0, 1.0));
		h0 = n + clamp(h0 - n, -GTAO_HALF_PI, GTAO_HALF_PI);
		h1 = n + clamp(h1 - n, -GTAO_HALF_PI, GTAO_HALF_PI);
		float local_visibility = projected_length * (GtaoArcIntegral(h0, n, cos_n) + GtaoArcIntegral(h1, n, cos_n));
		visibility += local_visibility;
		// Bent normal moments in the slice frame: t0 along the slice direction, t1 along the
		// view vector (the frame the arc integral was evaluated in).
		float t0 = (6.0 * sin(h0 - n) - sin(3.0 * h0 - n) + 6.0 * sin(h1 - n) - sin(3.0 * h1 - n) +
		            16.0 * sin(n) - 3.0 * (sin(h0 + n) + sin(h1 + n))) / 12.0;
		float t1 = (-cos(3.0 * h0 - n) - cos(3.0 * h1 - n) + 8.0 * cos(n) - 3.0 * (cos(h0 + n) + cos(h1 + n))) / 12.0;
		vec3 slice_dir = normalize(ortho_direction);
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
