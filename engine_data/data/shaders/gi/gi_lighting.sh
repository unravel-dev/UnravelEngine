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

#if defined(GI_SUN_SHADOWMAP_TIER)
/*
 * SUN SHADOW-MAP TIER: cascade 0 of the sun's own CSM, sampled instead of traced wherever a
 * position lands inside it.
 *
 * The traced field CANNOT answer the sun through real openings the bake fattened shut:
 * material-grouped architecture bakes whole arcades as one submesh, mostly non-manifold, so
 * every member becomes a two-sided shell floored at one mesh voxel - metres-scale slabs at
 * production resolutions - and the composed cascade inherits the fat. Measured on Sponza:
 * the corridor's light voxels converged black while the raster's shadow-mapped sun pools lit
 * the same floor on screen. The shadow map IS the raster's answer, so sampling it makes the
 * GI's notion of "where the sun lands" agree with the image by construction, and one tap is
 * far cheaper than the sphere trace it replaces.
 *
 * ONE split only, deliberately: the includer (cs_gi_light_voxels) has exactly one free
 * resource stage, and split 0 is the sharpest and covers the camera's neighbourhood - which
 * is where level-0/1 voxels live, the only cells fine enough to hold a sun pool anyway.
 * Outside its texcoord bounds, and for every other light, the traced field remains the
 * answer. Gated by a define so the debug direct view keeps showing the PURE traced tier -
 * the diagnostic contrast that found this bug.
 */
SAMPLER2D(s_gi_sun_shadowmap, 14);
uniform mat4 u_gi_sun_shadowmap_mtx;
/// x = light-buffer index of the sun the bound map belongs to (< 0 disables the tier),
/// y = cascade-0 constant receiver bias in stored depth (the generator's texel bias converted),
/// z = texcoord border, w = d(stored depth)/d(world distance along the sun).
uniform vec4 u_gi_sun_shadowmap_params;
/// The camera's (TAA-unjittered) view-projection - the frustum cascade 0 was fitted to.
uniform mat4 u_gi_sun_shadowmap_camera_vp;
/// x = cascade 0's view-space far distance (the slice's far plane); yzw unused.
uniform vec4 u_gi_sun_shadowmap_slice;
#define u_gi_sun_index          u_gi_sun_shadowmap_params.x
#define u_gi_sun_bias           u_gi_sun_shadowmap_params.y
#define u_gi_sun_border         u_gi_sun_shadowmap_params.z
#define u_gi_sun_world_to_depth u_gi_sun_shadowmap_params.w

/// The quadrature taps sit half a voxel off the face centre; a face tilted toward the sun
/// puts them at different depths than the centre. One voxel of receiver depth covers faces
/// tilted up to about 63 degrees, beyond which the face receives little sun anyway.
#define GI_SUN_SHADOWMAP_SLOPE_COVER_VOXELS 1.0

/**
 * Sun visibility from the bound cascade-0 map, when it covers @p world_position.
 *
 * AREA average, not a point sample: the receiver is a whole attribute-voxel FACE (up to
 * metres at coarse levels), and sun pools at that scale are cell-sized - a single centre
 * tap answers "is this exact point lit" and quantises a 40%-sunlit cell to all-or-nothing,
 * which erased every pool beyond the finest window (measured: the injected pools stopped
 * at level 0's edge). A 2x2 quadrature over the face integrates fractional coverage, which
 * is exactly the energy the cell re-emits. The face is axis-aligned, so its tangents are
 * axis permutations, and the projection is affine, so the four coords are two vector adds
 * each.
 *
 * Split out of GiEvalLight so the sun-tier debug view (cs_gi_light_voxels) attributes
 * coverage through EXACTLY the code the lighting takes - a parallel implementation would
 * drift and the attribution would lie.
 *
 * COARSE LEVELS DECLINE (GI_SUN_SHADOWMAP_MAX_VOXEL). The receiver bias below is one LEVEL
 * voxel of light-space depth, which the quadrature genuinely needs - and which also means the
 * tier reports LIT through any occluder thinner than that voxel. At the coarse cascades that
 * is metres, so a sealed room whose roof is thinner than one cell is lit from outside through
 * a path no field defence can see: not the SDF, not the cage visibility, not the dead-probe
 * gate (measured: interior ceiling brightest, sun-white, falling off downward, the walls
 * merely bouncing it). There is no bias that is simultaneously acne-free and leak-free over a
 * metre-wide face, so the honest move is to decline and let the traced field answer, exactly
 * as it did before this tier existed. The gate sits FIRST: declining costs one compare, and
 * it skips the projection and the four taps as well.
 *
 * @param voxel_size Voxel of the answering cascade level: the quadrature half-extent.
 * @return true when the map answered; @p out_lit then holds the lit fraction. False means
 *         out of cascade-0 coverage or too coarse a level, and the traced field must answer.
 */
