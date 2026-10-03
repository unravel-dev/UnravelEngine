#ifndef __LUMEN_SCREEN_TRACE_SH__
#define __LUMEN_SCREEN_TRACE_SH__

/*
 * Lumen's hierarchical screen trace (UE 5.8 HZBTracing.ush TraceHZB, as the screen probe gather
 * runs it: LumenScreenProbeTracing.usf:54-366), ported to this engine's Hi-Z: standard (not
 * reversed) device depth, CLOSEST depth per texel (min reduction), mip 0 at full resolution - which
 * is UE's "full-resolution depth as mip -1", so UE's mip m is this pyramid's mip m + 1.
 *
 * The trace runs in screen uv (xy) and device depth (z). A ray steps out of its start texel without a
 * test, then descends while it is in front of a texel's closest depth and climbs while it skips whole
 * texels. Reaching below full resolution ends it: a crossing within relative_thickness of the
 * depth behind the depth buffer is a hit; otherwise the ray rewinds to the last point it proved free.
 *
 * The helpers below it serve both screen tracers (the probe gather's and the reflections'): the ray origin
 * lifted off its own depth texel, the screen-edge vignette, and the radiance of a hit from last frame's
 * scene colour behind its reprojection and history depth test (UE LumenScreenTracing.ush,
 * LumenReflectionTracing.usf:166-217).
 *
 * The includer declares the samplers and passes them in.
 */

#include "lumen/lumen_common.sh"
#include "../pre_exposure.sh"

struct LumenScreenTraceResult
{
	bool hit;
	/// Screen uv and device depth of the hit, or of the last point proved free.
	vec3 position;
	/// Screen uv and device depth of the last point proved free (UE LastVisibleHitUVz): where a caller
	/// that rejects the hit resumes.
	vec3 last_visible;
	/// True when the trace reached its end or left the screen without a crossing.
	bool reached_end;
	/// True when the iteration budget ran out short of an answer: neither a hit nor a free segment.
	bool uncertain;
	/// Device depth of the depth buffer where the ray crossed it (UE HitTileZ); valid for a hit.
	float surface_z;
};

/// Screen uv (xy) and device depth (z) of a world point under the pass's view-projection.
vec3 LumenProjectToScreen(vec3 world)
{
	vec4 clip = mul(u_viewProj, vec4(world, 1.0));
	vec3 ndc = clip.xyz / clip.w;
	return vec3(clipToUv(ndc.xy * 0.5 + 0.5), toDepthTextureZ(ndc.z));
}

/// Parameter t in (0, 1] where S + t R leaves the [0, 1]^2 screen square (1 when it stays inside).
float LumenScreenExit(vec2 start, vec2 delta)
{
	float t = 1.0;
	if(delta.x > 0.0)
	{
		t = min(t, (1.0 - start.x) / delta.x);
	}
	else if(delta.x < 0.0)
	{
		t = min(t, -start.x / delta.x);
	}
	if(delta.y > 0.0)
	{
		t = min(t, (1.0 - start.y) / delta.y);
	}
	else if(delta.y < 0.0)
	{
		t = min(t, -start.y / delta.y);
	}
	return max(t, 0.0);
}

/// The texel-boundary plane ahead of @p p at @p mip along the ray's sign, nudged 0.005 texel past it.
vec2 LumenTexelPlane(vec2 p, vec2 texel_size, vec2 floor_offset, vec2 nudge)
{
	return (floor(p / texel_size) + floor_offset) * texel_size + nudge * texel_size;
}

