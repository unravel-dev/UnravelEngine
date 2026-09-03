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
/// y = the accumulation cap when the converged early-out is enabled, 0 to disable it.
/// z = luminance-stop floor as a fraction of the centre's own luminance (the
/// coherent-structure floor; 0 restores the pure variance-driven stop).
uniform vec4 u_gi_denoise_params2;
#define u_gi_denoise_low_count_boost u_gi_denoise_params2.x
#define u_gi_denoise_converged_cap   u_gi_denoise_params2.y
#define u_gi_denoise_luma_floor      u_gi_denoise_params2.z
/// w > 0 = REVEAL pass (GI_DENOISE_REVEAL_STEP): only pixels with an accumulation count below
/// this value are filtered; converged pixels pass through.
#define u_gi_denoise_reveal_count    u_gi_denoise_params2.w

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
	// REVEAL PASS gate (the ReBLUR history-fix idea): the wide extra pass serves only pixels
	// whose running mean is still short - a disocclusion, a reveal - and costs converged
	// pixels one moments fetch.
	if(u_gi_denoise_reveal_count > 0.0 && count >= u_gi_denoise_reveal_count)
	{
		gl_FragColor = center;
		return;
	}
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
	// COHERENT-STRUCTURE FLOOR: on converged pixels the variance collapses and the stop
	// preserves ANY leftover structure - including the probe/voxel-scale sampling-bias blobs
	// that are precisely what needs smoothing (coherent, not noise: no temporal window
	// removes them, and they survived every pass of this filter by design). The floor keeps
	// same-plane neighbours within this fraction of the centre's own luminance merging after
	// convergence; contrast above it, and anything across a plane or normal break, is
	// preserved exactly as before. Keep in step with the compute form.
	luma_sigma = max(luma_sigma, u_gi_denoise_luma_floor * max(center_luma, 1e-3));
	// CONVERGED EARLY-OUT: at a full accumulation count with collapsed variance, the stops
	// above reject every tap whose luminance differs meaningfully - the filter is an identity
	// operator producing its input at 24 taps of cost (the note on the luminance stop above
	// promises exactly this behaviour; this acts on it). The threshold is relative to the
	// centre's own luminance and sits well below the target's quantisation. Disabled by a
	// zero cap, the A/B switch.
	BRANCH
	if(u_gi_denoise_converged_cap > 0.0)
	{
		if(count >= u_gi_denoise_converged_cap && luma_sigma < 0.002 * max(center_luma, 1e-3))
		{
			gl_FragColor = center;
			return;
		}
	}
	// One mat4 fold per PIXEL instead of one clipToWorld per TAP: the taps only ever consume
	// the distance from the centre's plane, and with a = (n,0)^T * M and w = e4^T * M the
	// plane height of a reconstructed world point is (a.h)/(w.h) for the tap's clip-space h -
	// two dot4s and one divide per tap where the full reconstruction paid a matrix transform
	// and a perspective divide. Algebraically identical.
	vec4 plane_row = mul(vec4(center_normal, 0.0), u_invViewProj);
	vec4 w_row = mul(vec4(0.0, 0.0, 0.0, 1.0), u_invViewProj);
	float center_plane_height = dot(center_normal, center_position);
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
			float tap_depth = texture2DLod(s_gi_depth, tap_uv, 0.0).x;
			if(tap_depth >= 1.0)
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
			// but off the plane belongs to different geometry. Folded reconstruction: see the
			// plane_row derivation above.
			vec4 tap_h = vec4(clipTransform(vec3(tap_uv * 2.0 - 1.0, toClipSpaceDepth(tap_depth))), 1.0);
			float plane_distance = abs(dot(plane_row, tap_h) / dot(w_row, tap_h) - center_plane_height);
			// pow(x, 32) is two transcendentals per tap; the default exponent takes the exact
			// five-multiply chain, any other value keeps the general path.
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
			vec4 tap_value = texture2DLod(s_gi_input, tap_uv, 0.0);
			// One exponential for both stops: exp(-a) * exp(-b) = exp(-(a + b)) exactly, and
			// the transcendental count is what this pass is bound by (keep in step with the
			// compute form).
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
	// Both the estimate and its resolve weight are filtered: the weight is as noisy as the colour
	// at four rays per pixel, and filtering only the colour would leave the consumer's blend
	// flickering after the colour had settled.
	gl_FragColor = sum / max(weight_sum, 1e-6);
}
