/*
 * One-time zero of the world-probe atlases (radiance, irradiance, depth), dispatched with the
 * buffer seed - the 2D companion of cs_gi_volume_clear.sc, and for the same reason: probe
 * slots zero their strata lazily on first claim, but the reads (bilinear taps, tile gutters,
 * scroll-in seeding from the parent cascade) touch texels of probes that were never traced,
 * and those are only zero where the driver zero-fills fresh allocations. One dispatch covers
 * all three atlases; the radiance atlas is the larger grid, the two gutter atlases share
 * dimensions.
 */

#include "bgfx_compute.sh"

IMAGE2D_WO(s_clear_radiance, rgba16f, 0);
IMAGE2D_WO(s_clear_irradiance, rgba16f, 1);
IMAGE2D_WO(s_clear_depth, rg16f, 2);

/// xy = radiance atlas dimensions, zw = irradiance/depth (gutter) atlas dimensions.
uniform vec4 u_gi_atlas_clear_params;

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
	if(all(lessThan(texel, ivec2(u_gi_atlas_clear_params.xy))))
	{
		imageStore(s_clear_radiance, texel, vec4_splat(0.0));
	}
	if(all(lessThan(texel, ivec2(u_gi_atlas_clear_params.zw))))
	{
		imageStore(s_clear_irradiance, texel, vec4_splat(0.0));
		imageStore(s_clear_depth, texel, vec4_splat(0.0));
	}
}
