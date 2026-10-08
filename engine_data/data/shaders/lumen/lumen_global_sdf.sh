#ifndef __LUMEN_GLOBAL_SDF_SH__
#define __LUMEN_GLOBAL_SDF_SH__

/*
 * Lumen's global distance field march (UE 5.8 GlobalDistanceFieldUtils.ush RayTraceGlobalDistanceField,
 * as the screen probe gather runs it: LumenScreenProbeTracing.usf:712-891) over this engine's clipmap
 * (gi/sdf_clipmap.sh, stage 4 and its uniforms).
 *
 * The surface is expanded by up to LUMEN_GLOBAL_SDF_EXPAND_VOXELS of a voxel, ramping in over
 * LUMEN_GLOBAL_SDF_EXPAND_RAMP_VOXELS of either the ray's travel from its biased start (diffuse rays: errs
 * toward occlusion) or the largest distance the ray has kept from any surface (the radiance cache's probe
 * rays: no expansion until the ray has left the surfaces around its origin). A sample under the expansion
 * is a hit. The hit is pulled back by the expansion and hit_field carries the distance from there to the surface
 * estimate, so a reader of a surface store can step onto the surface.
 *
 * Each level has a budget of LUMEN_GLOBAL_SDF_MAX_STEPS: a ray that spends it in one level continues from where it
 * leaves that level (UE's per-clipmap loop, GlobalDistanceFieldUtils.ush:104-191); past the last level it is a miss.
 * Like UE's loop, which fixes the clipmap per iteration, the march keeps the stretch of the ray one level answers
 * unblended (LumenGlobalSdfLevelSpanEnd) and samples that level there without searching for it; the samples are the
 * searched ones exactly, the search and the cross-fade run only at level edges.
 * Where the answering level reads saturated, the ray steps by that level's coarse mip (SDF_CLIPMAP_MIP_STAGE, UE
 * GlobalDistanceFieldMipTexture), which holds the level's own objects, so a step never passes one the level shows;
 * an includer without the mip steps by the level alone.
 *
 * Coverage (UE GLOBALSDF_USE_COVERAGE_BASED_EXPAND, GlobalDistanceFieldUtils.ush:141-190): where only two-sided meshes are
 * near (coverage 0, gi/sdf_clipmap.sh), the expansion shrinks to LUMEN_GLOBAL_SDF_NOT_COVERED_EXPAND_SCALE, the min step
 * grows by LUMEN_GLOBAL_SDF_NOT_COVERED_MIN_STEP_SCALE, and a dithered trace (the screen probes', the radiance cache's)
 * hits there only when its per-step and per-trace noise pass LUMEN_GLOBAL_SDF_DITHER_STEP_THRESHOLD and
 * LUMEN_GLOBAL_SDF_DITHER_TRACE_THRESHOLD: foliage and curtains let part of the light through. The includer defines
 * SDF_CLIPMAP_COVERAGE_STAGE to bind the coverage; without it everything is covered.
 *
 * Needs only the clipmap: stages 0-3 and 12 stay free for the includer.
 */

#include "gi/sdf_clipmap.sh"
#include "lumen/lumen_constants.sh"
#include "sampling.sh"

/// The distance below which a sample's coverage can change the march: the largest min step it may take.
#define LUMEN_GLOBAL_SDF_COVERAGE_REACH_VOXELS (LUMEN_GLOBAL_SDF_MIN_STEP_VOXELS * LUMEN_GLOBAL_SDF_NOT_COVERED_MIN_STEP_SCALE)

/// The margin, in voxels of each box's level, a level span keeps from the box edges it is cut at: far above the
/// rounding of a march position, so every sample inside the span reads the level search's answer.
#define LUMEN_GLOBAL_SDF_SPAN_MARGIN_VOXELS 0.05

/// A trace's dithered transparency in uncovered space (UE bDitheredTransparency): its noise coordinate (UE
/// DitherScreenCoord) and frame % 8, or disabled.
struct LumenSdfDither
{
	vec2 coord;
	float frame_mod;
	bool enabled;
};

LumenSdfDither LumenSdfNoDither()
{
	LumenSdfDither dither;
	dither.coord = vec2_splat(0.0);
	dither.frame_mod = 0.0;
	dither.enabled = false;
	return dither;
}

LumenSdfDither LumenSdfMakeDither(vec2 coord, float frame_mod)
{
	LumenSdfDither dither;
	dither.coord = coord;
	dither.frame_mod = frame_mod;
	dither.enabled = true;
	return dither;
}

