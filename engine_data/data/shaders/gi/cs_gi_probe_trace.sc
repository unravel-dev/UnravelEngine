/*
 * Screen probe gather, tracing: the screen, then the global distance field. One ray per equal-area octahedral texel of
 * every probe (GI_PROBE_TRACE_RES^2); the directions shift together inside their texels by the probe tile's jitter
 * of this frame (GiProbeRayJitter).
 *
 * Each ray takes the first answer of these stages, each resuming GI_TRACE_RESUME_PULLBACK before the
 * distance the previous one vouched for. Where the radiance cache covers the probe, the near stages stop at
 * the hand-off distance (TMin + the cell diagonal of the probe's clipmap, 3.6 m at clipmap 0):
 *  1. Hi-Z screen trace from the probe lifted off its surface by two projected half pixels along the
 *     normal. A hit is lit from last frame's scene colour at its reprojection (where the hit surface was last
 *     frame, gi_motion.sh), unless it falls in the outer screen band (stochastic vignette) or last frame's
 *     depth there disagrees (occluded or newly revealed); a rejected hit hands the distance field its crossing.
 *     A lit hit whose surface moved against the probe by more than GI_TEMPORAL_MOVING_RELATIVE_SPEED of the
 *     probe's depth this frame marks the ray moving: the probe's lighting is changing, and the filter and the
 *     temporal shorten its history (the fast update).
 *  2. Global distance field from the probe lifted GI_SURFACE_BIAS along the ray and the normal, with dithered
 *     transparency where only two-sided meshes are near (gi_global_sdf.sh). A hit reads the surface cache through
 *     the object grid, faded to black within one voxel of the origin against self-lighting. A hit on an instance
 *     that moved more than GI_TEMPORAL_MOVING_RELATIVE_SPEED of the probe's depth this frame marks the ray
 *     moving, as a screen hit does (the hit instance's own velocity).
 *  3. The radiance cache, interpolated at the probe's position (gi_radiance_cache_sample.sh); beyond
 *     the cache's reach the distance field runs to the maximum trace distance and a miss reads the sky.
 *
 * Three passes: compiled plain, the SCREEN pass (one group per probe) answers what stage 1 can and appends every
 * other ray, with the distance the screen vouched for, to b_gi_trace_rays; compiled with GI_TRACE_FAR_FIELD,
 * the FAR-FIELD pass (cs_gi_probe_trace_far_field.sc, an indirect dispatch sized by
 * cs_gi_trace_far_field_args.sc) runs stages 2 and 3 over that dense list, so no distance-field march holds a
 * wave whose other rays the screen answered (the ray compaction). It stores a distance-field hit's march state in the
 * ray's slot instead of its radiance and appends the slot to b_gi_trace_hits; compiled with GI_TRACE_HIT_SHADE,
 * the HIT pass (cs_gi_probe_trace_hit_shade.sc, an indirect dispatch over that list) reads the surface cache at
 * those hits. The card lookup needs as many registers as the march, and in one kernel the march ran at the occupancy
 * of both.
 *
 * Writes rgb = pre-exposed radiance, a = the distance the spatial filter's angle weight sees: the screen
 * hit's distance, the trace length for distance-field hits, the cache probes' hit distance for the
 * hand-off, the maximum trace distance for the sky; encoded with the moving flag (GiEncodeTraceDistance).
 *
 * cs_gi_probe_trace_visualize.sc compiles this file with GI_VISUALIZE_TRACES for the probe trace visualization:
 * one group traces every stage of the probe nearest the visualized pixel again, with this frame's inputs - its rays
 * are the gather's own - and writes each ray as a line instead (gi_visualize.sh), knowing which rays a screen or
 * distance-field hit answered.
 */

#if defined(GI_VISUALIZE_TRACES)
#define GI_TRACE_SCREEN_STAGE 1
#define GI_TRACE_FAR_STAGES 1
#elif defined(GI_TRACE_FAR_FIELD) || defined(GI_TRACE_HIT_SHADE)
#define GI_TRACE_FAR_STAGES 1
#else
#define GI_TRACE_SCREEN_STAGE 1
#endif

#include "bgfx_compute.sh"
#include "../common.sh"
// eval_radiance_sh.
#include "../lighting.sh"
#include "gi/gi_common.sh"
#ifdef GI_TRACE_SCREEN_STAGE
#include "gi/gi_screen_trace.sh"
/// This frame's velocity buffer (where moving hit surfaces were last frame).
#define GI_VELOCITY_STAGE 12
#endif
/// The fast update's switch; the far stages read no velocity.
#include "gi/gi_motion.sh"
/// The distance-field hits compute their gradient where they are shaded (GiShadeFieldSurface).
#define GI_GLOBAL_SDF_DEFER_HIT_NORMAL 1
/// The global SDF's coverage (gi/sdf_clipmap.sh).
#define SDF_CLIPMAP_COVERAGE_STAGE 10
#define SDF_CLIPMAP_MIP_STAGE 9
#include "gi/gi_global_sdf.sh"
#include "gi/gi_radiance_cache_common.sh"
#include "gi/gi_constants.sh"
#include "gi/gi_pre_exposure.sh"

