/*
 * Lumen screen probe gather, tracing (UE 5.8 Global Tracing: ScreenProbeTraceScreenTexturesCS and
 * ScreenProbeTraceVoxelsCS, LumenScreenProbeTracing.usf:54-366 and 712-891). One 8x8 group per uniform
 * probe, one ray per equal-area octahedral texel; the 64 directions shift together inside their texels
 * by the probe tile's jitter of this frame (LumenProbeRayJitter).
 *
 * Each ray takes the first answer of these stages, each resuming LUMEN_TRACE_RESUME_PULLBACK before the
 * distance the previous one vouched for. Where the radiance cache covers the probe, the near stages stop at
 * the hand-off distance (TMin + the cell diagonal of the probe's clipmap, 3.6 m at clipmap 0):
 *  1. Hi-Z screen trace from the probe lifted off its surface by two projected half pixels along the
 *     normal. A hit is lit from last frame's scene colour at its reprojection, unless it falls in the
 *     outer screen band (stochastic vignette) or last frame's depth there disagrees (occluded or newly
 *     revealed); a rejected hit hands the distance field its crossing.
 *  2. Global distance field from the probe lifted LUMEN_SURFACE_BIAS along the ray and the normal, with dithered
 *     transparency where only two-sided meshes are near (lumen_global_sdf.sh; UE LumenScreenProbeTracing.usf:768). A
 *     hit reads the surface cache through the object grid, faded to black within one voxel of the origin against
 *     self-lighting.
 *  3. The radiance cache, interpolated at the probe's position (lumen_radiance_cache_sample.sh); beyond
 *     the cache's reach the distance field runs to the maximum trace distance and a miss reads the sky.
 *
 * Writes rgb = pre-exposed radiance, a = the distance the spatial filter's angle weight sees: the screen
 * hit's distance, the trace length for distance-field hits, the cache probes' hit distance for the
 * hand-off, the maximum trace distance for the sky.
 *
 * cs_lumen_probe_trace_visualize.sc compiles this file with LUMEN_VISUALIZE_TRACES for UE's
 * r.Lumen.ScreenProbeGather.VisualizeTraces (ScreenProbeSetupVisualizeTraces, LumenScreenProbeTracing.usf:893-1060):
 * one group traces again, with this frame's inputs, the probe nearest the visualized pixel - its rays are the
 * gather's own - and writes each ray as a line instead (lumen_visualize.sh), knowing which rays a screen or
 * distance-field hit answered.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
// eval_radiance_sh.
#include "../lighting.sh"
#include "lumen/lumen_common.sh"
#include "lumen/lumen_screen_trace.sh"
/// The global SDF's coverage (gi/sdf_clipmap.sh).
#define SDF_CLIPMAP_COVERAGE_STAGE 10
#include "lumen/lumen_global_sdf.sh"
#include "lumen/lumen_radiance_cache_common.sh"
#include "gi/gi_constants.sh"
#include "gi/gi_pre_exposure.sh"

SAMPLER2D(s_lumen_probe_records, 0);
SAMPLER2D(s_lumen_hiz, 1);
/// Last frame's scene colour, in last frame's pre-exposed space.
SAMPLER2D(s_lumen_prev_color, 2);
/// The environment SH (9 texels), absolute radiance.
SAMPLER2D(s_lumen_env_sh, 3);
#ifdef LUMEN_VISUALIZE_TRACES
/// The visualized probe's rays (lumen_visualize.sh), written in place of the trace radiance.
BUFFER_RW(b_lumen_visualize_traces, vec4, 5);
#else
IMAGE2D_WO(i_lumen_trace_radiance, rgba16f, 5);
#endif
/// Last frame's device depth.
SAMPLER2D(s_lumen_prev_depth, 6);
/// The radiance cache: this frame's indirection, the bordered final atlas and the probes' hit distances.
BUFFER_RO(b_lumen_rc_indirection, uint, 7);
SAMPLER2D(s_lumen_rc_final, 8);
SAMPLER2D(s_lumen_rc_depth, 9);
/// The probes' importance-sampled ray slots (cs_lumen_probe_generate_rays.sc).
SAMPLER2D(s_lumen_ray_info, 11);
/// The G-buffer normal (screen hits shaded from the surface cache, an experiment toggle).
SAMPLER2D(s_lumen_normal, 12);

#include "lumen/lumen_radiance_cache_sample.sh"

/// The surface cache (lumen_surface_cache.sh): global-SDF hits read the cards' final lighting through the
/// object grid when u_lumen_hit_lighting.x > 0.5 (the cache is lit), black otherwise.
BUFFER_RO(b_lumen_scene, vec4, 13);
SAMPLER2D(s_lumen_card_final, 14);
SAMPLER3D(s_lumen_object_grid, 15);
#define LUMEN_SURFACE_CACHE_OBJECT_GRID
#include "lumen/lumen_surface_cache.sh"
uniform vec4 u_lumen_hit_lighting;
#define u_lumen_hits_read_surface_cache (u_lumen_hit_lighting.x > 0.5)

/// x = Hi-Z mip count, y > 0 when the screen traces run (Hi-Z, last frame's colour and depth bound),
/// z > 0 when the radiance cache was updated this frame, w = experiment toggles: bit 1 starts the distance-field
/// stage 0.5 m out, bit 2 returns the global SDF at the probe instead of radiance (red outside / green inside at the
/// surface point, blue outside at the distance-field origin, 1.0 per 5 cm).
uniform vec4 u_lumen_trace;
/// Last frame's TAA-unjittered view projection.
uniform mat4 u_lumen_prev_view_proj;

#define u_lumen_hiz_mip_count  int(u_lumen_trace.x)
#define u_lumen_screen_traces  (u_lumen_trace.y > 0.0)
#define u_lumen_radiance_cache (u_lumen_trace.z > 0.0)
#define u_lumen_skip_near_field   ((uint(u_lumen_trace.w) & 2u) != 0u)
#define u_lumen_show_sdf_bias     ((uint(u_lumen_trace.w) & 4u) != 0u)
#define u_lumen_show_ray_sources  ((uint(u_lumen_trace.w) & 8u) != 0u)
/// Keep one stage's radiance only (diagnostic): 1 = screen, 2 = distance field, 3 = radiance cache, 0 = all.
#define u_lumen_keep_stage        int((uint(u_lumen_trace.w) >> 4u) & 3u)
/// Diagnostic: a screen hit the vignette or the history test rejects vouches for nothing.
#define u_lumen_rejected_hits_vouch_nothing ((uint(u_lumen_trace.w) & 64u) != 0u)
/// Diagnostic: rays reaching the distance-field stage paint (start / near field, hit, 0), the others nothing.
#define u_lumen_show_sdf_start ((uint(u_lumen_trace.w) & 128u) != 0u)

/// One ray's screen stage.
struct LumenScreenRay
{
	/// Radiance of a trusted hit.
	vec3 radiance;
	/// True when the screen answered the ray.
	bool answered;
	/// The hit's distance when answered; otherwise how far from the probe the screen vouched for the ray
	/// (proven free, or crossed by a hit it could not light).
	float distance;
};

/// One ray's screen stage over at most @p max_distance.
LumenScreenRay LumenTraceScreenRay(vec3 position, vec3 normal, vec2 uv, float depth01, vec3 direction, float noise,
                                   float max_distance)
{
	LumenScreenRay result;
	result.radiance = vec3_splat(0.0);
	result.answered = false;
	result.distance = 0.0;
	vec3 origin = LumenScreenTraceOrigin(position, normal, uv, depth01);
	LumenScreenRaySegment segment = LumenScreenSegment(origin, direction, max_distance);
	if(!segment.valid)
	{
		return result;
	}
	LumenScreenTraceResult trace = LumenTraceHZB(s_lumen_hiz, u_lumen_hiz_mip_count, segment.start, segment.end,
	                                             LUMEN_SCREEN_TRACE_MAX_ITERATIONS,
	                                             LUMEN_SCREEN_TRACE_RELATIVE_THICKNESS);
	vec3 trace_world = LumenWorldFromDepth(trace.position.xy, trace.position.z);
	float miss_offset = (!trace.hit && !trace.uncertain) ? LUMEN_SCREEN_TRACE_MISS_OFFSET : 0.0;
	result.distance = min(length(trace_world - position) + miss_offset, u_lumen_max_trace_distance);
	if(!trace.hit)
	{
		return result;
	}
	if(u_lumen_reject_near_hits)
	{
		float pixel_size = length(LumenWorldFromDepth(uv + vec2(u_lumen_view_texel.x, 0.0), depth01) - position);
		if(result.distance < 4.0 * pixel_size)
		{
			result.distance = 0.0;
			return result;
		}
	}
	vec4 lit = LumenScreenHistoryRadiance(s_lumen_prev_color, s_lumen_prev_depth, u_lumen_prev_view_proj, trace_world,
	                                      trace.position.xy, noise, LUMEN_SCREEN_TRACE_HISTORY_DEPTH_TEST);
	result.answered = lit.w > 0.5;
	result.radiance = lit.xyz;
	if(!result.answered && u_lumen_rejected_hits_vouch_nothing)
	{
		result.distance = 0.0;
	}
	BRANCH
	if(u_lumen_voxel_screen_hits)
	{
		// Experiment: every screen hit answered (no vignette or history rejection) and shaded from the
		// distance-field hits' store instead of last frame's colour.
		result.answered = true;
		ivec2 normal_size = textureSize(s_lumen_normal, 0);
		ivec2 normal_texel = min(ivec2(trace.position.xy * vec2(normal_size)), normal_size - ivec2(1, 1));
		vec3 hit_normal = decodeNormalOctahedron(texelFetch(s_lumen_normal, normal_texel, 0).xy);
		result.radiance = vec3_splat(0.0);
		BRANCH
		if(u_lumen_hits_read_surface_cache)
		{
			float voxel;
			SdfSampleClipmapEx(trace_world, voxel);
			vec4 cards = LumenSampleGlobalSdfHit(trace_world, hit_normal, 0.5 * voxel, s_lumen_card_final);
			if(cards.w > 0.0)
			{
				result.radiance = GiCachedToView(cards.xyz / cards.w);
			}
		}
	}
	return result;
}

/// The distance-field stage over [@p t_start, @p t_end]: rgb = the surface cache at the hit, faded against
/// self-lighting, a = 1; a = 0 on a miss. A hit no card covers is black, as in Lumen.
vec4 LumenTraceDistanceField(vec3 position, vec3 normal, vec3 direction, float t_start, float t_end, LumenSdfDither dither)
{
	vec3 origin = position + LUMEN_SURFACE_BIAS * direction + LUMEN_SURFACE_BIAS * normal;
	LumenSdfHit hit = LumenTraceGlobalSdfDithered(origin, direction, t_start, t_end, true, 0.0, 0.0, 1.0, dither);
	if(!hit.hit)
	{
		return vec4_splat(0.0);
	}
	// The march stops short of the surface by its expansion; the surface cache is a surface store.
	vec3 surface = origin + direction * (hit.t + hit.hit_field);
	vec3 surface_normal = dot(hit.normal, direction) > 0.0 ? -hit.normal : hit.normal;
	float self_lighting = smoothstep(LUMEN_SDF_SELF_LIGHTING_FADE_START * hit.voxel,
	                                 LUMEN_SDF_SELF_LIGHTING_FADE_END * hit.voxel,
	                                 hit.t);
	vec3 radiance = vec3_splat(0.0);
	BRANCH
	if(u_lumen_hits_read_surface_cache)
	{
		vec4 cards = LumenSampleGlobalSdfHit(surface, surface_normal, 0.5 * hit.voxel, s_lumen_card_final);
		if(cards.w > 0.0)
		{
			radiance = GiCachedToView(cards.xyz / cards.w) * self_lighting;
		}
	}
	return vec4(radiance, 1.0);
}

#ifdef LUMEN_VISUALIZE_TRACES
#include "lumen/lumen_adaptive_probes.sh"
#include "lumen/lumen_visualize.sh"

/// x, y = the full-resolution pixel whose probe the traces show (UE View.CursorPosition); x < 0 for the view's centre.
uniform vec4 u_lumen_visualize_traces;

/// UE shows a hit closer than LUMEN_VISUALIZE_SELF_HIT_DISTANCE as a self-intersection, LUMEN_VISUALIZE_SELF_HIT_LENGTH
/// long and red (1 cm and 5 cm).
#define LUMEN_VISUALIZE_SELF_HIT_DISTANCE 0.01
#define LUMEN_VISUALIZE_SELF_HIT_LENGTH 0.05
#define LUMEN_VISUALIZE_GROUP_THREADS (LUMEN_PROBE_TRACE_RES * LUMEN_PROBE_TRACE_RES)

SHARED float s_lumen_visualize_distance[LUMEN_VISUALIZE_GROUP_THREADS];
SHARED int s_lumen_visualize_index[LUMEN_VISUALIZE_GROUP_THREADS];

/// The probe UE ScreenProbeSetupVisualizeTraces shows: the uniform probe of the screen tile under the query pixel, or
/// the tile's adaptive probe nearer to the query. The distances are UE's, of unsigned pixel differences (a probe right
/// of or below the query lies far away). The group's threads share the search of the adaptive records for the tile's
/// probes; every thread calls this and gets the probe's atlas tile.
ivec2 LumenVisualizeTracesProbe(int thread)
{
	uvec2 query = u_lumen_visualize_traces.x >= 0.0 ? uvec2(u_lumen_visualize_traces.xy)
	                                                : uvec2(u_lumen_probe_count / 2) * uint(u_lumen_downsample);
	uvec2 screen_tile = min((query - uvec2(u_lumen_placement_jitter)) / uint(u_lumen_downsample),
	                        uvec2(u_lumen_probe_count) - uvec2(1, 1));
	float best_distance = length(vec2(query - uvec2(LumenProbePixel(ivec2(screen_tile)))));
	int best_index = -1;
	int adaptive_slots = (u_lumen_atlas_rows - u_lumen_probe_count.y) * u_lumen_probe_count.x;
	for(int index = thread; index < adaptive_slots; index += LUMEN_VISUALIZE_GROUP_THREADS)
	{
		ivec2 atlas_tile = LumenAdaptiveAtlasTile(index);
		vec4 record = texelFetch(s_lumen_probe_records, atlas_tile, 0);
		if(record.x > 0.0 && all(equal(LumenProbeScreenTile(atlas_tile, record), ivec2(screen_tile))))
		{
			float distance = length(vec2(query - uvec2(LumenProbeRecordPixel(record))));
			if(distance < best_distance)
			{
				best_distance = distance;
				best_index = index;
			}
		}
	}
	s_lumen_visualize_distance[thread] = best_distance;
	s_lumen_visualize_index[thread] = best_index;
	barrier();
	// The nearest of the threads' choices, the lower probe index on a tie (UE walks the tile's list in order).
	for(int other = 0; other < LUMEN_VISUALIZE_GROUP_THREADS; ++other)
	{
		float distance = s_lumen_visualize_distance[other];
		int index = s_lumen_visualize_index[other];
		if(distance < best_distance || (distance == best_distance && index < best_index))
		{
			best_distance = distance;
			best_index = index;
		}
	}
	return best_index >= 0 ? LumenAdaptiveAtlasTile(best_index) : ivec2(screen_tile);
}

/// UE WriteTraceForVisualization: the ray of probe texel @p texel as a line from the probe at @p position along
/// @p direction over the filter's distance; a @p hit closer than LUMEN_VISUALIZE_SELF_HIT_DISTANCE shows red.
void LumenStoreVisualizedTrace(ivec2 texel, vec3 radiance, float filter_distance, vec3 position, vec3 direction,
                               bool hit)
{
	if(hit && filter_distance < LUMEN_VISUALIZE_SELF_HIT_DISTANCE)
	{
		filter_distance = LUMEN_VISUALIZE_SELF_HIT_LENGTH;
		radiance = vec3(1.0, 0.0, 0.0);
	}
	int index = (texel.y * LUMEN_PROBE_TRACE_RES + texel.x) * LUMEN_VISUALIZE_TRACE_STRIDE;
	b_lumen_visualize_traces[index] = vec4(position, 0.0);
	b_lumen_visualize_traces[index + 1] = vec4(direction * filter_distance, 0.0);
	b_lumen_visualize_traces[index + 2] = vec4(radiance, 0.0);
}
#endif

NUM_THREADS(8, 8, 1)
void main()
{
#ifdef LUMEN_VISUALIZE_TRACES
	ivec2 tile = LumenVisualizeTracesProbe(int(gl_LocalInvocationIndex));
#else
	ivec2 tile = ivec2(gl_WorkGroupID.xy);
#endif
	ivec2 texel = ivec2(gl_LocalInvocationID.xy);
	ivec2 trace_texel = tile * LUMEN_PROBE_TRACE_RES + texel;
	vec4 record = texelFetch(s_lumen_probe_records, tile, 0);
	if(record.x <= 0.0)
	{
#ifdef LUMEN_VISUALIZE_TRACES
		LumenStoreVisualizedTrace(texel, vec3_splat(0.0), u_lumen_max_trace_distance, vec3_splat(0.0),
		                          vec3_splat(0.0), false);
#else
		imageStore(i_lumen_trace_radiance, trace_texel, vec4(0.0, 0.0, 0.0, u_lumen_max_trace_distance));
#endif
		return;
	}
	ivec2 pixel = LumenProbeRecordPixel(record);
	vec2 uv = LumenPixelUv(pixel);
	float depth01 = record.w;
	vec3 position = LumenWorldFromDepth(uv, depth01);
	vec3 normal = LumenProbeNormal(record);
	ivec2 screen_tile = LumenProbeScreenTile(tile, record);
	vec2 jitter = LumenProbeRayJitter(screen_tile, u_lumen_frame_mod);
	ivec3 ray = LumenProbeRaySlot(texel, texelFetch(s_lumen_ray_info, trace_texel, 0).x);
	vec3 direction = LumenEquiAreaSphericalMapping((vec2(ray.xy) + jitter) / float(LumenRayResolution(ray.z)));
	int cache_clipmap = u_lumen_radiance_cache ? LumenRcClipmapOf(position) : LUMEN_RADIANCE_CACHE_CLIPMAPS;
	bool cached_far_field = cache_clipmap < LUMEN_RADIANCE_CACHE_CLIPMAPS;
	float near_field = cached_far_field ? LumenRcHandOffDistance(cache_clipmap) : u_lumen_max_trace_distance;
	vec3 radiance = vec3_splat(0.0);
	float filter_distance = near_field;
	float vouched = 0.0;
	bool answered = false;
	/// Which stage answered (the ray-source diagnostic): x screen, y distance field, z radiance cache.
	vec3 source = vec3_splat(0.0);
	/// The distance-field stage's start over the near field and whether it hit (the start diagnostic).
	vec3 sdf_start = vec3_splat(0.0);
	BRANCH
	if(u_lumen_screen_traces)
	{
		float noise = InterleavedGradientNoise(vec2(trace_texel) + 0.5, u_lumen_frame_mod);
		LumenScreenRay screen = LumenTraceScreenRay(position, normal, uv, depth01, direction, noise, near_field);
		answered = screen.answered;
		radiance = screen.radiance;
		vouched = screen.distance;
		source.x = answered ? 1.0 : 0.0;
		if(answered)
		{
			filter_distance = screen.distance;
		}
	}
	BRANCH
	if(!answered)
	{
		float t_start = max(LUMEN_MIN_TRACE_DISTANCE, vouched - LUMEN_TRACE_RESUME_PULLBACK);
		if(u_lumen_skip_near_field)
		{
			t_start = max(t_start, 0.5);
		}
		// UE DitherScreenCoord: the probe's uniform tile x the tracing resolution + the ray's texel.
		LumenSdfDither dither = LumenSdfMakeDither(vec2(screen_tile * LUMEN_PROBE_TRACE_RES + texel), u_lumen_frame_mod);
		vec4 field = LumenTraceDistanceField(position, normal, direction, t_start, near_field, dither);
		answered = field.w > 0.5;
		radiance = field.xyz;
		source.y = answered ? 1.0 : 0.0;
		sdf_start = vec3(saturate(t_start / max(near_field, 1e-4)), source.y, 0.0);
	}
#ifdef LUMEN_VISUALIZE_TRACES
	/// A screen or distance-field hit answered the ray (UE bHit).
	bool hit = answered;
#endif
	BRANCH
	if(!answered)
	{
		if(cached_far_field)
		{
			LumenRcSample far_field = LumenRcSampleInterpolated(position, direction, cache_clipmap);
			radiance = GiCachedToView(far_field.radiance);
			filter_distance = far_field.hit_distance;
			source.z = 1.0;
		}
		else
		{
			radiance = eval_radiance_sh(s_lumen_env_sh, direction) * u_pre_exposure_value;
			filter_distance = u_lumen_max_trace_distance;
		}
	}
	BRANCH
	if(u_lumen_keep_stage > 0)
	{
		float keep = u_lumen_keep_stage == 1 ? source.x : (u_lumen_keep_stage == 2 ? source.y : source.z);
		radiance *= keep;
	}
	if(u_lumen_show_sdf_start)
	{
		radiance = sdf_start * (0.1 * u_pre_exposure_value);
	}
	if(u_lumen_show_ray_sources)
	{
		// Each ray's answering stage as a unit radiance: the gather then shows each stage's share.
		radiance = source * (0.1 * u_pre_exposure_value);
	}
	if(u_lumen_show_sdf_bias)
	{
		float at_surface = SdfSampleClipmap(position);
		float at_origin = SdfSampleClipmap(position + LUMEN_SURFACE_BIAS * normal);
		radiance = vec3(max(at_surface, 0.0), max(-at_surface, 0.0), max(at_origin, 0.0)) * 20.0;
	}
#ifdef LUMEN_VISUALIZE_TRACES
	LumenStoreVisualizedTrace(texel, radiance, filter_distance, position, direction, hit);
#else
	imageStore(i_lumen_trace_radiance, trace_texel, vec4(radiance, filter_distance));
#endif
}
