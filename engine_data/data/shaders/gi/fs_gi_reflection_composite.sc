$input v_texcoord0

/*
 * GI reflection composite: draws the temporally integrated reflection over the authored
 * probe layer in RBUFFER (src-alpha blend). SSR composites the sharp on-screen result on top
 * of both afterwards.
 *
 * SPATIAL FINISH: one roughness-scaled 3x3 cross-bilateral over the ACCUMULATED result - the
 * temporal EMA alone leaves visible sample shimmer once the GGX lobe widens (~0.35 measured),
 * because one stochastic ray per frame over an 8-frame window cannot fully integrate a wide
 * lobe. Neighbour weight ramps with roughness over the traced band (a mirror keeps its
 * sharpness untouched, wide lobes average fully - SSR's spatial-denoise convention), guarded
 * by depth and normal edge-stops so reflections never bleed across silhouettes. Filtering
 * AFTER accumulation means the blur never feeds back into history.
 */

#include "../common.sh"
#include "../lighting.sh"
#include "gi/gi_constants.sh"

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
		// History alpha carries the accumulation COUNT; RBUFFER wants coverage.
		gl_FragColor = vec4(center.xyz, saturate(center.w));
		return;
	}
	GBufferDataNormalMetalRoughness nd = DecodeGBufferNormalMetalRoughnessLod(uv, s_gi_normal, 0.0);
	// Neighbour weight ramps over the traced band: sharp lobes have no shimmer to hide and
	// keep full sharpness, lobes near the cutoff average the whole neighbourhood.
	float blur_scale = smoothstep(0.0, GI_REFLECTION_ROUGH_CUTOFF, nd.roughness);
	BRANCH
	if(blur_scale <= 0.0)
	{
		gl_FragColor = vec4(center.xyz, saturate(center.w));
		return;
	}
	vec3 center_normal = normalize(nd.world_normal);
	vec2 texel = u_gi_refl_composite.xy;
	vec3 color_sum = center.xyz;
	float weight_sum = 1.0;
	for(int y = -1; y <= 1; ++y)
	{
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
			float normal_weight = pow(saturate(dot(normalize(sample_normal), center_normal)), 32.0);
			float weight = blur_scale * depth_weight * normal_weight;
			color_sum += texture2DLod(s_refl_acc, sample_uv, 0.0).xyz * weight;
			weight_sum += weight;
		}
	}
	gl_FragColor = vec4(color_sum / weight_sum, saturate(center.w));
}