SAMPLER2D(s_gi_probe_records, 0);
/// The probes' importance-sampled ray slots (cs_gi_probe_generate_rays.sc).
SAMPLER2D(s_gi_ray_info, 11);
#ifdef GI_VISUALIZE_TRACES
/// The visualized probe's rays (gi_visualize.sh), written in place of the trace radiance.
BUFFER_RW(b_gi_visualize_traces, vec4, 5);
#else
IMAGE2D_WO(i_gi_trace_radiance, rgba16f, 5);
#endif
#ifdef GI_TRACE_SCREEN_STAGE
SAMPLER2D(s_gi_hiz, 1);
/// Last frame's scene colour, in last frame's pre-exposed space.
SAMPLER2D(s_gi_prev_color, 2);
/// Last frame's device depth.
SAMPLER2D(s_gi_prev_depth, 6);
#endif
#ifdef GI_TRACE_FAR_STAGES
/// The environment SH (9 texels), absolute radiance.
SAMPLER2D(s_gi_env_sh, 3);
/// The radiance cache: this frame's indirection and the bordered final atlas (radiance, hit distance in alpha).
BUFFER_RO(b_gi_rc_indirection, uint, 7);
SAMPLER2D(s_gi_rc_final, 8);
#include "gi/gi_radiance_cache_sample.sh"
#endif
#if !defined(GI_VISUALIZE_TRACES)
/// The rays the screen pass leaves to the far-field pass: [0] = their count, then GI_TRACE_RAY_STRIDE uints per ray
/// - its trace texel (x | y << 16) and the distance the screen vouched for (float bits); the far-field pass replaces
/// the distance with a distance-field hit's march state for the hit pass (t, then the hit field and voxel in the next
/// two slots), or -1 without one.
#define GI_TRACE_RAY_STRIDE 4
#if defined(GI_TRACE_SCREEN_STAGE)
BUFFER_RW(b_gi_trace_rays, uint, 3);
#elif defined(GI_TRACE_FAR_FIELD)
BUFFER_RW(b_gi_trace_rays, uint, 1);
#else
BUFFER_RO(b_gi_trace_rays, uint, 1);
#endif

/// The first uint of far ray @p index in b_gi_trace_rays.
uint GiTraceRaySlot(uint index)
{
	return 1u + uint(GI_TRACE_RAY_STRIDE) * index;
}

/// The far rays whose distance-field hit the hit pass shades, compacted by the far-field pass: [0] = their count,
/// then each one's slot in b_gi_trace_rays.
#if defined(GI_TRACE_FAR_FIELD)
BUFFER_RW(b_gi_trace_hits, uint, 2);
#elif defined(GI_TRACE_HIT_SHADE)
BUFFER_RO(b_gi_trace_hits, uint, 2);
#endif
#endif

/// The surface cache (gi_surface_cache.sh): global-SDF hits read the cards' final lighting through the
/// object grid when u_gi_hit_lighting.x > 0.5 (the cache is lit), black otherwise.
BUFFER_RO(b_gi_scene, vec4, 13);
SAMPLER2D(s_gi_card_final, 14);
SAMPLER3D(s_gi_object_grid, 15);
#define GI_SURFACE_CACHE_OBJECT_GRID
#include "gi/gi_surface_cache.sh"
uniform vec4 u_gi_hit_lighting;
#define u_gi_hits_read_surface_cache (u_gi_hit_lighting.x > 0.5)

/// x = Hi-Z mip count, y > 0 when the screen traces run (Hi-Z, last frame's colour and depth bound),
/// z > 0 when the radiance cache was updated this frame, w = experiment toggles: bit 1 starts the distance-field
/// stage 0.5 m out, bit 2 returns the global SDF at the probe instead of radiance (red outside / green inside at the
/// surface point, blue outside at the distance-field origin, 1.0 per 5 cm).
uniform vec4 u_gi_trace;

#define u_gi_hiz_mip_count  int(u_gi_trace.x)
#define u_gi_screen_traces  (u_gi_trace.y > 0.0)
#define u_gi_radiance_cache (u_gi_trace.z > 0.0)
#define u_gi_skip_near_field   ((uint(u_gi_trace.w) & 2u) != 0u)
#define u_gi_show_sdf_bias     ((uint(u_gi_trace.w) & 4u) != 0u)
#define u_gi_show_ray_sources  ((uint(u_gi_trace.w) & 8u) != 0u)
/// Keep one stage's radiance only (diagnostic): 1 = screen, 2 = distance field, 3 = radiance cache, 0 = all.
#define u_gi_keep_stage        int((uint(u_gi_trace.w) >> 4u) & 3u)
/// Diagnostic: a screen hit the vignette or the history test rejects vouches for nothing.
#define u_gi_rejected_hits_vouch_nothing ((uint(u_gi_trace.w) & 64u) != 0u)
/// Diagnostic: rays reaching the distance-field stage paint (start / near field, hit, 0), the others nothing.
#define u_gi_show_sdf_start ((uint(u_gi_trace.w) & 128u) != 0u)

