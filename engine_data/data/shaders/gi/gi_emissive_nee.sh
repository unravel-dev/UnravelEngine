#ifndef __GI_EMISSIVE_NEE_SH__
#define __GI_EMISSIVE_NEE_SH__

/*
 * Explicit emissive sampling for the probe tracers (GI S4, next-event estimation).
 *
 * A probe's cone rays find a small emitter by chance: a bulb-sized source at a few metres
 * subtends well under one percent of the hemisphere, so its energy arrives as rare bright
 * hits that the temporal filter has to average down - the blotches and the crawl on
 * emissive-lit walls. Here the probe instead AIMS a few rays at each of the brightest
 * emitters it can see: uniform directions inside the emitter's bounding-sphere cone,
 * traced through the real field like any other ray, so the sample reads whatever surface
 * is actually there - the emitter, or past a miss what stands behind it. Nothing about
 * the emitter changes: it keeps its shape, its cache radiance and its traced soft shadows,
 * and the cost is the same however large or small it is.
 *
 * Combined with the cone rays by multiple importance sampling (the balance heuristic
 * over fixed sample counts): every ray, cell-jittered or aimed, contributes
 *
 *     L(w) / (n_c(cell) / Omega_cell + sum over aimed cones containing w of n_e / Omega_e)
 *
 * to the cell its direction lands in, and the cell's radiance is that sum over Omega_cell.
 * With no emitter aimed at, this is exactly the per-cell mean the kernel always stored.
 *
 * The emitter table rides after the instances in b_sdf_instances (the tracers bind it
 * already; no stage was free for a buffer of its own): SDF_EMITTER_STRIDE vec4s each,
 * (center, radius), (radiance, power), u_sdf_emitter_count entries.
 * MIRROR OF surface_cache_system::emitter / upload_instances.
 *
 * Every helper returns by value: the shaderc HLSL path miscompiles out-parameters in
 * .sh helpers silently (tasks/lessons.md).
 */

#include "gi/gi_constants.sh"

#define SDF_EMITTER_STRIDE 2

#define GI_NEE_PI 3.1415926535897932

struct GiEmitter
{
	vec3 center;
	float radius;
	vec3 radiance;
	float power;
};

/// The emitter's bounding-sphere cone as seen from one point.
struct GiEmitterCone
{
	vec3 axis;
	float cos_max;
	float distance;
	/// False when the point lies inside the sphere - the cell rays own that case (the
	/// emitter fills the hemisphere and needs no aiming).
	bool valid;
};

GiEmitter GiLoadEmitter(int index)
{
	uint base = uint(u_sdf_instance_count) * uint(SDF_INSTANCE_STRIDE) + uint(index) * uint(SDF_EMITTER_STRIDE);
	vec4 e0 = b_sdf_instances[base + 0u];
	vec4 e1 = b_sdf_instances[base + 1u];
	GiEmitter e;
	e.center = e0.xyz;
	e.radius = e0.w;
	e.radiance = e1.xyz;
	e.power = e1.w;
	return e;
}

float GiEmitterLuminance(GiEmitter e)
{
	return dot(e.radiance, vec3(0.2126, 0.7152, 0.0722));
}

GiEmitterCone GiEmitterConeFrom(GiEmitter e, vec3 origin)
{
	GiEmitterCone cone;
	vec3 delta = e.center - origin;
	float distance = length(delta);
	cone.distance = distance;
	cone.axis = vec3(0.0, 1.0, 0.0);
	cone.cos_max = 1.0;
	cone.valid = false;
	if(distance <= e.radius * 1.05 || distance <= 1e-4)
	{
		return cone;
	}
	cone.axis = delta / distance;
	float sin_max = e.radius / distance;
	cone.cos_max = sqrt(max(1.0 - sin_max * sin_max, 0.0));
	cone.valid = true;
	return cone;
}

/// Solid angle of a cone with the given cosine half angle.
float GiConeSolidAngle(float cos_max)
{
	return 2.0 * GI_NEE_PI * (1.0 - cos_max);
}

/// Uniform direction inside the cone around @p axis from two uniform variates.
vec3 GiSampleCone(vec3 axis, float cos_max, vec2 xi)
{
	float cos_theta = 1.0 - xi.x * (1.0 - cos_max);
	float sin_theta = sqrt(max(1.0 - cos_theta * cos_theta, 0.0));
	float phi = 2.0 * GI_NEE_PI * xi.y;
	// A basis around the axis, switching the helper vector away from the pole.
	vec3 up = abs(axis.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
	vec3 tangent = normalize(cross(up, axis));
	vec3 bitangent = cross(axis, tangent);
	return normalize(tangent * (sin_theta * cos(phi)) + bitangent * (sin_theta * sin(phi)) + axis * cos_theta);
}

#endif // __GI_EMISSIVE_NEE_SH__
