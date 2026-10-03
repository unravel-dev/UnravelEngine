#ifndef __GI_SDF_CLIPMAP_SH__
#define __GI_SDF_CLIPMAP_SH__

/*
 * The global SDF clipmap: the cascaded distance field every tracer and every cage reader
 * samples. Its sampler is stage 4 and its uniforms are filled by global_sdf_clipmap_gpu, the
 * single owner. sdf_common.sh includes this for the tracers; a consumer that only needs the
 * field's verdict (the world probes' cage-visibility march) includes this alone and keeps
 * stages 0-3 for its own resources.
 */

#include "../bgfx_compute.sh"

/// Cascades in the global clipmap. Mirror of global_sdf_clipmap::level_count.
#define SDF_CLIPMAP_LEVEL_COUNT 4

/// All cascades in one volume, stacked along Z: level i occupies
/// [i * resolution, (i + 1) * resolution). See global_sdf_clipmap_gpu.
SAMPLER3D(s_sdf_clipmap, 4);

/// Voxels per coverage texel along each axis. Mirror of global_sdf_clipmap_gpu::coverage_downsample.
#define SDF_CLIPMAP_COVERAGE_DOWNSAMPLE 2

#ifdef SDF_CLIPMAP_COVERAGE_STAGE
/// The Lumen coverage of the distance-only clipmap (global_sdf_clipmap_gpu::get_coverage_texture), stacked like the
/// distance: 0 where only two-sided meshes lie within LUMEN_GLOBAL_SDF_COVERAGE_BAND_VOXELS of a voxel, 1 elsewhere
/// (UE 5.8 GlobalDistanceFieldCompositeObjects.usf:61-72, 232-238). Texel c holds voxel 2c of its level.
SAMPLER3D(s_sdf_clipmap_coverage, SDF_CLIPMAP_COVERAGE_STAGE);
#endif

/// Per cascade: xyz = world-space origin, w = voxel size. Zero w means the level is absent.
uniform vec4 u_sdf_clipmap_levels[SDF_CLIPMAP_LEVEL_COUNT];
/// x = voxels per axis in a level, y = cross-fade band width in voxels,
/// z = encode range in voxels, w = 1 when the clipmap is usable.
///
/// Filled by global_sdf_clipmap_gpu::get_sampling_params, which is the single owner: three
/// passes sample this cascade and any disagreement between them makes their resolved surface
/// points differ, which shows up as a radiance cache that never hits rather than as an error.
uniform vec4 u_sdf_clipmap_params;
#define u_sdf_clipmap_resolution   u_sdf_clipmap_params.x
#define u_sdf_clipmap_blend_voxels u_sdf_clipmap_params.y
#define u_sdf_clipmap_encode_range u_sdf_clipmap_params.z
#define u_sdf_clipmap_enabled      (u_sdf_clipmap_params.w > 0.0)
/// The levels are stacked along Z in one volume, which this file's texel addressing already
/// assumes, so the total depth is derived rather than uploaded -- one less value to disagree.
#define u_sdf_clipmap_depth        (u_sdf_clipmap_resolution * float(SDF_CLIPMAP_LEVEL_COUNT))

/// Returned wherever no cascade level answers. Matches global_sdf_clipmap::outside_distance:
/// large enough that a trace takes one long step, finite so it never poisons arithmetic.
#define SDF_CLIPMAP_OUTSIDE 1e6

/**
 * Samples ONE cascade level at a WORLD position, returning a world-space distance.
 *
 * Returns SDF_CLIPMAP_OUTSIDE where that level does not cover the position. The coverage test
 * keeps a half-voxel margin on every side, so the trilinear taps of an accepted sample stay
 * inside this level's slab and cannot reach into the neighbouring cascade stacked behind it in Z.
 */
float SdfSampleClipmapLevel(int index, vec3 world_position)
{
	vec4 level = u_sdf_clipmap_levels[index];
	float voxel_size = level.w;
	if(voxel_size <= 0.0)
	{
		return SDF_CLIPMAP_OUTSIDE;
	}
	float resolution = u_sdf_clipmap_resolution;
	vec3 grid = (world_position - level.xyz) / voxel_size;
	if(any(lessThan(grid, vec3_splat(0.5))) || any(greaterThan(grid, vec3_splat(resolution - 0.5))))
	{
		return SDF_CLIPMAP_OUTSIDE;
	}
	// Continuous texel coordinate within the level, then offset into the level's slab.
	vec3 texel = vec3(grid.x, grid.y, grid.z + float(index) * resolution);
	vec3 uvw = vec3(texel.x / resolution, texel.y / resolution, texel.z / u_sdf_clipmap_depth);
	float encoded = texture3DLod(s_sdf_clipmap, uvw, 0.0).x;
	return (encoded - 0.5) * (2.0 * u_sdf_clipmap_encode_range) * voxel_size;
}

