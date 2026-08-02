#ifndef __GI_SDF_COMMON_SH__
#define __GI_SDF_COMMON_SH__

/*
 * GPU side of the baked mesh distance fields.
 *
 * MIRROR OF engine/engine/rendering/gi/mesh_sdf.h AND sdf_atlas.cpp. The constants, the
 * indirection encoding, and the header/instance layouts must match those files exactly; the
 * gi_tests harness validates the CPU reference implementation (sample_mesh_sdf) that this is
 * the transcription of.
 *
 * RESERVED RESOURCE STAGES. This header declares its own resources rather than taking them
 * as function parameters, because GLSL cannot pass a shader storage buffer to a function.
 * Any shader including it must leave stages 0-3 alone.
 */

#include "../bgfx_compute.sh"

#define SDF_BRICK_SIZE   8.0
#define SDF_BRICK_BORDER 1.0
#define SDF_BRICK_STRIDE 10.0
#define SDF_ENCODE_RANGE 4.0

#define SDF_INDIRECTION_EMPTY_FLAG    0x80000000u
#define SDF_INDIRECTION_INSIDE_FLAG   0x40000000u
#define SDF_INDIRECTION_DISTANCE_MASK 0x00FFFFFFu

/// vec4 elements per field header, per sdf_atlas::header_vec4_count.
#define SDF_HEADER_STRIDE 3
/// vec4 elements per instance, per surface_cache_service::instance_vec4_stride.
#define SDF_INSTANCE_STRIDE 10
/// No instance produced this hit: either nothing was hit, or the global cascade answered, which
/// is composed from many fields and cannot attribute a sample to one.
#define SDF_NO_INSTANCE (-1)

/// Cascades in the global clipmap. Mirror of global_sdf_clipmap::level_count.
#define SDF_CLIPMAP_LEVEL_COUNT 4

SAMPLER3D(s_sdf_atlas, 0);
BUFFER_RO(b_sdf_headers, vec4, 1);
BUFFER_RO(b_sdf_indirection, uint, 2);
BUFFER_RO(b_sdf_instances, vec4, 3);
/// All cascades in one volume, stacked along Z: level i occupies
/// [i * resolution, (i + 1) * resolution). See global_sdf_clipmap_gpu.
SAMPLER3D(s_sdf_clipmap, 4);
/// Uniform world-space grid over the instances, so a ray tests the ones near it rather than all
/// of them. CSR: cell c owns [b_sdf_grid_offsets[c], b_sdf_grid_offsets[c + 1]) of
/// b_sdf_grid_instances. See sdf_instance_grid.
BUFFER_RO(b_sdf_grid_offsets, uint, 12);
BUFFER_RO(b_sdf_grid_instances, uint, 13);

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

/// [0] = grid origin xyz, cell size w. [1] = cell counts xyz, non-zero w when the grid is usable.
/// Filled by surface_cache_service::get_grid_params, the single owner: every pass that traces
/// must walk the same cells, and a pass that derived different ones would simply find different
/// instances -- geometry that occludes in one pass and not another, with no error anywhere.
uniform vec4 u_sdf_grid_params[2];
#define u_sdf_grid_origin    u_sdf_grid_params[0].xyz
#define u_sdf_grid_cell_size u_sdf_grid_params[0].w
#define u_sdf_grid_dim       u_sdf_grid_params[1].xyz
#define u_sdf_grid_enabled   (u_sdf_grid_params[1].w > 0.0)
/// Cells a traversal may visit before giving up. A ray crossing an n-cell grid diagonally touches
/// about 3n, so this is generous; it exists so a denormal direction cannot spin, not as a budget.
#define SDF_GRID_MAX_STEPS 256

/// x = atlas size in bricks per axis, y = atlas size in voxels per axis, z = instance count.
uniform vec4 u_sdf_params;
#define u_sdf_atlas_brick_dim u_sdf_params.x
#define u_sdf_atlas_voxel_dim u_sdf_params.y
#define u_sdf_instance_count  int(u_sdf_params.z)

struct SdfHeader
{
	vec3 bounds_min;
	float voxel_size;
	vec3 brick_dim;
	float indirection_offset;
	/// Local-space half thickness for two-sided (shell) fields; 0 when the field is signed.
	/// INFORMATIONAL ONLY: the bake has already applied it to the stored voxels, so sampling
	/// must not subtract it again.
	float two_sided_thickness;
	vec3 grid_dim;
};

SdfHeader SdfLoadHeader(uint header_index)
{
	uint base = header_index * uint(SDF_HEADER_STRIDE);
	vec4 h0 = b_sdf_headers[base + 0u];
	vec4 h1 = b_sdf_headers[base + 1u];
	vec4 h2 = b_sdf_headers[base + 2u];
	SdfHeader header;
	header.bounds_min = h0.xyz;
	header.voxel_size = h0.w;
	header.brick_dim = h1.xyz;
	header.indirection_offset = h1.w;
	header.two_sided_thickness = h2.x;
	header.grid_dim = h2.yzw;
	return header;
}

