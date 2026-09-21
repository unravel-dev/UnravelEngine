#ifndef __GI_REFLECTION_KERNEL_SH__
#define __GI_REFLECTION_KERNEL_SH__

/*
 * GI reflections (plan phase 9) - the world-space specular tier, layered UNDER SSR: drawn
 * into the reflection buffer over the authored probes, before SSR composites the sharp
 * on-screen result on top. What it adds is exactly what SSR cannot have: reflected content
 * that is off screen or behind the camera.
 *
 * SHARED KERNEL BODY, two consumers. cs_gi_reflection_trace.sc is the deliverable path:
 * the classify pass answers sky / degenerate / rough pixels directly and compacts the
 * sharp (tracing) pixels into a dense list, so every 64-lane trace group is fully
 * populated with rays - the fragment form paid a whole wave wherever ONE quad pixel
 * traced, and its worst-case register footprint throttled even the early-out pixels.
 * fs_gi_reflection.sc is that fragment form, kept as the fallback when the compute
 * chain is unavailable.
 *
 * ROUGHNESS TIERING, with the cutoff derived rather than tuned: past roughness ~0.4 the GGX
 * lobe is wide enough that its specular converges to the diffuse irradiance, so those pixels
 * reuse LAST frame's resolved GI - the temporally filtered, denoised per-pixel gather - and
 * pay no ray (the Lumen recipe: rough specular comes from your own gather, never from a raw
 * world lattice, whose 2 m granularity reads as mottling). Sharper lobes trace the SDF world
 * tier: mesh-exact out to a roughness-adaptive range, clipmap as a far-field FINDER whose every
 * find the mesh tier settles (GiReflectionTraceFarField), light voxels at mesh-snapped hits. An
 * unrefined clipmap hit - geometry no mesh field holds - is not an image on sharp pixels:
 * coverage goes to zero so the authored probe layer shows through. A true miss answers with
 * the authored probe layer itself - the multi-probe blended, parallax-projected sky capture at
 * this pixel - and with the sky SH only where no
 * probe reaches (an L2 SH cannot hold clouds or a sun disk; the probes can).
 *
 * ALBEDO REMODULATION (compute form only, GI_LIGHT_VOXEL_READ_ALBEDO): a measured light-voxel
 * answer at a hit with a known instance is rescaled by hit-albedo / voxel-mean-albedo, so the
 * image carries the hit surface's own colour instead of the attribute lattice's 0.25-1 m
 * trilinear mix - see the block at the read site for the full contract.
 *
 * NO SCREEN TIER: screen space belongs to SSR, which composites over this pass with its own
 * stochastic spread, denoise, and fades. Two screen tracers on the same pixel gave two
 * different answers wherever their validation or fades differed (measured, round 6), and a
 * single deterministic ray can never reproduce SSR's filtered result - so this pass never
 * traces the screen at all.
 *
 * STOCHASTIC GGX: each frame the ray direction is importance-sampled from the visible
 * normal distribution (Heitz VNDF) with an R2 sequence per frame + IGN per pixel - the
 * gather's proven jitter recipe - and the temporal pass integrates the lobe over
 * GI_REFLECTION_TEMPORAL_FRAMES of reprojected history. Roughness therefore SPREADS the
 * reflection the way SSR's stochastic trace does, instead of any fixed fade. At mirror
 * roughness alpha collapses the distribution and the ray is deterministic. The
 * GATHER_FADE_START..ROUGH_CUTOFF band still fades into the rough tier for continuity at the
 * cutoff. Per-pixel world-probe cage reads remain deliberately ABSENT: the 2 m lattice's
 * interpolation pattern stamps into the image as blotches, not blur (measured twice, rounds
 * 2 and 8). Output = incoming radiance along the sampled ray at FULL weight - energy is
 * constant across roughness (fading the sharp end read as brightness rising with roughness,
 * round 10). Alpha is coverage below 1 (mesh-exact and refined hits cover the probe layer,
 * an unrefined clipmap hit on a sharp pixel does not) and encodes the HIT DISTANCE above 1
 * for the temporal's mover gate - see the encoding note at the return. SSR composites on
 * top.
 */

#include "../common.sh"
#include "../lighting.sh"

#include "gi/sdf_common.sh"
#define GI_LIGHT_VOXEL_READ
#include "gi/gi_light_voxels.sh"
#include "gi/gi_emissive_nee.sh"
#include "gi/gi_noise.sh"
// Lobe sampling shared with the temporal resolve, which must reproduce this pass's ray.
#include "gi/gi_reflection_sampling.sh"
#include "gi/gi_env_sh.sh"
// The trace runs in the VIEW's pre-exposed space (Lumen's reflections): store reads convert,
// history reads correct from last frame's scale.
#include "gi/gi_pre_exposure.sh"

/// The per-ray firefly ceiling, in the pre-exposed space the radiance now lives in.
///
/// Lumen's MaxRayIntensity is a PRE-EXPOSED number, but GI_MAX_RAY_RADIANCE was tuned here as
/// an ABSOLUTE radiance (gi_emissive_research 2026-09-10, on emitters of 5 to 192), and the two
/// readings differ by the whole exposure range: in a sealed emissive room metering at P = 367,
/// a pre-exposed 40 is 0.109 of absolute radiance and crushes the room's own bounce (measured
/// 2026-09-16: the cell lost 24% of its display mean against the pre-pre-exposure baseline).
/// Converting the constant keeps the clamp where it was tuned, at the cost of no longer
/// following the adapted exposure the way Lumen's does. Measured: this site alone recovers
/// about half the loss (0.166 -> 0.187), the world-probe emitter gate the rest (-> 0.234).
#define GI_MAX_RAY_RADIANCE_VIEW (GI_MAX_RAY_RADIANCE * u_pre_exposure_value)