bool GiSunShadowmapVisibility(vec3 world_position, vec3 world_normal, float voxel_size, out float out_lit)
{
	out_lit = 0.0;
	if(voxel_size > GI_SUN_SHADOWMAP_MAX_VOXEL)
	{
		return false;
	}
	// THE SLICE CONTRACT. Cascade 0 is fitted to the camera's near frustum slice, and the
	// raster samples it for nothing outside that slice. Its crop footprint - a bounding sphere
	// of the slice - reaches metres BEHIND and beside the camera, so a world-space receiver
	// there projects inside the map's texcoords while nothing about the fit is contracted for
	// it. Measured: faces of a sealed room BEHIND the camera read LIT through this map while
	// the camera faced away, and every camera turn then revealed a lit room that decayed over
	// seconds through the relight EMA and the closed-room bounce (the first-look glow). A
	// receiver outside the slice - behind the near plane, past cascade 0's far plane, or
	// outside the field of view - declines here and the traced field answers, exactly as it
	// does past the map's edge. Costs one mat4 transform per face.
	vec4 camera_clip = mul(u_gi_sun_shadowmap_camera_vp, vec4(world_position, 1.0));
	if(camera_clip.w <= 0.0 || camera_clip.w > u_gi_sun_shadowmap_slice.x)
	{
		return false;
	}
	vec2 camera_ndc = camera_clip.xy / camera_clip.w;
	if(any(greaterThan(abs(camera_ndc), vec2_splat(1.0))))
	{
		return false;
	}
	vec4 shadow_coord = mul(u_gi_sun_shadowmap_mtx, vec4(world_position, 1.0));
	if(shadow_coord.w <= 1e-6)
	{
		return false;
	}
	vec2 texcoord = shadow_coord.xy / shadow_coord.w;
	if(any(lessThanEqual(texcoord, vec2_splat(u_gi_sun_border))) ||
	   any(greaterThanEqual(texcoord, vec2_splat(1.0 - u_gi_sun_border))))
	{
		return false;
	}
	// Quadrature points at the quarter-marks of the face: half-extent is one LEVEL voxel
	// (the attribute voxel spans two), taps at half that.
	float h = 0.5 * voxel_size;
	vec3 tangent = world_normal.yzx * h;
	vec3 bitangent = world_normal.zxy * h;
	vec4 delta_t = mul(u_gi_sun_shadowmap_mtx, vec4(tangent, 0.0));
	vec4 delta_b = mul(u_gi_sun_shadowmap_mtx, vec4(bitangent, 0.0));
	float lit = 0.0;
	float bias = u_gi_sun_bias + voxel_size * GI_SUN_SHADOWMAP_SLOPE_COVER_VOXELS * u_gi_sun_world_to_depth;
	for(int tap = 0; tap < 4; ++tap)
	{
		vec4 tap_coord = shadow_coord +
		                 (tap < 2 ? delta_t : -delta_t) +
		                 ((tap & 1) != 0 ? delta_b : -delta_b);
		float receiver = (tap_coord.z - bias) / tap_coord.w;
		float occluder = texture2DLod(s_gi_sun_shadowmap, tap_coord.xy / tap_coord.w, 0.0).x;
		lit += step(receiver, occluder);
	}
	out_lit = lit * 0.25;
	return true;
}
#endif // GI_SUN_SHADOWMAP_TIER

/// x = shadow ray max distance, y = normal offset in VOXELS of the answering level,
/// z = near-field handover, w = max steps per shadow ray.
uniform vec4 u_gi_shadow_params;
#define u_gi_shadow_distance    u_gi_shadow_params.x
#define u_gi_shadow_normal_bias u_gi_shadow_params.y
#define u_gi_shadow_near_field  u_gi_shadow_params.z
#define u_gi_shadow_max_steps   int(u_gi_shadow_params.w)