/// The coverage at a march sample @p s taken at @p p (1 without the coverage binding).
float LumenGlobalSdfCoverage(SdfClipmapSample s, vec3 p)
{
#ifdef SDF_CLIPMAP_COVERAGE_STAGE
	return SdfSampleClipmapCoverageAt(s, p);
#else
	return 1.0;
#endif
}

struct LumenSdfHit
{
	bool hit;
	/// Ray parameter of the hit (pulled back by the expansion), or the end of the march.
	float t;
	/// Distance from the pulled-back hit to the surface estimate along the ray.
	float hit_field;
	/// Field gradient at the hit (unit length); left at +y under LUMEN_GLOBAL_SDF_DEFER_HIT_NORMAL, whose includer
	/// computes it (LumenGlobalSdfNormal at origin + direction t) where it shades the hit.
	vec3 normal;
	/// Voxel size of the clipmap level that answered the hit.
	float voxel;
};

/// The one level every sample within @p reach of @p p reads, unblended (UE samples a hit's gradient in the hit's
/// clipmap alone, GlobalDistanceFieldShared.ush:291-314), or SDF_CLIPMAP_LEVEL_COUNT when a sample there could read
/// another level or a cross-fade: @p p's finest level holds it deeper than its cross-fade band plus @p reach, and
/// every finer level's box lies more than @p reach away along some axis. SdfSampleClipmap at those samples is then
/// SdfSampleClipmapLevel of this level, exactly.
int LumenGlobalSdfSingleLevel(vec3 p, float reach)
{
	BRANCH
	if((u_sdf_clipmap_experiments & 4) != 0)
	{
		return SDF_CLIPMAP_LEVEL_COUNT;
	}
	SdfClipmapLevelHit found = SdfFindClipmapLevel(p);
	int index = found.index;
	if(index >= SDF_CLIPMAP_LEVEL_COUNT)
	{
		return SDF_CLIPMAP_LEVEL_COUNT;
	}
	float resolution = u_sdf_clipmap_resolution;
	vec4 level = u_sdf_clipmap_levels[index];
	vec3 grid = (p - level.xyz) / level.w;
	vec3 nearest_face = min(grid - vec3_splat(0.5), vec3_splat(resolution - 0.5) - grid);
	float edge_voxels = min(nearest_face.x, min(nearest_face.y, nearest_face.z)) - reach / level.w;
	bool has_next = index + 1 < SDF_CLIPMAP_LEVEL_COUNT;
	if(has_next)
	{
		has_next = u_sdf_clipmap_levels[index + 1].w > 0.0 && u_sdf_clipmap_blend_voxels > 0.0;
	}
	bool is_single = edge_voxels >= (has_next ? u_sdf_clipmap_blend_voxels : 0.0);
	for(int finer = 0; finer < SDF_CLIPMAP_LEVEL_COUNT; ++finer)
	{
		vec4 finer_level = u_sdf_clipmap_levels[finer];
		if(finer < index && finer_level.w > 0.0)
		{
			vec3 low = finer_level.xyz + vec3_splat(0.5 * finer_level.w);
			vec3 high = finer_level.xyz + vec3_splat((resolution - 0.5) * finer_level.w);
			vec3 outside = max(low - p, p - high);
			is_single = is_single && max(outside.x, max(outside.y, outside.z)) > reach;
		}
	}
	return is_single ? index : SDF_CLIPMAP_LEVEL_COUNT;
}

/// The unit field gradient at @p p (UE ComputeGlobalDistanceFieldNormal / GlobalDistanceFieldPageCentralDiff,
/// GlobalDistanceFieldShared.ush:291-312): central differences half a voxel of @p voxel either side along each axis,
/// @p fallback where the field is flat. A wider stencil blends a floor's and a wall's gradients a voxel or more from
/// their corner, and the tilted normal picks and weighs the cards a hit is shaded from.
vec3 LumenGlobalSdfNormal(vec3 p, float voxel, vec3 fallback)
{
	float h = 0.5 * voxel;
	vec3 n;
	int level = LumenGlobalSdfSingleLevel(p, h);
	BRANCH
	if(level < SDF_CLIPMAP_LEVEL_COUNT)
	{
		n = vec3(SdfSampleClipmapLevel(level, p + vec3(h, 0.0, 0.0)) - SdfSampleClipmapLevel(level, p - vec3(h, 0.0, 0.0)),
		         SdfSampleClipmapLevel(level, p + vec3(0.0, h, 0.0)) - SdfSampleClipmapLevel(level, p - vec3(0.0, h, 0.0)),
		         SdfSampleClipmapLevel(level, p + vec3(0.0, 0.0, h)) - SdfSampleClipmapLevel(level, p - vec3(0.0, 0.0, h)));
	}
	else
	{
		n = vec3(SdfSampleClipmap(p + vec3(h, 0.0, 0.0)) - SdfSampleClipmap(p - vec3(h, 0.0, 0.0)),
		         SdfSampleClipmap(p + vec3(0.0, h, 0.0)) - SdfSampleClipmap(p - vec3(0.0, h, 0.0)),
		         SdfSampleClipmap(p + vec3(0.0, 0.0, h)) - SdfSampleClipmap(p - vec3(0.0, 0.0, h)));
	}
	float len = length(n);
	return len > LUMEN_GLOBAL_SDF_FLAT_GRADIENT ? n / len : fallback;
}

