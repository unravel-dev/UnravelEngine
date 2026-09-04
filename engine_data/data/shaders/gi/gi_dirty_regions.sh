#ifndef __GI_DIRTY_REGIONS_SH__
#define __GI_DIRTY_REGIONS_SH__

/// DIRTY REGIONS - where an instance moved, appeared, vanished or changed material within the
/// last GI_TEMPORAL_DIRTY_HOLD_FRAMES (surface_cache_system::get_dirty_regions; an emissive
/// placement's region is inflated to its light's reach). x = how many (min, max) pairs of
/// u_gi_temporal_bounds are live, y = the soft margin around each in metres (one level-0
/// probe spacing: the reach of a small mover's bounce pool), z = the camera's motion this
/// frame (gi_temporal_kernel.sh). Two consumers: inside a region the temporal's slow lane
/// collapses to the fast cap so the stale light flushes, and the screen probe trace stops
/// reading last frame's composite at hits inside it - that read carries the accumulated
/// glow back into the gather, and with the lane's memory on top the residual of a moved
/// emissive decayed over seconds. The old trigger was the content epoch - GLOBAL, so one
/// cube oscillating 30 m away pinned every pixel at the fast cap (measured: static-floor
/// noise doubled in a still shot).
uniform vec4 u_gi_temporal_dirty;
uniform vec4 u_gi_temporal_bounds[GI_TEMPORAL_DIRTY_MAX_BOUNDS * 2];

/// 1 inside a dirty region, fading to 0 over the margin outside its box.
float GiDirtyRegionFactor(vec3 world_position)
{
	int region_count = int(u_gi_temporal_dirty.x);
	float margin = max(u_gi_temporal_dirty.y, 1e-3);
	float factor = 0.0;
	LOOP
	for(int i = 0; i < GI_TEMPORAL_DIRTY_MAX_BOUNDS; ++i)
	{
		if(i >= region_count)
		{
			break;
		}
		vec3 region_min = u_gi_temporal_bounds[i * 2].xyz;
		vec3 region_max = u_gi_temporal_bounds[i * 2 + 1].xyz;
		vec3 outside = max(max(region_min - world_position, world_position - region_max), vec3_splat(0.0));
		factor = max(factor, saturate(1.0 - length(outside) / margin));
	}
	return factor;
}

#endif // __GI_DIRTY_REGIONS_SH__