/// x = hit acceptance in voxels, y = cone relaxation.
///
/// The relaxation ships as ZERO for shadow rays: a shadow ray accepts CONTACT only. With a cone,
/// a near-miss within the answering level's voxel -- metres, at coarse levels -- resolved as a
/// hit, and a resolved hit is FULL occlusion below, so every sun ray threading a real opening
/// (a colonnade, a window, clearance over a roofline) went black (measured: Sponza's arcade
/// light voxels converged black corridor-wide). The grazing-cost problem the cone once solved
/// belongs to the exhaustion contract now: a budget-dead ray answers with its accumulated
/// clearance (see below), which reads a graze as penumbra rather than as washout or blackness.
uniform vec4 u_gi_shadow_params2;
#define u_gi_shadow_surface_bias u_gi_shadow_params2.x
#define u_gi_shadow_relaxation   u_gi_shadow_params2.y
/// z is reserved. It carried the finest cascade voxel while the normal bias was clamped to it;
/// the bias now scales by the level that answers and nothing reads it any more.
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
/// @param near_field Range in which per-instance fields are traced for this ray. A parameter
///        rather than the raw uniform so the caller can scale it per point: the cache update
///        fades it out for far-from-camera entries, where mesh-exact shadowing is invisible and
///        the cost is not.
float GiTraceShadow(vec3 world_position, vec3 world_normal, vec3 to_light, float light_distance,
                    float voxel_size, float near_field)
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
	//
	// A FIXED count, deliberately - do not scale this by incidence. A slope-aware start
	// (start / dot(ray, normal), tried in round 15c against what turned out to be the trace's
	// exhaustion blob) teleports the origin THROUGH any sun-facing wall closer than the scaled
	// skip, and the launch suppression then walks out the far side: measured as lit strips at
	// wall bases on the shadow side (test_shadow_blob_floor_building). Walking out of the launch
	// band at grazing incidence is the suppression walk's job, and budget death on long grazing
	// marches is answered by the trace's saturation step boost + the clearance fallback below.
	origin += to_light * (u_gi_shadow_ray_start * voxel_size);
	// Expand OFF (-1): an occlusion-only ray toward a light must not see surfaces fattened by
	// up to a coarse voxel diagonal. Both directions of this trade were MEASURED: expand from
	// the mesh-tier boundary onward visibly darkened sunlit Bistro (grazing rays along real
	// geometry, audit A1c's failure), while the leak it chased turned out to be the world-probe
	// self-shadow bias tunnelling through walls, not shadow rays at all. Thin-geometry defence
	// for these rays stays the bake-time shell floor within the mesh tier.
	SdfRayHit hit = SdfTraceRayEx(origin, to_light, max_distance, near_field,
	                              u_gi_shadow_max_steps, u_gi_shadow_surface_bias,
	                              u_gi_shadow_relaxation, false, -1.0);
	// Exhaustion now REPORTS A HIT inside the trace itself (GI trace rework - a ray that ran
	// out of budget occludes at its final position), so a grazing shadow ray that gives up reads
	// as shadowed rather than as a surface that is inexplicably too bright. Over-occlusion is the
	// direction that degrades gracefully, and it is the same contract every tracing consumer now
	// shares; with the relaxation at zero, grazing rays reach that contract instead of being
	// cone-caught early, and its clearance fallback is what grades them.
	//
	// BEAM visibility, not a binary ray: the receiver is a VOXEL, so what reaches it is a
	// parallel beam half a receiving voxel wide (= voxel_size, the level voxel: the attribute
	// voxel spans two of them). Clearance smaller than the half-width partially occludes the
	// beam - a continuous penumbra where the binary answer flipped per voxel, whose quantised
	// lit/unlit patchwork read as blotches through the trilinear read and the gather (measured:
	// the test room's walls near the door's light path). One min per march step pays for it.
	// EXHAUSTION IS NOT OCCLUSION for a sun ray: a march that ran out of budget while
	// GRAZING open space (long floor-parallel paths at low sun angles) reported as a hit and
	// stamped a deterministic black shadow blob onto every voxel whose ray grazed longest -
	// anchored to the cascade layout (camera position) and swinging with the light (measured,
	// round 15; same failure Bistro exposed in the bounce). The ray never FOUND a surface, so
	// its beam clearance is the honest answer: a corridor at least a voxel wide stays lit, a
	// hug-the-floor graze keeps a proportional penumbra. Resolved hits stay fully dark.
	if(hit.hit && !hit.exhausted)
	{
		return 0.0;
	}
	return saturate(hit.clearance / max(voxel_size, 1e-4));
}

/**
 * Irradiance arriving at a world point from one light, with traced visibility.
 * Lambertian: multiply by albedo / PI for outgoing radiance.
 * @param light_index The light's slot in the GPU light buffer, so the sun shadow-map tier can
 *        recognise the one light its bound map belongs to.
 */
vec3 GiEvalLight(GpuLight light, int light_index, vec3 world_position, vec3 world_normal,
                 float voxel_size, float near_field)
{
	// The Ex form reports the direction and distance it derived for the attenuation, so the
	// shadow ray below does not redo the same length and normalize.
	vec3 to_light;
	float light_distance;
	vec3 unshadowed =
	    GpuEvalLightUnshadowedEx(light, world_position, world_normal, to_light, light_distance);
	// Nothing to occlude, so skip the ray entirely. This is the common case for a point far
	// outside a light's range, and shadow rays are by far the most expensive part of this.
	if(dot(unshadowed, unshadowed) <= 0.0)
	{
		return vec3_splat(0.0);
	}
	if(light.type == GPU_LIGHT_TYPE_DIRECTIONAL)
	{
		light_distance = u_gi_shadow_distance;
#if defined(GI_SUN_SHADOWMAP_TIER)
		// The sun's own map answers inside cascade 0 (see the tier note above); four taps
		// replace the whole sphere trace. Out of bounds falls through to the trace.
		if(u_gi_sun_index >= 0.0)
		{
			if(float(light_index) == u_gi_sun_index)
			{
				float lit;
				if(GiSunShadowmapVisibility(world_position, world_normal, voxel_size, lit))
				{
					return unshadowed * lit;
				}
			}
		}
#endif // GI_SUN_SHADOWMAP_TIER
	}
	return unshadowed *
	       GiTraceShadow(world_position, world_normal, to_light, light_distance, voxel_size, near_field);
}

