/*
 * Edge-preserving a-trous wavelet spatial denoiser for SSR.
 * Run between ray marching (fs_ssr_fidelityfx) and temporal resolve (fs_ssr_temporal_resolve).
 *
 * Pipeline: fs_ssr_fidelityfx -> [cs_ssr_spatial_denoise] -> fs_ssr_temporal_resolve
 *
 * Dispatch once with step_size=1 for light denoising.
 * For stronger filtering, dispatch multiple times with step_size=1,2,4
 * (ping-ponging input/output between passes).
 *
 * Uniforms (u_denoise_params):
 *   x: step_size    - a-trous step (1, 2, 4 for successive passes)
 *   y: depth_sigma  - depth edge-stopping threshold (~0.01-0.05)
 *   z: normal_power - normal edge-stopping exponent (~32-128)
 *   w: luma_sigma   - luminance edge-stopping threshold (~0.5-2.0)
 */

#include "../bgfx_compute.sh"
#include "../common.sh"
#include "../lighting.sh"

SAMPLER2D(s_ssr_input, 0);
IMAGE2D_WO(i_ssr_output, rgba16f, 1);
SAMPLER2D(s_normal, 2);
SAMPLER2D(s_depth,  3);

uniform vec4 u_denoise_params;
#define u_step_size    u_denoise_params.x
#define u_depth_sigma  u_denoise_params.y
#define u_normal_power u_denoise_params.z
#define u_luma_sigma   u_denoise_params.w

#define MAX_ROUGHNESS 0.6

// 5-tap a-trous 1D kernel: [1/16, 1/4, 3/8, 1/4, 1/16]
#define KW0 0.375
#define KW1 0.25
#define KW2 0.0625

float kernel_weight(int dx, int dy)
{
	int ax = abs(dx);
	int ay = abs(dy);
	float wx = (ax == 0) ? KW0 : ((ax == 1) ? KW1 : KW2);
	float wy = (ay == 0) ? KW0 : ((ay == 1) ? KW1 : KW2);
	return wx * wy;
}

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = imageSize(i_ssr_output);
	if (any(greaterThanEqual(coord, size)))
		return;

	vec2 texel_size = 1.0 / vec2(size);
	vec2 uv = (vec2(coord) + 0.5) * texel_size;

	vec4 center = texelFetch(s_ssr_input, coord, 0);

	BRANCH
	if (center.a <= 0.0)
	{
		imageStore(i_ssr_output, coord, center);
		return;
	}

	float center_depth = DecodeGBufferDepthLod(uv, s_depth, 0.0).depth01;
	GBufferDataNormalMetalRoughness center_nd = DecodeGBufferNormalMetalRoughnessLod(uv, s_normal, 0.0);
	vec3  center_normal = center_nd.world_normal;
	float roughness     = center_nd.roughness;

	BRANCH
	if (roughness < 0.05)
	{
		imageStore(i_ssr_output, coord, center);
		return;
	}

	float roughness_blend = smoothstep(0.05, MAX_ROUGHNESS, roughness);
	float center_luma = Luminance(center.rgb);
	int   step = int(u_step_size);

	float center_kw = kernel_weight(0, 0);
	vec4  result = center * center_kw;
	float total_w = center_kw;

	for (int dy = -2; dy <= 2; dy++)
	{
		for (int dx = -2; dx <= 2; dx++)
		{
			if (dx == 0 && dy == 0)
				continue;

			ivec2 sc = coord + ivec2(dx, dy) * step;

			if (any(lessThan(sc, ivec2(0, 0))) || any(greaterThanEqual(sc, size)))
				continue;

			float kw = kernel_weight(dx, dy);
			vec4  s  = texelFetch(s_ssr_input, sc, 0);
			vec2  suv = (vec2(sc) + 0.5) * texel_size;

			float sd = DecodeGBufferDepthLod(suv, s_depth, 0.0).depth01;
			float dw = exp(-abs(center_depth - sd) / max(u_depth_sigma, 1e-6));

			vec3 sn = DecodeGBufferNormalMetalRoughnessLod(suv, s_normal, 0.0).world_normal;
			float nw = pow(max(0.0, dot(center_normal, sn)), u_normal_power);

			float sl = Luminance(s.rgb);
			float lw = exp(-abs(center_luma - sl) / max(u_luma_sigma, 1e-6));

			float w = kw * dw * nw * lw * roughness_blend;

			result  += s * w;
			total_w += w;
		}
	}

	result /= max(total_w, 1e-6);
	imageStore(i_ssr_output, coord, result);
}
