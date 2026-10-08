/*
 * Radiance cache trace tiles (UE 5.8 GenerateUniformProbeTraceTilesCS, LumenRadianceCache.usf:370-429): each
 * traced probe appends its 8x8-direction tiles. A probe traces (R / 2) << level directions per axis: level 1
 * (R x R) normally, level 0 (a quarter of the rays) beyond LUMEN_RADIANCE_CACHE_DOWNSAMPLE_DISTANCE or when
 * forced down by the budget. One thread per trace.
 */

#include "bgfx_compute.sh"
#include "lumen/lumen_radiance_cache_common.sh"

BUFFER_RW(b_lumen_rc_counters, uint, 0);
BUFFER_RO(b_lumen_rc_traces, uint, 1);
BUFFER_RW(b_lumen_rc_tiles, uint, 2);

NUM_THREADS(64, 1, 1)
void main()
{
	uint trace = gl_GlobalInvocationID.x;
	if(trace >= b_lumen_rc_counters[LUMEN_RC_COUNTER_TRACE_COUNT])
	{
		return;
	}
	uint packed = b_lumen_rc_traces[2u * trace];
	ivec4 cell = LumenRcUnpackTrace(packed);
	vec3 to_camera = LumenRcProbePosition(cell.xyz, cell.w) - u_lumen_rc_camera.xyz;
	float downsample_distance = LUMEN_RADIANCE_CACHE_DOWNSAMPLE_DISTANCE;
	bool far = dot(to_camera, to_camera) >= downsample_distance * downsample_distance;
	uint level = (far || LumenRcTraceForceDownsample(packed)) ? 0u : 1u;
	uint tiles_per_side = (uint(u_lumen_rc_probe_res) / uint(2 * LUMEN_RC_TILE_RES)) << level;
	uint tile_count = tiles_per_side * tiles_per_side;
	uint first;
	atomicFetchAndAdd(b_lumen_rc_counters[LUMEN_RC_COUNTER_TILES], tile_count, first);
	for(uint tile = 0u; tile < tile_count; ++tile)
	{
		uint entry = first + tile;
		b_lumen_rc_tiles[2u * entry] = (tile % tiles_per_side) | ((tile / tiles_per_side) << 8u) | (level << 16u);
		b_lumen_rc_tiles[2u * entry + 1u] = trace;
	}
}
