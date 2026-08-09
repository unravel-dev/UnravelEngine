/*
 * One-time zero of the light-voxel volume, dispatched with the buffer seed.
 *
 * The volume's slots zero themselves lazily on FIRST CLAIM (the cell-id sentinel scheme), but
 * texels of slots nothing ever claims are still READ - GiLightVoxelRead filters over 2x2x2
 * neighbourhoods and falls back level by level - and a fresh allocation is only zero where the
 * driver happens to zero it. On drivers that do not (measured: Linux/Vulkan) the garbage
 * half-floats enter the probe<->voxel feedback loop, flash the GI white, and collapse it to
 * NaN black. Reading never-written memory is the defect; this makes every texel written, once.
 */

#include "bgfx_compute.sh"

IMAGE3D_WO(s_clear_volume, rgba16f, 0);

/// xyz = volume dimensions in texels.
uniform vec4 u_gi_volume_clear_params;

NUM_THREADS(4, 4, 4)
void main()
{
	uvec3 dims = uvec3(u_gi_volume_clear_params.xyz);
	if(any(greaterThanEqual(gl_GlobalInvocationID, dims)))
	{
		return;
	}
	imageStore(s_clear_volume, ivec3(gl_GlobalInvocationID), vec4_splat(0.0));
}
