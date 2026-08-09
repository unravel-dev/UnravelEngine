$input v_texcoord0

/*
 * Distance field debug view.
 *
 * The default modes trace through SdfTraceRay -- the SAME function the GI pass will use -- so
 * what is on screen is the tracer itself, not a parallel implementation that could drift from
 * it. The remaining modes are diagnostics that need per-instance internals a clean tracing API
 * should not expose, so they run their own minimal loop.
 *
 * Stages 0-4 are reserved by sdf_common.sh.
 */

#include "../common.sh"
#include "gi/sdf_common.sh"
#include "gi/gpu_lights.sh"
#include "gi/gi_lighting.sh"


uniform vec4 u_sdf_debug_params;
#define u_max_steps    int(u_sdf_debug_params.x)
#define u_max_distance u_sdf_debug_params.y
#define u_debug_mode   int(u_sdf_debug_params.z)
/// Hit threshold as a fraction of ONE VOXEL of the field being traced, not a world distance.
/// See sdf_debug_pass::settings::surface_bias.
#define u_surface_bias u_sdf_debug_params.w

uniform vec4 u_sdf_debug_params2;
/// Distance at which the per-instance tier hands over to the global cascade.
#define u_near_field_distance u_sdf_debug_params2.x
/// Minimum march step as a fraction of distance travelled. See SdfMinStep.
#define u_step_relaxation     u_sdf_debug_params2.y
/// Camera position, needed to pick a cache level for a traced hit.
uniform vec4 u_gi_debug_camera;
#define u_gi_debug_max_samples u_gi_debug_camera.w

#define SDF_DEBUG_NORMALS    0
#define SDF_DEBUG_STEP_COUNT 1
#define SDF_DEBUG_HEADERS    2
#define SDF_DEBUG_PROBE      3
#define SDF_DEBUG_ENTRY      4
#define SDF_DEBUG_CLIPMAP    5
#define SDF_DEBUG_DIRECT     6
#define SDF_DEBUG_CASCADE_LEVELS 7
#define SDF_DEBUG_ATTR_ALBEDO 8
#define SDF_DEBUG_LIGHT_VOXELS 9
#define SDF_DEBUG_WORLD_PROBES 10

#define GI_WORLD_PROBE_READ
#include "gi/gi_world_probes.sh"

/// Attribute albedo volume (toroidal slots, levels stacked along Z at attribute resolution).
/// The light volume and its slot math come from gi_light_voxels.sh (stage 10).
SAMPLER3D(s_attr_albedo, 8);
#define GI_LIGHT_VOXEL_READ
#include "gi/gi_light_voxels.sh"

/// Shades a traced hit by its normal, with a headlight so shape reads clearly.
vec4 ShadeNormal(vec3 normal)
{
	vec3 view_dir = -normalize(mul(u_invView, vec4(0.0, 0.0, 1.0, 0.0)).xyz);
	float lambert = saturate(dot(normal, -view_dir)) * 0.7 + 0.3;
	return vec4((normal * 0.5 + vec3_splat(0.5)) * lambert, 1.0);
}

/**
 * Per-instance introspection modes. These report what a specific stage of the residency path
 * resolved to, which the traced modes cannot show: every failure in that path degrades into
 * the same per-pixel noise regardless of which stage broke.
 */
