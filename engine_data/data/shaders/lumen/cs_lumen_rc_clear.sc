/*
 * Radiance cache, start of frame: this frame's indirection becomes all LUMEN_RC_INVALID before the marks. On a
 * frame that does not continue the cache (first frame, reset), last frame's indirection is cleared too, so nothing
 * carries over, and every probe's state restarts.
 * One thread per indirection entry.
 */

#include "bgfx_compute.sh"
#include "lumen/lumen_radiance_cache_common.sh"

BUFFER_RW(b_lumen_rc_indirection, uint, 0);
BUFFER_RW(b_lumen_rc_prev_indirection, uint, 1);
BUFFER_RW(b_lumen_rc_probe_state, uint, 2);

NUM_THREADS(64, 1, 1)
void main()
{
	uint index = gl_GlobalInvocationID.x;
	if(index >= uint(LUMEN_RC_INDIRECTION_SIZE))
	{
		return;
	}
	b_lumen_rc_indirection[index] = LUMEN_RC_INVALID;
	if(u_lumen_rc_persistent)
	{
		return;
	}
	b_lumen_rc_prev_indirection[index] = LUMEN_RC_INVALID;
	if(index < uint(LUMEN_RADIANCE_CACHE_MAX_PROBES))
	{
		b_lumen_rc_probe_state[LUMEN_RC_STATE_LAST_USED + index] = 0u;
		b_lumen_rc_probe_state[LUMEN_RC_STATE_LAST_TRACED + index] = 0u;
	}
}
