/*
 * Radiance cache allocation: every cell
 * marked this frame with no carried-over probe gets one - from the free list when the cache persists, else
 * from the bump allocator - as never traced; every live probe adds its trace cost to the priority histogram
 * bucket of its age. A pool overflow leaves the cell without a probe (it reads black).
 * One thread per indirection entry.
 */

#include "bgfx_compute.sh"
#include "gi/gi_radiance_cache_common.sh"

BUFFER_RW(b_gi_rc_indirection, uint, 0);
BUFFER_RW(b_gi_rc_probe_state, uint, 1);
BUFFER_RW(b_gi_rc_counters, uint, 2);

uint GiRcAllocateProbe()
{
	if(u_gi_rc_persistent)
	{
		uint previous;
		atomicFetchAndAdd(b_gi_rc_counters[GI_RC_COUNTER_FREE_LIST], 0xFFFFFFFFu, previous);
		int available = int(previous);
		if(available > 0)
		{
			return b_gi_rc_probe_state[GI_RC_STATE_FREE_LIST + uint(available - 1)];
		}
	}
	uint probe;
	atomicFetchAndAdd(b_gi_rc_counters[GI_RC_COUNTER_PROBES], 1u, probe);
	return probe;
}

NUM_THREADS(64, 1, 1)
void main()
{
	uint index = gl_GlobalInvocationID.x;
	if(index >= uint(GI_RC_INDIRECTION_SIZE))
	{
		return;
	}
	uint entry = b_gi_rc_indirection[index];
	if(entry == GI_RC_INVALID)
	{
		return;
	}
	ivec4 cell = GiRcIndirectionCell(index);
	uint cost = GiRcTraceCost(GiRcProbePosition(cell.xyz, cell.w));
	if(entry == GI_RC_USED)
	{
		uint probe = GiRcAllocateProbe();
		if(probe >= uint(GI_RADIANCE_CACHE_MAX_PROBES))
		{
			b_gi_rc_indirection[index] = GI_RC_INVALID;
			return;
		}
		b_gi_rc_probe_state[GI_RC_STATE_LAST_TRACED + probe] = 0u;
		b_gi_rc_probe_state[GI_RC_STATE_LAST_USED + probe] = u_gi_rc_frame;
		atomicAdd(b_gi_rc_counters[GI_RC_COUNTER_HISTOGRAM], cost);
		atomicAdd(b_gi_rc_counters[GI_RC_COUNTER_NEW_PROBE_COST], 1u);
		b_gi_rc_indirection[index] = probe;
		return;
	}
	uint bucket = GiRcPriorityBucket(b_gi_rc_probe_state[GI_RC_STATE_LAST_TRACED + entry],
	                                    b_gi_rc_probe_state[GI_RC_STATE_LAST_USED + entry],
	                                    cell.w);
	atomicAdd(b_gi_rc_counters[GI_RC_COUNTER_HISTOGRAM + int(bucket)], cost);
}
