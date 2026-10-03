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
#include "gi_constants.sh"
#include "sdf_clipmap.sh"

#define SDF_BRICK_SIZE   8.0
#define SDF_BRICK_BORDER 1.0
#define SDF_BRICK_STRIDE 10.0
#define SDF_ENCODE_RANGE 4.0

#define SDF_INDIRECTION_EMPTY_FLAG    0x80000000u
#define SDF_INDIRECTION_INSIDE_FLAG   0x40000000u
#define SDF_INDIRECTION_DISTANCE_MASK 0x00FFFFFFu

/// vec4 elements per field header, per sdf_atlas::header_vec4_count.
#define SDF_HEADER_STRIDE 5
/// vec4 elements per instance, per surface_cache_system::instance_vec4_stride.
#define SDF_INSTANCE_STRIDE 10
/// No instance produced this hit: either nothing was hit, or the global cascade answered, which
/// is composed from many fields and cannot attribute a sample to one.
#define SDF_NO_INSTANCE (-1)

SAMPLER3D(s_sdf_atlas, 0);
BUFFER_RO(b_sdf_headers, vec4, 1);
BUFFER_RO(b_sdf_indirection, uint, 2);
BUFFER_RO(b_sdf_instances, vec4, 3);
/// Uniform world-space grid over the instances, so a ray tests the ones near it rather than all
/// of them. ONE buffer: the CSR offsets first (cell c owns [offsets[c], offsets[c + 1]) of the
/// instance list), then the instance indices from u_sdf_grid_instance_base. See
/// sdf_instance_grid. One buffer rather than two keeps a bgfx stage free in every tracer.
BUFFER_RO(b_sdf_grid, uint, 12);

/// [0] = grid origin xyz, cell size w. [1] = cell counts xyz, w = the instance list's base entry
/// in b_sdf_grid (the offset count) - non-zero when the grid is usable.
/// Filled by surface_cache_system::get_grid_params, the single owner: every pass that traces
/// must walk the same cells, and a pass that derived different ones would simply find different
/// instances -- geometry that occludes in one pass and not another, with no error anywhere.
uniform vec4 u_sdf_grid_params[GI_SDF_GRID_PARAMS_VEC4];
#define u_sdf_grid_origin    u_sdf_grid_params[0].xyz
#define u_sdf_grid_cell_size u_sdf_grid_params[0].w
#define u_sdf_grid_dim       u_sdf_grid_params[1].xyz
#define u_sdf_grid_enabled   (u_sdf_grid_params[1].w > 0.0)
#define u_sdf_grid_instance_base uint(u_sdf_grid_params[1].w)
/// Cells a traversal may visit before giving up. A ray crossing an n-cell grid diagonally touches
/// about 3n, so this is generous; it exists so a denormal direction cannot spin, not as a budget.
#define SDF_GRID_MAX_STEPS 256
/// Steps one instance visit may spend walking OUT of the ray's own launch band before giving up
/// and treating the instance as non-occluding for the rest of that cell segment. See the
/// suppress_steps note in SdfTestInstance for why the walk cannot otherwise terminate.
#define SDF_SUPPRESS_MAX_STEPS 8

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
	/// The submesh's material renders both faces (UE bMostlyTwoSided): the Lumen coverage leaves space near only such
	/// instances uncovered.
	bool is_two_sided;
	/// World length of each local axis: the per-axis bounds of SdfInstanceWorldDistance.
	vec3 axis_scale;
	/// The material emits (UE's Emissive Light Source, derived): the Lumen cascade keeps it however small.
	bool is_emissive_light_source;
	/// The chain's coarsest resident level, or header_index when that is the coarsest (SdfInstanceStandaloneDistance).
	uint coarse_header_index;
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
	vec4 lane8 = b_sdf_instances[base + 8u];
	uint flags = uint(lane8.x + 0.5);
	inst.is_two_sided = (flags & 1u) != 0u;
	inst.is_emissive_light_source = (flags & 2u) != 0u;
	inst.axis_scale = lane8.yzw;
	inst.coarse_header_index = uint(b_sdf_instances[base + 9u].x + 0.5);
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
	// Outside the field the reading below is taken at the nearest BOUNDARY point and the two
	// lower bounds are combined at the end (see the CPU transcription, sample_mesh_sdf: the
	// padding bound alone would make every bounding-box face a phantom surface at the coarse
	// cascade levels).
	grid = clamped_grid;
	vec3 brick_coord = clamp(floor(grid / SDF_BRICK_SIZE), vec3_splat(0.0), header.brick_dim - vec3_splat(1.0));
	uint brick_index = uint(brick_coord.x) +
	                   uint(brick_coord.y) * uint(header.brick_dim.x) +
	                   uint(brick_coord.z) * uint(header.brick_dim.x) * uint(header.brick_dim.y);
	uint entry = b_sdf_indirection[uint(header.indirection_offset) + brick_index];
	if((entry & SDF_INDIRECTION_EMPTY_FLAG) != 0u)
	{
		float distance = float(entry & SDF_INDIRECTION_DISTANCE_MASK) * header.voxel_size;
		distance = (entry & SDF_INDIRECTION_INSIDE_FLAG) != 0u ? -distance : distance;
		return outside_distance > 0.0
		           ? max(outside_distance + SDF_ENCODE_RANGE * header.voxel_size, distance - outside_distance)
		           : distance;
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
	float distance = distance_voxels * header.voxel_size;
	return outside_distance > 0.0
	           ? max(outside_distance + SDF_ENCODE_RANGE * header.voxel_size, distance - outside_distance)
	           : distance;
}