/// The ray parameters at which a ray from @p origin along a direction with reciprocal @p inverse_direction enters (x)
/// and leaves (y) the box [@p low, @p high]; x > y where it misses the box.
vec2 LumenGlobalSdfBoxInterval(vec3 low, vec3 high, vec3 origin, vec3 inverse_direction)
{
	vec3 to_low = (low - origin) * inverse_direction;
	vec3 to_high = (high - origin) * inverse_direction;
	vec3 near_side = min(to_low, to_high);
	vec3 far_side = max(to_low, to_high);
	return vec2(max(near_side.x, max(near_side.y, near_side.z)), min(far_side.x, min(far_side.y, far_side.z)));
}

/// How far from @p t the ray keeps sampling level @p index alone, unblended: up to where it leaves the level's box less
/// its cross-fade band, or enters a finer level's box, each by LUMEN_GLOBAL_SDF_SPAN_MARGIN_VOXELS. Within that span
/// SdfSampleClipmapLevels answers SdfSampleClipmapLevelCovered(index) with no blend. -1 when @p t is not in such a span.
float LumenGlobalSdfLevelSpanEnd(int index, vec3 origin, vec3 inverse_direction, float t)
{
	float resolution = u_sdf_clipmap_resolution;
	vec4 level = u_sdf_clipmap_levels[index];
	bool has_next = index + 1 < SDF_CLIPMAP_LEVEL_COUNT;
	if(has_next)
	{
		has_next = u_sdf_clipmap_levels[index + 1].w > 0.0 && u_sdf_clipmap_blend_voxels > 0.0;
	}
	float inset = 0.5 + (has_next ? u_sdf_clipmap_blend_voxels : 0.0) + LUMEN_GLOBAL_SDF_SPAN_MARGIN_VOXELS;
	vec2 span = LumenGlobalSdfBoxInterval(level.xyz + vec3_splat(inset * level.w),
	                                      level.xyz + vec3_splat((resolution - inset) * level.w), origin,
	                                      inverse_direction);
	float outset = 0.5 - LUMEN_GLOBAL_SDF_SPAN_MARGIN_VOXELS;
	for(int finer = 0; finer < SDF_CLIPMAP_LEVEL_COUNT; ++finer)
	{
		vec4 finer_level = u_sdf_clipmap_levels[finer];
		if(finer < index && finer_level.w > 0.0)
		{
			vec2 finer_span = LumenGlobalSdfBoxInterval(finer_level.xyz + vec3_splat(outset * finer_level.w),
			                                            finer_level.xyz + vec3_splat((resolution - outset) * finer_level.w),
			                                            origin, inverse_direction);
			if(finer_span.x <= finer_span.y && finer_span.y >= t)
			{
				span.y = min(span.y, finer_span.x);
			}
		}
	}
	return t >= span.x && t <= span.y ? span.y : -1.0;
}

/// The march sample at @p p: level @p span_level's alone while the ray parameter @p t is inside its span (up to
/// @p span_end), else the searched, cross-faded one.
SdfClipmapSample LumenGlobalSdfMarchSample(vec3 p, float t, int span_level, float span_end)
{
	SdfClipmapSample field;
	BRANCH
	if(t <= span_end)
	{
		field.distance = SdfSampleClipmapLevelCovered(span_level, p);
		field.voxel_size = u_sdf_clipmap_levels[span_level].w;
		field.index = span_level;
		field.blend = 0.0;
	}
	else
	{
		field = SdfSampleClipmapLevels(p);
	}
	return field;
}

