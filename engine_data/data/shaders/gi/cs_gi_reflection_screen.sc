/*
 * Reflections, ray generation and screen trace, fused per trace texel. Every pixel traces, except at the lowest
 * reflection quality or the high tier, where one pixel of each 2 x 2 block traces (GiReflectionTracePixel) and
 * the resolve reconstructs the others.
 *
 * A pixel traces when its traced weight (GiReflectionFadeAlpha) is positive: the mirror direction below
 * GI_REFLECTION_MIRROR_ROUGHNESS, otherwise one GGX visible-normal sample of spatiotemporal noise
 * (GiSpatioTemporalNoise2D) whose E.y is scaled by 1 - GI_REFLECTION_GGX_SAMPLING_BIAS; the
 * ray's cone angle is 1 / pdf of the half vector.
 *
 * The ray then walks the Hi-Z from the pixel lifted off its own depth texel. A hit reads last frame's scene
 * colour at its reprojection, clamped to GI_REFLECTION_MAX_RAY_INTENSITY, unless the stochastic screen
 * vignette or last frame's device depth rejects it; a rejected hit rewinds to the last point the trace proved
 * free, which the distance-field pass resumes from. A miss reports its distance plus
 * GI_SCREEN_TRACE_MISS_OFFSET.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "gi/gi_reflection_common.sh"
#include "gi/gi_screen_trace.sh"
#include "gi/gi_reflection_sampling.sh"

IMAGE2D_WO(s_gi_reflection_ray_out, rgba16f, 0);
IMAGE2D_WO(s_gi_reflection_radiance_out, rgba16f, 1);
IMAGE2D_WO(s_gi_reflection_hit_out, r32f, 2);
/// The reflection tiles (gi_reflection_common.sh).
IMAGE2D_WO(s_gi_reflection_tiles_out, r8, 3);
SAMPLER2D(s_gi_depth, 8);
/// G-buffer target 1: octahedral normal, metalness, roughness.
SAMPLER2D(s_gi_normal, 9);
SAMPLER2D(s_gi_hiz, 10);
/// Last frame's scene colour, in last frame's pre-exposed space.
SAMPLER2D(s_gi_prev_color, 11);
/// Last frame's device depth.
SAMPLER2D(s_gi_prev_depth, 12);

/// This frame's velocity buffer (where moving hit surfaces were last frame).
#define GI_VELOCITY_STAGE 13
#include "gi/gi_motion.sh"

/// The ray of a pixel: xyz = direction, w = cone angle (1 / pdf).
vec4 GiReflectionRay(ivec2 pixel, vec3 position, vec3 normal, float roughness)
{
	vec3 view = normalize(GiReflectionCamera() - position);
	if(roughness < GI_REFLECTION_MIRROR_ROUGHNESS)
	{
		return vec4(reflect(-view, normal), GI_REFLECTION_MIN_CONE_ANGLE);
	}
	vec2 e = GiSpatioTemporalNoise2D(vec2(pixel));
	e.y *= 1.0 - GI_REFLECTION_GGX_SAMPLING_BIAS;
	float alpha = roughness * roughness;
	GiReflectionBasis basis = GiReflectionMakeBasis(normal);
	// A normal-mapped pixel can face away from the camera; the lobe is sampled for the grazing view instead.
	vec3 view_ts = GiReflectionToTangent(basis, view);
	view_ts.z = max(view_ts.z, 1e-4);
	view_ts = normalize(view_ts);
	vec3 half_ts = GiReflectionSampleGgxVndf(view_ts, alpha, e.x, e.y);
	float pdf = GiReflectionVndfPdf(view_ts, half_ts, alpha);
	vec3 half_vector = GiReflectionToWorld(basis, half_ts);
	vec3 sampled_view = GiReflectionToWorld(basis, view_ts);
	vec3 direction = normalize(2.0 * dot(sampled_view, half_vector) * half_vector - sampled_view);
	float cone = max(1.0 / max(pdf, GI_REFLECTION_MIN_PDF), GI_REFLECTION_MIN_CONE_ANGLE);
	return vec4(direction, cone);
}

/// @p color scaled so its largest channel is at most GI_REFLECTION_MAX_RAY_INTENSITY.
vec3 GiClampRayIntensity(vec3 color)
{
	float max_channel = max(max(color.x, color.y), color.z);
	return max_channel > GI_REFLECTION_MAX_RAY_INTENSITY ? color * (GI_REFLECTION_MAX_RAY_INTENSITY / max_channel)
	                                                         : color;
}

/// Whether any trace texel of the group traces.
SHARED uint s_gi_group_traces;

/// Marks the reflection tiles group @p group covers from its thread @p index, as @p group_traces decides: one tile at
/// full resolution, the span x span tiles of its pixels at the trace downsample.
void GiWriteReflectionTiles(ivec2 group, int index, bool group_traces)
{
	int span = u_gi_reflection_downsample;
	ivec2 tile = group * span + ivec2(index - (index / span) * span, index / span);
	int round_up = GI_REFLECTION_TILE_PIXELS - 1;
	ivec2 tile_count = (ivec2(u_gi_view_size) + ivec2(round_up, round_up)) / GI_REFLECTION_TILE_PIXELS;
	if(index < span * span && all(lessThan(tile, tile_count)))
	{
		imageStore(s_gi_reflection_tiles_out, tile, vec4(group_traces ? 1.0 : 0.0, 0.0, 0.0, 0.0));
	}
}

/// Whether a pixel of trace texel @p trace_coord's block wants traced reflections (inside the view, smooth enough, not
/// the sky).
bool GiReflectionBlockResolves(ivec2 trace_coord)
{
	int span = u_gi_reflection_downsample;
	ivec2 view_size = ivec2(u_gi_view_size);
	bool resolves = false;
	for(int i = 0; i < span * span; ++i)
	{
		ivec2 p = trace_coord * span + ivec2(i - (i / span) * span, i / span);
		if(all(lessThan(p, view_size)) && GiReflectionFadeAlpha(texelFetch(s_gi_normal, p, 0).w) > 0.0)
		{
			resolves = resolves || texelFetch(s_gi_depth, p, 0).x < 1.0;
		}
	}
	return resolves;
}

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 trace_coord = ivec2(gl_GlobalInvocationID.xy);
	bool is_inside = all(lessThan(trace_coord, GiReflectionTraceSize()));
	ivec2 pixel = GiReflectionTracePixel(min(trace_coord, GiReflectionTraceSize() - ivec2(1, 1)));
	vec4 gbuffer1 = texelFetch(s_gi_normal, pixel, 0);
	float roughness = gbuffer1.w;
	bool traces = is_inside && GiReflectionFadeAlpha(roughness) > 0.0;
	// Most pixels are too rough to trace: only the others read their depth.
	float depth01 = 1.0;
	BRANCH
	if(traces)
	{
		depth01 = texelFetch(s_gi_depth, pixel, 0).x;
		traces = depth01 < 1.0;
	}
	// At the downsample a tile resolves wherever a pixel of a traced block wants reflections, whether or not the
	// block's traced pixel does: the resolve serves it from the traces around.
	bool resolves = traces;
	BRANCH
	if(u_gi_reflection_downsample > 1 && is_inside && !traces)
	{
		resolves = GiReflectionBlockResolves(trace_coord);
	}
	// The tile classification: barriers in uniform control flow, then a group that traces nothing writes nothing.
	if(gl_LocalInvocationID.x == 0u && gl_LocalInvocationID.y == 0u)
	{
		s_gi_group_traces = u_gi_reflection_all_tiles ? 1u : 0u;
	}
	barrier();
	if(resolves)
	{
		atomicOr(s_gi_group_traces, 1u);
	}
	barrier();
	bool group_traces = s_gi_group_traces != 0u;
	GiWriteReflectionTiles(ivec2(gl_WorkGroupID.xy),
	                          int(gl_LocalInvocationID.y) * 8 + int(gl_LocalInvocationID.x),
	                          group_traces);
	if(!is_inside || !group_traces)
	{
		return;
	}
	if(!traces)
	{
		imageStore(s_gi_reflection_ray_out, trace_coord, vec4_splat(0.0));
		imageStore(s_gi_reflection_radiance_out, trace_coord, vec4_splat(0.0));
		imageStore(s_gi_reflection_hit_out, trace_coord, vec4_splat(0.0));
		return;
	}
	vec2 uv = GiPixelUv(pixel);
	vec3 position = GiWorldFromDepth(uv, depth01);
	vec3 normal = normalize(decodeNormalOctahedron(gbuffer1.xy));
	vec4 ray = GiReflectionRay(pixel, position, normal, roughness);
	imageStore(s_gi_reflection_ray_out, trace_coord, ray);
	vec3 radiance = vec3_splat(0.0);
	float hit_distance = 0.0;
	bool hit = false;
	BRANCH
	if(u_gi_reflection_screen_traces)
	{
		vec3 origin = GiScreenTraceOrigin(position, normal, uv, depth01);
		GiScreenRaySegment segment = GiScreenSegment(origin, ray.xyz, u_gi_max_trace_distance);
		if(segment.valid)
		{
			GiScreenTraceResult trace = GiTraceHZB(s_gi_hiz, u_gi_reflection_hiz_mip_count, segment.start,
			                                             segment.end, GI_SCREEN_TRACE_MAX_ITERATIONS,
			                                             GI_REFLECTION_SCREEN_TRACE_RELATIVE_THICKNESS);
			hit = trace.hit && !trace.uncertain;
			vec3 end_point = trace.position;
			BRANCH
			if(hit)
			{
				float noise = InterleavedGradientNoise(vec2(pixel) + 0.5, u_gi_frame_mod);
				vec3 surface = GiWorldFromDepth(trace.position.xy, trace.surface_z);
				vec4 lit = GiScreenHistoryRadiance(s_gi_prev_color, s_gi_prev_depth, u_gi_prev_view_proj,
				                                      GiPrevWorldPosition(trace.position.xy, surface),
				                                      trace.position.xy, noise, GI_REFLECTION_HISTORY_DEPTH_TEST);
				hit = lit.w > 0.5;
				radiance = GiClampRayIntensity(lit.xyz);
				end_point = hit ? trace.position : trace.last_visible;
			}
			float miss_offset = (!hit && !trace.uncertain) ? GI_SCREEN_TRACE_MISS_OFFSET : 0.0;
			vec3 end_world = GiWorldFromDepth(end_point.xy, end_point.z);
			hit_distance = min(length(end_world - position) + miss_offset, u_gi_max_trace_distance);
		}
	}
	if(hit && u_gi_reflection_show_trace_types)
	{
		radiance = vec3(0.5, 0.0, 0.0) * u_pre_exposure_value;
	}
	imageStore(s_gi_reflection_radiance_out, trace_coord, vec4(hit ? radiance : vec3_splat(0.0), 0.0));
	imageStore(s_gi_reflection_hit_out, trace_coord, vec4(GiEncodeRayDistance(hit_distance, hit), 0.0, 0.0, 0.0));
}