#ifdef GI_TRACE_SCREEN_STAGE
/// One ray's screen stage.
struct GiScreenRay
{
	/// Radiance of a trusted hit.
	vec3 radiance;
	/// True when the screen answered the ray.
	bool answered;
	/// The hit's distance when answered; otherwise how far from the probe the screen vouched for the ray
	/// (proven free, or crossed by a hit it could not light).
	float distance;
	/// The answering hit's surface moved against the probe this frame.
	bool moving;
};

/// One ray's screen stage over at most @p max_distance, from a probe at view depth @p probe_depth whose surface moved
/// @p probe_speed metres since last frame.
GiScreenRay GiTraceScreenRay(vec3 position, vec3 normal, vec2 uv, float depth01, vec3 direction, float noise,
                                   float max_distance, float probe_speed, float probe_depth)
{
	GiScreenRay result;
	result.radiance = vec3_splat(0.0);
	result.answered = false;
	result.distance = 0.0;
	result.moving = false;
	vec3 origin = GiScreenTraceOrigin(position, normal, uv, depth01);
	GiScreenRaySegment segment = GiScreenSegment(origin, direction, max_distance);
	if(!segment.valid)
	{
		return result;
	}
	GiScreenTraceResult trace = GiTraceHZB(s_gi_hiz, u_gi_hiz_mip_count, segment.start, segment.end,
	                                             GI_SCREEN_TRACE_MAX_ITERATIONS,
	                                             GI_SCREEN_TRACE_RELATIVE_THICKNESS);
	vec3 trace_world = GiWorldFromDepth(trace.position.xy, trace.position.z);
	float miss_offset = (!trace.hit && !trace.uncertain) ? GI_SCREEN_TRACE_MISS_OFFSET : 0.0;
	result.distance = min(length(trace_world - position) + miss_offset, u_gi_max_trace_distance);
	if(!trace.hit)
	{
		return result;
	}
	if(u_gi_reject_near_hits)
	{
		float pixel_size = length(GiWorldFromDepth(uv + vec2(u_gi_view_texel.x, 0.0), depth01) - position);
		if(result.distance < 4.0 * pixel_size)
		{
			result.distance = 0.0;
			return result;
		}
	}
	vec3 prev_hit = GiPrevWorldPosition(trace.position.xy, trace_world);
	vec4 lit = GiScreenHistoryRadiance(s_gi_prev_color, s_gi_prev_depth, u_gi_prev_view_proj, prev_hit,
	                                      trace.position.xy, noise, GI_SCREEN_TRACE_HISTORY_DEPTH_TEST);
	result.answered = lit.w > 0.5;
	result.radiance = lit.xyz;
	float hit_speed = length(trace_world - prev_hit);
	result.moving = result.answered && u_gi_fast_update &&
	                abs(probe_speed - hit_speed) / max(probe_depth, GI_TEMPORAL_MOVING_MIN_DEPTH) >
	                    GI_TEMPORAL_MOVING_RELATIVE_SPEED;
	if(!result.answered && u_gi_rejected_hits_vouch_nothing)
	{
		result.distance = 0.0;
	}
	BRANCH
	if(u_gi_voxel_screen_hits)
	{
		// Experiment: every screen hit answered (no vignette or history rejection) and shaded from the
		// distance-field hits' store instead of last frame's colour, facing the field's gradient.
		result.answered = true;
		result.moving = false;
		result.radiance = vec3_splat(0.0);
		BRANCH
		if(u_gi_hits_read_surface_cache)
		{
			float voxel = SdfSampleClipmapLevels(trace_world).voxel_size;
			vec3 hit_normal = GiGlobalSdfNormal(trace_world, voxel, -direction);
			vec4 cards = GiSampleGlobalSdfHit(trace_world, hit_normal, 0.5 * voxel, s_gi_card_final);
			if(cards.w > 0.0)
			{
				result.radiance = GiCachedToView(cards.xyz / cards.w);
			}
		}
	}
	return result;
}
#endif

#ifdef GI_TRACE_FAR_STAGES
/// A distance-field hit's march state: enough to shade it later (GiShadeFieldSurface), from the ray's origin and
/// direction.
struct GiFieldSurface
{
	bool hit;
	vec3 origin;
	vec3 direction;
	/// GiSdfHit's t, hit_field and voxel.
	float t;
	float hit_field;
	float voxel;
};