/// Analytic irradiance from one emitter piece at @p position for a receiver facing @p normal:
/// GI_REFLECTION_NEAR_FIELD_SAMPLES Lambertian patches along the piece's longest axis, each
/// the piece's area share; the piece's own facing is taken as the convex mean of one half.
/// Unshadowed - the caller uses it only as a ratio and a fraction.
float GiReflectionPieceIrradiance(GiEmitter e, vec3 position, vec3 normal)
{
	vec3 ext = max(e.extent, vec3_splat(0.0));
	vec3 axis = vec3(1.0, 0.0, 0.0);
	float half_length = 0.5 * ext.x;
	if(ext.y > ext.x && ext.y >= ext.z)
	{
		axis = vec3(0.0, 1.0, 0.0);
		half_length = 0.5 * ext.y;
	}
	else if(ext.z > ext.x && ext.z > ext.y)
	{
		axis = vec3(0.0, 0.0, 1.0);
		half_length = 0.5 * ext.z;
	}
	// 'patch' is a GLSL keyword (tessellation); the area of one sampled patch of the piece.
	float patch_area = GiEmitterSurfaceArea(ext) / float(GI_REFLECTION_NEAR_FIELD_SAMPLES);
	// Never closer than the piece's own half thickness: the surface, not its centre line.
	float d_min = max(0.5 * min(ext.x, min(ext.y, ext.z)), 0.02);
	float sum = 0.0;
	LOOP
	for(int i = 0; i < GI_REFLECTION_NEAR_FIELD_SAMPLES; ++i)
	{
		float t = (float(i) + 0.5) / float(GI_REFLECTION_NEAR_FIELD_SAMPLES) * 2.0 - 1.0;
		vec3 to_patch = e.center + axis * (t * half_length) - position;
		float d2 = max(dot(to_patch, to_patch), d_min * d_min);
		// inversesqrt: GLSL's spelling, mapped to rsqrt for HLSL by bgfx_shader.sh.
		float cos_receiver = max(dot(normal, to_patch * inversesqrt(d2)), 0.0);
		sum += cos_receiver / d2;
	}
	return 0.5 * GiEmitterLuminance(e) * patch_area * sum;
}

/// The near-field factor for a mirror hit's lit voxel value: the strongest
/// GI_REFLECTION_NEAR_FIELD_EMITTERS pieces (power / d^2 at the hit) give an analytic
/// irradiance at the hit and at the cell centres the walk measured; the fraction of the
/// cell's irradiance those pieces account for is rescaled by the ratio. 1 with no emitter
/// table, no extents (an older upload) or far from every piece.
float GiReflectionNearFieldFactor(vec3 hit_position, vec3 hit_normal, vec3 lit_position,
                                  vec3 lit_value, vec3 lit_albedo)
{
	int count = min(u_sdf_emitter_count, GI_EMISSIVE_NEE_MAX_EMITTERS);
	if(count <= 0)
	{
		return 1.0;
	}
	int pick[GI_REFLECTION_NEAR_FIELD_EMITTERS];
	float pick_score[GI_REFLECTION_NEAR_FIELD_EMITTERS];
	for(int k = 0; k < GI_REFLECTION_NEAR_FIELD_EMITTERS; ++k)
	{
		pick[k] = -1;
		pick_score[k] = 0.0;
	}
	LOOP
	for(int i = 0; i < count; ++i)
	{
		GiEmitter e = GiLoadEmitter(i);
		if(!e.has_extent)
		{
			return 1.0;
		}
		vec3 to_center = e.center - hit_position;
		float score = e.power / max(dot(to_center, to_center), 1e-4);
		// Insertion into the descending top-K.
		int slot = -1;
		for(int k = 0; k < GI_REFLECTION_NEAR_FIELD_EMITTERS; ++k)
		{
			if(slot < 0 && score > pick_score[k])
			{
				slot = k;
			}
		}
		if(slot >= 0)
		{
			for(int k = GI_REFLECTION_NEAR_FIELD_EMITTERS - 1; k > slot; --k)
			{
				pick[k] = pick[k - 1];
				pick_score[k] = pick_score[k - 1];
			}
			pick[slot] = i;
			pick_score[slot] = score;
		}
	}
	float irradiance_hit = 0.0;
	float irradiance_cell = 0.0;
	LOOP
	for(int k = 0; k < GI_REFLECTION_NEAR_FIELD_EMITTERS; ++k)
	{
		if(pick[k] < 0)
		{
			break;
		}
		GiEmitter e = GiLoadEmitter(pick[k]);
		irradiance_hit += GiReflectionPieceIrradiance(e, hit_position, hit_normal);
		irradiance_cell += GiReflectionPieceIrradiance(e, lit_position, hit_normal);
	}
	if(irradiance_cell <= 1e-6)
	{
		return 1.0;
	}
	// The cell's measured irradiance, E = pi x lit / albedo, bounds the emitters' share.
	float lit_luminance = dot(lit_value, vec3(0.2126, 0.7152, 0.0722));
	float albedo_luminance = max(dot(lit_albedo, vec3(0.2126, 0.7152, 0.0722)),
	                             GI_REFLECTION_REMODULATE_ALBEDO_FLOOR);
	float cell_irradiance = GI_NEE_PI * lit_luminance / albedo_luminance;
	float emitter_fraction = saturate(irradiance_cell / max(cell_irradiance, 1e-6));
	float ratio = clamp(irradiance_hit / irradiance_cell, 0.0, GI_REFLECTION_NEAR_FIELD_RATIO_MAX);
	return 1.0 + emitter_fraction * (ratio - 1.0);
}

SAMPLER2D(s_gi_normal, 5);
/// The authored probe layer (RBUFFER right after the probe pass): at trace time it holds
/// exactly the freshly drawn reflection probes - the GI composite and SSR write into it
/// later in the frame. Bound as transparent black when the probe stack did not run this
/// frame, so the read never sees this pass's own previous output.
SAMPLER2D(s_gi_probe_layer, 6);
SAMPLER2D(s_hiz, 8);
/// LAST frame's resolved GI (E/pi per pixel, temporally filtered and denoised): the rough
/// specular source, exactly as Lumen reuses its own gather - a wide lobe converges to the
/// diffuse irradiance, and this is the smoothest estimate of it the engine owns. Reading the
/// raw world-probe cage here instead produced 2 m-scale mottling.
SAMPLER2D(s_gi_diffuse, 9);
#if defined(GI_REFLECTION_ENV_SH_FROM_LIST)
/// LAST frame's composited output with each pixel's view depth in alpha (the gather's
/// screen tier and SSR read the same snapshot): the ON-SCREEN HIT UPGRADE below serves a
/// world hit the exact lit pixel when the depth buffer, the hit normal and the stored
/// depth all agree. The sky SH that held this stage rides the trace list's SH block.
SAMPLER2D(s_gi_prev_color, 14);
/// Reprojection of a world hit into last frame's snapshot (the unjittered pair).
uniform mat4 u_gi_refl_prev_view_proj;
/// x = 0 no previous colour, 1 colour only, 2 colour with view depth in alpha (the
/// upgrade needs the depth); yzw unused.
uniform vec4 u_gi_reflection_screen;
#else
SAMPLER2D(s_gi_env_sh, 14);
#endif // GI_REFLECTION_ENV_SH_FROM_LIST