/**
 * Transforms are stored as the three ROWS of an affine 3x4 rather than as a mat4.
 *
 * Reconstructing a mat4 from four vec4s would depend on whether the backend treats those
 * vec4s as rows or columns, and on which side mul() puts the matrix -- a convention mismatch
 * that produces a plausible-looking but wrong transform. Explicit rows plus explicit dot
 * products have exactly one interpretation, and the packing side (pack_sdf_instances) writes
 * them the same way.
 */
struct SdfInstance
{
	vec4 world_to_local_rows[3];
	vec4 local_to_world_rows[3];
	vec3 world_bounds_min;
	vec3 world_bounds_max;
	uint header_index;
	/// Smallest scale axis: converts a local-space distance to a conservative world distance.
	float local_to_world_scale;
	/// Material of the submesh this placement draws, so a bounce ray can colour a cell it
	/// discovers. Emission is already scaled by its intensity.
	vec3 albedo;
	vec3 emissive;
};

SdfInstance SdfLoadInstance(int index)
{
	uint base = uint(index) * uint(SDF_INSTANCE_STRIDE);
	SdfInstance inst;
	inst.world_to_local_rows[0] = b_sdf_instances[base + 0u];
	inst.world_to_local_rows[1] = b_sdf_instances[base + 1u];
	inst.world_to_local_rows[2] = b_sdf_instances[base + 2u];
	inst.local_to_world_rows[0] = b_sdf_instances[base + 3u];
	inst.local_to_world_rows[1] = b_sdf_instances[base + 4u];
	inst.local_to_world_rows[2] = b_sdf_instances[base + 5u];
	vec4 b0 = b_sdf_instances[base + 6u];
	vec4 b1 = b_sdf_instances[base + 7u];
	inst.world_bounds_min = b0.xyz;
	inst.header_index = uint(b0.w);
	inst.world_bounds_max = b1.xyz;
	inst.local_to_world_scale = b1.w;
	inst.albedo = b_sdf_instances[base + 8u].xyz;
	inst.emissive = b_sdf_instances[base + 9u].xyz;
	return inst;
}

vec3 SdfTransformPoint(vec4 rows[3], vec3 p)
{
	return vec3(dot(rows[0].xyz, p) + rows[0].w,
	            dot(rows[1].xyz, p) + rows[1].w,
	            dot(rows[2].xyz, p) + rows[2].w);
}

vec3 SdfTransformDirection(vec4 rows[3], vec3 d)
{
	return vec3(dot(rows[0].xyz, d), dot(rows[1].xyz, d), dot(rows[2].xyz, d));
}

/**
 * Samples a resident field at a LOCAL-space position, returning a LOCAL-space distance.
 *
 * The result is a conservative under-estimate of the true distance, never an over-estimate:
 * voxels saturate at SDF_ENCODE_RANGE and empty bricks report a distance valid for every
 * point they contain. Sphere tracing therefore never overshoots through a surface.
 */