LumenScreenTraceResult LumenTraceHZB(sampler2D hiz,
                                     int hiz_mip_count,
                                     vec3 start,
                                     vec3 end,
                                     int max_iterations,
                                     float relative_thickness)
{
	LumenScreenTraceResult result;
	result.hit = false;
	result.reached_end = true;
	result.uncertain = false;
	result.position = start;
	result.last_visible = start;
	result.surface_z = start.z;
	vec3 delta = end - start;
	float exit_t = LumenScreenExit(start.xy, delta.xy);
	delta *= exit_t;
	if(dot(delta.xy, delta.xy) < 1e-12)
	{
		return result;
	}
	vec2 floor_offset = vec2(delta.x < 0.0 ? 0.0 : 1.0, delta.y < 0.0 ? 0.0 : 1.0);
	vec2 nudge = vec2(delta.x < 0.0 ? -0.005 : 0.005, delta.y < 0.0 ? -0.005 : 0.005);
	vec2 inv_delta = vec2(delta.x != 0.0 ? 1.0 / delta.x : 1e30, delta.y != 0.0 ? 1.0 / delta.y : 1e30);
	int mip = 0;
	// Step out of the start texel with no test: the ray's own surface is never its hit.
	vec2 texel0 = vec2_splat(1.0) / vec2(textureSize(hiz, 0));
	vec2 plane0 = LumenTexelPlane(start.xy, texel0, floor_offset, nudge);
	vec2 t_xy0 = (plane0 - start.xy) * inv_delta;
	float t = min(t_xy0.x, t_xy0.y);
	// A segment inside its start texel never leaves it: nothing to trace.
	vec3 p = t >= 1.0 ? start : start + t * delta;
	float t_above = 0.0;
	LOOP
	for(int iteration = 0; iteration < max_iterations; ++iteration)
	{
		if(mip < 0 || t >= 1.0)
		{
			break;
		}
		ivec2 mip_size = textureSize(hiz, mip);
		vec2 texel_size = vec2_splat(1.0) / vec2(mip_size);
		vec2 plane = LumenTexelPlane(p.xy, texel_size, floor_offset, nudge);
		ivec2 texel = clamp(ivec2(p.xy * vec2(mip_size)), ivec2(0, 0), mip_size - ivec2(1, 1));
		float tile_z = texelFetch(hiz, texel, mip).x;
		vec2 t_xy = (plane - start.xy) * inv_delta;
		float t_z = delta.z > 0.0 ? (tile_z - start.z) / delta.z : 1.0;
		float t_intersect = min(min(t_xy.x, t_xy.y), t_z);
		bool above = p.z < tile_z;
		bool skipped = above && t_intersect != t_z;
		if(skipped)
		{
			t_above = t_intersect;
		}
		if(above)
		{
			t = t_intersect;
		}
		p = start + min(t, 1.0) * delta;
		mip += skipped ? 1 : -1;
		mip = min(mip, hiz_mip_count - 1);
	}
	// Running out of iterations short of the end leaves the trace uncertain: no hit, not the end either.
	result.reached_end = t >= 1.0;
	result.uncertain = mip >= 0 && t < 1.0;
	if(mip < 0 && t < 1.0)
	{
		ivec2 size0 = textureSize(hiz, 0);
		ivec2 texel = clamp(ivec2(p.xy * vec2(size0)), ivec2(0, 0), size0 - ivec2(1, 1));
		result.surface_z = texelFetch(hiz, texel, 0).x;
		float surface = LumenLinearDepth(result.surface_z);
		float ray = LumenLinearDepth(p.z);
		result.hit = (ray - surface) < relative_thickness * max(surface, 1e-5);
		if(!result.hit)
		{
			p = start + t_above * delta;
		}
	}
	result.position = p;
	result.last_visible = start + t_above * delta;
	return result;
}

/// The world point and how far the ray may run: @p origin projected, with the ray's end at most
/// @p max_distance away and short of the camera plane; valid false when the origin projects off screen (UE
/// skips the screen trace then).
struct LumenScreenRaySegment
{
	bool valid;
	vec3 start;
	vec3 end;
};

LumenScreenRaySegment LumenScreenSegment(vec3 origin, vec3 direction, float max_distance)
{
	LumenScreenRaySegment segment;
	segment.start = LumenProjectToScreen(origin);
	segment.end = segment.start;
	segment.valid = all(greaterThanEqual(segment.start, vec3_splat(0.0))) &&
	                all(lessThanEqual(segment.start, vec3_splat(1.0)));
	if(!segment.valid)
	{
		return segment;
	}
	float trace_length = max_distance;
	float view_origin_z = mul(u_view, vec4(origin, 1.0)).z;
	float view_direction_z = mul(u_view, vec4(direction, 0.0)).z;
	if(view_direction_z < 0.0)
	{
		trace_length = min(-LUMEN_SCREEN_TRACE_NEAR_STOP * view_origin_z / view_direction_z, trace_length);
	}
	segment.end = LumenProjectToScreen(origin + direction * trace_length);
	return segment;
}

