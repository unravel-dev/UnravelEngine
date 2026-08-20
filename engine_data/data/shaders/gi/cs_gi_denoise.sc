/*
 * Edge-preserving spatial filter for the surface cache gather (a-trous wavelet) - the
 * COMPUTE form of fs_gi_denoise.sc, same math, different memory shape.
 *
 * The fragment form paid three texture fetches per tap (input, depth, normal) plus a
 * normal decode-and-normalize, 24 taps per pixel per pass - measured fetch-bound at 4K.
 * Here each 8x8 tile stages its whole tap footprint into shared memory once: the input
 * colour and a packed guide (normalized normal + device depth), with the decode amortized
 * per STAGED TEXEL instead of per tap. Taps then read shared memory only. At step 4 a tile
 * stages 24x24 texels for 64 pixels - 9 staged texels per pixel against the 72 fetches per
 * pixel per pass the fragment form issued.
 *
 * Out-of-image staged texels carry the sky sentinel (guide.w = 1), which the tap loop
 * already skips - the exact set of taps the fragment form's uv-bounds test rejected.
 * Everything else transcribes fs_gi_denoise.sc line for line; keep the two in step.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "../lighting.sh"

SAMPLER2D(s_gi_input, 0);
SAMPLER2D(s_gi_depth, 1);
SAMPLER2D(s_gi_normal, 2);
SAMPLER2D(s_gi_moments, 3);
IMAGE2D_WO(s_gi_denoise_out, rgba16f, 4);

/// x = tap spacing in texels, y = normal exponent, z = plane tolerance as a fraction of view
/// distance, w = luminance edge-stop strength.
uniform vec4 u_gi_denoise_params;
#define u_gi_denoise_step       u_gi_denoise_params.x
#define u_gi_denoise_normal_pow u_gi_denoise_params.y
#define u_gi_denoise_plane_tol  u_gi_denoise_params.z
#define u_gi_denoise_luma_phi   u_gi_denoise_params.w

/// xy = one texel of the filtered buffer, zw = its dimensions.
uniform vec4 u_gi_denoise_texel;

/// x = tolerance multiplier applied at one accumulated sample, decaying to 1 as the count grows.
/// y = the accumulation cap when the converged early-out is enabled, 0 to disable it.
/// z = luminance-stop floor as a fraction of the centre's own luminance (the
/// coherent-structure floor; 0 restores the pure variance-driven stop).
uniform vec4 u_gi_denoise_params2;
#define u_gi_denoise_low_count_boost u_gi_denoise_params2.x
#define u_gi_denoise_converged_cap   u_gi_denoise_params2.y
#define u_gi_denoise_luma_floor      u_gi_denoise_params2.z

uniform vec4 u_gi_denoise_camera;

#define DENOISE_TILE 8
/// Largest supported spacing is 4 (three a-trous passes): footprint 8 + 2 * (2 * 4) = 24.
#define DENOISE_MAX_STAGE_EDGE 24

SHARED vec4 s_stage_color[DENOISE_MAX_STAGE_EDGE * DENOISE_MAX_STAGE_EDGE];
/// xyz = normalized world normal, zero when the G-buffer's is degenerate; w = device depth,
/// >= 1 for sky and for staged texels outside the image.
SHARED vec4 s_stage_guide[DENOISE_MAX_STAGE_EDGE * DENOISE_MAX_STAGE_EDGE];

NUM_THREADS(DENOISE_TILE, DENOISE_TILE, 1)
void main()
{
	int step_i = int(u_gi_denoise_step + 0.5);
	int reach = 2 * step_i;
	int stage_edge = DENOISE_TILE + 2 * reach;
	ivec2 image_size = ivec2(u_gi_denoise_texel.zw);
	ivec2 stage_base = ivec2(gl_WorkGroupID.xy) * DENOISE_TILE - ivec2(reach, reach);
	int lane = int(gl_LocalInvocationID.y) * DENOISE_TILE + int(gl_LocalInvocationID.x);
	int stage_count = stage_edge * stage_edge;
	LOOP
	for(int i = lane; i < stage_count; i += DENOISE_TILE * DENOISE_TILE)
	{
		ivec2 p = stage_base + ivec2(i % stage_edge, i / stage_edge);
		vec4 color = vec4_splat(0.0);
		vec4 guide = vec4(0.0, 0.0, 0.0, 1.0);
		if(all(greaterThanEqual(p, ivec2(0, 0))) && all(lessThan(p, image_size)))
		{
			vec2 p_uv = (vec2(p) + vec2_splat(0.5)) * u_gi_denoise_texel.xy;
			color = texture2DLod(s_gi_input, p_uv, 0.0);
			float z = texture2DLod(s_gi_depth, p_uv, 0.0).x;
			guide.w = z;
			if(z < 1.0)
			{
				vec3 n = DecodeGBufferNormalMetalRoughnessLod(p_uv, s_gi_normal, 0.0).world_normal;
				if(dot(n, n) >= 0.5)
				{
					guide.xyz = normalize(n);
				}
			}
		}
		s_stage_color[i] = color;
		s_stage_guide[i] = guide;
	}
	barrier();
	ivec2 pixel = ivec2(gl_WorkGroupID.xy) * DENOISE_TILE + ivec2(gl_LocalInvocationID.xy);
	if(pixel.x >= image_size.x || pixel.y >= image_size.y)
	{
		return;
	}
	ivec2 center_local = ivec2(gl_LocalInvocationID.xy) + ivec2(reach, reach);
	int center_index = center_local.y * stage_edge + center_local.x;
	vec4 center = s_stage_color[center_index];
	vec4 center_guide = s_stage_guide[center_index];
	// Sky, or a degenerate normal: nothing to reconstruct from - pass the input through,
	// exactly as the fragment form does.
	if(center_guide.w >= 1.0)
	{
		imageStore(s_gi_denoise_out, pixel, center);
		return;
	}
	vec3 center_normal = center_guide.xyz;
	if(dot(center_normal, center_normal) < 0.5)
	{
		imageStore(s_gi_denoise_out, pixel, center);
		return;
	}
	vec2 uv = (vec2(pixel) + vec2_splat(0.5)) * u_gi_denoise_texel.xy;
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(center_guide.w)));
	vec3 center_position = clipToWorld(u_invViewProj, clip);
	float view_distance = max(length(center_position - u_gi_denoise_camera.xyz), 1e-4);
	float plane_tolerance = max(u_gi_denoise_plane_tol * view_distance, 1e-4);
	bool use_luma_stop = u_gi_denoise_luma_phi > 0.0;
	vec4 moments = texture2DLod(s_gi_moments, uv, 0.0);
	float count = max(moments.z, 1.0);
	float variance = max(moments.y - moments.x * moments.x, 0.0);
	float luma_sigma = u_gi_denoise_luma_phi * sqrt(variance / count) + 1e-4;
	luma_sigma *= max(u_gi_denoise_low_count_boost / count, 1.0);
	float center_luma = Luminance(center.xyz);
	// Coherent-structure floor - see the fragment form's note; keep the two in step.
	luma_sigma = max(luma_sigma, u_gi_denoise_luma_floor * max(center_luma, 1e-3));
	BRANCH
	if(u_gi_denoise_converged_cap > 0.0)
	{
		if(count >= u_gi_denoise_converged_cap && luma_sigma < 0.002 * max(center_luma, 1e-3))
		{
			imageStore(s_gi_denoise_out, pixel, center);
			return;
		}
	}
	// One mat4 fold per pixel; taps evaluate the centre's plane with two dot4s (see the
	// fragment form's derivation).
	vec4 plane_row = mul(vec4(center_normal, 0.0), u_invViewProj);
	vec4 w_row = mul(vec4(0.0, 0.0, 0.0, 1.0), u_invViewProj);
	float center_plane_height = dot(center_normal, center_position);
	float kernel[3];
	kernel[0] = 3.0 / 8.0;
	kernel[1] = 1.0 / 4.0;
	kernel[2] = 1.0 / 16.0;
	vec4 sum = center * kernel[0] * kernel[0];
	float weight_sum = kernel[0] * kernel[0];
	LOOP
	for(int y = -2; y <= 2; ++y)
	{
		LOOP
		for(int x = -2; x <= 2; ++x)
		{
			if(x == 0 && y == 0)
			{
				continue;
			}
			ivec2 tap_local = center_local + ivec2(x, y) * step_i;
			int tap_index = tap_local.y * stage_edge + tap_local.x;
			vec4 tap_guide = s_stage_guide[tap_index];
			// Sky and out-of-image staged texels both carry w >= 1: the same rejects the
			// fragment form applied through its uv-bounds and depth tests.
			if(tap_guide.w >= 1.0)
			{
				continue;
			}
			vec3 tap_normal = tap_guide.xyz;
			if(dot(tap_normal, tap_normal) < 0.5)
			{
				continue;
			}
			vec2 tap_uv = uv + vec2(float(x), float(y)) * u_gi_denoise_step * u_gi_denoise_texel.xy;
			vec4 tap_h =
			    vec4(clipTransform(vec3(tap_uv * 2.0 - 1.0, toClipSpaceDepth(tap_guide.w))), 1.0);
			float plane_distance = abs(dot(plane_row, tap_h) / dot(w_row, tap_h) - center_plane_height);
			float ndotn = max(dot(center_normal, tap_normal), 0.0);
			float normal_weight;
			if(u_gi_denoise_normal_pow == 32.0)
			{
				float n2 = ndotn * ndotn;
				float n4 = n2 * n2;
				float n8 = n4 * n4;
				float n16 = n8 * n8;
				normal_weight = n16 * n16;
			}
			else
			{
				normal_weight = pow(ndotn, u_gi_denoise_normal_pow);
			}
			vec4 tap_value = s_stage_color[tap_index];
			// The plane and luminance stops share one exponential: exp(-a) * exp(-b) is
			// exp(-(a + b)) exactly, and the transcendental count per tap is what this pass
			// is actually bound by (measured: LDS staging barely moved it).
			float attenuation = plane_distance / plane_tolerance;
			if(use_luma_stop)
			{
				attenuation += abs(center_luma - Luminance(tap_value.xyz)) / luma_sigma;
			}
			float spatial_weight = kernel[abs(x)] * kernel[abs(y)];
			float weight = spatial_weight * normal_weight * exp(-attenuation);
			sum += tap_value * weight;
			weight_sum += weight;
		}
	}
	imageStore(s_gi_denoise_out, pixel, sum / max(weight_sum, 1e-6));
}
