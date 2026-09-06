/*
 * GTAO depth prefilter: converts the G-buffer depth to view-space depth at the AO
 * resolution (mip 0) and builds four coarser mips with the effect's own falloff-weighted
 * filter (GtaoDepthMipFilter). One 8x8 group produces a 16x16 tile of mip 0 and its
 * 8x8 / 4x4 / 2x2 / 1x1 reductions through group-shared memory - the XeGTAO layout.
 */

#include "../bgfx_compute.sh"
#include "../common.sh"
#include "gtao_common.sh"

/// Full-resolution G-buffer depth.
SAMPLER2D(s_gtao_depth, 0);
IMAGE2D_WO(i_gtao_depth_mip0, r32f, 1);
IMAGE2D_WO(i_gtao_depth_mip1, r32f, 2);
IMAGE2D_WO(i_gtao_depth_mip2, r32f, 3);
IMAGE2D_WO(i_gtao_depth_mip3, r32f, 4);
IMAGE2D_WO(i_gtao_depth_mip4, r32f, 5);

SHARED float s_gtao_tile[8][8];

/// View depth of the AO-resolution texel: the full-resolution texel the divisor maps it to.
float GtaoLoadViewDepth(ivec2 ao_texel)
{
	float device_depth = texelFetch(s_gtao_depth, GtaoFullTexel(ao_texel), 0).x;
	return GtaoViewDepthFromDevice(device_depth);
}

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 group_base = ivec2(gl_WorkGroupID.xy) * 16;
	ivec2 local = ivec2(gl_LocalInvocationID.xy);
	ivec2 base = group_base + local * 2;
	ivec2 size0 = ivec2(u_gtao_size.xy);
	// Mip 0: the 2x2 texels this thread owns (out-of-range texels read clamped, never stored).
	float d00 = GtaoLoadViewDepth(min(base + ivec2(0, 0), size0 - ivec2(1, 1)));
	float d10 = GtaoLoadViewDepth(min(base + ivec2(1, 0), size0 - ivec2(1, 1)));
	float d01 = GtaoLoadViewDepth(min(base + ivec2(0, 1), size0 - ivec2(1, 1)));
	float d11 = GtaoLoadViewDepth(min(base + ivec2(1, 1), size0 - ivec2(1, 1)));
	if(all(lessThan(base + ivec2(0, 0), size0))) imageStore(i_gtao_depth_mip0, base + ivec2(0, 0), vec4(d00, 0.0, 0.0, 0.0));
	if(all(lessThan(base + ivec2(1, 0), size0))) imageStore(i_gtao_depth_mip0, base + ivec2(1, 0), vec4(d10, 0.0, 0.0, 0.0));
	if(all(lessThan(base + ivec2(0, 1), size0))) imageStore(i_gtao_depth_mip0, base + ivec2(0, 1), vec4(d01, 0.0, 0.0, 0.0));
	if(all(lessThan(base + ivec2(1, 1), size0))) imageStore(i_gtao_depth_mip0, base + ivec2(1, 1), vec4(d11, 0.0, 0.0, 0.0));
	// Mip 1: one texel per thread.
	float m1 = GtaoDepthMipFilter(d00, d10, d01, d11);
	ivec2 size1 = max(size0 / 2, ivec2(1, 1));
	ivec2 t1 = ivec2(gl_WorkGroupID.xy) * 8 + local;
	if(all(lessThan(t1, size1))) imageStore(i_gtao_depth_mip1, t1, vec4(m1, 0.0, 0.0, 0.0));
	s_gtao_tile[local.x][local.y] = m1;
	barrier();
	// Mip 2: the threads with even local coordinates reduce their 2x2 of mip 1.
	if((local.x % 2) == 0 && (local.y % 2) == 0)
	{
		float m2 = GtaoDepthMipFilter(s_gtao_tile[local.x][local.y], s_gtao_tile[local.x + 1][local.y],
		                              s_gtao_tile[local.x][local.y + 1], s_gtao_tile[local.x + 1][local.y + 1]);
		ivec2 size2 = max(size0 / 4, ivec2(1, 1));
		ivec2 t2 = ivec2(gl_WorkGroupID.xy) * 4 + local / 2;
		if(all(lessThan(t2, size2))) imageStore(i_gtao_depth_mip2, t2, vec4(m2, 0.0, 0.0, 0.0));
		s_gtao_tile[local.x][local.y] = m2;
	}
	barrier();
	// Mip 3: every fourth thread.
	if((local.x % 4) == 0 && (local.y % 4) == 0)
	{
		float m3 = GtaoDepthMipFilter(s_gtao_tile[local.x][local.y], s_gtao_tile[local.x + 2][local.y],
		                              s_gtao_tile[local.x][local.y + 2], s_gtao_tile[local.x + 2][local.y + 2]);
		ivec2 size3 = max(size0 / 8, ivec2(1, 1));
		ivec2 t3 = ivec2(gl_WorkGroupID.xy) * 2 + local / 4;
		if(all(lessThan(t3, size3))) imageStore(i_gtao_depth_mip3, t3, vec4(m3, 0.0, 0.0, 0.0));
		s_gtao_tile[local.x][local.y] = m3;
	}
	barrier();
	// Mip 4: one texel per group.
	if(local.x == 0 && local.y == 0)
	{
		float m4 = GtaoDepthMipFilter(s_gtao_tile[0][0], s_gtao_tile[4][0], s_gtao_tile[0][4], s_gtao_tile[4][4]);
		ivec2 size4 = max(size0 / 16, ivec2(1, 1));
		ivec2 t4 = ivec2(gl_WorkGroupID.xy);
		if(all(lessThan(t4, size4))) imageStore(i_gtao_depth_mip4, t4, vec4(m4, 0.0, 0.0, 0.0));
	}
}
