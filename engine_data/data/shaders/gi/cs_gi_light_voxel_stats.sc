/*
 * Copies the relight convergence statistic out of the bounce vis-memo texture's statistics
 * slice (GiLightVoxelStatsTexel) into a small 2D image the CPU reads back through a staging
 * blit, and zeroes the slice for the next frame's accumulation. One thread per texel; see the
 * group reduction in gi_light_voxels_kernel.sh and gi_light_voxel_pass::collect_relight_stats.
 */

#include "bgfx_compute.sh"
#include "gi/gi_constants.sh"
#include "gi/sdf_common.sh"
#include "gi/gi_light_voxels.sh"

UIMAGE3D_RW(s_gi_vis_memo, r32ui, 0);
UIMAGE2D_WO(s_gi_stats_out, r32ui, 1);

/// u_gi_light_voxel_params.y = 1 on the per-frame readback path (the copy IS the drain), 0
/// for an on-demand census snapshot, which must leave the slice for the gate to drain.
#define u_stats_zero_after_copy (u_gi_light_voxel_params.y > 0.5)

// Every (level, quantity) texel of the slice: 4 x GI_STATS_QUANTITY_COUNT fits one group.
NUM_THREADS(64, 1, 1)
void main()
{
	int index = int(gl_LocalInvocationID.x);
	int level = index % SDF_CLIPMAP_LEVEL_COUNT;
	int quantity = index / SDF_CLIPMAP_LEVEL_COUNT;
	if(level >= SDF_CLIPMAP_LEVEL_COUNT || quantity >= GI_STATS_QUANTITY_COUNT)
	{
		return;
	}
	ivec3 texel = GiLightVoxelStatsTexel(level, quantity);
	uint value = imageLoad(s_gi_vis_memo, texel).x;
	imageStore(s_gi_stats_out, ivec2(level, quantity), uvec4(value, 0u, 0u, 0u));
	if(u_stats_zero_after_copy)
	{
		imageStore(s_gi_vis_memo, texel, uvec4(0u, 0u, 0u, 0u));
	}
}