/// Signed distance from a point at the absolute per-axis @p offset from a box's centre to the box of half extent
/// @p extent, each axis scaled by @p scale: exact in world units for a scaled, rotated box.
float SdfScaledBoxDistance(vec3 offset, vec3 extent, vec3 scale)
{
	vec3 to_box = (offset - extent) * scale;
	return length(max(to_box, vec3_splat(0.0))) + min(max(to_box.x, max(to_box.y, to_box.z)), 0.0);
}

/**
 * World distance from an instance's surface at the LOCAL position @p local_position, for the composed fields (the
 * global distance field and the Lumen object grid; UE DistanceToNearestSurfaceForObject, MeshDistanceFieldCommon.ush).
 * MIRROR OF sample_instance_distance (mesh_sdf_baker.cpp).
 *
 * A local distance times one scale is exact only along that scale's axis. With the smallest axis (the conservative
 * choice) a wall scaled (10, 3, 0.2) reads 50x too close past its ends, so stretched primitives swell along their
 * long axes and close the gaps between them. Two per-axis lower bounds keep the result tight and conservative: the
 * world distance to the box the surface lies in (header lanes 3-4, the baked geometry's box; the field's bounds
 * pad it unevenly), and outside the bounds the field at the nearest boundary point less the world distance to it
 * (1-Lipschitz). Inside the bounds the field is converted with the smallest axis scale, and a negative reading
 * there stands (UE's volume-box term): an open sheet signed by the bake's vote is solid beyond its own box. @p sheet
 * reads a two-sided field as the zero-thickness sheet it represents (the stored shell plus its half thickness); read
 * as stored, a shell reaches its thickness past the box.
 */
float SdfInstanceWorldDistance(SdfInstance inst, SdfHeader header, vec3 local_position, bool sheet)
{
	uint header_base = inst.header_index * uint(SDF_HEADER_STRIDE);
	vec3 surface_min = b_sdf_headers[header_base + 3u].xyz;
	vec3 surface_max = b_sdf_headers[header_base + 4u].xyz;
	vec3 surface_extent = 0.5 * (surface_max - surface_min) + vec3_splat(sheet ? 0.0 : header.two_sided_thickness);
	vec3 surface_offset = abs(local_position - 0.5 * (surface_min + surface_max));
	float to_surface_box = SdfScaledBoxDistance(surface_offset, surface_extent, inst.axis_scale);
	vec3 extent = 0.5 * header.grid_dim * header.voxel_size;
	vec3 offset = abs(local_position - (header.bounds_min + extent));
	float to_bounds = SdfScaledBoxDistance(offset, extent, inst.axis_scale);
	vec3 boundary = clamp(local_position, header.bounds_min, header.bounds_min + 2.0 * extent);
	float field = SdfSampleLocal(header, boundary) + (sheet ? header.two_sided_thickness : 0.0);
	if(to_bounds <= 0.0 && field < 0.0)
	{
		return field * inst.local_to_world_scale;
	}
	return max(field * inst.local_to_world_scale - max(to_bounds, 0.0), to_surface_box);
}

