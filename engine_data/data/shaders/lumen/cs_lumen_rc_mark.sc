/*
 * Radiance cache marking:
 * every screen probe marks the eight lattice probes around its own position in the finest clipmap that
 * covers it - exactly the probes its rays' hand-off will interpolate. One thread per screen probe of the atlas,
 * uniform and adaptive.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "lumen/lumen_common.sh"
#include "lumen/lumen_radiance_cache_common.sh"

SAMPLER2D(s_lumen_probe_records, 0);
BUFFER_RW(b_lumen_rc_indirection, uint, 1);

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 tile = ivec2(gl_GlobalInvocationID.xy);
	if(tile.x >= u_lumen_probe_count.x || tile.y >= u_lumen_atlas_rows)
	{
		return;
	}
	vec4 record = texelFetch(s_lumen_probe_records, tile, 0);
	if(record.x <= 0.0)
	{
		return;
	}
	vec3 position = LumenProbePosition(record);
	int clipmap = LumenRcClipmapOf(position);
	if(clipmap >= LUMEN_RADIANCE_CACHE_CLIPMAPS)
	{
		return;
	}
	ivec3 base = LumenRcInterpolationBase(position, clipmap);
	for(int corner = 0; corner < 8; ++corner)
	{
		ivec3 cell = base + ivec3(corner & 1, (corner >> 1) & 1, corner >> 2);
		if(all(greaterThanEqual(cell, ivec3(0, 0, 0))) && all(lessThan(cell, ivec3(LUMEN_RADIANCE_CACHE_GRID, LUMEN_RADIANCE_CACHE_GRID, LUMEN_RADIANCE_CACHE_GRID))))
		{
			b_lumen_rc_indirection[LumenRcIndirectionIndex(cell, clipmap)] = LUMEN_RC_USED;
		}
	}
}
