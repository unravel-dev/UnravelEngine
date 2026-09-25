$input v_texcoord0

/*
 * GI reflection composite: blends the temporally integrated TRACED reflection into RBUFFER,
 * the traced layers (src-alpha on rgb; alpha keeps the share left to the probe layer, see
 * ComposeIndirectSpecular). Its coverage is the traced tier's share only - the rough tier
 * composites into the probe layer instead (fs_gi_reflection_rough.sc, gi_reflection_tiers.sh).
 * SSR composites the sharp on-screen result on top afterwards.
 *
 * SPATIAL FINISH: one roughness-scaled 3x3 cross-bilateral over the ACCUMULATED result - the
 * temporal EMA alone leaves visible sample shimmer once the GGX lobe widens (~0.35 measured),
 * because one stochastic ray per frame over an 8-frame window cannot fully integrate a wide
 * lobe. Neighbour weight ramps with roughness over the traced band (a mirror keeps its
 * sharpness untouched, wide lobes average fully - SSR's spatial-denoise convention), guarded
 * by depth and normal edge-stops so reflections never bleed across silhouettes. Filtering
 * AFTER accumulation means the blur never feeds back into history.
 *
 * GATED, not unconditional. The roughness ramp alone kept filtering every pixel of the
 * traced band for as long as it existed, so a glossy surface that converged hundreds of
 * frames ago was still being blurred - a permanent sharpness loss across the whole band for
 * shimmer that was no longer there. Lumen runs its equivalent only where the temporal
 * variance says the pixel is still noisy, plus a ramp over the first few accumulated frames
 * (LumenReflectionDenoiserSpatial.usf:83-104, the ramp new in UE 5.8). We have no second
 * moment, so the gate is built from the local deviation of the same 3x3 the kernel already
 * fetches, taken with the accumulation count that already rides the target's alpha. A
 * converged, flat pixel now skips the kernel outright - which also skips its depth and
 * normal fetches, so the gate pays for itself.
 */

#include "../common.sh"
#include "../lighting.sh"
#include "gi/gi_constants.sh"
// GiReflLuma - the same luminance the temporal's statistics use.
#include "gi/gi_reflection_denoise.sh"
#include "gi/gi_reflection_tiers.sh"

SAMPLER2D(s_refl_acc, 0);
SAMPLER2D(s_gi_normal, 1);
SAMPLER2D(s_hiz, 2);

/// xy = 1 / target size.
uniform vec4 u_gi_refl_composite;

