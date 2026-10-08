/*
 * Lumen reflections, the distance-field stage (UE 5.8 ReflectionTraceVoxelsCS / TraceVoxels,
 * LumenReflectionTracing.usf:711-902, Global Tracing). Runs for every trace texel the screen trace did not answer,
 * from the pixel it traces (LumenReflectionTracePixel).
 *
 * The ray restarts from the pixel moved LUMEN_SURFACE_BIAS along itself, LUMEN_REFLECTION_SDF_PULLBACK_HALF_VOXELS
 * voxel extents (of the clipmap level where the screen trace ended) before the distance the screen vouched for,
 * so the field's surface expansion starts outside the surface. The expansion grows with the largest distance the
 * ray keeps from any surface, not with its travel, and near-mirror rays dither their step length against
 * stepping artefacts. A hit reads the surface cache (black where no card covers it), then last frame's scene
 * colour instead when the hit is the surface the depth buffer shows (UE SampleSceneColorAtHit). A miss traces the
 * screen once more from where the distance field ends (LumenReflectionDistantScreenTrace: content beyond its reach,
 * on screen), then reads the sky (LumenReflectionSkyRadiance). Radiance is pre-exposed and clamped to
 * LUMEN_REFLECTION_MAX_RAY_INTENSITY; its alpha is 1 where the ray hit the distance field (the surface cache feedback's
 * texels, cs_lumen_reflection_feedback.sc), 0 elsewhere.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
// eval_radiance_sh.
#include "../lighting.sh"
#define LUMEN_REFLECTION_TILES_STAGE 12
#include "lumen/lumen_reflection_common.sh"
#include "lumen/lumen_screen_trace.sh"
/// The global SDF's coverage (gi/sdf_clipmap.sh).
#define SDF_CLIPMAP_COVERAGE_STAGE 10
#define SDF_CLIPMAP_MIP_STAGE 9
#include "lumen/lumen_global_sdf.sh"
#include "gi/gi_pre_exposure.sh"

IMAGE2D_WO(s_lumen_reflection_radiance_out, rgba16f, 0);
/// The screen trace's encoded distance in, this stage's out.
IMAGE2D_RW(s_lumen_reflection_hit_rw, r32f, 1);
SAMPLER2D(s_lumen_reflection_ray, 2);
SAMPLER2D(s_lumen_depth, 3);
/// Last frame's scene colour, in last frame's pre-exposed space.
SAMPLER2D(s_lumen_prev_color, 5);
/// Last frame's device depth.
SAMPLER2D(s_lumen_prev_depth, 6);
/// The environment SH (9 texels), absolute radiance.
SAMPLER2D(s_lumen_env_sh, 7);
/// This frame's reflection-probe layer (PBUFFER as the probe pass drew it): rgb = the probes' prefiltered
/// reflection at the pixel, premultiplied and pre-exposed, a = their coverage; unoccluded.
SAMPLER2D(s_lumen_probe_layer, 8);

/// The surface cache (lumen_surface_cache.sh), read through the object grid and each card's hi-res pages where the
/// feedback mapped them.
BUFFER_RO(b_lumen_scene, vec4, 13);
SAMPLER2D(s_lumen_card_final, 14);
SAMPLER3D(s_lumen_object_grid, 15);
#define LUMEN_SURFACE_CACHE_OBJECT_GRID
#define LUMEN_SURFACE_CACHE_HI_RES
#include "lumen/lumen_surface_cache.sh"

/// x > 0.5 when the surface cache holds lighting.
uniform vec4 u_lumen_hit_lighting;
/// This frame's velocity buffer (where a moving hit surface was last frame).
#define LUMEN_VELOCITY_STAGE 11
#include "lumen/lumen_motion.sh"

/// UE SampleSceneColorAtHit (LumenScreenTracing.ush:78-137): last frame's colour at a distance-field hit (rgb,
/// a = 1) when the hit projects on screen within LUMEN_REFLECTION_SCENE_COLOR_RELATIVE_DEPTH of the depth buffer,
/// faces the camera within LUMEN_REFLECTION_SCENE_COLOR_NORMAL_COS, and passes the vignette and the history depth
/// test; a = 0 otherwise.
vec4 LumenReflectionSceneColorAtHit(ivec2 pixel, vec3 hit_world, vec3 hit_normal)
{
	vec4 clip = mul(u_viewProj, vec4(hit_world, 1.0));
	if(clip.w <= 0.0)
	{
		return vec4_splat(0.0);
	}
	vec2 ndc = clip.xy / clip.w;
	if(any(greaterThanEqual(abs(ndc), vec2_splat(1.0))))
	{
		return vec4_splat(0.0);
	}
	vec2 uv = clipToUv(ndc * 0.5 + 0.5);
	ivec2 size = ivec2(u_lumen_view_size);
	ivec2 texel = clamp(ivec2(uv * u_lumen_view_size), ivec2(0, 0), size - ivec2(1, 1));
	float scene_depth01 = texelFetch(s_lumen_depth, texel, 0).x;
	float scene_depth = LumenLinearDepth(scene_depth01);
	vec3 to_camera = normalize(LumenReflectionCamera() - hit_world);
	if(abs(clip.w - scene_depth) >= LUMEN_REFLECTION_SCENE_COLOR_RELATIVE_DEPTH * max(scene_depth, 1e-5) ||
	   dot(to_camera, hit_normal) < LUMEN_REFLECTION_SCENE_COLOR_NORMAL_COS)
	{
		return vec4_splat(0.0);
	}
	float noise = InterleavedGradientNoise(vec2(pixel) + 0.5, u_lumen_frame_mod);
	vec3 surface = LumenWorldFromDepth(uv, scene_depth01);
	return LumenScreenHistoryRadiance(s_lumen_prev_color, s_lumen_prev_depth, u_lumen_prev_view_proj,
	                                  LumenPrevWorldPosition(uv, surface), uv, noise,
	                                  LUMEN_REFLECTION_SCENE_COLOR_RELATIVE_DEPTH);
}

/// UE r.Lumen.Reflections.DistantScreenTraces: the farthest a distant screen trace reaches (MaxTraceDistance, 2 km),
/// its slope tolerance (DepthThreshold) and its linear steps (LumenScreenTracing.ush DistantScreenTrace).
#define LUMEN_REFLECTION_DISTANT_TRACE_DISTANCE 2000.0
#define LUMEN_REFLECTION_DISTANT_TRACE_SLOPE_TOLERANCE 2.0
#define LUMEN_REFLECTION_DISTANT_TRACE_STEPS 16
/// UE InitScreenSpaceRayFromWorldSpace: a ray toward the camera ends at this share of the start's view depth.
#define LUMEN_REFLECTION_DISTANT_TRACE_NEAR_STOP 0.95
/// A history depth tolerance beyond the whole depth range: UE reads a distant hit's history colour without a depth
/// test.
#define LUMEN_REFLECTION_DISTANT_TRACE_NO_DEPTH_TEST 2.0

/// The ray from @p origin along @p direction, @p length long, started where it leaves the global SDF's outermost level
/// shrunk by one voxel, pulled back into it by the range of a step's jitter (UE ClipRayToStartOutsideGlobalSDF,
/// GlobalDistanceFieldUtils.ush:202-221): xyz = the start, w = the length left.
vec4 LumenClipRayToOutsideGlobalSdf(vec3 origin, vec3 direction, float length)
{
	vec4 level = u_sdf_clipmap_levels[SDF_CLIPMAP_LEVEL_COUNT - 1];
	vec3 low = level.xyz + vec3_splat(1.5 * level.w);
	vec3 high = level.xyz + vec3_splat((u_sdf_clipmap_resolution - 1.5) * level.w);
	// UE LineBoxIntersect: the segment's parameters in [0, 1].
	vec3 segment = direction * length;
	vec3 inverse_segment = vec3_splat(1.0) / (sign(segment) * max(abs(segment), vec3_splat(1e-8)) +
	                                          vec3(equal(segment, vec3_splat(0.0))) * 1e-8);
	vec3 to_low = (low - origin) * inverse_segment;
	vec3 to_high = (high - origin) * inverse_segment;
	vec3 near_side = min(to_low, to_high);
	vec3 far_side = max(to_low, to_high);
	float enter = saturate(max(near_side.x, max(near_side.y, near_side.z)));
	float leave = saturate(min(far_side.x, min(far_side.y, far_side.z)));
	if(enter < leave)
	{
		// t + (t - 1) / (2 n - 1) for n distant steps: the jittered first step may start up to one step back.
		leave = max((32.0 / 31.0) * leave - 1.0 / 31.0, 0.0);
		origin += segment * leave;
		length = max(length * (1.0 - leave), 0.0);
	}
	return vec4(origin, length);
}

/// UE DistantScreenTrace (LumenScreenTracing.ush:144-213) for a ray from @p origin along @p direction that the distance
/// field missed: from where the distance field ends, 16 linear steps through this frame's depth (UE marches its
/// furthest HZB's first mip; here the full-resolution depth) with a jittered offset and UE's slope tolerance. A hit
/// returns last frame's colour at its velocity reprojection, outside the vignette (rgb pre-exposed, a = 1); a = 0
/// otherwise.
vec4 LumenReflectionDistantScreenTrace(ivec2 pixel, vec3 origin, vec3 direction)
{
	vec4 ray = LumenClipRayToOutsideGlobalSdf(origin, direction, LUMEN_REFLECTION_DISTANT_TRACE_DISTANCE);
	if(ray.w <= 0.0)
	{
		return vec4_splat(0.0);
	}
	vec4 start_clip = mul(u_viewProj, vec4(ray.xyz, 1.0));
	// A start off screen never meets anything on it.
	if(start_clip.w < 0.0 || any(greaterThan(abs(start_clip.xy), vec2_splat(start_clip.w))))
	{
		return vec4_splat(0.0);
	}
	// UE InitScreenSpaceRayFromWorldSpace: the ray in screen uv and device depth, ending short of the near plane, cut at
	// the screen's edge; the tolerance from the depth change of moving the trace length straight away from the camera.
	float view_depth = mul(u_view, vec4(ray.xyz, 1.0)).z;
	float view_direction_z = mul(u_view, vec4(direction, 0.0)).z;
	float trace_length = view_direction_z < 0.0
	                         ? min(-LUMEN_REFLECTION_DISTANT_TRACE_NEAR_STOP * view_depth / view_direction_z, ray.w)
	                         : ray.w;
	vec3 start = LumenProjectToScreen(ray.xyz);
	vec3 delta = LumenProjectToScreen(ray.xyz + direction * trace_length) - start;
	vec4 depth_clip = start_clip + mul(u_proj, vec4(0.0, 0.0, trace_length, 0.0));
	float depth_reference = toDepthTextureZ(depth_clip.z / depth_clip.w);
	delta *= min(LumenScreenExit(start.xy, delta.xy), 1.0);
	float steps = float(LUMEN_REFLECTION_DISTANT_TRACE_STEPS);
	float tolerance =
	    max(abs(delta.z), (depth_reference - start.z) * LUMEN_REFLECTION_DISTANT_TRACE_SLOPE_TOLERANCE) / steps;
	vec3 step_uvz = delta / steps;
	float noise = InterleavedGradientNoise(vec2(pixel) + 0.5, u_lumen_frame_mod);
	vec3 ray_uvz = start + step_uvz * noise;
	ivec2 size = ivec2(u_lumen_view_size);
	float last_difference = 0.0;
	bool is_hit = false;
	vec3 hit_uvz = vec3_splat(0.0);
	LOOP
	for(int i = 1; i <= LUMEN_REFLECTION_DISTANT_TRACE_STEPS; ++i)
	{
		vec3 sample_uvz = ray_uvz + step_uvz * float(i);
		ivec2 texel = clamp(ivec2(sample_uvz.xy * u_lumen_view_size), ivec2(0, 0), size - ivec2(1, 1));
		float scene_depth = texelFetch(s_lumen_depth, texel, 0).x;
		// In front of the depth buffer > 0; a hit lies behind it by less than twice the tolerance, not on the far plane.
		float difference = scene_depth - sample_uvz.z;
		BRANCH
		if(abs(difference + tolerance) < tolerance && scene_depth < 1.0)
		{
			// The crossing between this sample and the last (UE's line segment intersection).
			float crossing = saturate(last_difference / (last_difference - difference));
			hit_uvz = ray_uvz + step_uvz * (float(i - 1) + crossing);
			is_hit = true;
			break;
		}
		last_difference = difference;
	}
	if(!is_hit)
	{
		return vec4_splat(0.0);
	}
	vec3 hit_world = LumenWorldFromDepth(hit_uvz.xy, hit_uvz.z);
	return LumenScreenHistoryRadiance(s_lumen_prev_color, s_lumen_prev_depth, u_lumen_prev_view_proj,
	                                  LumenPrevWorldPosition(hit_uvz.xy, hit_world), hit_uvz.xy, noise,
	                                  LUMEN_REFLECTION_DISTANT_TRACE_NO_DEPTH_TEST);
}

/// What a ray that misses everything sees, pre-exposed (UE EvaluateSkyRadiance: the sky light cubemap in the ray
/// direction): this pixel's reflection-probe layer - the environment's and the authored probes' prefiltered cubemaps
/// in its reflection - with the environment SH along @p direction filling the share no probe covers.
vec3 LumenReflectionSkyRadiance(ivec2 pixel, vec3 direction)
{
	vec4 probe_layer = texelFetch(s_lumen_probe_layer, pixel, 0);
	float coverage = saturate(probe_layer.w);
	// A branch, not a select: full coverage skips the SH's nine fetches.
	BRANCH
	if(coverage >= 0.999)
	{
		return probe_layer.xyz;
	}
	return probe_layer.xyz + eval_radiance_sh(s_lumen_env_sh, direction) * u_pre_exposure_value * (1.0 - coverage);
}

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 trace_coord = ivec2(gl_GlobalInvocationID.xy);
	if(any(greaterThanEqual(trace_coord, LumenReflectionTraceSize())))
	{
		return;
	}
	vec4 ray = LumenReflectionTraceRay(s_lumen_reflection_ray, trace_coord);
	if(ray.w <= 0.0)
	{
		return;
	}
	float screen = imageLoad(s_lumen_reflection_hit_rw, trace_coord).x;
	if(screen < 0.0)
	{
		return;
	}
	ivec2 pixel = LumenReflectionTracePixel(trace_coord);
	vec3 direction = normalize(ray.xyz);
	float depth01 = texelFetch(s_lumen_depth, pixel, 0).x;
	vec3 position = LumenWorldFromDepth(LumenPixelUv(pixel), depth01);
	vec3 origin = position + LUMEN_SURFACE_BIAS * direction;
	float resume_voxel = SdfSampleClipmapLevels(position + screen * direction).voxel_size;
	float t_start = max(screen - LUMEN_REFLECTION_SDF_PULLBACK_HALF_VOXELS * 0.5 * resume_voxel, 0.0);
	float step_noise = mix(LUMEN_REFLECTION_SDF_STEP_DITHER, 1.0 / LUMEN_REFLECTION_SDF_STEP_DITHER,
	                       InterleavedGradientNoise(vec2(pixel), u_lumen_frame_mod));
	float step_factor = mix(step_noise, 1.0, saturate(ray.w / LUMEN_REFLECTION_SDF_STEP_DITHER_CONE));
	LumenSdfHit hit = LumenTraceGlobalSdfStepped(origin, direction, t_start, u_lumen_max_trace_distance, false, 0.0, 0.0,
	                                             step_factor);
	vec3 radiance = vec3_splat(0.0);
	float hit_distance = u_lumen_max_trace_distance;
	float field_hit = hit.hit ? 1.0 : 0.0;
	BRANCH
	if(hit.hit)
	{
		hit_distance = hit.t;
		vec3 surface_normal = dot(hit.normal, direction) > 0.0 ? -hit.normal : hit.normal;
		// The on-screen colour answers the hit when it can be trusted; only then are the cards, the expensive lookup,
		// skipped (the order changes no result).
		vec4 scene_color = LumenReflectionSceneColorAtHit(pixel, origin + direction * hit.t, surface_normal);
		radiance = scene_color.xyz;
		BRANCH
		if(scene_color.w <= 0.5 && u_lumen_hit_lighting.x > 0.5)
		{
			// The march stops short of the surface by its expansion; the cards are a surface store.
			vec3 surface = origin + direction * (hit.t + hit.hit_field);
			vec4 cards = LumenSampleGlobalSdfHit(surface, surface_normal, 0.5 * hit.voxel, s_lumen_card_final);
			radiance = cards.w > 0.0 ? GiCachedToView(cards.xyz / cards.w) : vec3_splat(0.0);
		}
		if(u_lumen_reflection_show_trace_types)
		{
			radiance = vec3(scene_color.w > 0.5 ? 0.5 : 0.0, 0.5, 0.0) * u_pre_exposure_value;
		}
	}
	else
	{
		vec4 distant = vec4_splat(0.0);
		BRANCH
		if(u_lumen_reflection_distant_traces)
		{
			distant = LumenReflectionDistantScreenTrace(pixel, origin, direction);
		}
		// A distant hit keeps the maximum hit distance (UE): the resolve sees it as far away.
		hit.hit = distant.w > 0.5;
		radiance = hit.hit ? distant.xyz : LumenReflectionSkyRadiance(pixel, direction);
		if(u_lumen_reflection_show_trace_types)
		{
			radiance = (hit.hit ? vec3(0.5, 0.0, 0.5) : vec3(0.0, 0.0, 0.5)) * u_pre_exposure_value;
		}
	}
	float max_channel = max(max(radiance.x, radiance.y), radiance.z);
	if(max_channel > LUMEN_REFLECTION_MAX_RAY_INTENSITY)
	{
		radiance *= LUMEN_REFLECTION_MAX_RAY_INTENSITY / max_channel;
	}
	imageStore(s_lumen_reflection_radiance_out, trace_coord, vec4(radiance, field_hit));
	imageStore(s_lumen_reflection_hit_rw, trace_coord, vec4(LumenEncodeRayDistance(hit_distance, hit.hit), 0.0, 0.0, 0.0));
}