/// xyz = camera position, w > 0 when s_gi_diffuse holds last frame's resolve.
uniform vec4 u_gi_reflection_camera;
/// xy = this frame's R2 low-discrepancy offset for the GGX sample; zw unused.
uniform vec4 u_gi_reflection_jitter;
/// x = far-field finder resumes (the GI setting reflection_finder_resumes, default
/// GI_REFLECTION_FINDER_RESUMES; see GiReflectionTraceFarField); yzw unused.
uniform vec4 u_gi_reflection_trace;

/// The environment radiance along @p direction: from the list's SH block in the compute
/// form, from the IRRADIANCE_SH texture in the fragment fallback. Ringing is clamped.
vec3 GiReflectionEnvRadiance(vec3 direction)
{
#if defined(GI_REFLECTION_ENV_SH_FROM_LIST)
	vec3 radiance = vec3_splat(0.0);
	for(int k = 0; k < GI_ENV_SH_COEFFS; ++k)
	{
		radiance += GiReflectionEnvSh(k) * GiEnvShBasis(k, direction);
	}
	// The SH holds absolute environment radiance; this pass is pre-exposed.
	return max(radiance, vec3_splat(0.0)) * u_pre_exposure_value;
#else
	return eval_radiance_sh(s_gi_env_sh, direction) * u_pre_exposure_value;
#endif // GI_REFLECTION_ENV_SH_FROM_LIST
}

#if defined(GI_REFLECTION_SCREEN_COLOR)
/*
 * ON-SCREEN HIT UPGRADE: a world hit that the depth
 * buffer shows THIS frame is served last frame's composited pixel - the exact lit surface
 * at pixel precision, direct light, shadows and all - instead of the voxel walk's cell
 * estimate. This is the tier SSR cannot reach (its screen march gave up behind a
 * foreground object, or its confidence faded) and where the voxel lattice printed as
 * blocks on reflected walls. Four gates, every one a rejection back to the voxel read:
 *   1. the hit faces the camera (a back-facing hit is not the pixel the depth shows),
 *   2. it projects on screen inside a dithered vignette (no hard edge at the border),
 *   3. the visible surface at that pixel is the hit, within a small fraction of its
 *      distance (the hit is not behind a nearer occluder),
 *   4. last frame's snapshot held THIS surface at the reprojected pixel - the gather
 *      screen tier's own depth validation, which is what keeps a departed mover's pixels
 *      from being read (the stored depth there was the mover's).
 * Returns the previous-frame colour; the caller applies the ray clamp.
 */
bool GiReflectionScreenColorAtHit(vec3 hit_position, vec3 hit_normal, vec2 frag_coord,
                                  out vec3 out_radiance)
{
	out_radiance = vec3_splat(0.0);
	vec3 to_camera = u_gi_reflection_camera.xyz - hit_position;
	float hit_distance = max(length(to_camera), 1e-4);
	if(dot(to_camera / hit_distance, hit_normal) < GI_REFLECTION_SCREEN_HIT_NORMAL_COS)
	{
		return false;
	}
	vec4 clip = mul(u_viewProj, vec4(hit_position, 1.0));
	if(clip.w <= 1e-6)
	{
		return false;
	}
	vec3 ndc = clipTransform(clip.xyz / clip.w);
	vec2 hit_uv = ndc.xy * 0.5 + 0.5;
	if(any(lessThan(hit_uv, vec2_splat(0.0))) || any(greaterThan(hit_uv, vec2_splat(1.0))))
	{
		return false;
	}
	// Dithered vignette: the fade over the outer band compared against the pixel's noise.
	vec2 edge = saturate((abs(ndc.xy) - vec2_splat(GI_SCREEN_HIT_VIGNETTE_START)) /
	                     (1.0 - GI_SCREEN_HIT_VIGNETTE_START));
	float vignette = 1.0 - max(edge.x * edge.x, edge.y * edge.y);
	if(vignette < GiIgnNoise(ivec2(frag_coord)).x)
	{
		return false;
	}
	float scene_depth = texture2DLod(s_hiz, hit_uv, 0.0).x;
	if(scene_depth >= 1.0)
	{
		return false;
	}
	vec3 scene_clip = clipTransform(vec3(hit_uv * 2.0 - 1.0, toClipSpaceDepth(scene_depth)));
	vec3 scene_position = clipToWorld(u_invViewProj, scene_clip);
	float scene_distance = length(scene_position - u_gi_reflection_camera.xyz);
	if(abs(scene_distance - hit_distance) > GI_REFLECTION_SCREEN_HIT_DEPTH_TOLERANCE * hit_distance)
	{
		return false;
	}
	vec4 prev_clip = mul(u_gi_refl_prev_view_proj, vec4(hit_position, 1.0));
	if(prev_clip.w <= 0.0)
	{
		return false;
	}
	vec3 prev_ndc = clipTransform(prev_clip.xyz / prev_clip.w);
	vec2 prev_uv = prev_ndc.xy * 0.5 + 0.5;
	if(any(lessThan(prev_uv, vec2_splat(0.0))) || any(greaterThan(prev_uv, vec2_splat(1.0))))
	{
		return false;
	}
	vec4 history = texture2DLod(s_gi_prev_color, prev_uv, 0.0);
	if(abs(history.w - prev_clip.w) > GI_TEMPORAL_DEPTH_TOLERANCE * prev_clip.w)
	{
		return false;
	}
	// The snapshot carries LAST frame's pre-exposure (UE P / Pprev).
	out_radiance = history.xyz * u_history_pre_exposure_correction;
	return true;
}
#endif // GI_REFLECTION_SCREEN_COLOR

