/*
 * The GI object grid: for every cell of one global SDF clipmap level (2 x 2 x 2 voxels), the four GI instances
 * whose surfaces are nearest the cell centre within reach, nearest first. A global-SDF hit cannot name the mesh it
 * hit; the surface cache shades it through these instances' cards.
 *
 * One thread per cell. Candidates come from the tracer's instance grid over the cells within reach, the same
 * walk the clipmap compose makes; distances are to the zero-thickness sheet of two-sided fields, as the
 * global field is composed. Written as index + 1 (0 = empty) in a 16-bit unorm channel, id / LUMEN_OBJECT_GRID_MAX_ID,
 * which holds every id up to LUMEN_OBJECT_GRID_MAX_ID exactly.
 *
 * A cell whose eight voxels of the level just composed all lie farther from every surface than the reach plus the
 * voxel centres' offset from the cell centre (and half an R8 step) holds no instance and skips the walk: the composed
 * distance is the minimum over the same instances, so no surface is within reach, and no hit lands there.
 */

#include "bgfx_compute.sh"
#include "gi/sdf_common.sh"
#include "lumen/lumen_constants.sh"

IMAGE3D_WO(s_lumen_object_grid_out, rgba16, 5);

/// x = level, y = cells per axis, z = cell size, w = reach (cell half diagonal + 3 voxel extents).
uniform vec4 u_lumen_object_grid;
/// xyz = the level's origin (minimum corner), w = the scale of the smallest object the level keeps (the clipmap
/// compose's: 1 / the scene detail).
uniform vec4 u_lumen_object_grid_origin;
/// The cell boxes this dispatch rebuilds (gi/brick_dispatch.sh, in cells): the whole level, or the cells within reach
/// of a partial recompose's boxes (global_sdf_clipmap::level::partial_boxes).
#define BRICK_DISPATCH_STAGE 7
#include "gi/brick_dispatch.sh"

/// The distance from a cell's centre to its voxels' centres, in voxels: sqrt(3) / 2.
#define LUMEN_OBJECT_GRID_CORNER_OFFSET_VOXELS 0.8660254

