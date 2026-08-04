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

/// x = shadow ray max distance, y = normal offset in VOXELS of the answering level,
/// z = near-field handover, w = max steps per shadow ray.
uniform vec4 u_gi_shadow_params;
#define u_gi_shadow_distance    u_gi_shadow_params.x
#define u_gi_shadow_normal_bias u_gi_shadow_params.y
#define u_gi_shadow_near_field  u_gi_shadow_params.z
#define u_gi_shadow_max_steps   int(u_gi_shadow_params.w)

/// x = hit acceptance in voxels, y = cone relaxation.
///
/// Both were hardcoded (0.5 and 0.0) until the relaxation turned out to matter more here than
/// anywhere else. A shadow ray toward a low sun runs nearly parallel to the ground, and a grazing
/// sphere trace advances by a distance that stays small for its whole length -- so it burns its
/// whole step budget without resolving. An exhausted ray is counted as LIT (see below), so the
/// failure does not read as a missing shadow, it reads as a surface that is too bright, and it
/// gets worse the further the origin is pushed out to escape self-intersection.
uniform vec4 u_gi_shadow_params2;
#define u_gi_shadow_surface_bias u_gi_shadow_params2.x
#define u_gi_shadow_relaxation   u_gi_shadow_params2.y
/// The FINEST cascade voxel, which the voxel-relative normal bias below is held to.
#define u_gi_shadow_finest_voxel u_gi_shadow_params2.z
/// How far along the ray a shadow ray starts, in voxels. Same reasoning as the gather ray:
/// see gi_resolve_pass::settings::ray_start_voxels.
#define u_gi_shadow_ray_start    u_gi_shadow_params2.w

/**
 * Visibility from a surface point toward a light. 1 is fully lit, 0 fully occluded.
 *
 * The ray starts offset along the surface normal. Without that it begins exactly on the
 * surface it was cast from, where the field reads zero, and every ray immediately reports
 * itself as occluded -- the whole scene goes black, which looks like a broken light rather
 * than a self-intersection.
 *
 * @param voxel_size Voxel size of the cascade level covering the point, which the caller already
 *        has. The offset is a count of VOXELS rather than a world distance, for the same reason
 *        the trace's hit acceptance is: what it has to clear is the field's own resolution, and
 *        the cascade's voxel spans 0.25 m to 2 m, so no fixed distance works at both ends. Too
 *        small and every shadow ray starts occluded, which converges the entry to black -- and
 *        black entries are indistinguishable from correctly shadowed ones in the final image.
 */
float GiTraceShadow(vec3 world_position, vec3 world_normal, vec3 to_light, float light_distance,
                    float voxel_size)
{
	// Scaled by the level that ANSWERS, not held to the finest one: this has to clear that
	// level's own hit acceptance, which is surface_bias voxels of it. See the gather.
	float offset = u_gi_shadow_normal_bias * voxel_size;
	vec3 origin = world_position + world_normal * offset;
	float max_distance = min(light_distance, u_gi_shadow_distance);
	if(max_distance <= offset)
	{
		return 1.0;
	}
	// Along the ray rather than further along the normal, for the reason the gather ray gives:
	// a normal offset moves the shaded point and lets it see past nearby occluders, which reads as
	// a surface that is simply too bright with nothing to say why.
	origin += to_light * (u_gi_shadow_ray_start * voxel_size);
	SdfRayHit hit = SdfTraceRay(origin, to_light, max_distance, u_gi_shadow_near_field,
	                            u_gi_shadow_max_steps, u_gi_shadow_surface_bias,
	                            u_gi_shadow_relaxation, false);
	// A ray that ran out of budget without resolving is treated as LIT. The alternative, treating
	// it as occluded, turns every exhausted ray into a black patch; over-lighting a region degrades
	// far more gracefully than stamping shadow onto it.
	//
	// That choice is why the relaxation above matters so much here. Exhaustion does not present as
	// a missing shadow -- it presents as a surface that is simply too bright, with nothing to say
	// the ray gave up. Widening the acceptance lets a grazing ray terminate at a bounded step count
	// instead, and it can only ever stop a ray EARLY, so it errs toward finding the occluder rather
	// than missing it. Cheaper and darker, both in the safe direction for this particular ray.
	return hit.hit ? 0.0 : 1.0;
}

/**
 * Irradiance arriving at a world point from one light, with traced visibility.
 * Lambertian: multiply by albedo / PI for outgoing radiance.
 */
vec3 GiEvalLight(GpuLight light, vec3 world_position, vec3 world_normal, float voxel_size)
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
	return unshadowed * GiTraceShadow(world_position, world_normal, to_light, light_distance, voxel_size);
}

/**
 * Total irradiance at a world point from every resident light, with traced visibility.
 */
vec3 GiEvalDirectLighting(vec3 world_position, vec3 world_normal, float voxel_size)
{
	vec3 total = vec3_splat(0.0);
	for(int i = 0; i < u_gpu_light_count; ++i)
	{
		total += GiEvalLight(GpuLoadLight(i), world_position, world_normal, voxel_size);
	}
	return total;
}

#endif // __GI_LIGHTING_SH__
