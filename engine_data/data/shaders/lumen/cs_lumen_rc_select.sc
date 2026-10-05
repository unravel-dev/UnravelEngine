/*
 * Radiance cache trace selection (UE 5.8 AllocateProbeTracesCS, LumenRadianceCacheUpdate.usf:431-530): every
 * probe whose priority bucket is inside the budget is appended to the trace list, up to
 * LUMEN_RADIANCE_CACHE_MAX_TRACES. New probes are always traced; once their running cost passes the
 * budget they trace a quarter of the rays. The last bucket gets only the budget's remainder.
 * One thread per indirection entry.
 */

#include "bgfx_compute.sh"
#include "lumen/lumen_radiance_cache_common.sh"

BUFFER_RW(b_lumen_rc_indirection, uint, 0);
BUFFER_RW(b_lumen_rc_probe_state, uint, 1);
BUFFER_RW(b_lumen_rc_counters, uint, 2);
BUFFER_RW(b_lumen_rc_traces, uint, 3);

NUM_THREADS(64, 1, 1)
void main()
{
	uint index = gl_GlobalInvocationID.x;
	if(index >= uint(LUMEN_RC_INDIRECTION_SIZE))
	{
		return;
	}
	uint probe = b_lumen_rc_indirection[index];
	if(probe == LUMEN_RC_INVALID || probe == LUMEN_RC_USED)
	{
		return;
	}
	ivec4 cell = LumenRcIndirectionCell(index);
	uint cost = LumenRcTraceCost(LumenRcProbePosition(cell.xyz, cell.w));
	uint bucket = LumenRcPriorityBucket(b_lumen_rc_probe_state[LUMEN_RC_STATE_LAST_TRACED + probe],
	                                    b_lumen_rc_probe_state[LUMEN_RC_STATE_LAST_USED + probe],
	                                    cell.w);
	uint max_bucket = b_lumen_rc_counters[LUMEN_RC_COUNTER_MAX_BUCKET];
	if(bucket > max_bucket)
	{
		return;
	}
	bool force_downsample = false;
	if(bucket == max_bucket && max_bucket > 0u)
	{
		uint spent;
		atomicFetchAndAdd(b_lumen_rc_counters[LUMEN_RC_COUNTER_SPENT_LAST], cost, spent);
		if(spent + cost > b_lumen_rc_counters[LUMEN_RC_COUNTER_REMAINDER])
		{
			return;
		}
	}
	else if(bucket == 0u)
	{
		uint new_cost;
		atomicFetchAndAdd(b_lumen_rc_counters[LUMEN_RC_COUNTER_NEW_PROBE_COST], cost - 1u, new_cost);
		force_downsample = new_cost + cost - 1u > u_lumen_rc_budget;
	}
	atomicAdd(b_lumen_rc_counters[LUMEN_RC_COUNTER_SPENT], cost);
	uint trace;
	atomicFetchAndAdd(b_lumen_rc_counters[LUMEN_RC_COUNTER_TRACES], 1u, trace);
	if(trace < u_lumen_rc_trace_cap)
	{
		b_lumen_rc_traces[2u * trace] = LumenRcPackTrace(cell.xyz, cell.w, force_downsample);
		b_lumen_rc_traces[2u * trace + 1u] = probe;
		b_lumen_rc_probe_state[LUMEN_RC_STATE_LAST_TRACED + probe] = u_lumen_rc_frame;
	}
}