/// World reach of a level's exact readings around the instance: its encoded band less the half voxel the filter
/// blends with saturated neighbours, plus a two-sided level's shell.
float SdfInstanceExactReach(SdfInstance inst, SdfHeader header)
{
	return ((SDF_ENCODE_RANGE - 0.5) * header.voxel_size + header.two_sided_thickness) * inst.local_to_world_scale;
}

/**
 * UE's DistanceToMeshSurfaceStandalone (DistanceFieldLightingShared.ush:452-471) over the two resident levels: the
 * coarsest answers where it reads farther than a quarter of the distance it encodes (one of its voxels), the traced
 * level nearer the surface. The traced level's narrow band saturates short of the Lumen cascade's coverage band; the
 * coarsest level's reaches it. x = the world distance (SdfInstanceWorldDistance), y = the answering level's exact
 * reach (SdfInstanceExactReach).
 */
vec2 SdfInstanceStandaloneDistance(SdfInstance inst, SdfHeader header, vec3 local_position, bool sheet)
{
	BRANCH
	if(inst.coarse_header_index != inst.header_index)
	{
		SdfHeader coarse = SdfLoadHeader(inst.coarse_header_index);
		float coarse_distance = SdfInstanceWorldDistance(inst, coarse, local_position, sheet);
		if(abs(coarse_distance) > 0.25 * SDF_ENCODE_RANGE * coarse.voxel_size * inst.local_to_world_scale)
		{
			return vec2(coarse_distance, SdfInstanceExactReach(inst, coarse));
		}
	}
	return vec2(SdfInstanceWorldDistance(inst, header, local_position, sheet), SdfInstanceExactReach(inst, header));
}

/// World radius of the sphere around the instance's geometry box (UE DFObjectBounds.SphereRadius).
float SdfInstanceWorldRadius(SdfInstance inst)
{
	uint header_base = inst.header_index * uint(SDF_HEADER_STRIDE);
	vec3 surface_half = 0.5 * (b_sdf_headers[header_base + 4u].xyz - b_sdf_headers[header_base + 3u].xyz);
	return length(surface_half * inst.axis_scale);
}

/**
 * Whether a Lumen cascade level of voxel size @p voxel holds the instance (UE CullObjectsToClipmapCS,
 * GlobalDistanceField.usf:125): an object whose bounding sphere is no larger than max(@p min_radius,
 * @p min_radius_voxels voxels) is left out, unless an emissive light source.
 */
bool SdfLumenCascadeKeepsInstance(SdfInstance inst, float voxel, float min_radius, float min_radius_voxels)
{
	return inst.is_emissive_light_source || SdfInstanceWorldRadius(inst) > max(min_radius, min_radius_voxels * voxel);
}

/**
 * Gradient of the field at a local-space position, giving the surface normal.
 *
 * FOUR tetrahedral taps rather than six central differences. The four corners of a tetrahedron
 * sample the directional derivative along four directions whose sum cancels every axis bias, so
 * the reconstructed gradient matches central differences to well within the field's own R8
 * quantisation -- and gradients are the bulk of every surface resolve, which is the bulk of the
 * pass's fixed cost, so a third fewer taps here is a third off the most-run code in GI.
 */
