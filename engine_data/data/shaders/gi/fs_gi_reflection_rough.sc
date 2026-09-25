$input v_texcoord0

/*
 * GI reflection ROUGH TIER into the probe layer (PBUFFER), after the traced composite.
 *
 * A wide lobe's specular converges to the diffuse irradiance, and last frame's resolved gather
 * is the engine's smoothest per-pixel estimate of it (the Lumen recipe). That estimate is not a
 * trace from this pixel: it carries the visibility of the probe lattice it was gathered at, so
 * it misses the occlusion below the lattice exactly as a reflection probe misses all of it.
 * Blending it into the probe layer puts it under the full specular occlusion the indirect pass
 * gives that layer (ComposeIndirectSpecular) - Lumen occludes its rough specular the same way -
 * while the traced tier went into RBUFFER. The weights (gi_reflection_tiers.sh) reproduce the
 * single blend the two replace, and read the same history coverage the traced composite did.
 * The weight also joins the layer's union coverage in alpha, so the indirect pass fills only
 * what neither the probes nor this tier answer with the environment (CompleteProbeLayer).
 *
 * Runs only when last frame's resolve exists; until then the probe layer answers the rough
 * lobes on its own.
 */

#include "../common.sh"
#include "../lighting.sh"
#include "gi/gi_reflection_tiers.sh"
// u_history_pre_exposure_correction: last frame's resolve into this frame's pre-exposed space.
#include "gi/gi_pre_exposure.sh"

SAMPLER2D(s_refl_acc, 0);
SAMPLER2D(s_gi_normal, 1);
SAMPLER2D(s_hiz, 2);
/// LAST frame's resolved GI (E/pi, written under the previous pre-exposure).
SAMPLER2D(s_gi_diffuse, 3);

void main()
{
	vec2 uv = v_texcoord0;
	BRANCH
	if(texture2DLod(s_hiz, uv, 0.0).x >= 1.0)
	{
		// Sky: nothing to reflect, and zero weight leaves the probe layer as it is.
		gl_FragColor = vec4_splat(0.0);
		return;
	}
	GBufferDataNormalMetalRoughness nd = DecodeGBufferNormalMetalRoughnessLod(uv, s_gi_normal, 0.0);
	float coverage = saturate(texture2DLod(s_refl_acc, uv, 0.0).w);
	float weight = GiReflectionRoughWeight(coverage, GiReflectionRoughShare(nd.roughness));
	vec3 rough_value = texture2DLod(s_gi_diffuse, uv, 0.0).xyz * u_history_pre_exposure_correction;
	gl_FragColor = vec4(rough_value, weight);
}
