/*
 * GI a-trous denoise as an 8x8 compute group (the SSIL spatial-denoise layout).
 *
 * Pass 1 always runs the 5x5. Passes 2-3 skip the kernel when EVERY texel is
 * temporally settled AND the tile's accumulated-mean luma (moments.x) is already
 * flat: the tile copies input to output and the wave returns. Spatial tests use
 * the mean, not the noisy gather - kernel taps on input never skipped.
 *
 * Passes whose 5x5 reach is at least one screen-probe spacing (pass 4+, step 8+)
 * always filter so extra passes still mix the 16px probe grid.
 *
 * Skip requires count >= GI_DENOISE_SKIP_MIN_COUNT so a freshly rejected history
 * (moments variance 0, count 1 - the SSIL blotch case) never skips.
 */

#include "../bgfx_compute.sh"
#include "../common.sh"
#include "../lighting.sh"
#include "gi/gi_constants.sh"

SAMPLER2D(s_gi_input, 0);
IMAGE2D_WO(s_gi_output, rgba16f, 1);
SAMPLER2D(s_gi_depth, 2);
SAMPLER2D(s_gi_normal, 3);
SAMPLER2D(s_gi_moments, 4);

uniform vec4 u_gi_denoise_params;
#define u_gi_denoise_step       u_gi_denoise_params.x
#define u_gi_denoise_normal_pow u_gi_denoise_params.y
#define u_gi_denoise_plane_tol  u_gi_denoise_params.z
#define u_gi_denoise_luma_phi   u_gi_denoise_params.w

uniform vec4 u_gi_denoise_texel;
uniform vec4 u_gi_denoise_params2;
#define u_gi_denoise_low_count_boost u_gi_denoise_params2.x
#define u_gi_denoise_skip            (u_gi_denoise_params2.y > 0.5)
uniform vec4 u_gi_denoise_camera;

#define GI_DENOISE_TILE_THREADS 64

SHARED int s_needs_filter;
SHARED float s_mean_min[GI_DENOISE_TILE_THREADS];
SHARED float s_mean_max[GI_DENOISE_TILE_THREADS];

bool GiWorldAt(vec2 uv, out vec3 out_position)
{
	float depth = texture2DLod(s_gi_depth, uv, 0.0).x;
	out_position = vec3_splat(0.0);
	if(depth >= 1.0)
	{
		return false;
	}
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(depth)));
	out_position = clipToWorld(u_invViewProj, clip);
	return true;
}

bool GiDenoisePixelSettled(vec4 moments)
{
	float count = max(moments.z, 1.0);
	if(count < float(GI_DENOISE_SKIP_MIN_COUNT))
	{
		return false;
	}
	float variance = max(moments.y - moments.x * moments.x, 0.0);
	float std_error = sqrt(variance / count);
	float limit = GI_DENOISE_SKIP_RELATIVE * max(moments.x, GI_DENOISE_SKIP_LUMA_FLOOR);
	return std_error <= limit;
}