/// The coarsest covering level's distance (the empty-space step of the u_sdf_clipmap_experiments bit 1 A/B: that level
/// leaves out objects the finer ones hold, so the step can pass them).
float LumenGlobalSdfCoarseDistance(vec3 p, float fine_distance)
{
	float step_distance = fine_distance;
	for(int coarse_index = SDF_CLIPMAP_LEVEL_COUNT - 1; coarse_index >= 0; --coarse_index)
	{
		float coarse_distance = SdfSampleClipmapLevel(coarse_index, p);
		if(coarse_distance < SDF_CLIPMAP_OUTSIDE)
		{
			step_distance = max(step_distance, coarse_distance);
			break;
		}
	}
	return step_distance;
}

/// The distance a march sample at @p p may step when its level (@p field) reads saturated. Outside every level the
/// sample's own distance (SDF_CLIPMAP_OUTSIDE) already ends the ray, and there is no level mip to read.
float LumenGlobalSdfEmptySpaceStep(SdfClipmapSample field, vec3 p)
{
	BRANCH
	if(field.index >= SDF_CLIPMAP_LEVEL_COUNT)
	{
		return field.distance;
	}
	if((u_sdf_clipmap_experiments & 1) != 0)
	{
		return LumenGlobalSdfCoarseDistance(p, field.distance);
	}
#ifdef SDF_CLIPMAP_MIP_STAGE
	return max(field.distance, SdfSampleClipmapMip(field.index, p));
#else
	return field.distance;
#endif
}

/**
 * The march with UE's VoxelSizeRelativeBias and VoxelSizeRelativeRayEndBias, both in voxel extents (half
 * voxels) of the level answering each sample: the ray starts no earlier than @p voxel_relative_bias extents,
 * ends @p voxel_relative_end_bias extents short of @p t_max, and the ray-time expansion ramps in from the
 * biased start, so a ray leaving a surface at a grazing angle does not find that surface under a full
 * expansion at its first sample. Each step advances @p step_factor times the distance (UE SDFStepFactor), at
 * least LUMEN_GLOBAL_SDF_MIN_STEP_VOXELS.
 */
