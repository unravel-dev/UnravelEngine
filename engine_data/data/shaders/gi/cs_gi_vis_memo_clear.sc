/*
 * One-time zero of the bounce visibility memo (gi_light_voxels_kernel.sh). Generation 0 is
 * reserved as "never stamped" and the CPU hands out 1..63, so a zeroed texel can never match
 * a live generation - without this clear, allocation garbage could masquerade as a stamped
 * mask on backends that hand out non-zero memory (measured on Linux/Vulkan for the light
 * volume, the same defect family).
 */

#include "bgfx_compute.sh"

UIMAGE3D_WO(s_gi_vis_memo_out, r32ui, 0);

/// xyz = memo volume dimensions in texels.
uniform vec4 u_gi_vis_memo_clear_params;

NUM_THREADS(4, 4, 4)
void main()
{
	ivec3 texel = ivec3(gl_GlobalInvocationID.xyz);
	if(texel.x >= int(u_gi_vis_memo_clear_params.x) || texel.y >= int(u_gi_vis_memo_clear_params.y) ||
	   texel.z >= int(u_gi_vis_memo_clear_params.z))
	{
		return;
	}
	imageStore(s_gi_vis_memo_out, texel, uvec4(0u, 0u, 0u, 0u));
}
