#ifndef __GI_GPU_LIGHTS_SH__
#define __GI_GPU_LIGHTS_SH__

/*
 * Scene lights as enumerable data.
 *
 * MIRROR OF engine/engine/rendering/gpu_light_buffer.h. The packing must match exactly.
 *
 * The deferred path draws one fullscreen pass per light with that light's parameters in
 * uniforms, which cannot answer "how much light reaches world point P" from inside a tracing
 * shader -- there is only ever the one light currently bound. Global illumination needs that
 * question answered at arbitrary points along a traced ray.
 *
 * The attenuation here is deliberately identical to RadialAttenuation / SpotAttenuation in
 * lighting.sh. If the two drift, indirect light stops agreeing with the direct light it is
 * supposed to be a bounce of, and the mismatch reads as a lighting bug with no obvious source.
 *
 * RESERVED RESOURCE STAGE 5.
 */

#include "../bgfx_compute.sh"

/// vec4 elements per light. Mirror of gpu_light_buffer::light_vec4_stride.
#define GPU_LIGHT_STRIDE 4

#define GPU_LIGHT_TYPE_SPOT        0
#define GPU_LIGHT_TYPE_POINT       1
#define GPU_LIGHT_TYPE_DIRECTIONAL 2

BUFFER_RO(b_gpu_lights, vec4, 5);

/// x = light count. yzw reserved.
uniform vec4 u_gpu_light_params;
#define u_gpu_light_count int(u_gpu_light_params.x)

struct GpuLight
{
	vec3 position;
	int type;
	vec3 direction;
	float range;
	vec3 color;
	float intensity;
	float cos_inner;
	float cos_outer;
	float falloff_exponent;
	/// Index into the shadow atlas, or -1 when the light casts no resident shadow.
	float shadow_slot;
};

GpuLight GpuLoadLight(int index)
{
	uint base = uint(index) * uint(GPU_LIGHT_STRIDE);
	vec4 l0 = b_gpu_lights[base + 0u];
	vec4 l1 = b_gpu_lights[base + 1u];
	vec4 l2 = b_gpu_lights[base + 2u];
	vec4 l3 = b_gpu_lights[base + 3u];
	GpuLight light;
	light.position = l0.xyz;
	light.type = int(l0.w);
	light.direction = l1.xyz;
	light.range = l1.w;
	light.color = l2.xyz;
	light.intensity = l2.w;
	light.cos_inner = l3.x;
	light.cos_outer = l3.y;
	light.falloff_exponent = l3.z;
	light.shadow_slot = l3.w;
	return light;
}

/// Matches RadialAttenuation in lighting.sh.
float GpuRadialAttenuation(vec3 light_vector_over_range, float falloff_exponent)
{
	float normalized_distance_sq = dot(light_vector_over_range, light_vector_over_range);
	return pow(1.0 - saturate(normalized_distance_sq), falloff_exponent);
}

/// Matches SpotAttenuation in lighting.sh, whose SpotAngles is (cos_outer, 1 / (cos_inner - cos_outer)).
float GpuSpotAttenuation(vec3 light_vector, vec3 spot_direction, float cos_inner, float cos_outer)
{
	float inv_range = 1.0 / max(cos_inner - cos_outer, 1e-4);
	float cone = saturate((dot(normalize(light_vector), -spot_direction) - cos_outer) * inv_range);
	return cone * cone;
}

/**
 * Irradiance arriving at a world point from one light, UNSHADOWED.
 *
 * Lambertian: the caller multiplies by albedo / PI to get outgoing radiance. Shadowing is not
 * applied here -- until the shadow atlas is resident there is nothing to test against, and a
 * silently unshadowed result is far easier to reason about than one that is shadowed for some
 * lights and not others.
 */
/// The Ex form also reports the shadow-ray geometry it already derived - direction toward the
/// light and the distance to it - so callers that trace do not recompute the same length and
/// normalize. For a directional light the distance is huge-but-finite; the caller substitutes
/// its own shadow range.
vec3 GpuEvalLightUnshadowedEx(GpuLight light, vec3 world_position, vec3 world_normal,
                              out vec3 out_to_light, out float out_distance)
{
	vec3 to_light;
	float attenuation = 1.0;
	out_distance = 1e8;
	if(light.type == GPU_LIGHT_TYPE_DIRECTIONAL)
	{
		to_light = -light.direction;
	}
	else
	{
		vec3 delta = light.position - world_position;
		vec3 over_range = delta / max(light.range, 1e-4);
		if(light.type == GPU_LIGHT_TYPE_POINT)
		{
			attenuation = GpuRadialAttenuation(over_range, light.falloff_exponent);
		}
		else
		{
			attenuation = GpuRadialAttenuation(over_range, 1.0) *
			              GpuSpotAttenuation(delta, light.direction, light.cos_inner, light.cos_outer);
		}
		float distance = length(delta);
		to_light = distance > 1e-6 ? delta / distance : vec3(0.0, 1.0, 0.0);
		out_distance = distance;
	}
	out_to_light = to_light;
	float n_dot_l = saturate(dot(world_normal, to_light));
	return light.color * (light.intensity * attenuation * n_dot_l);
}

vec3 GpuEvalLightUnshadowed(GpuLight light, vec3 world_position, vec3 world_normal)
{
	vec3 ignored_to_light;
	float ignored_distance;
	return GpuEvalLightUnshadowedEx(light, world_position, world_normal, ignored_to_light,
	                                ignored_distance);
}

/**
 * Total unshadowed irradiance at a world point from every resident light.
 */
vec3 GpuEvalDirectLightingUnshadowed(vec3 world_position, vec3 world_normal)
{
	vec3 total = vec3_splat(0.0);
	for(int i = 0; i < u_gpu_light_count; ++i)
	{
		total += GpuEvalLightUnshadowed(GpuLoadLight(i), world_position, world_normal);
	}
	return total;
}

#endif // __GI_GPU_LIGHTS_SH__
