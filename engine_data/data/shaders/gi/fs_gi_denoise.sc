$input v_texcoord0

/*
 * Edge-preserving spatial filter for the surface cache gather (a-trous wavelet).
 *
 * Temporal accumulation alone leaves visible grain: it averages roughly a dozen frames of four
 * rays, and the signal is strongly bimodal -- a ray either finds a lit surface or a shadowed one,
 * so neighbouring pixels disagree hugely. Averaging across SPACE as well is what closes the gap,
 * and it is nearly free here because indirect diffuse is genuinely low frequency.
 *
 * Run repeatedly with a doubling tap spacing, this reaches a wide radius at a fraction of the
 * cost of one wide blur, which is the point of the a-trous formulation.
 *
 * The edge stops are what keep it from being an ordinary blur: taps are rejected when they sit on
 * a different plane or face a different way, so light does not bleed across a silhouette or
 * around a corner -- the exact leak this whole system exists to avoid.
 */

#include "../common.sh"
#include "../lighting.sh"

SAMPLER2D(s_gi_input, 0);
SAMPLER2D(s_gi_depth, 1);
SAMPLER2D(s_gi_normal, 2);
SAMPLER2D(s_gi_moments, 3);

/// x = tap spacing in texels, y = normal exponent, z = plane tolerance as a fraction of view
/// distance, w = luminance edge-stop strength.
uniform vec4 u_gi_denoise_params;
#define u_gi_denoise_step       u_gi_denoise_params.x
#define u_gi_denoise_normal_pow u_gi_denoise_params.y
#define u_gi_denoise_plane_tol  u_gi_denoise_params.z
#define u_gi_denoise_luma_phi   u_gi_denoise_params.w

/// xy = one texel of the filtered buffer.
uniform vec4 u_gi_denoise_texel;

/// x = tolerance multiplier applied at one accumulated sample, decaying to 1 as the count grows.
uniform vec4 u_gi_denoise_params2;
#define u_gi_denoise_low_count_boost u_gi_denoise_params2.x

uniform vec4 u_gi_denoise_camera;

/// Reconstructs the world position behind a texel of the full-resolution depth buffer.
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

void main()
{
	vec2 uv = v_texcoord0;
	vec4 center = texture2DLod(s_gi_input, uv, 0.0);
	vec3 center_position;
	if(!GiWorldAt(uv, center_position))
	{
		gl_FragColor = center;
		return;
	}
	GBufferDataNormalMetalRoughness center_nd = DecodeGBufferNormalMetalRoughnessLod(uv, s_gi_normal, 0.0);
	vec3 center_normal = center_nd.world_normal;
	if(dot(center_normal, center_normal) < 0.5)
	{
		gl_FragColor = center;
		return;
	}
	center_normal = normalize(center_normal);
	// Scale-invariant: the acceptable off-plane distance grows with view distance, so one value
	// works across the whole depth range instead of over-blurring near geometry and under-
	// blurring far geometry.
	float view_distance = max(length(center_position - u_gi_denoise_camera.xyz), 1e-4);
	float plane_tolerance = max(u_gi_denoise_plane_tol * view_distance, 1e-4);
	// Variance-driven luminance stop. This is what stops a converged pixel from being blurred at
	// all: where the temporal estimate has settled the tolerance collapses, and only genuinely
	// similar neighbours contribute -- so detail survives and, crucially, the filter stops
	// re-mixing a stable value with its neighbours every frame, which is itself a source of
	// shimmer. Where the estimate is still noisy the tolerance is wide and it filters hard. A
	// fixed tolerance can only trade one of those against the other.
	// A non-positive phi disables the stop outright, which is what happens when temporal
	// accumulation is off and no variance estimate exists. Falling back to a tiny sigma instead
	// would reject every tap and turn the filter into an expensive copy.
	bool use_luma_stop = u_gi_denoise_luma_phi > 0.0;
	vec4 moments = texture2DLod(s_gi_moments, uv, 0.0);
	float count = max(moments.z, 1.0);
	// DIVIDED BY THE COUNT, and that is the whole reason the stop tightens over time. The moments
	// accumulate the RAW per-frame gather, so this variance is that of a SINGLE sample -- a
	// constant of the scene and the ray count, which does not decay however long the camera is
	// held still. What is being filtered is the MEAN of `count` such samples, and the standard
	// error of a mean is smaller by sqrt(count). Without the division the tolerance floors at the
	// single-sample noise level forever, so a fully converged image goes on being blurred at full
	// strength and the "stops touching the pixel" property above is never actually delivered.
	float variance = max(moments.y - moments.x * moments.x, 0.0);
	float luma_sigma = u_gi_denoise_luma_phi * sqrt(variance / count) + 1e-4;
	// Filter HARDER where little history has accumulated. Silhouettes are the worst case in the
	// whole pipeline: reprojection fails there so the count resets to one, and the depth and
	// normal stops reject most taps, so those pixels receive neither temporal nor spatial
	// averaging and show as fireflies along edges -- worst just after the camera moves, fading as
	// the count recovers. A temporal variance built from one sample is also meaningless, so
	// widening the tolerance there is the only estimate available.
	luma_sigma *= max(u_gi_denoise_low_count_boost / count, 1.0);
	float center_luma = Luminance(center.xyz);
	// B3 spline weights, the standard a-trous kernel.
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
			// Distance from the CENTRE'S PLANE, not between the two points. A tap further along
			// the same wall is perfectly valid however far away it is; one the same distance away
			// but off the plane belongs to different geometry.
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
	// Both the estimate and its resolve weight are filtered: the weight is as noisy as the colour
	// at four rays per pixel, and filtering only the colour would leave the consumer's blend
	// flickering after the colour had settled.
	gl_FragColor = sum / max(weight_sum, 1e-6);
}