float SdfSampleLocal(SdfHeader header, vec3 local_position)
{
	// Reject an unpopulated header before it can poison the arithmetic. With voxel_size 0 the
	// division below yields inf, the outside test then compares NaN (inf * 0) which is false
	// for every operator, and execution falls through to a garbage brick index that decodes as
	// "surface brick, slot 0" -- an instant hit on an arbitrary brick. The visible result is a
	// solid noise-filled box the size of the field's bounds, with nothing to indicate that the
	// header never arrived. Returning a large positive distance instead makes a mis-bound or
	// under-sized header buffer read as empty space, which is obvious rather than misleading.
	if(!(header.voxel_size > 0.0))
	{
		return 1e8;
	}
	// Position in voxels from the field origin.
	vec3 grid = (local_position - header.bounds_min) / header.voxel_size;
	// Outside the field entirely: report the distance to the bounds PLUS the padding the bake
	// guarantees between the bounds and the surface (mesh_sdf::get_bounds_padding).
	//
	// The padding term is required, not cosmetic. Distance-to-bounds alone is zero exactly on
	// the boundary, and that is precisely where a ray entering the field starts -- so the very
	// first sample of every entering ray reads as a hit and the tracer draws the bounding box,
	// shaded by the box's own face normals, instead of the mesh. Adding the padding is still
	// conservative: any straight path from outside to the surface crosses the boundary, so the
	// true distance is at least distance-to-bounds plus the padding.
	vec3 clamped_grid = clamp(grid, vec3_splat(0.0), header.grid_dim);
	vec3 outside_delta = (grid - clamped_grid) * header.voxel_size;
	float outside_distance = length(outside_delta);
	if(outside_distance > 0.0)
	{
		return outside_distance + SDF_ENCODE_RANGE * header.voxel_size;
	}
	vec3 brick_coord = clamp(floor(grid / SDF_BRICK_SIZE), vec3_splat(0.0), header.brick_dim - vec3_splat(1.0));
	uint brick_index = uint(brick_coord.x) +
	                   uint(brick_coord.y) * uint(header.brick_dim.x) +
	                   uint(brick_coord.z) * uint(header.brick_dim.x) * uint(header.brick_dim.y);
	uint entry = b_sdf_indirection[uint(header.indirection_offset) + brick_index];
	if((entry & SDF_INDIRECTION_EMPTY_FLAG) != 0u)
	{
		float distance = float(entry & SDF_INDIRECTION_DISTANCE_MASK) * header.voxel_size;
		return (entry & SDF_INDIRECTION_INSIDE_FLAG) != 0u ? -distance : distance;
	}
	// Surface brick: `entry` is the absolute atlas slot.
	float atlas_brick_dim = u_sdf_atlas_brick_dim;
	float slot = float(entry);
	vec3 slot_coord;
	slot_coord.x = mod(slot, atlas_brick_dim);
	slot_coord.y = mod(floor(slot / atlas_brick_dim), atlas_brick_dim);
	slot_coord.z = floor(slot / (atlas_brick_dim * atlas_brick_dim));
	// Position within the brick, in voxels, in [0, SDF_BRICK_SIZE].
	vec3 brick_local = grid - brick_coord * SDF_BRICK_SIZE;
	// Storage index l holds the value at brick-local position l - border + 0.5, so the
	// continuous texel coordinate (texel centres at index + 0.5) for position p is p + border.
	// Every filter tap then lands inside this brick's own tile, which is exactly what the
	// border is for -- see the note on mesh_sdf::brick_border.
	vec3 atlas_coord = slot_coord * SDF_BRICK_STRIDE + brick_local + vec3_splat(SDF_BRICK_BORDER);
	vec3 uvw = atlas_coord / u_sdf_atlas_voxel_dim;
	float encoded = texture3DLod(s_sdf_atlas, uvw, 0.0).x;
	float distance_voxels = (encoded - 0.5) * (2.0 * SDF_ENCODE_RANGE);
	// The shell thickness of a two-sided field is ALREADY baked into the stored voxels (see
	// bake_mesh_sdf), so it must not be subtracted again here. Doing so shifts the whole field
	// inward by the thickness, which turns the samples just inside the bounds negative -- and
	// the tracer reads any negative sample as a hit, so the field's bounding box renders solid,
	// dithering in and out as the ray direction crosses the sign boundary.
	return distance_voxels * header.voxel_size;
}

/**
 * Reports what the lookup at a local-space position actually resolved to.
 *
 * Diagnostic only. Every failure in the residency path -- indirection buffer not bound, atlas
 * never written, wrong slot arithmetic -- degrades into per-pixel noise in the traced image,
 * which looks identical whichever stage broke. This returns the intermediate values so the
 * stage can be identified directly instead of inferred.
 *
 *   x = 1 when the brick is empty (no voxel storage), 0 when it is a surface brick
 *   y = the raw encoded atlas texel, before decoding (0.5 is distance zero)
 *   z = the atlas slot, normalised by the atlas capacity
 *   w = 1 when the position is inside the field bounds
 */
vec4 SdfProbeLocal(SdfHeader header, vec3 local_position)
{
	if(!(header.voxel_size > 0.0))
	{
		return vec4(0.0, 0.0, 0.0, 0.0);
	}
	vec3 grid = (local_position - header.bounds_min) / header.voxel_size;
	vec3 clamped_grid = clamp(grid, vec3_splat(0.0), header.grid_dim);
	if(length((grid - clamped_grid) * header.voxel_size) > 0.0)
	{
		return vec4(0.0, 0.0, 0.0, 0.0);
	}
	vec3 brick_coord = clamp(floor(grid / SDF_BRICK_SIZE), vec3_splat(0.0), header.brick_dim - vec3_splat(1.0));
	uint brick_index = uint(brick_coord.x) +
	                   uint(brick_coord.y) * uint(header.brick_dim.x) +
	                   uint(brick_coord.z) * uint(header.brick_dim.x) * uint(header.brick_dim.y);
	uint entry = b_sdf_indirection[uint(header.indirection_offset) + brick_index];
	if((entry & SDF_INDIRECTION_EMPTY_FLAG) != 0u)
	{
		return vec4(1.0, 0.0, 0.0, 1.0);
	}
	float atlas_brick_dim = u_sdf_atlas_brick_dim;
	float slot = float(entry);
	vec3 slot_coord;
	slot_coord.x = mod(slot, atlas_brick_dim);
	slot_coord.y = mod(floor(slot / atlas_brick_dim), atlas_brick_dim);
	slot_coord.z = floor(slot / (atlas_brick_dim * atlas_brick_dim));
	vec3 brick_local = grid - brick_coord * SDF_BRICK_SIZE;
	vec3 atlas_coord = slot_coord * SDF_BRICK_STRIDE + brick_local + vec3_splat(SDF_BRICK_BORDER);
	float encoded = texture3DLod(s_sdf_atlas, atlas_coord / u_sdf_atlas_voxel_dim, 0.0).x;
	float capacity = atlas_brick_dim * atlas_brick_dim * atlas_brick_dim;
	return vec4(0.0, encoded, slot / max(capacity, 1.0), 1.0);
}

