/*
 * Radiance cache allocation (UE 5.8 AllocateUsedProbesCS, LumenRadianceCacheUpdate.usf:243-391): every cell
 * marked this frame with no carried-over probe gets one - from the free list when the cache persists, else
 * from the bump allocator - as never traced; every live probe adds its trace cost to the priority histogram
 * bucket of its age. A pool overflow leaves the cell without a probe (it reads black).
 * One thread per indirection entry.
 */

#include "bgfx_compute.sh"
#include "lumen/lumen_radiance_cache_common.sh"

BUFFER_RW(b_lumen_rc_indirection, uint, 0);
BUFFER_RW(b_lumen_rc_probe_state, uint, 1);
BUFFER_RW(b_lumen_rc_counters, uint, 2);

uint LumenRcAllocateProbe()
{
	if(u_lumen_rc_persistent)
	{
		uint previous;
		atomicFetchAndAdd(b_lumen_rc_counters[LUMEN_RC_COUNTER_FREE_LIST], 0xFFFFFFFFu, previous);
		int available = int(previous);
		if(available > 0)
		{
			return b_lumen_rc_probe_state[LUMEN_RC_STATE_FREE_LIST + uint(available - 1)];
		}
	}
	uint probe;
	atomicFetchAndAdd(b_lumen_rc_counters[LUMEN_RC_COUNTER_PROBES], 1u, probe);
	return probe;
}

NUM_THREADS(64, 1, 1)
void main()
{
	uint index = gl_GlobalInvocationID.x;
	if(index >= uint(LUMEN_RC_INDIRECTION_SIZE))
	{
		return;
	}
	uint entry = b_lumen_rc_indirection[index];
	if(entry == LUMEN_RC_INVALID)
	{
		return;
	}
	ivec4 cell = LumenRcIndirectionCell(index);
	uint cost = LumenRcTraceCost(LumenRcProbePosition(cell.xyz, cell.w));
	if(entry == LUMEN_RC_USED)
	{
		uint probe = LumenRcAllocateProbe();
		if(probe >= uint(LUMEN_RADIANCE_CACHE_MAX_PROBES))
		{
			b_lumen_rc_indirection[index] = LUMEN_RC_INVALID;
			return;
		}
		b_lumen_rc_probe_state[LUMEN_RC_STATE_LAST_TRACED + probe] = 0u;
		b_lumen_rc_probe_state[LUMEN_RC_STATE_LAST_USED + probe] = u_lumen_rc_frame;
		atomicAdd(b_lumen_rc_counters[LUMEN_RC_COUNTER_HISTOGRAM], cost);
		atomicAdd(b_lumen_rc_counters[LUMEN_RC_COUNTER_NEW_PROBE_COST], 1u);
		b_lumen_rc_indirection[index] = probe;
		return;
	}
	uint bucket = LumenRcPriorityBucket(b_lumen_rc_probe_state[LUMEN_RC_STATE_LAST_TRACED + entry],
	                                    b_lumen_rc_probe_state[LUMEN_RC_STATE_LAST_USED + entry],
	                                    cell.w);
	atomicAdd(b_lumen_rc_counters[LUMEN_RC_COUNTER_HISTOGRAM + int(bucket)], cost);
}
