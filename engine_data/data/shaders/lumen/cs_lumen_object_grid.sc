/*
 * Lumen object grid (UE 5.8 GlobalDistanceFieldCompositeObjects.usf CompositeObjectsIntoObjectGridPagesCS:259-367):
 * for every cell of one global SDF clipmap level (2 x 2 x 2 voxels), the four GI instances whose surfaces are
 * nearest the cell centre within reach, nearest first. A global-SDF hit cannot name the mesh it hit; the
 * surface cache shades it through these instances' cards.
 *
 * One thread per cell. Candidates come from the tracer's instance grid over the cells within reach, the same
 * walk the clipmap compose makes; distances are to the zero-thickness sheet of two-sided fields, as the
 * global field is composed. Written as index + 1 (0 = empty), as floats (exact far beyond any instance count).
 */

#include "bgfx_compute.sh"
#include "gi/sdf_common.sh"
#include "lumen/lumen_constants.sh"

IMAGE3D_WO(s_lumen_object_grid_out, rgba32f, 4);

/// x = level, y = cells per axis, z = cell size, w = reach (cell half diagonal + 3 voxel extents).
uniform vec4 u_lumen_object_grid;
/// xyz = the level's origin (minimum corner), w = the scale of the smallest object the level keeps (the clipmap
/// compose's: 1 / the scene detail).
uniform vec4 u_lumen_object_grid_origin;

NUM_THREADS(4, 4, 4)
void main()
{
	ivec3 cell = ivec3(gl_GlobalInvocationID.xyz);
	int resolution = int(u_lumen_object_grid.y);
	if(any(greaterThanEqual(cell, ivec3(resolution, resolution, resolution))))
	{
		return;
	}
	float cell_size = u_lumen_object_grid.z;
	float reach = u_lumen_object_grid.w;
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
						// The objects the cascade composes at this level (cells are 2 x 2 x 2 voxels): UE builds both
						// from one culled object list.
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
	imageStore(s_lumen_object_grid_out, ivec3(cell.x, cell.y, cell.z + level * resolution), vec4(best));
}