vec3 SdfGradientLocal(SdfHeader header, vec3 local_position)
{
	float e = header.voxel_size;
	vec3 k0 = vec3(1.0, -1.0, -1.0);
	vec3 k1 = vec3(-1.0, -1.0, 1.0);
	vec3 k2 = vec3(-1.0, 1.0, -1.0);
	vec3 k3 = vec3(1.0, 1.0, 1.0);
	vec3 gradient = k0 * SdfSampleLocal(header, local_position + k0 * e) +
	                k1 * SdfSampleLocal(header, local_position + k1 * e) +
	                k2 * SdfSampleLocal(header, local_position + k2 * e) +
	                k3 * SdfSampleLocal(header, local_position + k3 * e);
	float len = length(gradient);
	return len > 1e-8 ? gradient / len : vec3(0.0, 1.0, 0.0);
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
	/// The RAW field reading at the accepted point, in world units (0 for a miss). The march
	/// accepts a hit bias + expand voxels short of the surface, and no surface lies within
	/// this reading of that point, so t + hit_field never exceeds the distance along the ray
	/// to the first surface: a consumer that reads a surface store (the cards) steps onto the
	/// surface with it.
	float hit_field;
	/// Instance that produced the hit, or SDF_NO_INSTANCE when the global cascade answered: the
	/// cascade is composed from many fields at once and cannot attribute a sample to one of them.
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
 * worse than the fade it would cure. Widening the acceptance can only ever stop a ray
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
	result.hit_field = 0.0;
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
                     bool resumed, inout SdfRayHit result)
{
	// Bounds FIRST, and only the bounds. The full instance record is ten vec4s and most
	// candidates in a dense cell are rejected right here -- the grid deliberately over-reports,
	// and the per-cell walk revisits instances that span cells -- so loading everything up front
	// would pay five times the buffer traffic the reject needs. This loop is the single hottest
	// thing in the GI frame; the two redundant reads the accepted path repeats inside
	// SdfLoadInstance are noise beside what the rejected paths skip.
	uint bounds_base = uint(index) * uint(SDF_INSTANCE_STRIDE);
	vec4 bounds0 = b_sdf_instances[bounds_base + 6u];
	vec4 bounds1 = b_sdf_instances[bounds_base + 7u];
	float t_near;
	float t_far;
	// Broad phase against the instance bounds, capped by the best hit so far so a nearer result
	// short-circuits everything behind it. This is also what makes the duplicate visits the grid
	// allows cheap: the second visit to an instance already behind a hit rejects immediately.
	if(!SdfIntersectBounds(origin, inv_dir, bounds0.xyz, bounds1.xyz,
	                       min(result.t, t_max), t_near, t_far))
	{
		return;
	}
	t_near = max(t_near, t_min);
	if(t_near > t_far)
	{
		return;
	}
	SdfInstance inst = SdfLoadInstance(index);
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
	// Ceiling on the acceptance radius: ONE voxel of this field, mirroring the clipmap tier's cap.
	//
	// The correctness bound is the encode range -- the field saturates at SDF_ENCODE_RANGE voxels,
	// so a cone grown past that makes every saturated sample read as a hit and the instance's
	// whole bounding box renders solid. The useful bound is far tighter, for the reason the
	// clipmap tier's comment lays out: a cone radius is over-occlusion by construction, and the
	// ray it hurts most is one grazing along the surface it STARTED on. At four voxels a grazing
	// ray a fraction of a voxel above its own wall keeps being caught however far it travels --
	// over-darkening that grows with bake coarseness -- and the suppression escape below, which
	// compares against this same radius, becomes unreachable. One voxel bounds the damage to
	// geometry the field genuinely cannot resolve anyway. The comparison below is strict, so the
	// cap also keeps a saturated sample from ever reading as a hit.
	float accept_ceiling = header.voxel_size * inst.local_to_world_scale;
	// Launch-surface suppression: a ray that STARTS inside this field's hit-acceptance band must
	// see clear space before this instance may claim a hit.
	//
	// Every gather, bounce and shadow ray is born ON a surface, and when that surface's own field
	// answers here, the first sample is a hit BY CONSTRUCTION -- nothing occludes the ray; the
	// launch surface occludes itself. For large open submeshes the effect is total: the unsigned
	// shell is floored at one voxel and a street-sized sheet's voxel sits at the max_voxel_size
	// clamp, so the field is a slab about A METRE thick around the walkable surface and every ray
	// on it dies at t = 0. The origin biases cannot clear this, because they are measured in
	// CASCADE voxels while this acceptance is measured in MESH voxels -- two unrelated units.
	// Without the suppression the symptoms are GI black pools with edges following SUBMESH seams,
	// worse near the camera (the biases grow with the answering cascade level and eventually
	// clear the shell at range), cells converging black (their shadow rays die the same way),
	// and immunity to every origin-side knob.
	//
	// Derived from the RAY ORIGIN, not the segment start, so the duplicated per-cell visits the
	// grid walk makes re-derive it identically, and an instance entered further along the ray --
	// a genuine occluder -- is never suppressed. A SIGNED field reading clearly negative is real
	// burial in solid geometry and hits immediately; only the on-surface band (and a shell's
	// interior, which has no inside) walks out. test_ray_from_open_sheet_escapes_its_own_shell
	// pins all four cases.
	bool suppressed = false;
	// Highest reading seen while suppressed, for the re-descent test below.
	float suppress_best = -1e8;
	// Steps spent walking out of the launch band this visit. The escape test compares against
	// the CONE radius, which grows with t, so a grazing ray hugging its own launch surface holds
	// a near-constant reading while the exit recedes from it: the walk cannot terminate, and
	// unbounded it would burn the entire step budget -- again in EVERY grid cell that lists the
	// launch instance, since suppression re-arms per visit. Grazing rays are half of every
	// hemisphere, which would make this the dominant near-field cost. The budget bounds it: on
	// exhaustion the visit gives up and treats the instance as non-occluding for the REST OF THIS
	// CELL SEGMENT only -- the next cell re-arms and re-tests, so a genuine fold of the same
	// instance further along is missed at most within one segment, erring bright at bounded
	// scope where the alternative burns unbounded steps for the same answer.
	int suppress_steps = 0;
	if(all(greaterThanEqual(origin, inst.world_bounds_min)) &&
	   all(lessThanEqual(origin, inst.world_bounds_max)))
	{
		float origin_distance = SdfSampleLocal(header, local_origin) * inst.local_to_world_scale;
		bool two_sided = header.two_sided_thickness > 0.0;
		// A RESUMED ray (t_min > 0) starts in space a screen march verified empty: a first
		// sample inside a band there is a genuine occluder, never the launch surface, and
		// walking out of it would tunnel.
		suppressed = !resumed && origin_distance < hit_threshold &&
		             (two_sided || origin_distance > -hit_threshold);
	}
	float t = t_near;
	bool resolved = false;
	// LOOP on every march in this file: max_steps is a compile-time constant at most call
	// sites after inlining, and fxc then attempts to fully unroll a ~100-line body 64 times
	// per instantiation - nested inside the grid walk and duplicated per caller, a large share
	// of the GI shaders' compile time. Divergent early-exit marches gain nothing from unrolling
	// at runtime.
	LOOP
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
		float accept = min(SdfConeRadius(t, hit_threshold, relaxation), accept_ceiling);
		if(suppressed)
		{
			if(world_distance >= accept)
			{
				suppressed = false;
			}
			// RE-DESCENT while escaping means a NEW surface, not the launch one. The walk out of
			// the launch band sees a monotonically rising distance; if the reading rose and then
			// drops by more than a voxel, the ray has crossed into a different fold of this field
			// -- an L-shaped submesh's other wing, a wall of the same merged sheet -- and walking
			// on would TUNNEL through it. Falling through to the hit test occludes instead, which
			// errs dark rather than leaking light through geometry.
			else if(world_distance < suppress_best - header.voxel_size * inst.local_to_world_scale)
			{
				suppressed = false;
			}
			else if(header.two_sided_thickness > 0.0 || world_distance > -hit_threshold)
			{
				// Still in the launch band. |distance| is the distance to the shell boundary, so
				// stepping by it converges on the exit without ever crossing it -- the same
				// Lipschitz argument the ordinary march rests on, pointed outward.
				++suppress_steps;
				if(suppress_steps > SDF_SUPPRESS_MAX_STEPS)
				{
					// Give up: the escape is receding faster than the walk approaches it (see the
					// suppress_steps note above). `resolved` marks a deliberate finish, not budget
					// exhaustion.
					resolved = true;
					break;
				}
				suppress_best = max(suppress_best, world_distance);
				t += max(abs(world_distance), hit_threshold);
				continue;
			}
			// Signed and clearly negative: genuinely inside solid geometry. Fall through to the
			// hit test, which accepts it -- that burial is real occlusion, not a launch artefact.
		}
		if(world_distance < accept)
		{
			if(t < result.t || !result.hit)
			{
				result.hit = true;
				result.t = t;
				result.hit_field = max(world_distance, 0.0);
				// The accepted LOCAL point: the walk derives the normal once it has settled its
				// winner (SdfDeriveInstanceNormal), so a hit a nearer instance replaces later in the
				// walk never pays for a gradient, and no lane stalls its wave on one mid-walk.
				if(want_normal)
				{
					result.normal = local_position;
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

/// The world normal of a per-instance walk's winning hit, from the local point SdfTestInstance left in
/// result.normal.
SdfRayHit SdfDeriveInstanceNormal(SdfRayHit result, bool want_normal)
{
	BRANCH
	if(want_normal && result.hit)
	{
		SdfInstance inst = SdfLoadInstance(result.instance_index);
		SdfHeader header = SdfLoadHeader(inst.header_index);
		vec3 local_normal = SdfGradientLocal(header, result.normal);
		result.normal = normalize(SdfTransformDirection(inst.local_to_world_rows, local_normal));
	}
	return result;
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
/// @p resumed: the ray starts at @p t_min in space a screen march verified empty - the
/// launch-band walk-out is off (see SdfTestInstance).
SdfRayHit SdfTraceInstancesEx(vec3 origin, vec3 direction, float t_min, float t_max, int max_steps,
                              float surface_bias, float relaxation, bool want_normal, bool resumed)
{
	SdfRayHit result = SdfMakeMiss();
	result.t = t_max;
	vec3 inv_dir = 1.0 / max(abs(direction), vec3_splat(1e-8)) * sign(direction + vec3_splat(1e-20));
	if(!u_sdf_grid_enabled)
	{
		// No grid this frame (nothing resident, or the upload failed). Testing everything is the
		// slow answer, not a wrong one, and it keeps the tier working rather than silently
		// dropping every instance.
		LOOP
		for(int i = 0; i < u_sdf_instance_count; ++i)
		{
			SdfTestInstance(i, origin, direction, inv_dir, t_min, t_max, max_steps, surface_bias,
			                relaxation, want_normal, resumed, result);
		}
		return SdfDeriveInstanceNormal(result, want_normal);
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
	// Where the ray enters the CURRENT cell. Each instance test is clamped to this cell's segment
	// rather than given the whole ray.
	//
	// An instance is listed in every cell its bounds touch, so handing it [t_min, t_max] in each of
	// them would re-trace the identical range once per cell: a building-sized submesh spanning ten
	// cells would pay ten full sphere traces from the same starting point. The per-instance broad
	// phase only rejects the repeats once a hit exists, so the duplication would be worst for rays
	// that do NOT hit early -- the grazing case that already dominates this tier's cost.
	//
	// Clamping is safe on the same invariant the break below already relies on: cells are visited in
	// increasing t with disjoint, contiguous segments, and an instance appears in every cell its
	// bounds touch, so the union of its per-cell segments still covers its whole overlap with the
	// ray. A trace that runs out of cell resumes in the next one, so no gap opens at a boundary.
	//
	// It also bounds per-instance cost without a separate budget: a visit can only cover one cell's
	// worth of distance, which largely avoids the "max_steps PER INSTANCE" blowup on its own.
	float t_cell_enter = t_enter;
	// LOOP: the walk body carries the whole per-instance sphere trace (see the compile-time
	// note at the mesh march above).
	LOOP
	for(int visited = 0; visited < SDF_GRID_MAX_STEPS; ++visited)
	{
		float t_step = min(t_next.x, min(t_next.y, t_next.z));
		// The segment this cell owns, clipped to the ray. Computed BEFORE the instance loop, which
		// is the only reason the tests can be bounded by it.
		float t_cell_min = max(t_min, t_cell_enter);
		float t_cell_max = min(t_max, min(t_step, t_exit));
		int cell_index = int(cell_f.x + cell_f.y * dim.x + cell_f.z * dim.x * dim.y);
		uint begin = b_sdf_grid[cell_index] + u_sdf_grid_instance_base;
		uint end = b_sdf_grid[cell_index + 1] + u_sdf_grid_instance_base;
		LOOP
		for(uint entry_index = begin; entry_index < end; ++entry_index)
		{
			SdfTestInstance(int(b_sdf_grid[entry_index]), origin, direction, inv_dir,
			                t_cell_min, t_cell_max, max_steps, surface_bias, relaxation, want_normal,
			                resumed, result);
		}
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
		// The cell just left ended here, so the next one begins here. Contiguous by construction,
		// which is what makes the per-cell clamping above lossless.
		t_cell_enter = t_step;
		if(any(lessThan(cell_f, vec3_splat(0.0))) || any(greaterThan(cell_f, dim - vec3_splat(1.0))))
		{
			break;
		}
	}
	return SdfDeriveInstanceNormal(result, want_normal);
}

SdfRayHit SdfTraceInstances(vec3 origin, vec3 direction, float t_min, float t_max, int max_steps,
                            float surface_bias, float relaxation, bool want_normal)
{
	return SdfTraceInstancesEx(origin, direction, t_min, t_max, max_steps, surface_bias, relaxation,
	                           want_normal, false);
}

#endif // __GI_SDF_COMMON_SH__