/**
 * Total irradiance at a world point from every resident light, with traced visibility.
 * @param near_field See GiTraceShadow: the per-instance range for this point's shadow rays.
 */
vec3 GiEvalDirectLighting(vec3 world_position, vec3 world_normal, float voxel_size, float near_field)
{
	vec3 total = vec3_splat(0.0);
	// LOOP: the body carries a sphere trace; unrolling it multiplies the largest instruction
	// footprint in the kernel by the light count.
	LOOP
	for(int i = 0; i < u_gpu_light_count; ++i)
	{
		total += GiEvalLight(GpuLoadLight(i), i, world_position, world_normal, voxel_size, near_field);
	}
	return total;
}

/**
 * The light-voxel variant: like GiEvalDirectLighting, with one traced DIRECTIONAL ray per
 * VOXEL instead of per face.
 *
 * A voxel's sun-facing faces launch from within one attribute voxel of each other along the
 * identical direction, so tracing each one separately paid up to three ~100 m marches for
 * one answer - and for level >= 2 (no CSM cover, no mesh near field) that was the majority
 * of the pass's shadow cost. The trace is memoised per (voxel, light): the first face out of
 * shadow-map coverage traces from a SHARED origin - the voxel centre lifted along the ray
 * itself, by the same centre lift the faces use plus the answering level's normal bias -
 * and every later face reuses the verdict with its own n.l. The CSM tier stays per face:
 * four taps, area-averaged over the face, and the sharper answer wherever it covers.
 *
 * The receiver was already treated as a voxel-wide beam (see GiTraceShadow), so a shared
 * per-voxel verdict is the same contract at the same scale; what changes is only that the
 * faces of one voxel can no longer disagree about the traced tier's answer.
 *
 * One cached slot: scenes with several directional lights fall back to per-face traces for
 * all but the first one encountered, which is the safe direction.
 */
vec3 GiEvalDirectLightingVoxel(vec3 world_position, vec3 world_normal, float voxel_size,
                               float near_field, vec3 voxel_center, float center_lift,
                               inout float cached_dir_visibility, inout int cached_dir_index)
{
	vec3 total = vec3_splat(0.0);
	LOOP
	for(int i = 0; i < u_gpu_light_count; ++i)
	{
		GpuLight light = GpuLoadLight(i);
		if(light.type == GPU_LIGHT_TYPE_DIRECTIONAL)
		{
			vec3 to_light;
			float light_distance;
			vec3 unshadowed = GpuEvalLightUnshadowedEx(light, world_position, world_normal,
			                                           to_light, light_distance);
			if(dot(unshadowed, unshadowed) <= 0.0)
			{
				continue;
			}
#if defined(GI_SUN_SHADOWMAP_TIER)
			// Per face on purpose: four taps, and the map's area average over THIS face is
			// sharper than any shared verdict.
			if(u_gi_sun_index >= 0.0)
			{
				if(float(i) == u_gi_sun_index)
				{
					float lit;
					if(GiSunShadowmapVisibility(world_position, world_normal, voxel_size, lit))
					{
						total += unshadowed * lit;
						continue;
					}
				}
			}
#endif // GI_SUN_SHADOWMAP_TIER
			float visibility;
			if(cached_dir_index == i)
			{
				visibility = cached_dir_visibility;
			}
			else if(cached_dir_index < 0)
			{
				// The shared origin lifts along the RAY, so the trace's own normal bias
				// (measured along what it is given as the normal) cannot teleport through a
				// sun-facing wall the way a slope-scaled skip would.
				vec3 shared_origin = voxel_center + to_light * center_lift;
				visibility = GiTraceShadow(shared_origin, to_light, to_light, u_gi_shadow_distance,
				                           voxel_size, near_field);
				cached_dir_visibility = visibility;
				cached_dir_index = i;
			}
			else
			{
				visibility = GiTraceShadow(world_position, world_normal, to_light,
				                           u_gi_shadow_distance, voxel_size, near_field);
			}
			total += unshadowed * visibility;
			continue;
		}
		total += GiEvalLight(light, i, world_position, world_normal, voxel_size, near_field);
	}
	return total;
}

#endif // __GI_LIGHTING_SH__
