$input v_texcoord0

/*
 * GI reflection ROUGH TIER into the probe layer (PBUFFER), after this frame's gather.
 *
 * The rough tier is the screen-probe radiance integrated against the pixel's GGX lobe - the
 * gather's rough specular (fs_gi_rough_specular.sc, Lumen's rough specular) - fading into the
 * diffuse resolve toward GI_REFLECTION_ROUGH_SPECULAR_MAX, where the lobe is wide enough that
 * the two agree, and the diffuse resolve alone where no probe served the pixel. Both are
 * gathered at the probe lattice and carry its visibility, not the pixel's, so they go into the
 * probe layer, which the indirect pass occludes (ComposeIndirectSpecular) - Lumen occludes its
 * rough specular the same way - while the traced tier went into RBUFFER. The weights
 * (gi_reflection_tiers.sh) reproduce the single blend the two replace, and read the same
 * history coverage the traced composite did. The weight also joins the layer's union coverage
 * in alpha, so the indirect pass fills only what neither the probes nor this tier answer with
 * the environment (CompleteProbeLayer).
 *
 * The rough specular is at the gather's trace resolution, and its lobe samples are rotated per
 * texel by interleaved gradient noise, whose values spread evenly over any 3x3 block: the 3x3
 * texels around the pixel are averaged under a tent over their distance, which both upsamples
 * and removes that per-texel sampling pattern. Each is also weighted by plane distance and
 * normal agreement (the gather upsample's edge stops, fs_gi_upsample.sc), and texels that hold
 * no estimate are skipped.
 */

#include "../common.sh"
#include "../lighting.sh"
#include "gi/gi_constants.sh"
#include "gi/gi_reflection_tiers.sh"

SAMPLER2D(s_refl_acc, 0);
SAMPLER2D(s_gi_normal, 1);
SAMPLER2D(s_gi_depth, 2);
/// This frame's resolved GI (E/pi), pre-exposed.
SAMPLER2D(s_gi_diffuse, 3);
/// This frame's rough specular at the trace resolution: rgb pre-exposed, a = the accumulated
/// frame count, 0 where it holds no estimate.
SAMPLER2D(s_gi_rough_specular, 4);

/// xyz = camera position (the reflection chain's shared uniform), w unused.
uniform vec4 u_gi_reflection_camera;
/// x = 1 when s_gi_rough_specular holds this frame's rough specular; yzw unused.
uniform vec4 u_gi_refl_rough;

/// Plane tolerance as a fraction of view distance - the gather upsample's default
/// (gi_resolve_pass::settings::upsample_plane_tolerance).
#define GI_ROUGH_UPSAMPLE_PLANE_TOLERANCE 0.02
/// Radius of the tent over the 3x3 trace texels, in texels: wide enough that the ring keeps
/// half the centre's weight, so the kernel stays close to the even 3x3 average the noise's
/// stratification is built for.
#define GI_ROUGH_UPSAMPLE_TENT_RADIUS 2.0

/// The rough specular at this pixel from its trace-resolution texels; w = 1 when any texel of
/// the pixel's surface held an estimate.
vec4 GiRoughSpecularAt(vec2 uv, vec3 world_position, vec3 normal)
{
	vec2 low_size = vec2(textureSize(s_gi_rough_specular, 0));
	vec2 sample_pos = uv * low_size - vec2_splat(0.5);
	vec2 centre = floor(sample_pos + vec2_splat(0.5));
	float view_distance = max(length(world_position - u_gi_reflection_camera.xyz), 1e-4);
	float plane_tolerance = max(GI_ROUGH_UPSAMPLE_PLANE_TOLERANCE * view_distance, 1e-4);
	vec3 sum = vec3_splat(0.0);
	float weight_sum = 0.0;
	for(int tap = 0; tap < 9; ++tap)
	{
		vec2 texel = centre + vec2(float(tap % 3 - 1), float(tap / 3 - 1));
		vec2 offset = abs(texel - sample_pos) / GI_ROUGH_UPSAMPLE_TENT_RADIUS;
		float tent = saturate(1.0 - offset.x) * saturate(1.0 - offset.y);
		vec2 tap_uv = (texel + vec2_splat(0.5)) / low_size;
		vec4 tap_value = texture2DLod(s_gi_rough_specular, tap_uv, 0.0);
		if(tent <= 0.0 || tap_value.w < 0.5)
		{
			continue;
		}
		// The surface this texel was computed for: the gather read the G-buffer at this uv.
		float tap_depth = texture2DLod(s_gi_depth, tap_uv, 0.0).x;
		vec3 tap_normal = DecodeGBufferNormalMetalRoughnessLod(tap_uv, s_gi_normal, 0.0).world_normal;
		if(tap_depth >= 1.0 || dot(tap_normal, tap_normal) < 0.5)
		{
			continue;
		}
		vec3 tap_clip = clipTransform(vec3(tap_uv * 2.0 - 1.0, toClipSpaceDepth(tap_depth)));
		vec3 tap_position = clipToWorld(u_invViewProj, tap_clip);
		float plane_distance = abs(dot(tap_position - world_position, normal));
		float depth_weight = exp(-plane_distance / plane_tolerance);
		// The gather upsample's normal exponent (32) as five multiplies.
		float normal_weight = saturate(dot(normalize(tap_normal), normal));
		normal_weight *= normal_weight;
		normal_weight *= normal_weight;
		normal_weight *= normal_weight;
		normal_weight *= normal_weight;
		normal_weight *= normal_weight;
		float weight = tent * depth_weight * normal_weight;
		sum += tap_value.xyz * weight;
		weight_sum += weight;
	}
	return weight_sum > 1e-6 ? vec4(sum / weight_sum, 1.0) : vec4_splat(0.0);
}

void main()
{
	vec2 uv = v_texcoord0;
	float depth = texture2DLod(s_gi_depth, uv, 0.0).x;
	BRANCH
	if(depth >= 1.0)
	{
		// Sky: nothing to reflect, and zero weight leaves the probe layer as it is.
		gl_FragColor = vec4_splat(0.0);
		return;
	}
	GBufferDataNormalMetalRoughness nd = DecodeGBufferNormalMetalRoughnessLod(uv, s_gi_normal, 0.0);
	float coverage = saturate(texture2DLod(s_refl_acc, uv, 0.0).w);
	float weight = GiReflectionRoughWeight(coverage, GiReflectionRoughShare(nd.roughness));
	BRANCH
	if(weight <= 0.0)
	{
		gl_FragColor = vec4_splat(0.0);
		return;
	}
	vec3 rough_value = texture2DLod(s_gi_diffuse, uv, 0.0).xyz;
	float diffuse_share = GiReflectionRoughDiffuseShare(nd.roughness);
	BRANCH
	if(u_gi_refl_rough.x > 0.5 && diffuse_share < 1.0 && dot(nd.world_normal, nd.world_normal) >= 0.5)
	{
		vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(depth)));
		vec3 world_position = clipToWorld(u_invViewProj, clip);
		vec4 rough_specular = GiRoughSpecularAt(uv, world_position, normalize(nd.world_normal));
		if(rough_specular.w > 0.5)
		{
			rough_value = mix(rough_specular.xyz, rough_value, diffuse_share);
		}
	}
	gl_FragColor = vec4(rough_value, weight);
}