/// The distance-field march's origin for a ray from @p position (normal @p normal) along @p direction.
vec3 GiFieldOrigin(vec3 position, vec3 normal, vec3 direction)
{
	return position + GI_SURFACE_BIAS * direction + GI_SURFACE_BIAS * normal;
}

/// The distance-field stage's march over [@p t_start, @p t_end], without the shading.
GiFieldSurface GiMarchDistanceField(vec3 position, vec3 normal, vec3 direction, float t_start, float t_end,
                                          GiSdfDither dither)
{
	GiFieldSurface surface;
	surface.origin = GiFieldOrigin(position, normal, direction);
	surface.direction = direction;
	GiSdfHit hit =
	    GiTraceGlobalSdfDithered(surface.origin, direction, t_start, t_end, true, 0.0, 0.0, 1.0, dither);
	surface.hit = hit.hit;
	surface.t = hit.t;
	surface.hit_field = hit.hit_field;
	surface.voxel = hit.voxel;
	return surface;
}

/// The surface cache at a distance-field hit, faded against self-lighting; a hit no card covers is black. The march
/// stops short of the surface by its expansion and the surface cache is a surface store, so the hit moves onto the
/// surface; its normal is the field's gradient where the march stopped (GiTraceGlobalSdfDithered's).
vec3 GiShadeFieldSurface(GiFieldSurface surface)
{
	vec3 radiance = vec3_splat(0.0);
	BRANCH
	if(u_gi_hits_read_surface_cache)
	{
		vec3 gradient = GiGlobalSdfNormal(surface.origin + surface.direction * surface.t, surface.voxel,
		                                     -surface.direction);
		vec3 normal = dot(gradient, surface.direction) > 0.0 ? -gradient : gradient;
		vec3 position = surface.origin + surface.direction * (surface.t + surface.hit_field);
		vec4 cards = GiSampleGlobalSdfHit(position, normal, 0.5 * surface.voxel, s_gi_card_final);
		if(cards.w > 0.0)
		{
			float self_lighting = smoothstep(GI_SDF_SELF_LIGHTING_FADE_START * surface.voxel,
			                                 GI_SDF_SELF_LIGHTING_FADE_END * surface.voxel,
			                                 surface.t);
			radiance = GiCachedToView(cards.xyz / cards.w) * self_lighting;
		}
	}
	return radiance;
}

/// Whether a distance-field hit marks its ray moving: the instance it hit moved more than
/// GI_TEMPORAL_MOVING_RELATIVE_SPEED of the probe's depth @p probe_depth this frame (the probe's own motion is not
/// compared here).
bool GiIsFieldHitMoving(GiFieldSurface surface, float probe_depth)
{
	BRANCH
	if(!u_gi_fast_update)
	{
		return false;
	}
	vec3 outside = surface.origin + surface.direction * (surface.t + surface.hit_field - 0.5 * surface.voxel);
	return GiGlobalSdfHitMotion(outside) / max(probe_depth, GI_TEMPORAL_MOVING_MIN_DEPTH) >
	       GI_TEMPORAL_MOVING_RELATIVE_SPEED;
}
#endif

#ifdef GI_VISUALIZE_TRACES
#include "gi/gi_adaptive_probes.sh"
#include "gi/gi_visualize.sh"

/// x, y = the full-resolution pixel whose probe the traces show (the cursor's); x < 0 for the view's centre.
uniform vec4 u_gi_visualize_traces;

/// A hit closer than GI_VISUALIZE_SELF_HIT_DISTANCE shows as a self-intersection, GI_VISUALIZE_SELF_HIT_LENGTH
/// long and red (1 cm and 5 cm).
#define GI_VISUALIZE_SELF_HIT_DISTANCE 0.01
#define GI_VISUALIZE_SELF_HIT_LENGTH 0.05
#define GI_VISUALIZE_GROUP_THREADS (GI_PROBE_TRACE_RES * GI_PROBE_TRACE_RES)

SHARED float s_gi_visualize_distance[GI_VISUALIZE_GROUP_THREADS];
SHARED int s_gi_visualize_index[GI_VISUALIZE_GROUP_THREADS];