/**
 * Finest level covering a world position, plus how far into its cross-fade band it lies.
 *
 * Transcription of global_sdf_clipmap::find_level. Returns SDF_CLIPMAP_LEVEL_COUNT when no level
 * covers the position.
 */
int SdfFindClipmapLevel(vec3 world_position, out float out_blend, out float out_voxel_size)
{
	out_blend = 0.0;
	out_voxel_size = max(u_sdf_clipmap_levels[0].w, 1e-6);
	float resolution = u_sdf_clipmap_resolution;
	for(int i = 0; i < SDF_CLIPMAP_LEVEL_COUNT; ++i)
	{
		vec4 level = u_sdf_clipmap_levels[i];
		float voxel_size = level.w;
		if(voxel_size <= 0.0)
		{
			continue;
		}
		vec3 grid = (world_position - level.xyz) / voxel_size;
		if(any(lessThan(grid, vec3_splat(0.5))) || any(greaterThan(grid, vec3_splat(resolution - 0.5))))
		{
			continue;
		}
		out_voxel_size = voxel_size;
		// Distance to the nearest FACE of this level's addressable box, in its own voxels, so the
		// fade follows the box the coverage test above actually uses rather than a radius.
		vec3 to_low = grid - vec3_splat(0.5);
		vec3 to_high = vec3_splat(resolution - 0.5) - grid;
		vec3 nearest_face = min(to_low, to_high);
		float edge_distance = min(nearest_face.x, min(nearest_face.y, nearest_face.z));
		bool has_next = (i + 1) < SDF_CLIPMAP_LEVEL_COUNT;
		if(has_next)
		{
			has_next = u_sdf_clipmap_levels[i + 1].w > 0.0;
		}
		// The outermost level never fades. Beyond it there is only the give-up value, and mixing
		// toward that would report a distance far larger than the truth -- the one direction a
		// conservative field must never err in, since a trace would step straight through
		// whatever is out there.
		if(has_next && u_sdf_clipmap_blend_voxels > 0.0)
		{
			out_blend = 1.0 - clamp(edge_distance / u_sdf_clipmap_blend_voxels, 0.0, 1.0);
		}
		return i;
	}
	return SDF_CLIPMAP_LEVEL_COUNT;
}

/// How far @p world_position sits into the cross-fade out of @p index, for a caller-chosen
/// band width. 0 = well inside the level, 1 = on the face (about to lose coverage).
/// The field fade uses u_sdf_clipmap_blend_voxels; reflection lighting uses a wider band
/// so a doubled voxel size does not read as a seam.
float SdfClipmapEdgeBlend(int index, vec3 world_position, float fade_voxels)
{
	if(fade_voxels <= 0.0 || (index + 1) >= SDF_CLIPMAP_LEVEL_COUNT)
	{
		return 0.0;
	}
	if(!(u_sdf_clipmap_levels[index + 1].w > 0.0))
	{
		return 0.0;
	}
	vec4 level = u_sdf_clipmap_levels[index];
	if(!(level.w > 0.0))
	{
		return 0.0;
	}
	vec3 grid = (world_position - level.xyz) / level.w;
	vec3 to_low = grid - vec3_splat(0.5);
	vec3 to_high = vec3_splat(u_sdf_clipmap_resolution - 0.5) - grid;
	vec3 nearest_face = min(to_low, to_high);
	float edge_distance = min(nearest_face.x, min(nearest_face.y, nearest_face.z));
	return 1.0 - clamp(edge_distance / fade_voxels, 0.0, 1.0);
}

/**
 * Samples the global clipmap at a WORLD position, returning a world-space distance, and reports
 * the voxel size of the cascade that answered.
 *
 * Transcription of global_sdf_clipmap::sample; the CPU version is the reference the tests pin
 * down, so the two must stay in step.
 *
 * The finest covering level is CROSS-FADED into the next over a band at the edge of its coverage.
 * Levels are composed independently at different voxel sizes, so their isosurfaces do not
 * coincide; switching abruptly puts a step in the field exactly where two consumers are most
 * likely to disagree about where a surface is, and they then resolve onto points a voxel apart
 * and never find each other's cache entries. Blending makes every consumer quote one function.
 *
 * Still conservative: a convex combination of two under-estimates is an under-estimate.
 *
 * The reported voxel size follows the blend for the reason it is reported at all -- the cascades
 * differ in voxel size by orders of magnitude, so anything scaled to "a voxel" (a hit threshold,
 * a gradient epsilon) is meaningless unless it refers to what actually produced the value. Inside
 * the band that is a mixture of two levels, and jumping it at the boundary would produce the
 * banding the per-level size exists to avoid.
 */
/// One clipmap sample with the level that answered it, so a volume of the same layout (the coverage) reads the same
/// levels without searching them again.
struct SdfClipmapSample
{
	float distance;
	float voxel_size;
	/// The finest covering level, SDF_CLIPMAP_LEVEL_COUNT where none covers the position.
	int index;
	/// The cross-fade toward level index + 1.
	float blend;
};

