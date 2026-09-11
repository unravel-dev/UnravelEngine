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
 *     L(w) / (n_c(cell) * p_c(w) + sum over aimed cones containing w of n_e / Omega_e)
 *
 * to the cell its direction lands in, and the cell's radiance is that sum over Omega_cell.
 * p_c is the solid-angle PDF of the cell's uniform octahedral UV sampler, including its
 * projection Jacobian. With no emitter aimed at, this estimates the cell's solid-angle
 * mean rather than its UV mean; those differ because the octahedral map is not equal-area.
 *
 * The emitter table rides after the instances in b_sdf_instances (the tracers bind it
 * already; no stage was free for a buffer of its own): SDF_EMITTER_STRIDE vec4s each,
 * (center, radius), (radiance, packed extent), u_sdf_emitter_count entries. The fourth lane
 * carries the piece's extent, not its power - the ranking weight is rebuilt from the extent
 * and the radiance (GiLoadEmitter).
 * MIRROR OF surface_cache_system::emitter / upload_instances and gi_emitter_packing.h.
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
	/// The piece's ranking weight, luminance x emitting area - ALWAYS POSITIVE. The upload
	/// spends the fourth lane on the extent, so this is RECONSTRUCTED from the decode below
	/// rather than read; only a legacy table (a positive lane) supplies it directly.
	float power;
	/// The piece's axis-aligned extent in metres, decoded from the power lane (the upload
	/// packs it there, negated and offset by one); zero with has_extent false when the
	/// table came from an upload that still wrote the power.
	vec3 extent;
	bool has_extent;
};

float GiEmitterLuminance(GiEmitter e)
{
	return dot(e.radiance, vec3(0.2126, 0.7152, 0.0722));
}

/// Full box area of an axis-aligned piece. MIRROR OF gi::emitter_surface_area.
float GiEmitterSurfaceArea(vec3 extent)
{
	return 2.0 * (extent.x * extent.y + extent.y * extent.z + extent.z * extent.x);
}

/// The area of a piece that can radiate - its two largest faces, the ranking weight's basis.
/// MIRROR OF gi::emitter_emitting_area; see that function for why the box area is wrong here.
float GiEmitterEmittingArea(vec3 extent)
{
	float smallest = min(extent.x, min(extent.y, extent.z));
	float largest = max(extent.x, max(extent.y, extent.z));
	float middle = (extent.x + extent.y + extent.z) - smallest - largest;
	return 2.0 * largest * middle;
}

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
	e.extent = vec3_splat(0.0);
	e.has_extent = e1.w < -0.5;
	if(e.has_extent)
	{
		// 'packed' is a GLSL keyword (a layout qualifier); the lane is named for what it holds.
		float extent_bits = -e1.w - 1.0;
		float x8 = floor(mod(extent_bits, 256.0));
		float y8 = floor(mod(extent_bits / 256.0, 256.0));
		float z8 = floor(extent_bits / 65536.0);
		e.extent = vec3(x8, y8, z8) * (GI_EMISSIVE_NEE_SEGMENT / 255.0);
		// The packed lane is NEGATIVE, so leaving it in power made every consumer's score
		// negative: the reflection near-field's descending top-K starts its scores at zero,
		// so no piece was ever selected and the correction silently returned its identity.
		// MIRROR OF gi::emitter_selection_weight - the same weight the CPU ranks the table by.
		e.power = GiEmitterLuminance(e) * GiEmitterEmittingArea(e.extent);
	}
	return e;
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