/**
 * Gradient of the field at a local-space position, giving the surface normal.
 * Central differences over one voxel; the field is smooth inside the band so this is stable.
 */
vec3 SdfGradientLocal(SdfHeader header, vec3 local_position)
{
	float e = header.voxel_size;
	vec3 gradient;
	gradient.x = SdfSampleLocal(header, local_position + vec3(e, 0.0, 0.0)) -
	             SdfSampleLocal(header, local_position - vec3(e, 0.0, 0.0));
	gradient.y = SdfSampleLocal(header, local_position + vec3(0.0, e, 0.0)) -
	             SdfSampleLocal(header, local_position - vec3(0.0, e, 0.0));
	gradient.z = SdfSampleLocal(header, local_position + vec3(0.0, 0.0, e)) -
	             SdfSampleLocal(header, local_position - vec3(0.0, 0.0, e));
	float len = length(gradient);
	return len > 1e-8 ? gradient / len : vec3(0.0, 1.0, 0.0);
}

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
 * the band that is a mixture of two levels, and jumping it at the boundary would reintroduce the
 * banding the per-level size was added to cure.
 */
float SdfSampleClipmapEx(vec3 world_position, out float out_voxel_size)
{
	out_voxel_size = max(u_sdf_clipmap_levels[0].w, 1e-6);
	if(!u_sdf_clipmap_enabled)
	{
		return SDF_CLIPMAP_OUTSIDE;
	}
	float blend;
	int index = SdfFindClipmapLevel(world_position, blend, out_voxel_size);
	if(index >= SDF_CLIPMAP_LEVEL_COUNT)
	{
		return SDF_CLIPMAP_OUTSIDE;
	}
	float fine = SdfSampleClipmapLevel(index, world_position);
	if(blend <= 0.0)
	{
		return fine;
	}
	float coarse = SdfSampleClipmapLevel(index + 1, world_position);
	if(coarse >= SDF_CLIPMAP_OUTSIDE)
	{
		// The next level should always cover here -- it is larger and shares a centre -- so this
		// only fires if snapping has pushed it off. Keeping the fine value is both conservative
		// and the better answer; blending toward the give-up value would not be.
		return fine;
	}
	out_voxel_size = mix(out_voxel_size, u_sdf_clipmap_levels[index + 1].w, blend);
	return mix(fine, coarse, blend);
}

float SdfSampleClipmap(vec3 world_position)
{
	float ignored_voxel_size;
	return SdfSampleClipmapEx(world_position, ignored_voxel_size);
}

/// Iterations used to converge onto the isosurface. More than one, because a single Newton step
/// lands somewhere that still depends on where it started, and the callers start up to a voxel
/// apart -- but TWO, not more, and that is measured rather than assumed.
///
/// Mirror of global_sdf_clipmap::surface_resolve_steps. Each iteration is 7 cascade samples per
/// ray, in the pass that dominates GI cost, so the count is worth money. Swept against
/// writer/reader addressing agreement it gives 48.9% / 53.3% / 52.9% / 52.8% for 1 / 2 / 3 / 4:
/// the quality plateaus at two, and the four this used to be paid twice the samples for nothing.
/// `test_surface_resolve_addresses_one_cell_from_both_sides` pins it in both directions.
#define SDF_SURFACE_RESOLVE_STEPS 2
/// Cap on a single Newton step, in voxels of the answering level. A start point that happens to
/// sit in the saturated far field would otherwise be thrown across the scene by its first step.
#define SDF_SURFACE_RESOLVE_MAX_STEP 4.0

struct SdfSurfacePoint
{
	vec3 position;
	vec3 normal;
	/// False when there was no isosurface to converge onto, so `position` and `normal` are the
	/// untouched inputs rather than an answer. Callers MUST check it.
	///
	/// Without this the function cannot say "I do not know": outside every cascade level every
	/// sample saturates, the gradient is exactly zero, the loop breaks on its first iteration and
	/// the initialised up vector is returned as though it were the surface's facing. A caller then
	/// derives a cache key from a fabricated normal at an address no ray can ever resolve to.
	bool valid;
};

/**
 * @brief Resolves a point NEAR a surface to the canonical point ON the field's isosurface, plus
 *        the field's own normal there.
 *
 * This is the address a world-space cache has to be keyed by, and the reason is that its writers
 * and readers arrive from different directions. One comes from the rasterised G-buffer, the other
 * from a traced ray -- and those are two DIFFERENT surfaces. The field's zero level set is
 * displaced from the rendered triangles by a fraction of a voxel, and a sphere trace stops short
 * of even that by its acceptance radius. Keyed off either raw position, the two sides disagree by
 * roughly a voxel, which is the same order as a cache cell, so they address different cells and
 * never see each other's work. The failure is worst on large flat surfaces, where the field
 * deviates most from the geometry, and mildest on small detailed props, where a fine per-instance
 * field answers instead.
 *
 * Converging both onto the field's own isosurface removes the disagreement at its source: after
 * this, both sides are quoting the same function rather than two approximations of it. The normal
 * comes from the field for the same reason -- a G-buffer normal and a field gradient differ
 * enough to fall on opposite sides of a quantisation boundary.
 */
