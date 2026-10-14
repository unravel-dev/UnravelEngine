/*
 * Radiance cache carry-over: every probe of last frame's indirection moves to the cell at its world position in
 * this frame's (scrolled) clipmap. It keeps its index and texels when that cell was marked this frame or it was
 * used within GI_RADIANCE_CACHE_KEEP_FRAMES; otherwise - or when it scrolled out - its index goes to the free
 * list. One thread per entry of last frame's indirection.
 */

#include "bgfx_compute.sh"
#include "gi/gi_radiance_cache_common.sh"

BUFFER_RW(b_gi_rc_indirection, uint, 0);
BUFFER_RW(b_gi_rc_prev_indirection, uint, 1);
BUFFER_RW(b_gi_rc_probe_state, uint, 2);
BUFFER_RW(b_gi_rc_counters, uint, 3);

NUM_THREADS(64, 1, 1)
void main()
{
	uint index = gl_GlobalInvocationID.x;
	if(index >= uint(GI_RC_INDIRECTION_SIZE))
	{
		return;
	}
	uint probe = b_gi_rc_prev_indirection[index];
	if(probe == GI_RC_INVALID || probe == GI_RC_USED)
	{
		return;
	}
	ivec4 prev_cell = GiRcIndirectionCell(index);
	int clipmap = prev_cell.w;
	vec4 prev_clip = u_gi_rc_prev_clipmaps[clipmap];
	vec4 clip = u_gi_rc_clipmaps[clipmap];
	vec3 position = prev_clip.xyz + (vec3(prev_cell.xyz) + 0.5) * prev_clip.w;
	ivec3 cell = ivec3(floor((position - clip.xyz) / clip.w));
	bool kept = false;
	if(all(greaterThanEqual(cell, ivec3(0, 0, 0))) && all(lessThan(cell, ivec3(GI_RADIANCE_CACHE_GRID, GI_RADIANCE_CACHE_GRID, GI_RADIANCE_CACHE_GRID))))
	{
		uint target = GiRcIndirectionIndex(cell, clipmap);
		bool marked = b_gi_rc_indirection[target] == GI_RC_USED;
		uint last_used = b_gi_rc_probe_state[GI_RC_STATE_LAST_USED + probe];
		if(marked || u_gi_rc_frame - last_used < uint(GI_RADIANCE_CACHE_KEEP_FRAMES))
		{
			kept = true;
			if(marked)
			{
				b_gi_rc_probe_state[GI_RC_STATE_LAST_USED + probe] = u_gi_rc_frame;
			}
			b_gi_rc_indirection[target] = probe;
		}
	}
	if(!kept)
	{
		uint slot;
		atomicFetchAndAdd(b_gi_rc_counters[GI_RC_COUNTER_FREE_LIST], 1u, slot);
		b_gi_rc_probe_state[GI_RC_STATE_FREE_LIST + slot] = probe;
	}
}
