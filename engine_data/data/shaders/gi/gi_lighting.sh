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
 * EVERY split (phase E of gi_single_lighting_plan.md): the includer (cs_gi_light_voxels) has
 * exactly one free resource stage, so the cascades arrive as the layers of ONE texture array
 * (gi_light_voxel_pass.cpp blits the generator's maps into it on the frames the relight runs;
 * nothing is re-rendered and the shadow pass's per-cascade caster sets are untouched). The
 * receiver picks the SMALLEST cascade whose crop contains it, exactly the raster's rule
 * (fs_pbr_lighting.sh): that rule is what the shadow pass's nested-cascade caster culling is
 * exact for (shadow.cpp - a caster fully inside a nearer crop is drawn into that map only,
 * and whatever can shadow a receiver of crop j shares its light-space column and is in map
 * j). Selecting by view distance instead would read maps that lack the near casters.
 * Outside every crop, outside the frustum slices the cascades were fitted to (see the
 * contract in GiSunShadowmapVisibility), and for every other light, the traced field
 * remains the answer. A world-stable map of the GI's own would answer the unseen faces
 * too, but it costs a second scene render per window scroll, which was judged not worth
 * it. Gated by a define so the debug direct view keeps showing the PURE traced tier -
 * the diagnostic contrast that found this bug.
 */
SAMPLER2DARRAY(s_gi_sun_shadowmap, 14);
/// bgfx_shader.sh maps the array samplers for HLSL and ESSL but leaves texture2DArrayLod
/// undefined on desktop GLSL, where the native call is textureLod on a sampler2DArray.
#if BGFX_SHADER_LANGUAGE_GLSL && !defined(texture2DArrayLod)
#	define texture2DArrayLod(_sampler, _coord, _lod) textureLod(_sampler, _coord, _lod)
#endif
/// World -> shadow texcoord of every cascade (the raster's u_shadowMapMtx0..3), layer = split.
uniform mat4 u_gi_sun_shadowmap_mtx[4];
/// x = light-buffer index of the sun the bound maps belong to (< 0 disables the tier),
/// y = the number of active splits, z = texcoord border, w = d(stored depth)/d(world
/// distance along the sun) - the base ortho depth range is shared by every cascade.
uniform vec4 u_gi_sun_shadowmap_params;
/// The camera's (TAA-unjittered) view-projection - the frustum the cascades were fitted to.
uniform mat4 u_gi_sun_shadowmap_camera_vp;
/// xyzw = the cascades' view-space far distances (the slices' far planes), 0 past the count.
uniform vec4 u_gi_sun_shadowmap_slice;
/// xyzw = each cascade's constant receiver bias in stored depth (cascade 0's texel bias
/// scaled by the cascade's texel size, as the raster scales its own).
uniform vec4 u_gi_sun_shadowmap_bias;
#define u_gi_sun_index          u_gi_sun_shadowmap_params.x
#define u_gi_sun_splits         int(u_gi_sun_shadowmap_params.y)
#define u_gi_sun_border         u_gi_sun_shadowmap_params.z
#define u_gi_sun_world_to_depth u_gi_sun_shadowmap_params.w

/// The quadrature taps sit half a voxel off the face centre; a face tilted toward the sun
/// puts them at different depths than the centre. One voxel of receiver depth covers faces
/// tilted up to about 63 degrees, beyond which the face receives little sun anyway.
#define GI_SUN_SHADOWMAP_SLOPE_COVER_VOXELS 1.0

/**
 * Sun visibility from the bound cascades, when one of them covers @p world_position.
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
 * THE RECEIVER BIAS IS CAPPED (GI_SUN_SHADOWMAP_SLOPE_CAP): the slope cover the quadrature
 * needs was one LEVEL voxel of light-space depth, which also meant the tier reported LIT
 * through any occluder thinner than that voxel - at the coarse cascades metres, a sealed
 * room lit from outside through its roof by a path no field defence can see (measured:
 * interior ceiling brightest, sun-white, falling off downward). The coarse levels declined
 * the map for that (GI_SUN_SHADOWMAP_MAX_VOXEL 0.125 kept only level 0). Now every level
 * biases by min(voxel, cap) - level 0 is unchanged and a coarse face carries level 0's
 * bias: a tilted coarse face may self-shadow at its outer taps (a darker pool), never light
 * a sealed room, and the large axis-aligned sunlit surfaces (floors, sun-facing walls) read
 * the raster's exact answer at every level. The level gate sits FIRST: declining costs one
 * compare and skips the projection and the taps.
 *
 * @param voxel_size Voxel of the answering cascade level: the quadrature half-extent.
 * @return true when a map answered; @p out_lit then holds the lit fraction and
 *         @p out_cascade the split that answered. False means no cascade covers the point
 *         (or too coarse a level), and the traced field must answer.
 */
bool GiSunShadowmapVisibility(vec3 world_position, vec3 world_normal, float voxel_size, out float out_lit,
                              out int out_cascade)
{
	out_lit = 0.0;
	out_cascade = -1;
	if(voxel_size > GI_SUN_SHADOWMAP_MAX_VOXEL)
	{
		return false;
	}
	// THE SLICE CONTRACT. The cascades are fitted to the camera's frustum slices, and the
	// raster samples them for nothing outside the frustum. A crop footprint - a bounding
	// sphere of its slice - reaches metres BEHIND and beside the camera, so a world-space
	// receiver there projects inside a map's texcoords while nothing about the fit is
	// contracted for it. Measured: faces of a sealed room BEHIND the camera read LIT through
	// cascade 0 while the camera faced away, and every camera turn then revealed a lit room
	// that decayed over seconds through the relight EMA and the closed-room bounce (the
	// first-look glow). A receiver outside the frustum - behind the near plane, past the last
	// cascade's far plane, or outside the field of view - declines here and the traced field
	// answers, exactly as it does past the maps' edges. Costs one mat4 transform per face.
	int splits = clamp(u_gi_sun_splits, 1, 4);
	float far_last = splits == 1 ? u_gi_sun_shadowmap_slice.x
	                             : (splits == 2 ? u_gi_sun_shadowmap_slice.y
	                                            : (splits == 3 ? u_gi_sun_shadowmap_slice.z
	                                                           : u_gi_sun_shadowmap_slice.w));
	vec4 camera_clip = mul(u_gi_sun_shadowmap_camera_vp, vec4(world_position, 1.0));
	if(camera_clip.w <= 0.0 || camera_clip.w > far_last)
	{
		return false;
	}
	vec2 camera_ndc = camera_clip.xy / camera_clip.w;
	if(any(greaterThan(abs(camera_ndc), vec2_splat(1.0))))
	{
		return false;
	}
	// THE CROP CONTRACT: the smallest cascade whose crop contains the receiver (xy inside the
	// crop's border, depth inside the map's range), in the raster's order. A near crop is as
	// deep as the shadow range, so it answers for far ground at low sun too - with its full
	// resolution and, by the nested-cascade culling, every caster of its column.
	int cascade = -1;
	vec4 shadow_coord = vec4_splat(0.0);
	LOOP
	for(int split = 0; split < splits; ++split)
	{
		vec4 candidate = mul(u_gi_sun_shadowmap_mtx[split], vec4(world_position, 1.0));
		if(candidate.w <= 1e-6)
		{
			continue;
		}
		vec3 projected = candidate.xyz / candidate.w;
		if(any(lessThanEqual(projected.xy, vec2_splat(u_gi_sun_border))) ||
		   any(greaterThanEqual(projected.xy, vec2_splat(1.0 - u_gi_sun_border))) ||
		   projected.z <= 0.0 || projected.z >= 1.0)
		{
			continue;
		}
		cascade = split;
		shadow_coord = candidate;
		break;
	}
	if(cascade < 0)
	{
		return false;
	}
	// Quadrature points at the quarter-marks of the face: half-extent is one LEVEL voxel
	// (the attribute voxel spans two), taps at half that.
	float h = 0.5 * voxel_size;
	vec3 tangent = world_normal.yzx * h;
	vec3 bitangent = world_normal.zxy * h;
	vec4 delta_t = mul(u_gi_sun_shadowmap_mtx[cascade], vec4(tangent, 0.0));
	vec4 delta_b = mul(u_gi_sun_shadowmap_mtx[cascade], vec4(bitangent, 0.0));
	float constant_bias = cascade == 0 ? u_gi_sun_shadowmap_bias.x
	                                   : (cascade == 1 ? u_gi_sun_shadowmap_bias.y
	                                                   : (cascade == 2 ? u_gi_sun_shadowmap_bias.z
	                                                                   : u_gi_sun_shadowmap_bias.w));
	float bias = constant_bias + min(voxel_size, GI_SUN_SHADOWMAP_SLOPE_CAP) *
	                                 GI_SUN_SHADOWMAP_SLOPE_COVER_VOXELS * u_gi_sun_world_to_depth;
	float layer = float(cascade);
	float lit = 0.0;
	for(int tap = 0; tap < 4; ++tap)
	{
		vec4 tap_coord = shadow_coord +
		                 (tap < 2 ? delta_t : -delta_t) +
		                 ((tap & 1) != 0 ? delta_b : -delta_b);
		float receiver = (tap_coord.z - bias) / tap_coord.w;
		float occluder =
		    texture2DArrayLod(s_gi_sun_shadowmap, vec3(tap_coord.xy / tap_coord.w, layer), 0.0).x;
		lit += step(receiver, occluder);
	}
	out_lit = lit * 0.25;
	out_cascade = cascade;
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
/// z = 1 while the editor's GI census is armed (gi_quiescence_gate_pass::is_census_armed): the
/// relight's census rows (GI_STATS_RELIGHT_FACES_MOVED / _VISIBLE) accumulate only then. A lane
/// of a uniform every relight consumer binds anyway (it carried the finest cascade voxel once).
#define u_gi_stats_census        (u_gi_shadow_params2.z > 0.5)
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
		// The sun's own maps answer inside the cascades (see the tier note above); four taps
		// replace the whole sphere trace. Out of every crop falls through to the trace.
		if(u_gi_sun_index >= 0.0)
		{
			if(float(light_index) == u_gi_sun_index)
			{
				float lit;
				int cascade;
				if(GiSunShadowmapVisibility(world_position, world_normal, voxel_size, lit, cascade))
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
 * all but the first one encountered, which is the safe direction. A cached index of -2
 * marks a REFUSED share (see GiSharedOriginClear): every face of that voxel traces its own
 * ray for every directional light.
 */
/*
 * SHARED-ORIGIN VALIDATION (GI_SHARED_ORIGIN_REDESCENT_VOXELS). The shared ray launches
 * from the voxel centre lifted along the light by the centre's depth plus half an attribute
 * voxel. That lift is the answering level's own scale - 0.5 to 1 m at the coarse levels -
 * and it crosses any occluder thinner than itself standing between the voxel and the sun
 * (measured: the door tunnel's floor faces lit through a 25 cm baffle, the shared ray
 * starting on the baffle's sunlit side). The field along the lift tells the two cases
 * apart: leaving the voxel's own surface the distance RISES; a re-descent before the
 * origin means the segment entered another surface. Sampled once per voxel, on the face
 * that would establish the memo.
 */
bool GiSharedOriginClear(int level, vec3 voxel_center, vec3 shared_origin, float voxel_size)
{
	float peak = SdfSampleClipmapLevel(level, voxel_center);
	float tolerance = GI_SHARED_ORIGIN_REDESCENT_VOXELS * voxel_size;
	LOOP
	for(int sample_index = 1; sample_index <= GI_SHARED_ORIGIN_SAMPLES; ++sample_index)
	{
		vec3 sample_position = mix(voxel_center, shared_origin,
		                           float(sample_index) / float(GI_SHARED_ORIGIN_SAMPLES));
		float distance = SdfSampleClipmapLevel(level, sample_position);
		if(distance < peak - tolerance)
		{
			return false;
		}
		peak = max(peak, distance);
	}
	return true;
}

vec3 GiEvalDirectLightingVoxel(vec3 world_position, vec3 world_normal, float voxel_size,
                               float near_field, vec3 voxel_center, float center_lift,
                               int level, inout float cached_dir_visibility,
                               inout int cached_dir_index)
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
					int cascade;
					if(GiSunShadowmapVisibility(world_position, world_normal, voxel_size, lit, cascade))
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
			else if(cached_dir_index == -1)
			{
				// The shared origin lifts along the RAY, so the trace's own normal bias
				// (measured along what it is given as the normal) cannot teleport through a
				// sun-facing wall the way a slope-scaled skip would. The lift itself can cross
				// a thin occluder; the validation refuses the share when it does.
				vec3 shared_origin = voxel_center + to_light * center_lift;
				BRANCH
				if(GiSharedOriginClear(level, voxel_center, shared_origin, voxel_size))
				{
					visibility = GiTraceShadow(shared_origin, to_light, to_light,
					                           u_gi_shadow_distance, voxel_size, near_field);
					cached_dir_visibility = visibility;
					cached_dir_index = i;
				}
				else
				{
					cached_dir_index = -2;
					visibility = GiTraceShadow(world_position, world_normal, to_light,
					                           u_gi_shadow_distance, voxel_size, near_field);
				}
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
