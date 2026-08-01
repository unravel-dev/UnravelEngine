#ifndef __GI_LIGHTING_SH__
#define __GI_LIGHTING_SH__

/*
 * Direct lighting at an arbitrary world point, with visibility resolved by tracing the
 * distance fields rather than by sampling a shadow map.
 *
 * WHY NOT SHADOW MAPS. The obvious route is a shadow atlas: pack every light's shadow map into
 * one texture and look it up. That means reworking how shadows are rendered, which touches
 * every shadow-casting scene in the project, and it inherits every shadow map limitation --
 * finite resolution, finite range, and nothing outside the light's own frustum.
 *
 * Tracing the fields avoids all of it. The geometry is already resident and already validated,
 * a ray answers visibility for geometry anywhere including behind the camera, and the shipping
 * shadow path is left completely untouched. The trade is that occlusion resolves at field
 * resolution rather than shadow map resolution, which is coarse for a sharp direct shadow but
 * entirely adequate for the indirect light this feeds -- and it degrades toward softness rather
 * than toward leaking.
 */

#include "gi/sdf_common.sh"
#include "gi/gpu_lights.sh"

/// x = shadow ray max distance, y = normal offset in world units, z = near-field handover,
/// w = max steps per shadow ray.
uniform vec4 u_gi_shadow_params;
#define u_gi_shadow_distance    u_gi_shadow_params.x
#define u_gi_shadow_normal_bias u_gi_shadow_params.y
#define u_gi_shadow_near_field  u_gi_shadow_params.z
#define u_gi_shadow_max_steps   int(u_gi_shadow_params.w)

/**
 * Visibility from a surface point toward a light. 1 is fully lit, 0 fully occluded.
 *
 * The ray starts offset along the surface normal. Without that it begins exactly on the
 * surface it was cast from, where the field reads zero, and every ray immediately reports
 * itself as occluded -- the whole scene goes black, which looks like a broken light rather
 * than a self-intersection.
 */
float GiTraceShadow(vec3 world_position, vec3 world_normal, vec3 to_light, float light_distance)
{
	vec3 origin = world_position + world_normal * u_gi_shadow_normal_bias;
	float max_distance = min(light_distance, u_gi_shadow_distance);
	if(max_distance <= u_gi_shadow_normal_bias)
	{
		return 1.0;
	}
	SdfRayHit hit = SdfTraceRay(origin, to_light, max_distance, u_gi_shadow_near_field,
	                            u_gi_shadow_max_steps, 0.5, 0.0);
	// A ray that ran out of budget without resolving is treated as LIT. The alternative,
	// treating it as occluded, turns every exhausted ray into a black patch; over-lighting a
	// region degrades far more gracefully than stamping shadow onto it.
	return hit.hit ? 0.0 : 1.0;
}

/**
 * Irradiance arriving at a world point from one light, with traced visibility.
 * Lambertian: multiply by albedo / PI for outgoing radiance.
 */
vec3 GiEvalLight(GpuLight light, vec3 world_position, vec3 world_normal)
{
	vec3 unshadowed = GpuEvalLightUnshadowed(light, world_position, world_normal);
	// Nothing to occlude, so skip the ray entirely. This is the common case for a point far
	// outside a light's range, and shadow rays are by far the most expensive part of this.
	if(dot(unshadowed, unshadowed) <= 0.0)
	{
		return vec3_splat(0.0);
	}
	vec3 to_light;
	float light_distance;
	if(light.type == GPU_LIGHT_TYPE_DIRECTIONAL)
	{
		to_light = -light.direction;
		light_distance = u_gi_shadow_distance;
	}
	else
	{
		vec3 delta = light.position - world_position;
		light_distance = length(delta);
		to_light = light_distance > 1e-6 ? delta / light_distance : world_normal;
	}
	return unshadowed * GiTraceShadow(world_position, world_normal, to_light, light_distance);
}

/**
 * Total irradiance at a world point from every resident light, with traced visibility.
 */
vec3 GiEvalDirectLighting(vec3 world_position, vec3 world_normal)
{
	vec3 total = vec3_splat(0.0);
	for(int i = 0; i < u_gpu_light_count; ++i)
	{
		total += GiEvalLight(GpuLoadLight(i), world_position, world_normal);
	}
	return total;
}

#endif // __GI_LIGHTING_SH__