NUM_THREADS(GI_DENOISE_TILE_EDGE, GI_DENOISE_TILE_EDGE, 1)
void main()
{
	ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = ivec2(int(u_gi_denoise_texel.z), int(u_gi_denoise_texel.w));
	bool in_bounds = all(lessThan(coord, size));
	vec2 uv = (vec2(coord) + vec2_splat(0.5)) * u_gi_denoise_texel.xy;
	uint lid = gl_LocalInvocationIndex;
	vec4 center = vec4_splat(0.0);
	bool skip_enabled = u_gi_denoise_skip &&
	                    (u_gi_denoise_step * 2.0 < float(GI_SCREEN_PROBE_SPACING));
	bool noisy = true;
	if(!in_bounds)
	{
		noisy = false;
		s_mean_min[lid] = 1e10;
		s_mean_max[lid] = -1e10;
	}
	else
	{
		center = texture2DLod(s_gi_input, uv, 0.0);
		if(!skip_enabled)
		{
			s_mean_min[lid] = 1e10;
			s_mean_max[lid] = -1e10;
		}
		else
		{
			vec3 world_position;
			if(!GiWorldAt(uv, world_position))
			{
				noisy = false;
				s_mean_min[lid] = 1e10;
				s_mean_max[lid] = -1e10;
			}
			else
			{
				vec4 moments = texture2DLod(s_gi_moments, uv, 0.0);
				noisy = !GiDenoisePixelSettled(moments);
				s_mean_min[lid] = moments.x;
				s_mean_max[lid] = moments.x;
			}
		}
	}
	if(int(gl_LocalInvocationID.x) == 0 && int(gl_LocalInvocationID.y) == 0)
	{
		s_needs_filter = 0;
	}
	barrier();
	if(noisy)
	{
		s_needs_filter = 1;
	}
	for(uint stride = 32u; stride > 0u; stride >>= 1u)
	{
		barrier();
		if(lid < stride)
		{
			s_mean_min[lid] = min(s_mean_min[lid], s_mean_min[lid + stride]);
			s_mean_max[lid] = max(s_mean_max[lid], s_mean_max[lid + stride]);
		}
	}
	barrier();
	if(skip_enabled && lid == 0u && s_mean_max[0] >= s_mean_min[0])
	{
		float mid = 0.5 * (s_mean_min[0] + s_mean_max[0]);
		float limit = GI_DENOISE_SKIP_RELATIVE * max(mid, GI_DENOISE_SKIP_LUMA_FLOOR);
		if((s_mean_max[0] - s_mean_min[0]) > limit)
		{
			s_needs_filter = 1;
		}
	}
	barrier();
	if(!in_bounds)
	{
		return;
	}
	if(s_needs_filter == 0)
	{
		imageStore(s_gi_output, coord, center);
		return;
	}
	vec3 center_position;
	if(!GiWorldAt(uv, center_position))
	{
		imageStore(s_gi_output, coord, center);
		return;
	}
	GBufferDataNormalMetalRoughness center_nd =
	    DecodeGBufferNormalMetalRoughnessLod(uv, s_gi_normal, 0.0);
	vec3 center_normal = center_nd.world_normal;
	if(dot(center_normal, center_normal) < 0.5)
	{
		imageStore(s_gi_output, coord, center);
		return;
	}
	center_normal = normalize(center_normal);
	float view_distance = max(length(center_position - u_gi_denoise_camera.xyz), 1e-4);
	float plane_tolerance = max(u_gi_denoise_plane_tol * view_distance, 1e-4);
	bool use_luma_stop = u_gi_denoise_luma_phi > 0.0;
	vec4 moments = texture2DLod(s_gi_moments, uv, 0.0);
	float count = max(moments.z, 1.0);
	float variance = max(moments.y - moments.x * moments.x, 0.0);
	float luma_sigma = u_gi_denoise_luma_phi * sqrt(variance / count) + 1e-4;
	luma_sigma *= max(u_gi_denoise_low_count_boost / count, 1.0);
	float center_luma = Luminance(center.xyz);
	float kernel[3];
	kernel[0] = 3.0 / 8.0;
	kernel[1] = 1.0 / 4.0;
	kernel[2] = 1.0 / 16.0;
	vec4 sum = center * kernel[0] * kernel[0];
	float weight_sum = kernel[0] * kernel[0];
	for(int y = -2; y <= 2; ++y)
	{
		for(int x = -2; x <= 2; ++x)
		{
			if(x == 0 && y == 0)
			{
				continue;
			}
			vec2 offset = vec2(float(x), float(y)) * u_gi_denoise_step * u_gi_denoise_texel.xy;
			vec2 tap_uv = uv + offset;
			if(any(lessThan(tap_uv, vec2_splat(0.0))) || any(greaterThan(tap_uv, vec2_splat(1.0))))
			{
				continue;
			}
			vec3 tap_position;
			if(!GiWorldAt(tap_uv, tap_position))
			{
				continue;
			}
			GBufferDataNormalMetalRoughness tap_nd =
			    DecodeGBufferNormalMetalRoughnessLod(tap_uv, s_gi_normal, 0.0);
			vec3 tap_normal = tap_nd.world_normal;
			if(dot(tap_normal, tap_normal) < 0.5)
			{
				continue;
			}
			tap_normal = normalize(tap_normal);
			float plane_distance = abs(dot(center_normal, tap_position - center_position));
			float depth_weight = exp(-plane_distance / plane_tolerance);
			float normal_weight = pow(max(dot(center_normal, tap_normal), 0.0), u_gi_denoise_normal_pow);
			vec4 tap_value = texture2DLod(s_gi_input, tap_uv, 0.0);
			float luma_weight = 1.0;
			if(use_luma_stop)
			{
				luma_weight = exp(-abs(center_luma - Luminance(tap_value.xyz)) / luma_sigma);
			}
			float spatial_weight = kernel[abs(x)] * kernel[abs(y)];
			float weight = spatial_weight * depth_weight * normal_weight * luma_weight;
			sum += tap_value * weight;
			weight_sum += weight;
		}
	}
	imageStore(s_gi_output, coord, sum / max(weight_sum, 1e-6));
}