/// The probe the visualization shows: the uniform probe of the screen tile under the query pixel, or the tile's
/// adaptive probe nearer to the query. The distances are of unsigned pixel differences (a probe right of or below the
/// query lies far away). The group's threads share the search of the adaptive records for the tile's probes; every
/// thread calls this and gets the probe's atlas tile.
ivec2 GiVisualizeTracesProbe(int thread)
{
	uvec2 query = u_gi_visualize_traces.x >= 0.0 ? uvec2(u_gi_visualize_traces.xy)
	                                                : uvec2(u_gi_probe_count / 2) * uint(u_gi_downsample);
	uvec2 screen_tile = min((query - uvec2(u_gi_placement_jitter)) / uint(u_gi_downsample),
	                        uvec2(u_gi_probe_count) - uvec2(1, 1));
	float best_distance = length(vec2(query - uvec2(GiProbePixel(ivec2(screen_tile)))));
	int best_index = -1;
	int adaptive_slots = (u_gi_atlas_rows - u_gi_probe_count.y) * u_gi_probe_count.x;
	for(int index = thread; index < adaptive_slots; index += GI_VISUALIZE_GROUP_THREADS)
	{
		ivec2 atlas_tile = GiAdaptiveAtlasTile(index);
		vec4 record = texelFetch(s_gi_probe_records, atlas_tile, 0);
		if(record.x > 0.0 && all(equal(GiProbeScreenTile(atlas_tile, record), ivec2(screen_tile))))
		{
			float distance = length(vec2(query - uvec2(GiProbeRecordPixel(record))));
			if(distance < best_distance)
			{
				best_distance = distance;
				best_index = index;
			}
		}
	}
	s_gi_visualize_distance[thread] = best_distance;
	s_gi_visualize_index[thread] = best_index;
	barrier();
	// The nearest of the threads' choices, the lower probe index on a tie (the first in the tile's list order).
	for(int other = 0; other < GI_VISUALIZE_GROUP_THREADS; ++other)
	{
		float distance = s_gi_visualize_distance[other];
		int index = s_gi_visualize_index[other];
		if(distance < best_distance || (distance == best_distance && index < best_index))
		{
			best_distance = distance;
			best_index = index;
		}
	}
	return best_index >= 0 ? GiAdaptiveAtlasTile(best_index) : ivec2(screen_tile);
}

/// The ray of probe texel @p texel as a line from the probe at @p position along
/// @p direction over the filter's distance; a @p hit closer than GI_VISUALIZE_SELF_HIT_DISTANCE shows red.
void GiStoreVisualizedTrace(ivec2 texel, vec3 radiance, float filter_distance, vec3 position, vec3 direction,
                               bool hit)
{
	if(hit && filter_distance < GI_VISUALIZE_SELF_HIT_DISTANCE)
	{
		filter_distance = GI_VISUALIZE_SELF_HIT_LENGTH;
		radiance = vec3(1.0, 0.0, 0.0);
	}
	int index = (texel.y * GI_PROBE_TRACE_RES + texel.x) * GI_VISUALIZE_TRACE_STRIDE;
	b_gi_visualize_traces[index] = vec4(position, 0.0);
	b_gi_visualize_traces[index + 1] = vec4(direction * filter_distance, 0.0);
	b_gi_visualize_traces[index + 2] = vec4(radiance, 0.0);
}
#endif

/// One probe ray: its probe (record, position, normal, screen tile) and direction, and how far its near stages run.
struct GiProbeRay
{
	bool valid;
	ivec2 texel;
	ivec2 trace_texel;
	ivec2 screen_tile;
	vec4 record;
	vec2 uv;
	float depth01;
	vec3 position;
	vec3 normal;
	vec3 direction;
	/// The radiance cache clipmap holding the probe, or GI_RADIANCE_CACHE_CLIPMAPS without one.
	int cache_clipmap;
	/// The hand-off distance where the cache covers the probe, the maximum trace distance otherwise.
	float near_field;
};

/// The ray of texel @p texel of the probe at atlas tile @p tile.
GiProbeRay GiMakeProbeRay(ivec2 tile, ivec2 texel)
{
	GiProbeRay ray;
	ray.texel = texel;
	ray.trace_texel = tile * GI_PROBE_TRACE_RES + texel;
	ray.record = texelFetch(s_gi_probe_records, tile, 0);
	ray.valid = ray.record.x > 0.0;
	ray.uv = GiPixelUv(GiProbeRecordPixel(ray.record));
	ray.depth01 = ray.record.w;
	ray.position = GiWorldFromDepth(ray.uv, ray.depth01);
	ray.normal = GiProbeNormal(ray.record);
	ray.screen_tile = GiProbeScreenTile(tile, ray.record);
	vec2 jitter = GiProbeRayJitter(ray.screen_tile, u_gi_frame_mod);
	ivec3 slot = GiProbeRaySlot(texel, texelFetch(s_gi_ray_info, ray.trace_texel, 0).x);
	ray.direction = GiEquiAreaSphericalMapping((vec2(slot.xy) + jitter) / float(GiRayResolution(slot.z)));
	ray.cache_clipmap =
	    (ray.valid && u_gi_radiance_cache) ? GiRcClipmapOf(ray.position) : GI_RADIANCE_CACHE_CLIPMAPS;
	ray.near_field = ray.cache_clipmap < GI_RADIANCE_CACHE_CLIPMAPS ? GiRcHandOffDistance(ray.cache_clipmap)
	                                                                    : u_gi_max_trace_distance;
	return ray;
}

