/*
 * Culls the volumetric fog's point and spot lights per cluster of froxels (fog_pass, fog_lights.sh): one thread per
 * cluster writes the mask of the lights whose range sphere reaches the cluster's bounds. A light adds nothing past its
 * range (LocalLightRangeMask), and the bounds hold every point the lighting samples in the cluster (the froxel centres
 * and their shadow taps), so the froxel lighting that reads only the masked lights gives the same image.
 */

#include "../bgfx_compute.sh"

// One mask per cluster (fog_cluster_index): bit i set = light i of u_fog_lights reaches the cluster.
BUFFER_WO(b_fog_light_masks, uint, 0);

#include "fog.sh"
#include "fog_volume.sh"
#include "fog_lights.sh"

/// Grows the bounds @p low..@p high by the grid point at grid coordinate @p coord (0..1 along each axis).
void fog_grow_bounds(vec3 coord, inout vec3 low, inout vec3 high)
{
	vec3 corner = fog_grid_ray(coord.xy) * fog_slice_depth(coord.z);
	low = min(low, corner);
	high = max(high, corner);
}

NUM_THREADS(4, 4, 4)
void main()
{
	ivec3 cluster = ivec3(gl_GlobalInvocationID.xyz);
	ivec3 counts = fog_cluster_counts();
	if(any(greaterThanEqual(vec3(cluster), vec3(counts))))
	{
		return;
	}
	// The cluster's corners in grid coordinates; the cells it spans are linear in uv and monotonic in depth, so the
	// eight corner points bound the whole cluster.
	vec3 grid = u_fog_grid.xyz;
	vec3 cluster_size = vec3(FOG_CLUSTER_COLUMNS, FOG_CLUSTER_COLUMNS, FOG_CLUSTER_SLICES);
	vec3 first = vec3(cluster) * cluster_size / grid;
	vec3 last = min(vec3(cluster + 1) * cluster_size / grid, vec3_splat(1.0));
	vec3 low = vec3_splat(FOG_HALF_MAX);
	vec3 high = vec3_splat(-FOG_HALF_MAX);
	fog_grow_bounds(vec3(first.x, first.y, first.z), low, high);
	fog_grow_bounds(vec3(last.x, first.y, first.z), low, high);
	fog_grow_bounds(vec3(first.x, last.y, first.z), low, high);
	fog_grow_bounds(vec3(last.x, last.y, first.z), low, high);
	fog_grow_bounds(vec3(first.x, first.y, last.z), low, high);
	fog_grow_bounds(vec3(last.x, first.y, last.z), low, high);
	fog_grow_bounds(vec3(first.x, last.y, last.z), low, high);
	fog_grow_bounds(vec3(last.x, last.y, last.z), low, high);
	uint mask = 0u;
	int count = int(u_fog_light_info.x);
	for(int i = 0; i < FOG_MAX_LOCAL_LIGHTS; ++i)
	{
		if(i >= count)
		{
			break;
		}
		vec4 placement = u_fog_lights[i * FOG_LOCAL_LIGHT_VEC4S];
		float range = 1.0 / placement.w;
		// The squared distance from the light to the nearest point of the bounds.
		vec3 outside = max(max(low - placement.xyz, placement.xyz - high), vec3_splat(0.0));
		if(dot(outside, outside) < range * range)
		{
			mask |= 1u << uint(i);
		}
	}
	b_fog_light_masks[fog_cluster_index(cluster)] = mask;
}