/// Sky answer for rays our own geometry data calls OPEN: the authored probe layer at this
/// pixel, already multi-probe blended and parallax projected - frequency content an L2 SH
/// cannot hold (clouds, sun disk, horizon). Probe alpha is the layer's own coverage, so
/// unprobed pixels keep the SH answer instead of going black (RBUFFER.rgb is consumed flat
/// by the indirect pass - a hole here IS the final specular).
vec3 GiReflectionSkyFallback(vec2 uv, vec3 direction)
{
	vec4 probe_layer = texture2DLod(s_gi_probe_layer, uv, 0.0);
	float probe_alpha = saturate(probe_layer.w);
	// Full probe coverage is the normal state, and the SH is 9 texelFetches evaluated only
	// to be mixed out. Skip it whenever its weight is below half a quantum of the RGBA16F
	// target. A branch, not a ternary: HLSL ?: is a select that may evaluate both arms.
	BRANCH
	if(probe_alpha >= 0.999)
	{
		return probe_layer.xyz;
	}
	vec3 sky_sh = GiReflectionEnvRadiance(direction);
	return mix(sky_sh, probe_layer.xyz, probe_alpha);
}

/// Mesh-exact walk length: mirrors pay the long range, gloss pays less than the old flat 16 m.
float GiReflectionMeshRange(float roughness)
{
	float range_t = saturate(roughness / max(GI_REFLECTION_GATHER_FADE_START, 1e-4));
	return min(mix(GI_REFLECTION_MESH_SDF_RANGE_SHARP, GI_REFLECTION_MESH_SDF_RANGE_GLOSS, range_t),
	           GI_SHADOW_DISTANCE);
}

/// Half-width of the refine window around a clipmap hit at @p hit_position, sized to the
/// COARSER of the covering pair so a cascade-border hit, whose isosurface can sit a coarse
/// voxel off the mesh, still contains the surface.
float GiReflectionRefineWindow(vec3 hit_position)
{
	float field_blend;
	float voxel;
	int level = SdfFindClipmapLevel(hit_position, field_blend, voxel);
	// Nested rather than one &&-chain: SdfFindClipmapLevel returns LEVEL_COUNT off coverage,
	// and HLSL && does not short-circuit, so the flat form indexed one past the uniform array.
	if(level + 1 < SDF_CLIPMAP_LEVEL_COUNT)
	{
		float edge = max(field_blend,
		                 SdfClipmapEdgeBlend(level, hit_position, GI_REFLECTION_CASCADE_FADE_VOXELS));
		if(edge > 0.0)
		{
			if(u_sdf_clipmap_levels[level + 1].w > 0.0)
			{
				voxel = max(voxel, u_sdf_clipmap_levels[level + 1].w);
			}
		}
	}
	return GI_REFLECTION_REFINE_VOXELS * max(voxel, 0.01);
}

/// Where the ray leaves the outermost clipmap level's addressable box (GI_SHADOW_DISTANCE when
/// it never does within range): past it the finder sees nothing, whatever is there.
float GiReflectionClipmapExit(vec3 origin, vec3 direction)
{
	float exit_t = GI_SHADOW_DISTANCE;
	LOOP
	for(int i = SDF_CLIPMAP_LEVEL_COUNT - 1; i >= 0; --i)
	{
		vec4 level = u_sdf_clipmap_levels[i];
		if(level.w > 0.0)
		{
			vec3 inv_dir = 1.0 / max(abs(direction), vec3_splat(1e-8)) * sign(direction + vec3_splat(1e-20));
			vec3 box_min = level.xyz + vec3_splat(0.5 * level.w);
			vec3 box_max = level.xyz + vec3_splat((u_sdf_clipmap_resolution - 0.5) * level.w);
			float t_near;
			float t_far;
			if(SdfIntersectBounds(origin, inv_dir, box_min, box_max, GI_SHADOW_DISTANCE, t_near, t_far))
			{
				exit_t = t_far;
			}
			break;
		}
	}
	return exit_t;
}

/// A far-field answer: the hit, and whether the ray ended inside a mesh's fattened clipmap
/// shell (see GiReflectionTraceFarField) - shaded as the surface it grazes, never as a hole.
struct GiReflectionFarField
{
	SdfRayHit hit;
	bool shell;
};

GiReflectionFarField GiReflectionFarFieldMake(SdfRayHit hit, bool shell)
{
	GiReflectionFarField result;
	result.hit = hit;
	result.shell = shell;
	return result;
}

/**
 * FAR FIELD past the mesh-exact range: the clipmap as a FINDER, every find settled by the mesh tier.
 *
 * The clipmap is composed conservatively, so its isosurface is FATTER than the meshes it holds. A
 * ray passing BESIDE an object within that margin reports a hit on the fat shell - or, grazing it,
 * burns its step budget there - and the old answers for such hits drew a bright achromatic outline
 * around every far reflected silhouette: unrefined, the authored probe layer on a mirror; given up,
 * the receiver's own irradiance (measured 2026-09-21 on the Bistro mirror slab: those two paths
 * carried 79% of the outline's excess luminance, all of it past the mesh-exact range - far
 * reflections showed it, close ones did not). So every find is refined in its window, given-up
 * ones included:
 *   - a mesh hit there is the answer;
 *   - a miss with no mesh inside the window (clearance) is geometry only the clipmap holds, and
 *     keeps the unrefined answer as before;
 *   - a miss that PASSED a mesh inside the window was the fat shell: the finder resumes past the
 *     window up to u_gi_reflection_trace.x times (the setting; clamped here too), and a ray still
 *     in a shell after that is answered as the surface it grazes (shell) - an object-coloured
 *     fringe, not a white one.
 * A ray that runs off the outermost level saw nothing past it - blind, not open - so the mesh tier
 * walks the rest of the range: its clean miss is the sky, its hit the surface the finder could not
 * see. The refine and that walk share one call site: an inlined grid walk is this shader's largest
 * body, and fxc's compile time grows superlinearly with their count.
 */