/// Screen-trace origin: @p position lifted along @p normal by LUMEN_SCREEN_TRACE_BIAS_TEXELS times the world
/// size of half a pixel at its depth, out of its own depth texel's reach (UE ApplyScreenSpaceRayBias).
vec3 LumenScreenTraceOrigin(vec3 position, vec3 normal, vec2 uv, float depth01)
{
	vec3 corner = LumenWorldFromDepth(uv + 0.5 * u_lumen_view_texel, depth01);
	return position + LUMEN_SCREEN_TRACE_BIAS_TEXELS * abs(dot(corner - position, normal)) * normal;
}

/// Lumen's screen-edge vignette for one screen uv: 1 inside, falling to 0 over the outer tenth of each
/// screen half (UE ComputeHitVignetteFromScreenPos).
float LumenScreenVignette(vec2 uv)
{
	vec2 v = saturate(LUMEN_SCREEN_TRACE_VIGNETTE_SCALE * abs(uv * 2.0 - 1.0) - LUMEN_SCREEN_TRACE_VIGNETTE_OFFSET);
	return saturate(1.0 - dot(v, v));
}

/// Last frame's scene colour at a world point seen at screen @p uv this frame (rgb, in this frame's
/// pre-exposure; a = 1), or a = 0 when it cannot be trusted there: its reprojection is off screen, inside the
/// stochastic vignette band (of this frame or the last, against @p noise), or last frame's device depth at it
/// differs from the point's by @p depth_tolerance x lerp(0.5, 2, noise) or more (UE
/// LumenReflectionTracing.usf:166-217, LumenScreenTracing.ush:78-137).
vec4 LumenScreenHistoryRadiance(sampler2D prev_color,
                                sampler2D prev_depth,
                                mat4 prev_view_proj,
                                vec3 world,
                                vec2 uv,
                                float noise,
                                float depth_tolerance)
{
	vec4 prev_clip = mul(prev_view_proj, vec4(world, 1.0));
	if(prev_clip.w <= 0.0)
	{
		return vec4_splat(0.0);
	}
	vec3 prev_ndc = prev_clip.xyz / prev_clip.w;
	vec2 history_uv = clipToUv(prev_ndc.xy * 0.5 + 0.5);
	if(any(lessThan(history_uv, vec2_splat(0.0))) || any(greaterThan(history_uv, vec2_splat(1.0))))
	{
		return vec4_splat(0.0);
	}
	if(min(LumenScreenVignette(uv), LumenScreenVignette(history_uv)) < noise)
	{
		return vec4_splat(0.0);
	}
	ivec2 depth_size = textureSize(prev_depth, 0);
	ivec2 depth_texel = min(ivec2(history_uv * vec2(depth_size)), depth_size - ivec2(1, 1));
	float history_depth = texelFetch(prev_depth, depth_texel, 0).x;
	float tolerance = depth_tolerance *
	                  mix(LUMEN_SCREEN_TRACE_HISTORY_NOISE_MIN, LUMEN_SCREEN_TRACE_HISTORY_NOISE_MAX, noise);
	if(abs(history_depth - toDepthTextureZ(prev_ndc.z)) >= tolerance)
	{
		return vec4_splat(0.0);
	}
	ivec2 color_size = textureSize(prev_color, 0);
	ivec2 color_texel = min(ivec2(history_uv * vec2(color_size)), color_size - ivec2(1, 1));
	return vec4(texelFetch(prev_color, color_texel, 0).xyz * u_history_pre_exposure_correction, 1.0);
}

#endif // __LUMEN_SCREEN_TRACE_SH__
