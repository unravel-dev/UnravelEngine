/*
 * Variance-guided edge-preserving a-trous wavelet spatial denoiser for SSR.
 * Run between ray marching (fs_ssr_fidelityfx) and temporal resolve (fs_ssr_temporal_resolve).
 *
 * Pipeline: fs_ssr_fidelityfx -> [cs_ssr_spatial_denoise xN] -> fs_ssr_temporal_resolve
 *
 * Driver dispatches N times with step_size = 1, 2, 4, ... (1 << pass_index),
 * ping-ponging between two framebuffers. Each pass uses the same 5x5 kernel,
 * so N passes give an effective reach of (4 * (2^N - 1) + 1) pixels per axis.
 *
 * Alpha channel is treated as trace confidence in [0, 1]:
 *   - Low alpha => wider luminance sigma (variance boost) for more blur.
 *   - Low-confidence centers prefer high-confidence neighbours.
 *   - Blended alpha is propagated so the temporal stage sees a smoother
 *     confidence field.
 *
 * SSR-specific: mirror-like surfaces (roughness < 0.05) bypass the filter,
 * and the per-neighbour weight is scaled by smoothstep(0.05, 0.6, roughness)
 * so glossy reflections stay sharp while rough reflections blur aggressively.
 *
 * Uniforms (u_denoise_params):
 *   x: step_size    - a-trous step (1, 2, 4, ... for successive passes)
 *   y: depth_sigma  - depth edge-stopping threshold (~0.01-0.05)
 *   z: normal_power - normal edge-stopping exponent (~32-128)
 *   w: luma_sigma   - luminance edge-stopping threshold (~0.5-2.0)
 */

#include "../bgfx_compute.sh"
#include "../common.sh"
#include "../lighting.sh"
#include "../hiz_trace.sh"

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

	vec2 depth_dim = vec2(textureSize(s_depth, 0));
	// Per-axis scale: scalar (depth_dim.x / size.x) misses the Y axis when odd full-res W
	// and even full-res H produce different X and Y ratios, which corrupts gbuffer fetches
	// for half-res edge rows. See HizScreenPassToFullResUV docs in hiz_trace.sh.
	vec2 resolution_scale = depth_dim / max(vec2(size), vec2_splat(1.0));
	vec2 full_uv_center = HizScreenPassToFullResUV(uv, resolution_scale, depth_dim);

	vec4 center = texelFetch(s_ssr_input, coord, 0);

	BRANCH
	if (center.a <= 0.0)
	{
		imageStore(i_ssr_output, coord, center);
		return;
	}

	float center_depth = DecodeGBufferDepthLod(full_uv_center, s_depth, 0.0).depth01;
	GBufferDataNormalMetalRoughness center_nd = DecodeGBufferNormalMetalRoughnessLod(full_uv_center, s_normal, 0.0);
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

	// Variance boost: low-confidence centers get a wider luma sigma so the
	// filter blurs more aggressively (matches the FidelityFX/SVGF approach
	// used in the SSIL denoiser).
	//
	// SSR-specific tuning: unlike SSIL (where alpha is a temporal sample
	// count that rises toward 1.0 over a few frames), SSR's alpha is a
	// fixed geometric mask -- low at screen-edge fades, grazing rays and
	// invalid hits. An unbounded 1/alpha boost would permanently disable
	// the luma stop in those regions, letting the kernel pull bright
	// neighbours from rows above into dim faded rows and producing a
	// visible stripe at the viewport edges (especially at half-res where
	// each sample covers a 2x2 block). Floor center_conf at 0.25 and cap
	// the boost at MAX_VARIANCE_BOOST so faded pixels still respect the
	// luma edge-stop.
	#define MAX_VARIANCE_BOOST 4.0
	float center_conf = clamp(center.a, 0.25, 1.0);
	float variance_boost = min(1.0 / center_conf, MAX_VARIANCE_BOOST);
	float effective_luma_sigma = u_luma_sigma * variance_boost;

	// Weight the centre by its own confidence so a noisy hit doesn't anchor
	// the result if better neighbours exist.
	float center_kw = kernel_weight(0, 0) * center_conf;
	vec4  result = center * center_kw;
	float total_w = center_kw;
	float alpha_sum = center.a * center_kw;

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
			vec2  full_uv_s = HizScreenPassToFullResUV(suv, resolution_scale, depth_dim);

			float sd = DecodeGBufferDepthLod(full_uv_s, s_depth, 0.0).depth01;
			float dw = exp(-abs(center_depth - sd) / max(u_depth_sigma, 1e-6));

			vec3 sn = DecodeGBufferNormalMetalRoughnessLod(full_uv_s, s_normal, 0.0).world_normal;
			float nw = pow(max(0.0, dot(center_normal, sn)), u_normal_power);

			float sl = Luminance(s.rgb);
			float lw = exp(-abs(center_luma - sl) / max(effective_luma_sigma, 1e-6));

			// When the centre is low-confidence, lean on high-confidence
			// neighbours; when the centre is fully trusted, weight neighbours
			// uniformly (cw -> 1) so the kernel acts as a vanilla a-trous.
			float cw = mix(s.a, 1.0, center_conf);

			float w = kw * dw * nw * lw * cw * roughness_blend;

			result   += s * w;
			total_w  += w;
			alpha_sum += s.a * w;
		}
	}

	result /= max(total_w, 1e-6);
	// Propagate the blended confidence so successive passes / the temporal
	// stage see an improving alpha field instead of the raw trace mask.
	result.a = alpha_sum / max(total_w, 1e-6);
	imageStore(i_ssr_output, coord, result);
}