SdfSurfacePoint SdfResolveSurfacePoint(vec3 world_position)
{
	SdfSurfacePoint result;
	result.position = world_position;
	result.normal = vec3(0.0, 1.0, 0.0);
	result.valid = false;
	for(int i = 0; i < SDF_SURFACE_RESOLVE_STEPS; ++i)
	{
		float voxel_size;
		float distance = SdfSampleClipmapEx(result.position, voxel_size);
		// No cascade covers this point, so there is no isosurface here to converge onto. Giving up
		// with valid = false is the whole reason that flag exists: the alternative is to fall out
		// of the loop below on a zero gradient and hand back the caller's own input decorated with
		// an up vector, which reads as a perfectly good answer.
		if(distance >= SDF_CLIPMAP_OUTSIDE)
		{
			result.valid = false;
			break;
		}
		// Differencing over the answering level's voxel. Using a fixed epsilon samples far
		// inside a single coarse voxel, where the field is flat and the normal is noise.
		float e = voxel_size;
		vec3 gradient = vec3(SdfSampleClipmap(result.position + vec3(e, 0.0, 0.0)) -
		                         SdfSampleClipmap(result.position - vec3(e, 0.0, 0.0)),
		                     SdfSampleClipmap(result.position + vec3(0.0, e, 0.0)) -
		                         SdfSampleClipmap(result.position - vec3(0.0, e, 0.0)),
		                     SdfSampleClipmap(result.position + vec3(0.0, 0.0, e)) -
		                         SdfSampleClipmap(result.position - vec3(0.0, 0.0, e)));
		float gradient_length = length(gradient);
		// A flat field has no direction to step in. Inside coverage this means the point sits in a
		// saturated region the cascade does not represent -- geometry thinner than its voxel, or a
		// level not composed yet -- which is equally unusable as an address.
		if(gradient_length < 1e-8)
		{
			result.valid = false;
			break;
		}
		result.normal = gradient / gradient_length;
		result.valid = true;
		float step_limit = voxel_size * SDF_SURFACE_RESOLVE_MAX_STEP;
		result.position -= result.normal * clamp(distance, -step_limit, step_limit);
	}
	return result;
}

/**
 * Slab test of a ray against an axis-aligned box. Returns false when the ray misses.
 * `t_near` is clamped to zero so a ray starting inside the box begins at its origin.
 */
bool SdfIntersectBounds(vec3 origin, vec3 inv_direction, vec3 bounds_min, vec3 bounds_max,
                        float t_max, out float t_near, out float t_far)
{
	vec3 t0 = (bounds_min - origin) * inv_direction;
	vec3 t1 = (bounds_max - origin) * inv_direction;
	vec3 t_small = min(t0, t1);
	vec3 t_big = max(t0, t1);
	t_near = max(max(t_small.x, t_small.y), max(t_small.z, 0.0));
	t_far = min(min(t_big.x, t_big.y), min(t_big.z, t_max));
	return t_near <= t_far;
}

/// Result of a traced ray. `t` is a WORLD distance along the ray, so hits found by different
/// tiers are directly comparable.
struct SdfRayHit
{
	bool hit;
	float t;
	vec3 normal;
	/// Steps consumed, for cost visualisation.
	int steps;
	/// True when a march ran out of budget instead of resolving. Distinguishes "found nothing"
	/// from "gave up", which are otherwise indistinguishable in the output.
	bool exhausted;
	/// Instance that produced the hit, or SDF_NO_INSTANCE when the global cascade answered.
	///
	/// Carries the MATERIAL to the caller. A distance field stores geometry only, so a bounce ray
	/// that discovers a surface no on-screen pixel has ever registered would otherwise have no
	/// colour to give it. The cascade is composed from many fields at once and cannot attribute a
	/// sample to one of them, so a far-field hit legitimately has no material.
	int instance_index;
};

/**
 * Hit acceptance radius at ray parameter @p t -- a cone trace rather than a pure sphere trace.
 *
 * Sphere tracing advances by the distance to the nearest surface, which for a ray grazing a
 * surface stays small for the ray's entire length. The step count then grows without bound as
 * the angle flattens and the march runs out of budget, which is what makes a traced floor fade
 * out with distance.
 *
 * The fix is to widen what counts as a HIT with distance, modelling the ray as a cone whose
 * radius grows as it travels. A grazing ray then terminates once the surface is within the cone
 * -- at a bounded t -- and because each step still advances by at least the current radius, the
 * march covers distance D in O(log D) steps.
 *
 * It is important that this grows the ACCEPTANCE and not the STEP. Forcing a minimum step
 * larger than the distance to the surface makes the ray jump straight through it, so a grazing
 * ray punches through a floor and misses in bands -- visible as concentric rings, and much
 * worse than the fade it was meant to cure. Widening the acceptance can only ever stop a ray
 * EARLY, never past a surface, so the trace stays conservative.
 *
 * The cost is that distant surfaces are effectively fattened by the cone radius. For occlusion
 * that errs toward over-occluding at range, which is the safe direction.
 */