/// What a ray found: its radiance, the filter's distance, the moving flag, whether a screen or distance-field hit
/// answered, which stage answered (the ray-source diagnostic: x screen, y distance field, z radiance cache)
/// and the distance-field stage's start over the near field and whether it hit (the start diagnostic).
struct GiRayResult
{
	vec3 radiance;
	float filter_distance;
	bool moving;
	bool hit;
	vec3 source;
	vec3 sdf_start;
};

#ifdef GI_TRACE_SCREEN_STAGE
/// Stage 1 for @p ray (no answer without screen traces).
GiScreenRay GiTraceProbeScreen(GiProbeRay ray)
{
	GiScreenRay screen;
	screen.radiance = vec3_splat(0.0);
	screen.answered = false;
	screen.distance = 0.0;
	screen.moving = false;
	BRANCH
	if(u_gi_screen_traces)
	{
		float noise = InterleavedGradientNoise(vec2(ray.trace_texel) + 0.5, u_gi_frame_mod);
		float probe_speed = 0.0;
		BRANCH
		if(u_gi_fast_update)
		{
			probe_speed = length(ray.position - GiPrevWorldPosition(ray.uv, ray.position));
		}
		screen = GiTraceScreenRay(ray.position, ray.normal, ray.uv, ray.depth01, ray.direction, noise, ray.near_field,
		                             probe_speed, ray.record.x);
	}
	return screen;
}

/// The result of a ray the screen answered.
GiRayResult GiScreenResult(GiScreenRay screen)
{
	GiRayResult result;
	result.radiance = screen.radiance;
	result.filter_distance = screen.distance;
	result.moving = screen.moving;
	result.hit = true;
	result.source = vec3(1.0, 0.0, 0.0);
	result.sdf_start = vec3_splat(0.0);
	return result;
}
#endif

#ifdef GI_TRACE_FAR_STAGES
/// What the far-field stages found before the distance field's shading: a hit leaves its radiance black and its
/// surface to GiShadeFieldSurface.
struct GiFarFieldTrace
{
	GiRayResult result;
	GiFieldSurface surface;
};

/// Stages 2 and 3 for @p ray, resuming before the distance @p vouched the screen vouched for, unshaded.
GiFarFieldTrace GiTraceFarFieldUnshaded(GiProbeRay ray, float vouched)
{
	GiRayResult result;
	result.radiance = vec3_splat(0.0);
	result.filter_distance = ray.near_field;
	result.moving = false;
	result.source = vec3_splat(0.0);
	float t_start = max(GI_MIN_TRACE_DISTANCE, vouched - GI_TRACE_RESUME_PULLBACK);
	if(u_gi_skip_near_field)
	{
		t_start = max(t_start, 0.5);
	}
	// The dither's screen coordinate: the probe's uniform tile x the tracing resolution + the ray's texel.
	GiSdfDither dither =
	    GiSdfMakeDither(vec2(ray.screen_tile * GI_PROBE_TRACE_RES + ray.texel), u_gi_frame_mod);
	GiFarFieldTrace trace;
	trace.surface = GiMarchDistanceField(ray.position, ray.normal, ray.direction, t_start, ray.near_field, dither);
	result.hit = trace.surface.hit;
	result.source.y = result.hit ? 1.0 : 0.0;
	result.sdf_start = vec3(saturate(t_start / max(ray.near_field, 1e-4)), result.source.y, 0.0);
	BRANCH
	if(!result.hit)
	{
		if(ray.cache_clipmap < GI_RADIANCE_CACHE_CLIPMAPS)
		{
			GiRcSample far_field = GiRcSampleInterpolated(ray.position, ray.direction, ray.cache_clipmap);
			result.radiance = GiCachedToView(far_field.radiance);
			result.filter_distance = far_field.hit_distance;
			result.source.z = 1.0;
		}
		else
		{
			result.radiance = eval_radiance_sh(s_gi_env_sh, ray.direction) * u_pre_exposure_value;
			result.filter_distance = u_gi_max_trace_distance;
		}
	}
	trace.result = result;
	return trace;
}

/// Stages 2 and 3 for @p ray, shaded.
GiRayResult GiTraceFarField(GiProbeRay ray, float vouched)
{
	GiFarFieldTrace trace = GiTraceFarFieldUnshaded(ray, vouched);
	BRANCH
	if(trace.surface.hit)
	{
		trace.result.radiance = GiShadeFieldSurface(trace.surface);
	}
	return trace.result;
}
#endif

/// What a ray stores in place of its radiance, decided before any shading: the radiance x keep, or a diagnostic's
/// override.
struct GiTraceOutput
{
	float keep;
	bool is_override;
	vec3 override_radiance;
};