GiReflectionFarField GiReflectionTraceFarField(vec3 origin, vec3 direction, float t_start,
                                               int finder_steps)
{
	SdfRayHit found = SdfMakeMiss();
	float finder_start = t_start;
	int resumes = int(clamp(u_gi_reflection_trace.x, float(GI_REFLECTION_FINDER_RESUMES_MIN),
	                         float(GI_REFLECTION_FINDER_RESUMES_MAX)));
	LOOP
	for(int attempt = 0; attempt <= resumes; ++attempt)
	{
		// The last flag declines the exhaustion-path normal: a given-up ray is refined, resumed,
		// answered as a shell with the ray-facing normal, or answered with rough_value.
		found = SdfTraceClipmap(origin, direction, finder_start, GI_SHADOW_DISTANCE, finder_steps,
		                        GI_REFLECTION_TRACE_SURFACE_BIAS, GI_REFLECTION_TRACE_RELAXATION, true,
		                        -1.0, false, false);
		// The mesh walk: the refine window around a find, or the rest of the range past the
		// clipmap's outermost level.
		float mesh_min = finder_start;
		float mesh_max = GI_SHADOW_DISTANCE;
		int mesh_steps = GI_TRACE_MAX_STEPS;
		float window = 0.0;
		if(found.hit)
		{
			window = GiReflectionRefineWindow(origin + direction * found.t);
			mesh_min = max(found.t - window, t_start);
			mesh_max = min(found.t + window, GI_SHADOW_DISTANCE);
			mesh_steps = GI_REFLECTION_REFINE_STEPS;
		}
		else
		{
			mesh_min = max(GiReflectionClipmapExit(origin, direction), finder_start);
			if(mesh_min >= GI_SHADOW_DISTANCE)
			{
				// Open inside the clipmap all the way to the range: a true miss.
				return GiReflectionFarFieldMake(found, false);
			}
		}
		SdfRayHit mesh = SdfTraceInstances(origin, direction, mesh_min, mesh_max, mesh_steps,
		                                   GI_REFLECTION_TRACE_SURFACE_BIAS,
		                                   GI_REFLECTION_TRACE_RELAXATION, true);
		if(mesh.hit)
		{
			return GiReflectionFarFieldMake(mesh, false);
		}
		if(!found.hit)
		{
			// Past the clipmap: the mesh walk's clean miss is open sky; if it gave up as well,
			// the finder's miss stands. (A branch: HLSL has no ?: over structs.)
			if(mesh.exhausted)
			{
				return GiReflectionFarFieldMake(found, false);
			}
			return GiReflectionFarFieldMake(mesh, false);
		}
		if(!(mesh.clearance < window))
		{
			return GiReflectionFarFieldMake(found, false);
		}
		if(attempt == resumes)
		{
			// Still in a shell: answer as the surface grazed. A given-up march has no normal; the
			// ray-facing one reads the faces the ray sees.
			if(found.exhausted)
			{
				found.exhausted = false;
				found.normal = -direction;
			}
			return GiReflectionFarFieldMake(found, true);
		}
		finder_start = found.t + window;
	}
	return GiReflectionFarFieldMake(found, false);
}

