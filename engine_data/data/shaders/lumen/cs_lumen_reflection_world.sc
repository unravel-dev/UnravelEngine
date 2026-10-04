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
 * colour instead when the hit is the surface the depth buffer shows (UE SampleSceneColorAtHit); a miss reads the
 * sky (LumenReflectionSkyRadiance). Radiance is pre-exposed and clamped to LUMEN_REFLECTION_MAX_RAY_INTENSITY.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
// eval_radiance_sh.
#include "../lighting.sh"
#include "lumen/lumen_reflection_common.sh"
#include "lumen/lumen_screen_trace.sh"
/// The global SDF's coverage (gi/sdf_clipmap.sh).
#define SDF_CLIPMAP_COVERAGE_STAGE 10
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

/// The surface cache (lumen_surface_cache.sh), read through the object grid.
BUFFER_RO(b_lumen_scene, vec4, 13);
SAMPLER2D(s_lumen_card_final, 14);
SAMPLER3D(s_lumen_object_grid, 15);
#define LUMEN_SURFACE_CACHE_OBJECT_GRID
#include "lumen/lumen_surface_cache.sh"

/// x > 0.5 when the surface cache holds lighting.
uniform vec4 u_lumen_hit_lighting;
/// Last frame's TAA-unjittered view projection.
uniform mat4 u_lumen_prev_view_proj;

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
	return LumenScreenHistoryRadiance(s_lumen_prev_color, s_lumen_prev_depth, u_lumen_prev_view_proj, surface, uv, noise,
	                                  LUMEN_REFLECTION_SCENE_COLOR_RELATIVE_DEPTH);
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
	vec4 ray = texelFetch(s_lumen_reflection_ray, trace_coord, 0);
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
	float resume_voxel;
	SdfSampleClipmapEx(position + screen * direction, resume_voxel);
	float t_start = max(screen - LUMEN_REFLECTION_SDF_PULLBACK_HALF_VOXELS * 0.5 * resume_voxel, 0.0);
	float step_noise = mix(LUMEN_REFLECTION_SDF_STEP_DITHER, 1.0 / LUMEN_REFLECTION_SDF_STEP_DITHER,
	                       InterleavedGradientNoise(vec2(pixel), u_lumen_frame_mod));
	float step_factor = mix(step_noise, 1.0, saturate(ray.w / LUMEN_REFLECTION_SDF_STEP_DITHER_CONE));
	LumenSdfHit hit = LumenTraceGlobalSdfStepped(origin, direction, t_start, u_lumen_max_trace_distance, false, 0.0, 0.0,
	                                             step_factor);
	vec3 radiance = vec3_splat(0.0);
	float hit_distance = u_lumen_max_trace_distance;
	BRANCH
	if(hit.hit)
	{
		hit_distance = hit.t;
		vec3 surface_normal = dot(hit.normal, direction) > 0.0 ? -hit.normal : hit.normal;
		BRANCH
		if(u_lumen_hit_lighting.x > 0.5)
		{
			// The march stops short of the surface by its expansion; the cards are a surface store.
			vec3 surface = origin + direction * (hit.t + hit.hit_field);
			vec4 cards = LumenSampleGlobalSdfHit(surface, surface_normal, 0.5 * hit.voxel, s_lumen_card_final);
			if(cards.w > 0.0)
			{
				radiance = GiCachedToView(cards.xyz / cards.w);
			}
		}
		vec4 scene_color = LumenReflectionSceneColorAtHit(pixel, origin + direction * hit.t, surface_normal);
		if(scene_color.w > 0.5)
		{
			radiance = scene_color.xyz;
		}
		if(u_lumen_reflection_show_trace_types)
		{
			radiance = vec3(scene_color.w > 0.5 ? 0.5 : 0.0, 0.5, 0.0) * u_pre_exposure_value;
		}
	}
	else
	{
		radiance = u_lumen_reflection_show_trace_types ? vec3(0.0, 0.0, 0.5) * u_pre_exposure_value
		                                               : LumenReflectionSkyRadiance(pixel, direction);
	}
	float max_channel = max(max(radiance.x, radiance.y), radiance.z);
	if(max_channel > LUMEN_REFLECTION_MAX_RAY_INTENSITY)
	{
		radiance *= LUMEN_REFLECTION_MAX_RAY_INTENSITY / max_channel;
	}
	imageStore(s_lumen_reflection_radiance_out, trace_coord, vec4(radiance, 0.0));
	imageStore(s_lumen_reflection_hit_rw, trace_coord, vec4(LumenEncodeRayDistance(hit_distance, hit.hit), 0.0, 0.0, 0.0));
}