bool RunDiagnosticMode(vec3 ray_origin, vec3 ray_dir, out vec4 out_color)
{
	out_color = vec4(0.0, 0.0, 0.0, 0.0);
	vec3 inv_dir = 1.0 / max(abs(ray_dir), vec3_splat(1e-8)) * sign(ray_dir + vec3_splat(1e-20));
	for(int i = 0; i < u_sdf_instance_count; ++i)
	{
		SdfInstance inst = SdfLoadInstance(i);
		float t_near;
		float t_far;
		if(!SdfIntersectBounds(ray_origin, inv_dir, inst.world_bounds_min, inst.world_bounds_max,
		                       u_max_distance, t_near, t_far))
		{
			continue;
		}
		SdfHeader header = SdfLoadHeader(inst.header_index);
		if(u_debug_mode == SDF_DEBUG_ENTRY)
		{
			// Classifies the FIRST sample of a march, at the point where the ray enters the
			// bounds. That is the only place the instance scale and the hit threshold are
			// applied, and the one point the probe mode does not inspect.
			//   GREEN -> healthy, comfortably above the threshold.
			//   BLUE  -> positive but under the threshold, so it reads as a hit.
			//   RED   -> negative, reads as a hit.
			vec3 entry_local = SdfTransformPoint(inst.world_to_local_rows, ray_origin + ray_dir * t_near);
			float entry_distance = SdfSampleLocal(header, entry_local) * inst.local_to_world_scale;
			float entry_threshold = max(u_surface_bias * header.voxel_size * inst.local_to_world_scale, 1e-6);
			// Explicit branches rather than a chained ternary: HLSL rejects nested ternaries
			// whose arms it cannot unify, and the error surfaces only on the D3D backend.
			if(entry_distance < 0.0)
			{
				out_color = vec4(1.0, 0.0, 0.0, 1.0);
			}
			else if(entry_distance < entry_threshold)
			{
				out_color = vec4(0.0, 0.0, 1.0, 1.0);
			}
			else
			{
				out_color = vec4(0.0, 1.0, 0.0, 1.0);
			}
			return true;
		}
		if(u_debug_mode == SDF_DEBUG_PROBE)
		{
			// What the brick lookup resolved to just inside the bounds, where a correct field
			// must report an EMPTY brick.
			//   RED   -> empty brick, as expected. The residency path is working.
			//   GREEN -> surface brick with a non-zero texel: atlas has data but the
			//            indirection resolved to the wrong brick.
			//   BLACK -> surface brick reading a zero texel: the atlas was never written.
			vec3 probe_local = SdfTransformPoint(inst.world_to_local_rows,
			                                     ray_origin + ray_dir * (t_near + 0.05));
			vec4 probe = SdfProbeLocal(header, probe_local);
			out_color = vec4(probe.x, probe.y, probe.z, 1.0);
			return true;
		}
		if(u_debug_mode == SDF_DEBUG_HEADERS)
		{
			// Header contents, so a buffer that never arrived reads as black rather than being
			// inferred from a wrong-looking trace.
			//   red = voxel size, green = grid dimension / 256, blue = header present
			float has_size = header.voxel_size > 0.0 ? 1.0 : 0.0;
			out_color = vec4(saturate(header.voxel_size * 20.0),
			                 saturate(header.grid_dim.x / 256.0),
			                 has_size,
			                 1.0);
			return true;
		}
	}
	return false;
}