float SdfConeRadius(float t, float base_threshold, float relaxation)
{
	return max(base_threshold, t * relaxation);
}

SdfRayHit SdfMakeMiss()
{
	SdfRayHit result;
	result.hit = false;
	result.t = 0.0;
	result.normal = vec3(0.0, 1.0, 0.0);
	result.steps = 0;
	result.exhausted = false;
	result.instance_index = SDF_NO_INSTANCE;
	return result;
}

/**
 * Near field: traces the per-instance baked fields.
 *
 * Accurate to each mesh's own voxel size, which is what resolves thin geometry and stops light
 * leaking through it. Costs one bounds test per instance per ray, so it is bounded to the near
 * field and the cascade takes over beyond it.
 */
/**
 * Sphere traces ONE instance and merges the result into @p result if it is nearer.
 *
 * Split out of the loop so the grid traversal and the ungridded fallback share exactly one copy
 * of the per-instance logic; two copies of a sphere trace is two places for the hit threshold or
 * the transform convention to drift apart.
 */
void SdfTestInstance(int index, vec3 origin, vec3 direction, vec3 inv_dir, float t_min, float t_max,
                     int max_steps, float surface_bias, float relaxation, bool want_normal,
                     inout SdfRayHit result)
{
	SdfInstance inst = SdfLoadInstance(index);
	float t_near;
	float t_far;
	// Broad phase against the instance bounds, capped by the best hit so far so a nearer result
	// short-circuits everything behind it. This is also what makes the duplicate visits the grid
	// allows cheap: the second visit to an instance already behind a hit rejects immediately.
	if(!SdfIntersectBounds(origin, inv_dir, inst.world_bounds_min, inst.world_bounds_max,
	                       min(result.t, t_max), t_near, t_far))
	{
		return;
	}
	t_near = max(t_near, t_min);
	if(t_near > t_far)
	{
		return;
	}
	SdfHeader header = SdfLoadHeader(inst.header_index);
	vec3 local_origin = SdfTransformPoint(inst.world_to_local_rows, origin);
	// Deliberately not normalised: the linear part of world_to_local already maps a world
	// displacement to the matching local one, so local_origin + local_dir * t is the exact
	// local image of the world point at t. Rescaling would apply the instance scale twice.
	vec3 local_dir = SdfTransformDirection(inst.world_to_local_rows, direction);
	// Relative to the field's own resolution, never an absolute world distance: a field
	// resolves nothing finer than a voxel, and a voxel's world size varies with both bake
	// resolution and instance scale.
	float hit_threshold = max(surface_bias * header.voxel_size * inst.local_to_world_scale, 1e-6);
	float t = t_near;
	bool resolved = false;
	for(int step_index = 0; step_index < max_steps; ++step_index)
	{
		if(t > t_far)
		{
			resolved = true;
			break;
		}
		vec3 local_position = local_origin + local_dir * t;
		float world_distance = SdfSampleLocal(header, local_position) * inst.local_to_world_scale;
		++result.steps;
		float accept = SdfConeRadius(t, hit_threshold, relaxation);
		if(world_distance < accept)
		{
			if(t < result.t || !result.hit)
			{
				result.hit = true;
				result.t = t;
				if(want_normal)
				{
					vec3 local_normal = SdfGradientLocal(header, local_position);
					result.normal =
					    normalize(SdfTransformDirection(inst.local_to_world_rows, local_normal));
				}
				else
				{
					// Zero, not a plausible up vector, so a caller that reads this without having
					// asked for it fails the dot(n, n) test every consumer here already applies
					// rather than silently accepting a fabricated facing.
					result.normal = vec3_splat(0.0);
				}
				result.instance_index = index;
			}
			resolved = true;
			break;
		}
		// Step by the true distance, never more: overstepping would pass through the
		// surface. Floored only by the base threshold so a zero reading cannot stall.
		// Termination comes from the widening acceptance above, not from a forced step.
		t += max(world_distance, hit_threshold);
	}
	result.exhausted = result.exhausted || !resolved;
}

/**
 * Traces the per-instance tier: exact fields, bounded to the near field.
 *
 * Walks the world-space cull grid rather than testing every instance, which is what keeps the
 * cost proportional to the instances a ray actually passes near instead of to the scene's total.
 * An instance appears in every cell its bounds overlap, so the walk may test it more than once;
 * that is deliberate, and the broad phase above makes a repeat nearly free. Guaranteeing
 * exactly-once would mean reasoning about parametric ties at cell boundaries, where a
 * floating-point coin flip skips an instance -- and a skipped instance is geometry that silently
 * stops occluding, which is the one failure this tier must not have.
 */