/// The whole reflection answer for one texel of the trace target: rgb = incoming radiance
/// along the (stochastically sampled) reflection ray, a = coverage for the probe-layer
/// composite. `frag_coord` is the texel centre in target pixels (gl_FragCoord.xy in the
/// fragment form, pixel + 0.5 in the compute form) - it only seeds the IGN.
vec4 GiReflectionShade(vec2 uv, vec2 frag_coord)
{
	float depth = texture2DLod(s_hiz, uv, 0.0).x;
	if(depth >= 1.0)
	{
		return vec4_splat(0.0);
	}
	GBufferDataNormalMetalRoughness nd = DecodeGBufferNormalMetalRoughnessLod(uv, s_gi_normal, 0.0);
	if(dot(nd.world_normal, nd.world_normal) < 0.5)
	{
		return vec4_splat(0.0);
	}
	vec3 normal = normalize(nd.world_normal);
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(depth)));
	vec3 world_position = clipToWorld(u_invViewProj, clip);
	vec3 view = normalize(u_gi_reflection_camera.xyz - world_position);
	vec3 reflected = normalize(reflect(-view, normal));
	// RAW authored roughness for the tiering: MakeRoughnessSafe floors it, and a floored
	// mirror (authored 0) leaked a fraction of the coarse world tier through the fade.
	float roughness = nd.roughness;
	// ROUGH VALUE - the wide-lobe limit. Rough specular converges to the diffuse irradiance,
	// and last frame's resolved GI is the engine's smoothest per-pixel estimate of it (the
	// Lumen recipe: reuse your own denoised gather; never a raw world lattice).
	// An explicit branch on the wave-uniform condition: as a ternary both arms ran, and the
	// SH arm is 9 texelFetches paid by every pixel including the rough fast path below.
	vec3 rough_value;
	BRANCH
	if(u_gi_reflection_camera.w > 0.5)
	{
		// LAST frame's resolve, written under the previous pre-exposure.
		rough_value = texture2DLod(s_gi_diffuse, uv, 0.0).xyz * u_history_pre_exposure_correction;
	}
	else
	{
		rough_value = GiReflectionEnvRadiance(reflected);
	}
	BRANCH
	if(roughness >= GI_REFLECTION_ROUGH_CUTOFF)
	{
		return vec4(rough_value, 1.0);
	}
	// GLOSS CONTINUITY FADE: the traced tiers below spread with roughness on their own, so
	// this fade's only remaining job is C0 continuity into the rough tier at the cutoff -
	// it covers just the residual band (a wide fade read as content dissolving, not blurring).
	float gloss_blend = smoothstep(GI_REFLECTION_GATHER_FADE_START, GI_REFLECTION_ROUGH_CUTOFF, roughness);
	// STOCHASTIC direction: jitter the ray inside the GGX lobe (bounded-cap VNDF), decorrelated
	// per pixel by IGN and advanced per frame by R2; the temporal pass integrates. The whole
	// sampler lives in gi_reflection_sampling.sh because the temporal RESOLVE has to reproduce
	// this exact ray, and its density, for every neighbouring texel it reuses.
	//
	// The determinism gate (inside GiReflectionMakeRay) is on DECODED roughness against the
	// encoder floor, never on alpha against a small epsilon: the G-buffer write clamps
	// roughness to >= 0.05, so an authored mirror decodes at the floor and its alpha (2.5e-3)
	// sailed over the old 1e-4 gate - every mirror pixel jittered, and rays near-missing a
	// small emissive hit it on the VNDF tail as full-radiance fireflies (the recorded
	// decoded-roughness-floor lesson).
	//
	// TWO independent noise channels for a true 2D point: deriving the second coordinate from
	// the first put every sample on a 1D curve through the unit square, so the azimuthal half
	// of the lobe was never properly covered and high-contrast regions could not converge
	// (measured, round 13; the shared pattern in gi_noise.sh).
	vec2 xi = fract(GiIgnNoise(ivec2(frag_coord)) + u_gi_reflection_jitter.xy);
	reflected = GiReflectionMakeRay(normal, view, roughness, xi).direction;
	// WORLD tier: launch clear of the composed surface, exactly as the gather lifts.
	// (No screen tier - SSR owns screen space and composites over this pass; see header.)
	float voxel;
	float field = SdfSampleClipmapEx(world_position, voxel);
	voxel = max(voxel, 0.01);
	float lift = max(0.0, -field) + GI_PROBE_TRACE_SURFACE_BIAS * voxel;
	vec3 origin = world_position + normal * lift;
	// Adaptive mesh-exact range, then clipmap as a FINDER (expand never: image vs estimate,
	// round 3) whose every find the mesh tier settles - see GiReflectionTraceFarField; an
	// unrefined clipmap hit is not drawn on sharp pixels (coverage 0, authored probes show
	// through). Acceptance is contact-only -
	// the gather cone is what fattened the 16 m handover into boxes.
	float mesh_range = GiReflectionMeshRange(roughness);
	SdfRayHit hit = SdfTraceInstances(origin, reflected, 0.0, mesh_range, GI_TRACE_MAX_STEPS,
	                                  GI_REFLECTION_TRACE_SURFACE_BIAS,
	                                  GI_REFLECTION_TRACE_RELAXATION, true);
	// A far-field ray that ended inside a mesh's fattened clipmap shell (GiReflectionFarField).
	bool shell = false;
	if(!hit.hit)
	{
		// Roughness-adaptive step budget, mirroring the range: a gloss-band ray is spread by
		// the GGX lobe, integrated over the temporal window, and blurred by the composite -
		// its finder does not need mirror precision, and its exhaustion path already fades
		// toward rough_value by design. The sharp band keeps the full budget.
		int finder_steps = int(mix(float(GI_TRACE_MAX_STEPS), 24.0,
		                           saturate(roughness / max(GI_REFLECTION_GATHER_FADE_START, 1e-4))));
		GiReflectionFarField far_field =
		    GiReflectionTraceFarField(origin, reflected, mesh_range, finder_steps);
		hit = far_field.hit;
		shell = far_field.shell;
	}
	vec3 radiance;
	float coverage = 1.0;
	if(hit.hit)
	{
		vec3 hit_position = origin + reflected * hit.t;
		vec3 hit_normal = hit.normal;
		if(dot(hit_normal, reflected) > 0.0)
		{
			hit_normal = -hit_normal;
		}
		// Shape classification FIRST, so the light-voxel read below can be skipped on the
		// sharpest pixels, whose result it fully replaces with the probe layer anyway.
		// GAVE UP = a clipmap march that stopped on its step budget and reports a position the
		// ray never reached. A mesh-tier hit can carry the exhausted flag too - it is set whenever
		// ANY instance march along the ray ran out of steps in its cell segment, the 16-step
		// refine walk included - but its hit is a real accepted surface; serving it the stand-in
		// painted the receiver's own irradiance onto refined far hits (measured 2026-09-21: most
		// exhausted outline pixels on the Bistro mirror slab carried an instance).
		bool gave_up = hit.exhausted && hit.instance_index == SDF_NO_INSTANCE;
		bool clipmap_shape = hit.instance_index == SDF_NO_INSTANCE && !gave_up;
		float shape_ok = 1.0;
		if(clipmap_shape)
		{
			// Unrefined clipmap isosurface: legitimate lighting for satin, a wrong silhouette
			// on a mirror. Fade coverage out so the probe layer replaces the blob - except in a
			// grazed mesh's shell, where the probe layer drew the white outline: that answer is
			// the surface itself, a fringe in its own colour.
			shape_ok = shell ? 1.0
			                 : smoothstep(GI_REFLECTION_CLIPMAP_SHAPE_CUTOFF * 0.5,
			                              GI_REFLECTION_CLIPMAP_SHAPE_CUTOFF, roughness);
			coverage = shape_ok;
		}
		// A gave-up march ("hits" mid-air when a grazing far ray exhausts its budget) or
		// an unmeasured coarse face must NOT answer with black. The gather's
		// honest-darkness contract exists to stop light leaking INTO the scene; a
		// reflection fallback cannot leak light anywhere - black here only punches holes
		// in the image, which grew with camera distance as rays grazed ever-coarser
		// cascades (measured, round 14). The receiver's own gather value is the smoothest
		// energy-plausible stand-in; faces MEASURED dark stay honestly dark.
		//
		// Branched, never an || chain: HLSL's || may evaluate both operands, and the
		// light-voxel read is up to two dozen 3D fetches that an exhausted hit discards.
		radiance = rough_value;
		bool screen_lit = false;
#if defined(GI_LIGHT_VOXEL_READ_ALBEDO)
		// The hit's own material (compute form): the remodulation ratio's numerator, the
		// metal blend and the tint of the stand-in below. Emission is already scaled by its
		// intensity; the albedo is the base-colour factor times the texture mean the args
		// pass staged; metalness rides lane 9's w (surface_cache_system packs it).
		bool hit_has_material = hit.instance_index != SDF_NO_INSTANCE;
		vec3 hit_albedo = vec3_splat(0.0);
		vec3 hit_emissive = vec3_splat(0.0);
		float hit_metalness = 0.0;
		BRANCH
		if(hit_has_material)
		{
			uint material_base = uint(hit.instance_index) * uint(SDF_INSTANCE_STRIDE);
			vec4 material0 = b_sdf_instances[material_base + 8u];
			vec4 material1 = b_sdf_instances[material_base + 9u];
			// An absolute material value entering a pre-exposed buffer.
			hit_emissive = material1.xyz * u_pre_exposure_value;
			hit_metalness = saturate(material1.w);
			hit_albedo = material0.xyz;
			uint mean_slot = SdfMeanSlotColor(material0.w);
			// Slot 0 is the composer's "no mean" convention: factor only.
			if(mean_slot != 0u)
			{
				hit_albedo *= GiReflectionMeanAlbedo(mean_slot);
			}
			hit_albedo = min(hit_albedo, vec3_splat(GI_MAX_ALBEDO));
		}
#endif // GI_LIGHT_VOXEL_READ_ALBEDO
#if defined(GI_REFLECTION_SCREEN_COLOR)
		// The on-screen hit upgrade first (GiReflectionScreenColorAtHit): a hit the depth
		// buffer shows is the exact lit pixel; the voxel walk answers only what it rejects.
		BRANCH
		if(!gave_up && shape_ok > 0.0 && u_gi_reflection_screen.x > 1.5)
		{
			vec3 screen_radiance;
			screen_lit = GiReflectionScreenColorAtHit(hit_position, hit_normal, frag_coord, screen_radiance);
			if(screen_lit)
			{
				// The gather's per-ray contract: the snapshot carries emissive unbounded too.
				radiance = GiClampRayRadiance(screen_radiance, GI_MAX_RAY_RADIANCE_VIEW);
			}
		}
#endif // GI_REFLECTION_SCREEN_COLOR
		BRANCH
		if(!screen_lit && !gave_up && shape_ok > 0.0)
		{
			vec3 measured;
#if defined(GI_LIGHT_VOXEL_READ_ALBEDO)
			// MATCHED-WEIGHT read (compute form): the radiance and the remodulation
			// denominator below come from ONE walk with identical face-alpha x facing x
			// trilinear weights. The split readers this replaces disagreed wherever face
			// culling thinned the radiance set but not the albedo set - crevices, rims,
			// every silhouette a mirror ray grazes - and the ratio slammed to its clamp:
			// a standing bright outline along reflected junctions, and a x4 window that
			// kept a departed emitter's residual glowing as a line long after the lattice
			// had converged (the "leftover red lines where the cubes passed").
			vec3 measured_albedo;
			bool measured_albedo_ok;
			vec3 measured_lit;
			float measured_lit_share;
			vec3 measured_lit_position;
			float measured_mass;
			// The walk is entirely in the store's cached-lighting space, so the receiver's
			// own value goes IN de-pre-exposed and the radiance lanes come back converted;
			// the albedo, share, position and mass lanes are unitless either way.
			bool measured_ok = GiLightVoxelReadFadeRemod(hit_position, hit_normal,
			                                             rough_value * u_pre_exposure_inverse,
			                                             GI_REFLECTION_CASCADE_FADE_VOXELS,
			                                             measured, measured_albedo,
			                                             measured_albedo_ok, measured_lit,
			                                             measured_lit_share,
			                                             measured_lit_position, measured_mass);
			measured = GiCachedToView(measured);
			measured_lit = GiCachedToView(measured_lit);
			// CULLED IS DARK FOR THE GATHER, A HOLE FOR AN IMAGE. A face whose cavity cone is
			// closed stores the provenance alpha GI_LIGHT_VOXEL_CULLED_ALPHA, which the read
			// above normalises into a MEASURED black - right for irradiance (a closed cone must
			// not leak), wrong here: the underside of a box floating 0.4 m over a mirror floor
			// is culled toward that floor, and the floor reflected it as a black rectangle
			// framed by the lit side faces bleeding in at its edges (the "projection of the
			// cube" under the mover). A footprint whose measured share is only that epsilon
			// takes the stand-in path below with the unmeasured ones.
			bool measured_image = measured_ok && measured_mass >= GI_REFLECTION_MEASURED_MASS_MIN;
#else
			// De-pre-exposed in, converted out - see the remodulating form above.
			bool measured_ok = GiLightVoxelReadFade(hit_position, hit_normal,
			                                       rough_value * u_pre_exposure_inverse,
			                                       GI_REFLECTION_CASCADE_FADE_VOXELS, measured);
			measured = GiCachedToView(measured);
			bool measured_image = measured_ok;
#endif // GI_LIGHT_VOXEL_READ_ALBEDO
			BRANCH
#if defined(GI_LIGHT_VOXEL_READ_ALBEDO)
			// The hit's exact emission is added AFTER the firefly clamp when the remodulated
			// branch composes it: a mirror's emission is deterministic, never a firefly, and
			// clamping it capped every reflected emitter at GI_MAX_RAY_RADIANCE
			// (gi_emissive_research 2026-09-10).
			bool exact_emission = false;
#endif // GI_LIGHT_VOXEL_READ_ALBEDO
			if(measured_image)
			{
				radiance = measured;
#if defined(GI_LIGHT_VOXEL_READ_ALBEDO)
				// ALBEDO REMODULATION - the "voxelized colour" fix. The volume stores
				// bounded_albedo * E / pi + emissive per face, and its trilinear read mixes
				// the WINNING albedos of a 0.25-1 m cell neighbourhood - a curtain hit next
				// to a wall answers pink. A mesh-exact (or refined) hit knows its instance,
				// so divide the cell-mixed albedo back out and multiply the hit's own in:
				// colour boundaries move from the attribute lattice to the SDF silhouette,
				// while the irradiance keeps the volume's smooth estimate. Only measured
				// radiance is remodulated (rough_value and the sky fallback are not voxel
				// products); emissive hits are skipped rather than having their emission
				// separated (the albedo ratio does not apply to a source term). With the
				// matched-weight denominator the ratio is exact under locally uniform
				// lighting at every boundary; the floor and cap now bound only
				// quantisation noise and genuinely non-uniform lighting across the
				// footprint. A fallback-mixed answer carries no matching albedo
				// (measured_albedo_ok false) and is served unremodulated.
				BRANCH
				if(hit_has_material && measured_albedo_ok)
				{
					// SOURCE SPLIT: the volume mixes a voxelised emitter's own emission into
					// every read within a cell or two of it, so a mirror showed the strip's
					// exact silhouette next to a fat glowing bar of the ceiling's voxels
					// (the user's "light blob voxels next to accurate geometry"). The walk
					// now hands back the LIT estimate over non-source faces, which is
					// remodulated by the hit's own albedo as before, and the hit's OWN
					// emission - exact, at the SDF silhouette - is added on top: the
					// ceiling reflects the strip's light as a smooth gradient, the strip
					// reflects as itself. A footprint with no lit face (all source) borrows
					// the first coarser level's lit estimate inside the walk.
					vec3 voxel_albedo = min(measured_albedo, vec3_splat(GI_MAX_ALBEDO));
					vec3 ratio = hit_albedo /
					             max(voxel_albedo,
					                 vec3_splat(GI_REFLECTION_REMODULATE_ALBEDO_FLOOR));
					bool hit_emits = dot(hit_emissive, hit_emissive) > 0.0;
					// No lit face at any level (the walk borrows from coarser levels first):
					// a non-emissive hit keeps the voxel total rather than the receiver's own
					// gather, which on a mirror floor beside a strip is bright and painted a
					// block along the strip line.
					vec3 lit = hit_emits ? vec3_splat(0.0) : measured;
					BRANCH
					if(measured_lit_share > 0.0)
					{
						lit = measured_lit * clamp(ratio,
						                           vec3_splat(0.0),
						                           vec3_splat(GI_REFLECTION_REMODULATE_RATIO_MAX));
						// NEAR-FIELD REMODULATION: a cell beside a strip holds one lit value
						// for its whole 0.25 m, and the mirror drew it as a block. The
						// emitter table knows the strip exactly, so the part of the cell's
						// value the emitters account for is rescaled from where the walk
						// measured it (the lit-weighted cell centres) to the hit - the 1/d
						// gradient at pixel precision. Unshadowed on purpose: the voxel's
						// own occlusion stays, the factor only reshapes it.
						lit *= GiReflectionNearFieldFactor(hit_position, hit_normal, measured_lit_position,
						                                   measured_lit * u_pre_exposure_inverse, measured_albedo);
					}
					// METAL LIFT: the lattice holds diffuse bounce (albedo x E / pi) and a metal
					// has none - its appearance is F0 times the radiance arriving from its mirror
					// direction, which no diffuse lattice can hold, so a metal reached through
					// this tier can come back near black wherever its own cell measured little.
					// Through the mirror floor the brushed north wall, in the band hidden behind
					// the floating box on screen (neither SSR nor the on-screen upgrade can see
					// it), reflected as a black rectangle beside the bright rendered wall SSR
					// shows around it. The receiver's own irradiance is a second isotropic
					// estimate of the light arriving at the hit, and the base colour is F0; a
					// metal takes the BRIGHTER of the two estimates, so a cell the lattice already
					// lit keeps its answer and only the dark ones are lifted; blended by metalness
					// so dielectrics keep their measured bounce untouched.
					vec3 metal_lift = max(lit, hit_albedo * rough_value);
					radiance = mix(lit, metal_lift, hit_metalness);
					exact_emission = true;
				}
#endif // GI_LIGHT_VOXEL_READ_ALBEDO
				// FIREFLY CLAMP, the gather's per-ray contract applied to the one tier that
				// lacked it: the volume stores emissive UNBOUNDED, so a single ray landing on
				// a small bright emitter returned its full radiance - two orders over the
				// scene - and no temporal window can hide an unbounded spike (1/p samples
				// needed). Only the voxel-measured answer is capped: rough_value is last
				// frame's denoised resolve and the sky fallback is a stable per-pixel image,
				// neither a stochastic spike source.
				radiance = GiClampRayRadiance(radiance, GI_MAX_RAY_RADIANCE_VIEW);
#if defined(GI_LIGHT_VOXEL_READ_ALBEDO)
				if(exact_emission)
				{
					radiance += hit_emissive;
				}
#endif // GI_LIGHT_VOXEL_READ_ALBEDO
			}
#if defined(GI_LIGHT_VOXEL_READ_ALBEDO)
			else if(hit_has_material)
			{
				// MATERIAL STAND-IN for a hit the lattice holds nothing usable for (never
				// measured, or culled - see measured_image above). rough_value alone is the
				// receiver's own E/pi, the same energy-plausible stand-in the exhausted path
				// serves - but a hit with a known material is that surface, not the receiver:
				// its albedo over the receiver's irradiance is what it would bounce if that
				// light reached it (for a mirror receiver exactly the mirror image's light,
				// which no diffuse lattice can hold), and its own emission rides on top. The
				// culled underside now reflects as the dim orange of the box it belongs to,
				// continuous with the front face's reflection, instead of neutral grey or black.
				radiance = GiClampRayRadiance(hit_albedo * rough_value, GI_MAX_RAY_RADIANCE_VIEW) + hit_emissive;
			}
#endif // GI_LIGHT_VOXEL_READ_ALBEDO
		}
		if(clipmap_shape)
		{
			// At shape_ok 1 the mix is the identity and the fallback's fetches are dead;
			// at 0 the radiance above was skipped and the fallback answers alone.
			BRANCH
			if(shape_ok < 1.0)
			{
				radiance = mix(GiReflectionSkyFallback(uv, reflected), radiance, shape_ok);
			}
		}
	}
	else
	{
		// MISS means OPEN by our own geometry data - the SDF found nothing within 100 m along
		// this ray, so the sky answers: the authored probe layer at this pixel, SH where no
		// probe reaches. Coverage stays 1 so the miss remains an image and the temporal mean
		// integrates hit and sky fractions of the lobe together - dropping coverage instead
		// would make the composite full-cover the probes with a geometry-only mean on every
		// glossy silhouette. (The cage was tried here twice and both reads were wrong: 100 m
		// out it leaves the lattice, at the receiver it stamps the 2 m pattern - round 8.)
		radiance = GiReflectionSkyFallback(uv, reflected);
	}
	// CONSTANT ENERGY across roughness: the lobe's incoming radiance barely changes from a
	// mirror to satin, so the tier serves full weight at EVERY roughness - the early fades
	// (mirror fade, footprint confidence) removed energy at the sharp end, which read as
	// "brightness rises with roughness" next to SSR's constant-energy blur (measured, round
	// 10). Roughness now changes only WHERE the stochastic rays go, exactly as it should.
	//
	// RAW ALPHA ENCODING: coverage for the probe-layer composite in [0, 1) (shape-fade),
	// EXACTLY 1.0 for the rough tier (the classify pass writes it too), and above 1.0 the
	// HIT DISTANCE rides along - 1 + t / GI_SHADOW_DISTANCE for a full-coverage geometric
	// hit, 2.0 for a miss (the sky answered). The temporal pass rebuilds the reflected hit
	// from this to read the velocity buffer THERE (its mover gate); every >= 0.5 image test
	// downstream and the composite's saturate() are unchanged by construction.
	float alpha = coverage;
	if(hit.hit && coverage >= 1.0)
	{
		alpha = 1.0 + clamp(hit.t / GI_SHADOW_DISTANCE, 1e-3, 1.0);
	}
	else if(!hit.hit)
	{
		alpha = 2.0;
	}
	return vec4(mix(radiance, rough_value, gloss_blend), alpha);
}

#endif // __GI_REFLECTION_KERNEL_SH__