void main()
{
	vec2 uv = v_texcoord0;
	// World-space ray through this pixel.
	//
	// Built with the engine's own clip helpers and deliberately without assuming which end of
	// the depth range is near: unprojecting one point at an arbitrary depth and taking the
	// direction from the camera is correct under either convention, so this cannot break if
	// the project ever switches to a reversed range.
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(0.5)));
	vec3 world_point = clipToWorld(u_invViewProj, clip);
	vec3 ray_origin = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	vec3 ray_dir = normalize(world_point - ray_origin);

	if(u_debug_mode == SDF_DEBUG_HEADERS || u_debug_mode == SDF_DEBUG_PROBE ||
	   u_debug_mode == SDF_DEBUG_ENTRY)
	{
		vec4 diagnostic;
		if(RunDiagnosticMode(ray_origin, ray_dir, diagnostic))
		{
			gl_FragColor = diagnostic;
			return;
		}
		gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
		return;
	}

	// The cascade in isolation. Worth seeing on its own: a fault in it is invisible in the
	// combined trace, where the per-instance tier covers the near field and hides it.
	SdfRayHit hit;
	// The cascade-level view traces the cascade alone for the same reason the clipmap view does:
	// a combined trace resolves the near field against per-instance fields, so the hit would not
	// be on the cascade's isosurface and the level it reports would not be the one under test.
	if(u_debug_mode == SDF_DEBUG_CLIPMAP || u_debug_mode == SDF_DEBUG_CASCADE_LEVELS)
	{
		// This is the one consumer that wants the hit normal -- the normals view shades by it. The
		// GI passes all re-derive the facing from SdfResolveSurfacePoint instead and never read
		// this field, so they ask the tracer to skip the gradient entirely.
		hit = SdfTraceClipmap(ray_origin, ray_dir, 0.0, u_max_distance, u_max_steps, u_surface_bias,
		                      u_step_relaxation, true, 0.0);
	}
	else
	{
		hit = SdfTraceRay(ray_origin, ray_dir, u_max_distance, u_near_field_distance, u_max_steps,
		                  u_surface_bias, u_step_relaxation, true);
	}

	if(u_debug_mode == SDF_DEBUG_STEP_COUNT)
	{
		// Step-count heat map: green is cheap, red is expensive.
		//
		// BLUE marks a ray that used its whole budget without resolving, which is what makes
		// the traced surface fade out at distance and at shallow angles: sphere tracing
		// converges slowly along a ray grazing a surface, because every step is limited by a
		// distance that stays small the whole way. Without this, an exhausted ray and a ray
		// that genuinely hit nothing look identical.
		if(!hit.hit)
		{
			gl_FragColor = hit.exhausted ? vec4(0.0, 0.0, 1.0, 1.0) : vec4(0.0, 0.0, 0.0, 0.0);
			return;
		}
		float cost = saturate(float(hit.steps) / float(max(u_max_steps, 1)));
		gl_FragColor = vec4(cost, 1.0 - cost, 0.0, 1.0);
		return;
	}

	if(!hit.hit)
	{
		gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
		return;
	}

	if(u_debug_mode == SDF_DEBUG_CASCADE_LEVELS)
	{
		// WHICH cascade answered at the traced surface, and how far into its cross-fade band it
		// is. Levels are composed independently at different voxel sizes, so their isosurfaces do
		// not coincide; where two consumers straddle a boundary they resolve onto points a voxel
		// apart and stop finding each other's cache entries. That failure is invisible in every
		// other view -- it looks like a cache that misses, not like a geometry problem -- so
		// seeing the boundaries directly is what makes it diagnosable.
		//
		//   RED / GREEN / BLUE / YELLOW -> levels 0..3, finest first.
		//   A smooth GRADIENT between two of those is the blend band doing its job.
		//   A hard edge means the fade is off (blend width 0) and the field steps there.
		vec3 level_color[4];
		level_color[0] = vec3(1.0, 0.15, 0.15);
		level_color[1] = vec3(0.15, 1.0, 0.15);
		level_color[2] = vec3(0.2, 0.4, 1.0);
		level_color[3] = vec3(1.0, 0.9, 0.2);
		float blend;
		float answered_voxel_size;
		int level = SdfFindClipmapLevel(ray_origin + ray_dir * hit.t, blend, answered_voxel_size);
		if(level >= SDF_CLIPMAP_LEVEL_COUNT)
		{
			// Outside every cascade, so the per-instance tier is the only thing that answered.
			gl_FragColor = vec4(0.25, 0.25, 0.25, 1.0);
			return;
		}
		vec3 color = level_color[level];
		if(level + 1 < SDF_CLIPMAP_LEVEL_COUNT)
		{
			color = mix(color, level_color[level + 1], blend);
		}
		gl_FragColor = vec4(color, 1.0);
		return;
	}

	if(u_debug_mode == SDF_DEBUG_ATTR_ALBEDO || u_debug_mode == SDF_DEBUG_LIGHT_VOXELS)
	{
		// GI scene representation, read exactly as a gather ray will read it: the traced hit
		// mapped to its attribute voxel in the finest covering cascade.
		//
		//   MAGENTA -> hit outside every cascade level (per-instance tier answered alone).
		//   YELLOW  -> the voxel is not marked SURFACE (attribution missed the hit - a band or
		//              gradient-gate failure worth investigating if widespread).
		vec3 hit_position = ray_origin + ray_dir * hit.t;
		float attr_blend;
		float attr_answered_voxel;
		int attr_level = SdfFindClipmapLevel(hit_position, attr_blend, attr_answered_voxel);
		if(attr_level >= SDF_CLIPMAP_LEVEL_COUNT)
		{
			gl_FragColor = vec4(1.0, 0.0, 1.0, 1.0);
			return;
		}
		// Level fallback, exactly as GiLightVoxelRead performs it: the blended isosurface can sit
		// a coarse voxel off the finest level's own, so a reader steps down until a level's
		// surface band covers the hit. Yellow now means NO level attributed it.
		int attr_res = u_light_voxel_resolution;
		vec4 albedo = vec4_splat(0.0);
		for(int probe_level = attr_level; probe_level < SDF_CLIPMAP_LEVEL_COUNT; ++probe_level)
		{
			vec4 level_data = u_sdf_clipmap_levels[probe_level];
			if(!(level_data.w > 0.0))
			{
				continue;
			}
			float attr_voxel_size = level_data.w * 2.0;
			ivec3 slot = GiLightVoxelSlot(GiLightVoxelCell(hit_position, attr_voxel_size));
			vec4 candidate = texelFetch(s_attr_albedo, ivec3(slot.x, slot.y, slot.z + probe_level * attr_res), 0);
			if(candidate.a > 0.0)
			{
				albedo = candidate;
				break;
			}
		}
		if(albedo.a <= 0.0)
		{
			gl_FragColor = vec4(1.0, 0.9, 0.0, 1.0);
			return;
		}
		if(u_debug_mode == SDF_DEBUG_ATTR_ALBEDO)
		{
			gl_FragColor = vec4(albedo.xyz, 1.0);
			return;
		}
		// Light voxels: exactly what a gather ray reads, through the shared reader.
		//
		//   DARK BLUE -> attributed but UNMEASURED: every candidate face was zeroed by the
		//                lighting pass's gates (cavity visibility, tunnel guard) or has not had
		//                a rotation slot yet. Distinct from measured darkness (true black),
		//                because the two implicate different stages: a gate refusing the face
		//                versus a shadow ray measuring no light.
		vec3 radiance;
		if(!GiLightVoxelRead(hit_position, hit.normal, radiance))
		{
			gl_FragColor = vec4(0.0, 0.05, 0.35, 1.0);
			return;
		}
		gl_FragColor = vec4(radiance, 1.0);
		return;
	}

	if(u_debug_mode == SDF_DEBUG_WORLD_PROBES)
	{
		// World probe irradiance interpolated AT THE TRACED HIT through the full DDGI weight
		// chain - exactly what the light voxels' bounce term and a shortened gather ray's
		// completion will read.
		//
		//   MAGENTA -> no cascade's probe window covers the hit, or every cage weight died.
		vec3 hit_position = ray_origin + ray_dir * hit.t;
		vec3 probe_irradiance;
		float sky_fraction;
		if(!GiWorldProbeIrradianceCascade(hit_position,
		                                  hit.normal,
		                                  -ray_dir,
		                                  u_gi_debug_camera.xyz,
		                                  probe_irradiance,
		                                  sky_fraction))
		{
			gl_FragColor = vec4(1.0, 0.0, 1.0, 1.0);
			return;
		}
		gl_FragColor = vec4(probe_irradiance, 1.0);
		return;
	}

	if(u_debug_mode == SDF_DEBUG_DIRECT)
	{
		// Direct lighting evaluated AT THE TRACED HIT, from the resident light buffer. This is
		// the operation the surface cache is built on: given a point found by a ray, work out
		// how much light reaches it. Seeing it standalone verifies the whole chain -- fields
		// resident, ray traced, hit position and normal correct, lights enumerable from a
		// shader -- before any caching or accumulation is layered on top.
		//
		// Shadowing is resolved by tracing the fields toward each light, so it covers
		// geometry anywhere -- including outside any shadow map's frustum.
		vec3 hit_position = ray_origin + ray_dir * hit.t;
		// Visibility comes from tracing the fields toward each light, not from a shadow map.
		float direct_voxel;
		SdfSampleClipmapEx(hit_position, direct_voxel);
		vec3 irradiance = GiEvalDirectLighting(hit_position,
		                                       hit.normal,
		                                       max(direct_voxel, 0.01),
		                                       u_gi_shadow_near_field);
		// A neutral albedo keeps this a view of the LIGHTING rather than of surface colour,
		// which the fields do not carry yet (material voxels are a later phase).
		gl_FragColor = vec4(irradiance * 0.8, 1.0);
		return;
	}

	gl_FragColor = ShadeNormal(hit.normal);
}