SdfClipmapSample SdfSampleClipmapLevels(vec3 world_position)
{
	SdfClipmapSample s;
	s.distance = SDF_CLIPMAP_OUTSIDE;
	s.voxel_size = max(u_sdf_clipmap_levels[0].w, 1e-6);
	s.index = SDF_CLIPMAP_LEVEL_COUNT;
	s.blend = 0.0;
	if(!u_sdf_clipmap_enabled)
	{
		return s;
	}
	float blend;
	float voxel_size;
	int index = SdfFindClipmapLevel(world_position, blend, voxel_size);
	s.voxel_size = voxel_size;
	s.index = index;
	if(index >= SDF_CLIPMAP_LEVEL_COUNT)
	{
		return s;
	}
	float fine = SdfSampleClipmapLevel(index, world_position);
	s.distance = fine;
	if(blend <= 0.0)
	{
		return s;
	}
	float coarse = SdfSampleClipmapLevel(index + 1, world_position);
	if(coarse >= SDF_CLIPMAP_OUTSIDE)
	{
		// The next level should always cover here -- it is larger and shares a centre -- so this
		// only fires if snapping has pushed it off. Keeping the fine value is both conservative
		// and the better answer; blending toward the give-up value would not be.
		return s;
	}
	s.blend = blend;
	s.voxel_size = mix(voxel_size, u_sdf_clipmap_levels[index + 1].w, blend);
	s.distance = mix(fine, coarse, blend);
	return s;
}

float SdfSampleClipmapEx(vec3 world_position, out float out_voxel_size)
{
	SdfClipmapSample s = SdfSampleClipmapLevels(world_position);
	out_voxel_size = s.voxel_size;
	return s.distance;
}

float SdfSampleClipmap(vec3 world_position)
{
	float ignored_voxel_size;
	return SdfSampleClipmapEx(world_position, ignored_voxel_size);
}

#ifdef SDF_CLIPMAP_COVERAGE_STAGE
/// One level's coverage at a WORLD position, trilinear within its slab (1 where the level does not cover it).
float SdfSampleClipmapCoverageLevel(int index, vec3 world_position)
{
	vec4 level = u_sdf_clipmap_levels[index];
	if(level.w <= 0.0)
	{
		return 1.0;
	}
	float resolution = u_sdf_clipmap_resolution;
	float coverage_resolution = resolution / float(SDF_CLIPMAP_COVERAGE_DOWNSAMPLE);
	vec3 grid = (world_position - level.xyz) / level.w;
	if(any(lessThan(grid, vec3_splat(0.5))) || any(greaterThan(grid, vec3_splat(resolution - 0.5))))
	{
		return 1.0;
	}
	// Voxel 2c's centre (grid 2c + 0.5) is coverage texel c's centre (c + 0.5); clamped so the filter stays in the
	// level's slab.
	vec3 texel = clamp((grid + vec3_splat(0.5)) / float(SDF_CLIPMAP_COVERAGE_DOWNSAMPLE),
	                   vec3_splat(0.5),
	                   vec3_splat(coverage_resolution - 0.5));
	vec3 uvw = vec3(texel.x / coverage_resolution,
	                texel.y / coverage_resolution,
	                (texel.z + float(index) * coverage_resolution) / (coverage_resolution * float(SDF_CLIPMAP_LEVEL_COUNT)));
	return texture3DLod(s_sdf_clipmap_coverage, uvw, 0.0).x;
}

/// The coverage at a WORLD position from the levels @p s was read from, cross-faded the same way.
float SdfSampleClipmapCoverageAt(SdfClipmapSample s, vec3 world_position)
{
	if(s.index >= SDF_CLIPMAP_LEVEL_COUNT)
	{
		return 1.0;
	}
	float fine = SdfSampleClipmapCoverageLevel(s.index, world_position);
	if(s.blend <= 0.0)
	{
		return fine;
	}
	return mix(fine, SdfSampleClipmapCoverageLevel(s.index + 1, world_position), s.blend);
}

/// The coverage at a WORLD position from the levels SdfSampleClipmapEx reads, cross-faded the same way.
float SdfSampleClipmapCoverage(vec3 world_position)
{
	float blend;
	float voxel_size;
	int index = SdfFindClipmapLevel(world_position, blend, voxel_size);
	if(index >= SDF_CLIPMAP_LEVEL_COUNT)
	{
		return 1.0;
	}
	float fine = SdfSampleClipmapCoverageLevel(index, world_position);
	if(blend <= 0.0)
	{
		return fine;
	}
	return mix(fine, SdfSampleClipmapCoverageLevel(index + 1, world_position), blend);
}
#endif

#endif // __GI_SDF_CLIPMAP_SH__