void main()
{
	vec2 uv = v_texcoord0;
	vec4 center = texture2DLod(s_refl_acc, uv, 0.0);
	float center_depth = texture2DLod(s_hiz, uv, 0.0).x;
	BRANCH
	if(center_depth >= 1.0)
	{
		// History alpha is the accumulation COUNT, or 0 when no geometric sample has
		// landed (unrefined clipmap on a sharp pixel). RBUFFER wants coverage.
		gl_FragColor = vec4(center.xyz, saturate(center.w));
		return;
	}
	GBufferDataNormalMetalRoughness nd = DecodeGBufferNormalMetalRoughnessLod(uv, s_gi_normal, 0.0);
	// The rough tier takes the rest of the history's coverage in the probe layer.
	float traced_coverage = GiReflectionTracedCoverage(saturate(center.w), GiReflectionRoughShare(nd.roughness));
	// Neighbour weight ramps over the traced band: sharp lobes have no shimmer to hide and
	// keep full sharpness, lobes near the cutoff average the whole neighbourhood.
	float blur_scale = smoothstep(0.0, GI_REFLECTION_ROUGH_CUTOFF, nd.roughness);
	// Two early-outs bound the kernel to the band that has shimmer to hide. Below: authored
	// mirrors are rarely exactly 0, so `<= 0.0` never fired - a roughness-0.02 pixel ran all
	// 24 taps to apply a total neighbour weight under 0.004. Anything below 0.05 is beneath
	// the target's own quantisation. Above: pixels past the cutoff never traced - their
	// accumulated value is last frame's temporally filtered, denoised resolve, which has no
	// stochastic shimmer for the kernel to remove.
	BRANCH
	if(blur_scale < 0.05 || nd.roughness >= GI_REFLECTION_ROUGH_CUTOFF)
	{
		gl_FragColor = vec4(center.xyz, traced_coverage);
		return;
	}
	vec3 center_normal = normalize(nd.world_normal);
	vec2 texel = u_gi_refl_composite.xy;
	// PASS 1 - the colour taps the kernel needs anyway, plus the local luminance statistics
	// the gate is built from. Held in registers so pass 2 never re-fetches them.
	vec3 neighbor_rgb[8];
	float center_luma = GiReflLuma(center.xyz);
	float luma_sum = center_luma;
	float luma_sq_sum = center_luma * center_luma;
	float luma_count = 1.0;
	int neighbor_index = 0;
	LOOP
	for(int y = -1; y <= 1; ++y)
	{
		LOOP
		for(int x = -1; x <= 1; ++x)
		{
			if(x == 0 && y == 0)
			{
				continue;
			}
			vec4 s = texture2DLod(s_refl_acc, uv + vec2(float(x), float(y)) * texel, 0.0);
			neighbor_rgb[neighbor_index] = s.xyz;
			++neighbor_index;
			// A zero-count tap holds no accumulated image; it may still be averaged (the
			// kernel's tap set is unchanged) but it must not claim the pixel is noisy.
			if(s.w >= 0.5)
			{
				float sample_luma = GiReflLuma(s.xyz);
				luma_sum += sample_luma;
				luma_sq_sum += sample_luma * sample_luma;
				luma_count += 1.0;
			}
		}
	}
	// THE GATE. Local deviation relative to the local level says "this pixel is still
	// noisy"; the accumulation count says "this pixel has no history to be trusted yet".
	// Either opens the kernel, so persistently noisy content keeps its filter for as long
	// as it needs it while converged content is served sharp.
	float luma_mean = luma_sum / luma_count;
	float luma_deviation = sqrt(max(luma_sq_sum / luma_count - luma_mean * luma_mean, 0.0));
	float noise_gate = saturate(luma_deviation / (GI_REFLECTION_FILTER_NOISE_RATIO *
	                                              max(luma_mean, GI_REFLECTION_FILTER_LUMA_FLOOR)));
	float disocclusion_gate =
	    1.0 - saturate(center.w / GI_REFLECTION_FILTER_DISOCCLUSION_FRAMES);
	float gate = max(noise_gate, disocclusion_gate);
	BRANCH
	if(gate < 0.01)
	{
		// Converged and flat: serve the accumulated value untouched and skip the kernel's
		// depth and normal fetches entirely.
		gl_FragColor = vec4(center.xyz, traced_coverage);
		return;
	}
	blur_scale *= gate;
	// PASS 2 - the edge stops, over the taps already in hand.
	vec3 color_sum = center.xyz;
	float weight_sum = 1.0;
	neighbor_index = 0;
	LOOP
	for(int y = -1; y <= 1; ++y)
	{
		LOOP
		for(int x = -1; x <= 1; ++x)
		{
			if(x == 0 && y == 0)
			{
				continue;
			}
			vec2 sample_uv = uv + vec2(float(x), float(y)) * texel;
			float sample_depth = texture2DLod(s_hiz, sample_uv, 0.0).x;
			vec3 sample_normal =
			    DecodeGBufferNormalMetalRoughnessLod(sample_uv, s_gi_normal, 0.0).world_normal;
			// Same edge-stop shape as the gather's a-trous: depth agreement within a small
			// screen-depth band, tight normal cone so silhouettes stay crisp.
			float depth_weight =
			    saturate(1.0 - abs(sample_depth - center_depth) / (GI_TEMPORAL_DEPTH_TOLERANCE * 0.01));
			// pow(x, 32) is two transcendentals; five multiplies are cheaper and exact.
			float nw = saturate(dot(normalize(sample_normal), center_normal));
			nw = nw * nw;
			nw = nw * nw;
			nw = nw * nw;
			nw = nw * nw;
			float normal_weight = nw * nw;
			float weight = blur_scale * depth_weight * normal_weight;
			color_sum += neighbor_rgb[neighbor_index] * weight;
			++neighbor_index;
			weight_sum += weight;
		}
	}
	gl_FragColor = vec4(color_sum / weight_sum, traced_coverage);
}