GiTraceOutput GiMakeTraceOutput(GiProbeRay ray, GiRayResult result)
{
	GiTraceOutput trace_output;
	trace_output.keep = 1.0;
	trace_output.is_override = false;
	trace_output.override_radiance = vec3_splat(0.0);
	BRANCH
	if(u_gi_keep_stage > 0)
	{
		trace_output.keep = u_gi_keep_stage == 1 ? result.source.x
		                                            : (u_gi_keep_stage == 2 ? result.source.y : result.source.z);
	}
	if(u_gi_show_sdf_start)
	{
		trace_output.override_radiance = result.sdf_start * (0.1 * u_pre_exposure_value);
		trace_output.is_override = true;
	}
	if(u_gi_show_ray_sources)
	{
		// Each ray's answering stage as a unit radiance: the gather then shows each stage's share.
		trace_output.override_radiance = result.source * (0.1 * u_pre_exposure_value);
		trace_output.is_override = true;
	}
	// A diagnostic's samples stay out of the production path: fxc flattens an unguarded if.
	BRANCH
	if(u_gi_show_sdf_bias)
	{
		float at_surface = SdfSampleClipmap(ray.position);
		float at_origin = SdfSampleClipmap(ray.position + GI_SURFACE_BIAS * ray.normal);
		trace_output.override_radiance = vec3(max(at_surface, 0.0), max(-at_surface, 0.0), max(at_origin, 0.0)) * 20.0;
		trace_output.is_override = true;
	}
	return trace_output;
}

vec3 GiApplyTraceOutput(GiTraceOutput trace_output, vec3 radiance)
{
	return trace_output.is_override ? trace_output.override_radiance : radiance * trace_output.keep;
}

/// The radiance a ray stores: its result's, or a diagnostic's.
vec3 GiTraceOutputRadiance(GiProbeRay ray, GiRayResult result)
{
	return GiApplyTraceOutput(GiMakeTraceOutput(ray, result), result.radiance);
}

#if defined(GI_VISUALIZE_TRACES)
NUM_THREADS(GI_PROBE_TRACE_RES, GI_PROBE_TRACE_RES, 1)
void main()
{
	ivec2 texel = ivec2(gl_LocalInvocationID.xy);
	GiProbeRay ray = GiMakeProbeRay(GiVisualizeTracesProbe(int(gl_LocalInvocationIndex)), texel);
	if(!ray.valid)
	{
		GiStoreVisualizedTrace(texel, vec3_splat(0.0), u_gi_max_trace_distance, vec3_splat(0.0),
		                          vec3_splat(0.0), false);
		return;
	}
	GiScreenRay screen = GiTraceProbeScreen(ray);
	GiRayResult result;
	BRANCH
	if(screen.answered)
	{
		result = GiScreenResult(screen);
	}
	else
	{
		result = GiTraceFarField(ray, screen.distance);
	}
	GiStoreVisualizedTrace(texel, GiTraceOutputRadiance(ray, result), result.filter_distance, ray.position,
	                          ray.direction, result.hit);
}
#elif defined(GI_TRACE_FAR_FIELD) || defined(GI_TRACE_HIT_SHADE)
/// The far ray a thread takes (the far-field and hit passes share their dispatch).
uint GiFarRayIndex(uvec3 group, uint local)
{
	return (group.y * uint(GI_TRACE_FAR_FIELD_ROW_GROUPS) + group.x) * uint(GI_TRACE_FAR_FIELD_GROUP) + local;
}

/// The ray of far ray slot @p slot.
GiProbeRay GiFarRay(uint slot)
{
	uint packed_texel = b_gi_trace_rays[slot];
	ivec2 trace_texel = ivec2(int(packed_texel & 0xFFFFu), int(packed_texel >> 16u));
	ivec2 tile = trace_texel / GI_PROBE_TRACE_RES;
	return GiMakeProbeRay(tile, trace_texel - tile * GI_PROBE_TRACE_RES);
}

#if defined(GI_TRACE_FAR_FIELD)
/// The group's hits for the hit pass and their first entry in b_gi_trace_hits.
SHARED uint s_gi_hits;
SHARED uint s_gi_hits_base;

