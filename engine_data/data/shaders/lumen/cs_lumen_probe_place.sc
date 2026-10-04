/*
 * Lumen screen probe gather, uniform placement (UE 5.8 ScreenProbeDownsampleDepthUniformCS,
 * LumenScreenProbeGather.usf:83-133): one probe per DOWNSAMPLE x DOWNSAMPLE tile, at exactly one
 * G-buffer pixel - the tile origin plus this frame's screen-wide Hammersley offset (period 8). No
 * probe state carries over between frames.
 *
 * Writes the probe record (lumen_common.sh LumenPackProbe; x = 0: no probe, the sky). Over the atlas rows below
 * the uniform probes it writes records without probe, and it clears the adaptive probes' counter and tile lists
 * (lumen_adaptive_probes.sh; UE clears them before its placement, LumenScreenProbeGather.cpp:2331-2333): the adaptive
 * placement then fills both for this frame.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "../lighting.sh"
#include "lumen/lumen_common.sh"
#include "lumen/lumen_adaptive_probes.sh"

SAMPLER2D(s_lumen_depth, 0);
SAMPLER2D(s_lumen_normal, 1);
IMAGE2D_WO(i_lumen_probe_records, rgba32f, 2);
BUFFER_RW(b_lumen_adaptive, uint, 3);

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 tile = ivec2(gl_GlobalInvocationID.xy);
	if(tile.x >= u_lumen_probe_count.x || tile.y >= u_lumen_atlas_rows)
	{
		return;
	}
	if(tile.y >= u_lumen_probe_count.y)
	{
		imageStore(i_lumen_probe_records, tile, vec4(0.0, 0.0, 0.0, 1.0));
		return;
	}
	b_lumen_adaptive[LumenAdaptiveCountEntry(LumenAdaptiveTileIndex(tile))] = 0u;
	if(tile.x == 0 && tile.y == 0)
	{
		b_lumen_adaptive[LUMEN_ADAPTIVE_COUNTER] = 0u;
	}
	ivec2 pixel = LumenProbePixel(tile);
	float depth01 = texelFetch(s_lumen_depth, pixel, 0).x;
	if(depth01 >= 1.0)
	{
		imageStore(i_lumen_probe_records, tile, vec4(0.0, 0.0, 0.0, 1.0));
		return;
	}
	vec3 normal = decodeNormalOctahedron(texelFetch(s_lumen_normal, pixel, 0).xy);
	imageStore(i_lumen_probe_records, tile, LumenPackProbe(LumenLinearDepth(depth01), normal, pixel, depth01));
}
