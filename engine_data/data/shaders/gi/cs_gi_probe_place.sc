/*
 * Screen probe gather, uniform placement: one probe per DOWNSAMPLE x DOWNSAMPLE tile, at exactly one
 * G-buffer pixel - the tile origin plus this frame's screen-wide Hammersley offset (period 8). No
 * probe state carries over between frames.
 *
 * Writes the probe record (gi_common.sh GiPackProbe; x = 0: no probe, the sky). Over the atlas rows below
 * the uniform probes it writes records without probe, and it clears the adaptive probes' counter and tile lists
 * (gi_adaptive_probes.sh) ahead of the adaptive placement, which then fills both for this frame.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "../lighting.sh"
#include "gi/gi_common.sh"
#include "gi/gi_adaptive_probes.sh"

SAMPLER2D(s_gi_depth, 0);
SAMPLER2D(s_gi_normal, 1);
IMAGE2D_WO(i_gi_probe_records, rgba32f, 2);
BUFFER_RW(b_gi_adaptive, uint, 3);

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 tile = ivec2(gl_GlobalInvocationID.xy);
	if(tile.x >= u_gi_probe_count.x || tile.y >= u_gi_atlas_rows)
	{
		return;
	}
	if(tile.y >= u_gi_probe_count.y)
	{
		imageStore(i_gi_probe_records, tile, vec4(0.0, 0.0, 0.0, 1.0));
		return;
	}
	b_gi_adaptive[GiAdaptiveCountEntry(GiAdaptiveTileIndex(tile))] = 0u;
	if(tile.x == 0 && tile.y == 0)
	{
		b_gi_adaptive[GI_ADAPTIVE_COUNTER] = 0u;
	}
	ivec2 pixel = GiProbePixel(tile);
	float depth01 = texelFetch(s_gi_depth, pixel, 0).x;
	if(depth01 >= 1.0)
	{
		imageStore(i_gi_probe_records, tile, vec4(0.0, 0.0, 0.0, 1.0));
		return;
	}
	vec3 normal = decodeNormalOctahedron(texelFetch(s_gi_normal, pixel, 0).xy);
	imageStore(i_gi_probe_records, tile, GiPackProbe(GiLinearDepth(depth01), normal, pixel, depth01));
}