NUM_THREADS(GI_TRACE_FAR_FIELD_GROUP, 1, 1)
void main()
{
	uint index = GiFarRayIndex(gl_WorkGroupID, gl_LocalInvocationIndex);
	if(gl_LocalInvocationIndex == 0u)
	{
		s_gi_hits = 0u;
	}
	barrier();
	// The hits are compacted for the hit pass, so no thread leaves before the group's barriers.
	bool is_active = index < b_gi_trace_rays[0];
	uint slot = GiTraceRaySlot(is_active ? index : 0u);
	bool is_deferred = false;
	uint hit_index = 0u;
	BRANCH
	if(is_active)
	{
		float vouched = uintBitsToFloat(b_gi_trace_rays[slot + 1u]);
		GiProbeRay ray = GiFarRay(slot);
		GiFarFieldTrace trace = GiTraceFarFieldUnshaded(ray, vouched);
		GiTraceOutput trace_output = GiMakeTraceOutput(ray, trace.result);
		// A hit the output keeps is shaded by the hit pass, from its march state.
		is_deferred = trace.surface.hit && !trace_output.is_override;
		b_gi_trace_rays[slot + 1u] = floatBitsToUint(is_deferred ? trace.surface.t : -1.0);
		BRANCH
		if(is_deferred)
		{
			b_gi_trace_rays[slot + 2u] = floatBitsToUint(trace.surface.hit_field);
			b_gi_trace_rays[slot + 3u] = floatBitsToUint(trace.surface.voxel);
			atomicFetchAndAdd(s_gi_hits, 1u, hit_index);
		}
		else
		{
			imageStore(i_gi_trace_radiance, ray.trace_texel,
			           vec4(GiApplyTraceOutput(trace_output, trace.result.radiance),
			                GiEncodeTraceDistance(trace.result.filter_distance, false)));
		}
	}
	barrier();
	if(gl_LocalInvocationIndex == 0u && s_gi_hits > 0u)
	{
		atomicFetchAndAdd(b_gi_trace_hits[0], s_gi_hits, s_gi_hits_base);
	}
	barrier();
	if(is_deferred)
	{
		b_gi_trace_hits[1u + s_gi_hits_base + hit_index] = slot;
	}
}
#else
NUM_THREADS(GI_TRACE_FAR_FIELD_GROUP, 1, 1)
void main()
{
	uint index = GiFarRayIndex(gl_WorkGroupID, gl_LocalInvocationIndex);
	if(index >= b_gi_trace_hits[0])
	{
		return;
	}
	uint slot = b_gi_trace_hits[1u + index];
	float t = uintBitsToFloat(b_gi_trace_rays[slot + 1u]);
	GiProbeRay ray = GiFarRay(slot);
	GiFieldSurface surface;
	surface.hit = true;
	surface.origin = GiFieldOrigin(ray.position, ray.normal, ray.direction);
	surface.direction = ray.direction;
	surface.t = t;
	surface.hit_field = uintBitsToFloat(b_gi_trace_rays[slot + 2u]);
	surface.voxel = uintBitsToFloat(b_gi_trace_rays[slot + 3u]);
	// A distance-field hit keeps the near field as its filter distance (GiTraceFarFieldUnshaded).
	float keep = u_gi_keep_stage == 0 || u_gi_keep_stage == 2 ? 1.0 : 0.0;
	bool moving = GiIsFieldHitMoving(surface, ray.record.x);
	imageStore(i_gi_trace_radiance, ray.trace_texel,
	           vec4(GiShadeFieldSurface(surface) * keep, GiEncodeTraceDistance(ray.near_field, moving)));
}
#endif
#else
/// The group's rays left to the far-field pass and their first slot in b_gi_trace_rays.
SHARED uint s_gi_far_rays;
SHARED uint s_gi_far_base;

NUM_THREADS(GI_PROBE_TRACE_RES, GI_PROBE_TRACE_RES, 1)
void main()
{
	ivec2 texel = ivec2(gl_LocalInvocationID.xy);
	int index = texel.y * GI_PROBE_TRACE_RES + texel.x;
	if(index == 0)
	{
		s_gi_far_rays = 0u;
	}
	barrier();
	GiProbeRay ray = GiMakeProbeRay(ivec2(gl_WorkGroupID.xy), texel);
	GiScreenRay screen;
	screen.radiance = vec3_splat(0.0);
	screen.answered = false;
	screen.distance = 0.0;
	screen.moving = false;
	BRANCH
	if(ray.valid)
	{
		screen = GiTraceProbeScreen(ray);
	}
	// The unanswered rays take one slot each, the group one global add (the barriers stay in uniform flow control).
	bool is_far = ray.valid && !screen.answered;
	uint far_index = 0u;
	if(is_far)
	{
		atomicFetchAndAdd(s_gi_far_rays, 1u, far_index);
	}
	barrier();
	if(index == 0)
	{
		uint base = 0u;
		atomicFetchAndAdd(b_gi_trace_rays[0], s_gi_far_rays, base);
		s_gi_far_base = base;
	}
	barrier();
	if(!ray.valid)
	{
		imageStore(i_gi_trace_radiance, ray.trace_texel, vec4(0.0, 0.0, 0.0, u_gi_max_trace_distance));
		return;
	}
	if(is_far)
	{
		uint slot = GiTraceRaySlot(s_gi_far_base + far_index);
		b_gi_trace_rays[slot] = uint(ray.trace_texel.x) | (uint(ray.trace_texel.y) << 16u);
		b_gi_trace_rays[slot + 1u] = floatBitsToUint(screen.distance);
		return;
	}
	GiRayResult result = GiScreenResult(screen);
	imageStore(i_gi_trace_radiance, ray.trace_texel,
	           vec4(GiTraceOutputRadiance(ray, result), GiEncodeTraceDistance(result.filter_distance, result.moving)));
}
#endif