SdfRayHit SdfTraceInstances(vec3 origin, vec3 direction, float t_min, float t_max, int max_steps,
                            float surface_bias, float relaxation, bool want_normal)
{
	SdfRayHit result = SdfMakeMiss();
	result.t = t_max;
	vec3 inv_dir = 1.0 / max(abs(direction), vec3_splat(1e-8)) * sign(direction + vec3_splat(1e-20));
	if(!u_sdf_grid_enabled)
	{
		// No grid this frame (nothing resident, or the upload failed). Testing everything is the
		// slow answer, not a wrong one, and it keeps the tier working rather than silently
		// dropping every instance.
		for(int i = 0; i < u_sdf_instance_count; ++i)
		{
			SdfTestInstance(i, origin, direction, inv_dir, t_min, t_max, max_steps, surface_bias,
			                relaxation, want_normal, result);
		}
		return result;
	}
	vec3 grid_min = u_sdf_grid_origin;
	vec3 grid_max = u_sdf_grid_origin + u_sdf_grid_dim * u_sdf_grid_cell_size;
	float t_enter;
	float t_exit;
	// Rays routinely start outside the grid -- the cache update pass casts from entries anywhere
	// in the world -- so the walk begins where the ray ENTERS, not at t_min.
	if(!SdfIntersectBounds(origin, inv_dir, grid_min, grid_max, t_max, t_enter, t_exit))
	{
		return result;
	}
	t_enter = max(t_enter, t_min);
	if(t_enter > t_exit)
	{
		return result;
	}
	// Amanatides-Woo. Clamped because a segment entering exactly on a cell plane can floor to a
	// cell just outside the grid.
	vec3 entry = origin + direction * t_enter;
	vec3 cell_f = floor((entry - grid_min) / u_sdf_grid_cell_size);
	cell_f = clamp(cell_f, vec3_splat(0.0), u_sdf_grid_dim - vec3_splat(1.0));
	vec3 dir_sign = sign(direction);
	vec3 abs_dir = max(abs(direction), vec3_splat(1e-8));
	vec3 t_delta = vec3_splat(u_sdf_grid_cell_size) / abs_dir;
	// Plane the ray crosses next on each axis: the far side of this cell when moving positively,
	// the near side when moving negatively.
	vec3 next_plane = grid_min + (cell_f + max(dir_sign, vec3_splat(0.0))) * u_sdf_grid_cell_size;
	vec3 t_next = (next_plane - origin) * inv_dir;
	// An axis with no motion never crosses a plane. Its t_next would otherwise be a huge value of
	// arbitrary SIGN, and a large negative one would win every min() below and step that axis
	// forever. Force it out of the running instead.
	vec3 moving = step(vec3_splat(1e-7), abs(direction));
	t_next = mix(vec3_splat(SDF_CLIPMAP_OUTSIDE), t_next, moving);
	t_delta = mix(vec3_splat(SDF_CLIPMAP_OUTSIDE), t_delta, moving);
	vec3 dim = u_sdf_grid_dim;
	for(int visited = 0; visited < SDF_GRID_MAX_STEPS; ++visited)
	{
		int cell_index = int(cell_f.x + cell_f.y * dim.x + cell_f.z * dim.x * dim.y);
		uint begin = b_sdf_grid_offsets[cell_index];
		uint end = b_sdf_grid_offsets[cell_index + 1];
		for(uint entry_index = begin; entry_index < end; ++entry_index)
		{
			SdfTestInstance(int(b_sdf_grid_instances[entry_index]), origin, direction, inv_dir,
			                t_min, t_max, max_steps, surface_bias, relaxation, want_normal, result);
		}
		float t_step = min(t_next.x, min(t_next.y, t_next.z));
		if(t_step > t_exit)
		{
			break;
		}
		// Nothing beyond here can be nearer than the hit already found, so stop walking.
		//
		// Safe despite an instance being able to span many cells: it is listed in EVERY cell its
		// bounds touch, so one whose bounds reach back before t_step was already tested in the
		// cells covering that range. Only instances that begin further along the ray than the
		// current hit are skipped, and those could never have won.
		//
		// Worth doing even though the per-instance broad phase already caps itself by the nearest
		// hit and rejects them cheaply: without this the walk still steps through every remaining
		// cell to the far side of the grid, at two buffer reads each, for a ray that is finished.
		if(result.hit && t_step > result.t)
		{
			break;
		}
		// Mask rather than a branch, which also steps BOTH axes when a ray crosses a corner
		// exactly. Picking one there would leave the walk in a cell the ray does not occupy.
		vec3 mask = step(t_next, vec3_splat(t_step));
		cell_f += mask * dir_sign;
		t_next += mask * t_delta;
		if(any(lessThan(cell_f, vec3_splat(0.0))) || any(greaterThan(cell_f, dim - vec3_splat(1.0))))
		{
			break;
		}
	}
	return result;
}