LumenSdfHit LumenTraceGlobalSdfDithered(vec3 origin,
                                        vec3 direction,
                                        float t_min,
                                        float t_max,
                                        bool expand_by_ray_time,
                                        float voxel_relative_bias,
                                        float voxel_relative_end_bias,
                                        float step_factor,
                                        LumenSdfDither dither)
{
	LumenSdfHit result;
	result.hit = false;
	result.t = t_max;
	result.hit_field = 0.0;
	result.normal = vec3(0.0, 1.0, 0.0);
	result.voxel = 0.0;
	if(!u_sdf_clipmap_enabled || t_min >= t_max)
	{
		return result;
	}
	float t = t_min;
	float max_distance = 0.0;
	float trace_noise = InterleavedGradientNoise(dither.coord, dither.frame_mod);
	bool has_ray_budget = (u_sdf_clipmap_experiments & 2) != 0;
	bool has_level_spans = (u_sdf_clipmap_experiments & 8) == 0;
	vec3 inverse_direction = vec3_splat(1.0) / (sign(direction) * max(abs(direction), vec3_splat(1e-8)) +
	                                            vec3(equal(direction, vec3_splat(0.0))) * 1e-8);
	int level = -1;
	int level_steps = 0;
	// The march only moves forward, so a span holds from the step it is found at to its end.
	int span_level = 0;
	float span_end = -1.0;
	LOOP
	for(int step = 0; step < LUMEN_GLOBAL_SDF_MAX_STEPS * SDF_CLIPMAP_LEVEL_COUNT; ++step)
	{
		if(t > t_max || (has_ray_budget && step >= LUMEN_GLOBAL_SDF_MAX_STEPS))
		{
			return result;
		}
		vec3 p = origin + direction * t;
		SdfClipmapSample field = LumenGlobalSdfMarchSample(p, t, span_level, span_end);
		BRANCH
		if(has_level_spans && t > span_end && field.index < SDF_CLIPMAP_LEVEL_COUNT && field.blend <= 0.0)
		{
			span_level = field.index;
			span_end = LumenGlobalSdfLevelSpanEnd(field.index, origin, inverse_direction, t);
		}
		if(field.index != level)
		{
			level = field.index;
			level_steps = 0;
		}
		float d = field.distance;
		float voxel = field.voxel_size;
		float ray_bias = voxel_relative_bias * 0.5 * voxel;
		if(t < ray_bias)
		{
			// The level's biased start; a coarser level there restarts at its own, larger bias.
			t = ray_bias;
			continue;
		}
		if(t > t_max - voxel_relative_end_bias * 0.5 * voxel)
		{
			return result;
		}
		max_distance = max(max_distance, d);
		float coverage = 1.0;
		BRANCH
		if(d * step_factor < LUMEN_GLOBAL_SDF_COVERAGE_REACH_VOXELS * voxel)
		{
			coverage = LumenGlobalSdfCoverage(field, p);
		}
		float expand_progress = expand_by_ray_time ? t - ray_bias : max_distance;
		float expand = LUMEN_GLOBAL_SDF_EXPAND_VOXELS * voxel *
		               saturate(expand_progress / max(LUMEN_GLOBAL_SDF_EXPAND_RAMP_VOXELS * voxel, 1e-4)) *
		               mix(LUMEN_GLOBAL_SDF_NOT_COVERED_EXPAND_SCALE, LUMEN_GLOBAL_SDF_COVERED_EXPAND_SCALE, coverage);
		bool solid = true;
		BRANCH
		if(dither.enabled && coverage < 1.0)
		{
			float step_noise =
			    InterleavedGradientNoise(dither.coord, dither.frame_mod * float(LUMEN_GLOBAL_SDF_MAX_STEPS) + float(level_steps));
			solid = step_noise * (1.0 - coverage) <= LUMEN_GLOBAL_SDF_DITHER_STEP_THRESHOLD &&
			        trace_noise * (1.0 - coverage) <= LUMEN_GLOBAL_SDF_DITHER_TRACE_THRESHOLD;
		}
		if(d < expand && solid)
		{
			result.hit = true;
			result.t = max(t + d - expand, 0.0);
			result.hit_field = max(t + d - result.t, 0.0);
#ifndef LUMEN_GLOBAL_SDF_DEFER_HIT_NORMAL
			result.normal = LumenGlobalSdfNormal(origin + direction * result.t, voxel, -direction);
#endif
			result.voxel = voxel;
			return result;
		}
		float step_distance = d;
		BRANCH
		if(d >= (u_sdf_clipmap_encode_range - 0.5) * voxel)
		{
			step_distance = LumenGlobalSdfEmptySpaceStep(field, p);
		}
		float min_step = LUMEN_GLOBAL_SDF_MIN_STEP_VOXELS * voxel *
		                 mix(LUMEN_GLOBAL_SDF_NOT_COVERED_MIN_STEP_SCALE, 1.0, coverage);
		t += max(step_distance * step_factor, min_step);
		++level_steps;
		if(level_steps >= LUMEN_GLOBAL_SDF_MAX_STEPS && level < SDF_CLIPMAP_LEVEL_COUNT)
		{
			// The level's budget is spent: continue where the ray leaves it, in the next level.
			t = max(t, SdfClipmapLevelExit(level, origin, inverse_direction) + LUMEN_GLOBAL_SDF_LEVEL_EXIT_VOXELS * voxel);
			level_steps = 0;
		}
	}
	return result;
}

LumenSdfHit LumenTraceGlobalSdfStepped(vec3 origin,
                                       vec3 direction,
                                       float t_min,
                                       float t_max,
                                       bool expand_by_ray_time,
                                       float voxel_relative_bias,
                                       float voxel_relative_end_bias,
                                       float step_factor)
{
	return LumenTraceGlobalSdfDithered(origin, direction, t_min, t_max, expand_by_ray_time, voxel_relative_bias,
	                                   voxel_relative_end_bias, step_factor, LumenSdfNoDither());
}

LumenSdfHit LumenTraceGlobalSdfBiased(vec3 origin,
                                      vec3 direction,
                                      float t_min,
                                      float t_max,
                                      bool expand_by_ray_time,
                                      float voxel_relative_bias,
                                      float voxel_relative_end_bias)
{
	return LumenTraceGlobalSdfStepped(origin, direction, t_min, t_max, expand_by_ray_time, voxel_relative_bias,
	                                  voxel_relative_end_bias, 1.0);
}

LumenSdfHit LumenTraceGlobalSdf(vec3 origin, vec3 direction, float t_min, float t_max, bool expand_by_ray_time)
{
	return LumenTraceGlobalSdfBiased(origin, direction, t_min, t_max, expand_by_ray_time, 0.0, 0.0);
}

#endif // __LUMEN_GLOBAL_SDF_SH__
