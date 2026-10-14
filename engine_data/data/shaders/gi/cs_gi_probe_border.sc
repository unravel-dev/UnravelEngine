/*
 * Screen probe gather, the filtered probe radiance with an octahedral border (a single mip):
 * each probe's GI_PROBE_TRACE_RES^2 texels surrounded by GI_PROBE_RADIANCE_BORDER texels wrapped across
 * the octahedron's folds, so a hardware bilinear lookup in any direction stays inside the probe's own tile. The
 * rough specular reads it (cs_gi_integrate.sc). One thread per bordered texel; probes on the sky are black.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "gi/gi_common.sh"

SAMPLER2D(s_gi_probe_filtered, 0);
SAMPLER2D(s_gi_probe_records, 1);
IMAGE2D_WO(s_gi_probe_border_out, rgba16f, 2);

#define GI_PROBE_BORDER_RES (GI_PROBE_TRACE_RES + 2 * GI_PROBE_RADIANCE_BORDER)

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
	ivec2 tile = coord / GI_PROBE_BORDER_RES;
	if(tile.x >= u_gi_probe_count.x || tile.y >= u_gi_atlas_rows)
	{
		return;
	}
	vec3 radiance = vec3_splat(0.0);
	vec4 record = texelFetch(s_gi_probe_records, tile, 0);
	if(record.x > 0.0)
	{
		ivec2 texel = GiOctahedralMapWrapBorder(coord - tile * GI_PROBE_BORDER_RES, GI_PROBE_BORDER_RES,
		                                           GI_PROBE_RADIANCE_BORDER);
		radiance = texelFetch(s_gi_probe_filtered, tile * GI_PROBE_TRACE_RES + texel, 0).xyz;
	}
	imageStore(s_gi_probe_border_out, coord, vec4(radiance, 0.0));
}