SdfRayHit SdfTraceClipmap(vec3 origin, vec3 direction, float t_min, float t_max, int max_steps,
                          float surface_bias, float relaxation, bool want_normal)
{
	SdfRayHit result = SdfMakeMiss();
	if(!u_sdf_clipmap_enabled || t_min >= t_max)
	{
		return result;
	}
	float t = t_min;
	for(int step = 0; step < max_steps; ++step)
	{
		if(t > t_max)
		{
			return result;
		}
		vec3 p = origin + direction * t;
		// Everything scaled to "a voxel" must use the voxel size of the cascade that actually
		// answered, not the finest level's. The cascades differ by orders of magnitude, and
		// using the wrong one collapses the gradient into quantisation noise, which breaks the
		// traced surface into bands.
		float voxel;
		float d = SdfSampleClipmapEx(p, voxel);
		++result.steps;
		float base_threshold = max(surface_bias * voxel, 1e-6);
		float accept = SdfConeRadius(t, base_threshold, relaxation);
		if(d < accept)
		{
			result.hit = true;
			result.t = t;
			if(want_normal)
			{
				// Central differences over one voxel OF THE ANSWERING LEVEL.
				float e = voxel;
				float ignored;
				vec3 n = vec3(SdfSampleClipmapEx(p + vec3(e, 0.0, 0.0), ignored) -
				                  SdfSampleClipmapEx(p - vec3(e, 0.0, 0.0), ignored),
				              SdfSampleClipmapEx(p + vec3(0.0, e, 0.0), ignored) -
				                  SdfSampleClipmapEx(p - vec3(0.0, e, 0.0), ignored),
				              SdfSampleClipmapEx(p + vec3(0.0, 0.0, e), ignored) -
				                  SdfSampleClipmapEx(p - vec3(0.0, 0.0, e), ignored));
				float len = length(n);
				result.normal = len > 1e-8 ? n / len : vec3(0.0, 1.0, 0.0);
			}
			else
			{
				result.normal = vec3_splat(0.0);
			}
			return result;
		}
		t += max(d, base_threshold);
	}
	result.exhausted = true;
	return result;
}

/**
 * Traces a world-space ray through the cascade of representations, nearest and most accurate
 * first.
 *
 * The tiers exist because no single structure is right at every range. Per-instance fields
 * resolve thin geometry but cost a bounds test per instance; the global cascade covers the
 * whole world in one lookup but cannot represent anything thinner than its voxels. Splitting
 * by distance uses each where it is both cheapest and most accurate.
 *
 * A screen-space tier in front of these two is the remaining piece (see the design doc): the
 * depth buffer resolves the first metre or so exactly and in a few steps, which is also where
 * sphere tracing is at its worst.
 */
SdfRayHit SdfTraceRay(vec3 origin, vec3 direction, float t_max, float near_field_distance,
                      int max_steps, float surface_bias, float relaxation, bool want_normal)
{
	// Relaxation applies to the near field too, and used to be forced to zero here on the grounds
	// that "stepping past a wall would leak light through it". That reasoning does not hold: the
	// relaxation term feeds SdfConeRadius, which is consulted ONLY by the hit test
	// (`world_distance < accept`), while the advance is `t += max(world_distance, hit_threshold)`
	// and uses the base threshold. So it widens what counts as a hit and never the step -- it can
	// only stop a ray EARLY, never carry it past a surface, which is the whole reason the cone
	// formulation was chosen over a forced minimum step.
	//
	// This matters because the near field is where the cost is. Measured on Bistro, taking the
	// per-instance tier out entirely drops the gather from 8.9 ms to 1.0 ms, and the step-count
	// view shows that cost concentrated exactly where a ray runs nearly parallel to a large
	// surface -- the grazing case a growing acceptance radius exists to bound.
	//
	// What it does cost is over-occlusion at range (distant geometry is effectively fattened by the
	// cone radius) and a hit that sits further short of the surface, which the cache addressing has
	// to absorb. Both are measurable rather than arguable: the first by eye, the second by the
	// agreement rates in test_surface_resolve_addresses_one_cell_from_both_sides. The default stays
	// zero so this changes nothing until it is deliberately dialled up.
	SdfRayHit near_hit = SdfTraceInstances(origin, direction, 0.0, min(near_field_distance, t_max),
	                                       max_steps, surface_bias, relaxation, want_normal);
	if(near_hit.hit)
	{
		return near_hit;
	}
	SdfRayHit far_hit = SdfTraceClipmap(origin, direction, near_field_distance, t_max, max_steps,
	                                    surface_bias, relaxation, want_normal);
	// Carry the near-field cost and exhaustion forward, so the caller sees the whole ray's
	// expense rather than only the tier that happened to answer.
	far_hit.steps += near_hit.steps;
	far_hit.exhausted = far_hit.exhausted || near_hit.exhausted;
	return far_hit;
}

#endif // __GI_SDF_COMMON_SH__