NUM_THREADS(4, 4, 4)
void main()
{
	BrickDispatchVoxel target = BrickDispatchFindVoxel(gl_WorkGroupID, gl_LocalInvocationID);
	if(!target.valid)
	{
		return;
	}
	ivec3 cell = target.voxel;
	int resolution = int(u_lumen_object_grid.y);
	if(any(greaterThanEqual(cell, ivec3(resolution, resolution, resolution))))
	{
		return;
	}
	float cell_size = u_lumen_object_grid.z;
	float reach = u_lumen_object_grid.w;
	// The cell's eight voxels of the level (s_sdf_clipmap, this level's slab).
	ivec3 first_voxel = cell * 2 + ivec3(0, 0, int(u_lumen_object_grid.x) * 2 * resolution);
	float nearest_voxels = u_sdf_clipmap_encode_range;
	for(int corner = 0; corner < 8; ++corner)
	{
		ivec3 voxel = first_voxel + ivec3(corner & 1, (corner >> 1) & 1, corner >> 2);
		float encoded = texelFetch(s_sdf_clipmap, voxel, 0).x;
		nearest_voxels = min(nearest_voxels, (encoded - 0.5) * (2.0 * u_sdf_clipmap_encode_range));
	}
	float voxel_size = 0.5 * cell_size;
	float empty_voxels = reach / voxel_size + LUMEN_OBJECT_GRID_CORNER_OFFSET_VOXELS + u_sdf_clipmap_encode_range / 255.0;
	if(nearest_voxels >= empty_voxels)
	{
		imageStore(s_lumen_object_grid_out, ivec3(cell.x, cell.y, cell.z + int(u_lumen_object_grid.x) * resolution),
		           vec4_splat(0.0));
		return;
	}
	vec3 center = u_lumen_object_grid_origin.xyz + (vec3(cell) + vec3_splat(0.5)) * cell_size;
	vec4 best_distance = vec4_splat(reach);
	uvec4 best = uvec4(0u, 0u, 0u, 0u);
	if(u_sdf_grid_enabled)
	{
		vec3 inv_cell = vec3_splat(1.0) / vec3_splat(u_sdf_grid_cell_size);
		vec3 last_cell = u_sdf_grid_dim - vec3_splat(1.0);
		ivec3 lo = ivec3(clamp(floor((center - vec3_splat(reach) - u_sdf_grid_origin) * inv_cell), vec3_splat(0.0), last_cell));
		ivec3 hi = ivec3(clamp(floor((center + vec3_splat(reach) - u_sdf_grid_origin) * inv_cell), vec3_splat(0.0), last_cell));
		int dim_x = int(u_sdf_grid_dim.x);
		int dim_xy = int(u_sdf_grid_dim.x * u_sdf_grid_dim.y);
		for(int cz = lo.z; cz <= hi.z; ++cz)
		{
			for(int cy = lo.y; cy <= hi.y; ++cy)
			{
				for(int cx = lo.x; cx <= hi.x; ++cx)
				{
					uint grid_cell = uint(cx + cy * dim_x + cz * dim_xy);
					uint candidate_begin = b_sdf_grid[grid_cell] + u_sdf_grid_instance_base;
					uint candidate_end = b_sdf_grid[grid_cell + 1u] + u_sdf_grid_instance_base;
					for(uint candidate = candidate_begin; candidate < candidate_end; ++candidate)
					{
						int index = int(b_sdf_grid[candidate]);
						uint id = uint(index) + 1u;
						// An instance listed in several grid cells is met more than once.
						if(id == best.x || id == best.y || id == best.z || id == best.w)
						{
							continue;
						}
						SdfInstance inst = SdfLoadInstance(index);
						// Only the objects the cascade composes at this level (cells are 2 x 2 x 2 voxels), so the grid
						// lists the same objects the field holds.
						if(!SdfLumenCascadeKeepsInstance(inst,
						                                 0.5 * cell_size,
						                                 LUMEN_GLOBAL_SDF_MIN_OBJECT_RADIUS * u_lumen_object_grid_origin.w,
						                                 LUMEN_GLOBAL_SDF_MIN_OBJECT_RADIUS_VOXELS * u_lumen_object_grid_origin.w))
						{
							continue;
						}
						vec3 clamped = clamp(center, inst.world_bounds_min, inst.world_bounds_max);
						if(length(center - clamped) >= best_distance.w)
						{
							continue;
						}
						SdfHeader header = SdfLoadHeader(inst.header_index);
						vec3 local_position = SdfTransformPoint(inst.world_to_local_rows, center);
						float d = abs(SdfInstanceStandaloneDistance(inst, header, local_position, true).x);
						if(d >= best_distance.w)
						{
							continue;
						}
						// Insert into the sorted four.
						if(d < best_distance.z)
						{
							best_distance.w = best_distance.z;
							best.w = best.z;
							if(d < best_distance.y)
							{
								best_distance.z = best_distance.y;
								best.z = best.y;
								if(d < best_distance.x)
								{
									best_distance.y = best_distance.x;
									best.y = best.x;
									best_distance.x = d;
									best.x = id;
								}
								else
								{
									best_distance.y = d;
									best.y = id;
								}
							}
							else
							{
								best_distance.z = d;
								best.z = id;
							}
						}
						else
						{
							best_distance.w = d;
							best.w = id;
						}
					}
				}
			}
		}
	}
	int level = int(u_lumen_object_grid.x);
	uvec4 ids = min(best, uvec4_splat(uint(LUMEN_OBJECT_GRID_MAX_ID)));
	imageStore(s_lumen_object_grid_out, ivec3(cell.x, cell.y, cell.z + level * resolution),
	           vec4(ids) * (1.0 / float(LUMEN_OBJECT_GRID_MAX_ID)));
}
