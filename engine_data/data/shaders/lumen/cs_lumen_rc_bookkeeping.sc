/*
 * Radiance cache single-thread bookkeeping, one dispatch per mode (u_lumen_rc_mode):
 *  0 reset: the free list and the probe allocator restart (a frame that does not continue the cache);
 *  1 frame start: the per-frame counters and the priority histogram clear
 *    (UE 5.8 ClearRadianceCacheUpdateResources, LumenRadianceCacheUpdate.usf:189-219);
 *  2 budget: the highest-priority buckets that fit the trace budget, oldest first; the last one gets only
 *    the remainder (SelectMaxPriorityBucketCS, :393-429);
 *  3 trace list: the allocators clamp back into range and the tile pass's dispatch is written
 *    (SetupProbeIndirectArgsCS, LumenRadianceCache.usf:85-135);
 *  4 tiles: the trace (rows of LUMEN_RC_TRACE_DISPATCH_WIDTH tiles) and filter dispatches are written
 *    (SetupTraceFromProbesCS, :657-692).
 */

#include "bgfx_compute.sh"
#include "lumen/lumen_radiance_cache_common.sh"

BUFFER_RW(b_lumen_rc_counters, uint, 0);
BUFFER_RW(b_lumen_rc_args, uvec4, 1);

/// Threads per group of the per-trace tile pass.
#define LUMEN_RC_TILE_PASS_GROUP 64

void LumenRcSelectBudget()
{
	uint budget = u_lumen_rc_budget;
	uint sum = 0u;
	uint max_bucket = uint(LUMEN_RADIANCE_CACHE_PRIORITY_BUCKETS);
	uint remainder = budget;
	for(int bucket = 0; bucket < LUMEN_RADIANCE_CACHE_PRIORITY_BUCKETS; ++bucket)
	{
		uint cost = b_lumen_rc_counters[LUMEN_RC_COUNTER_HISTOGRAM + bucket];
		if(sum + cost >= budget)
		{
			max_bucket = uint(bucket);
			remainder = budget - sum;
			break;
		}
		sum += cost;
	}
	b_lumen_rc_counters[LUMEN_RC_COUNTER_MAX_BUCKET] = max_bucket;
	b_lumen_rc_counters[LUMEN_RC_COUNTER_REMAINDER] = remainder;
}

void LumenRcClampAllocators()
{
	int free_list = int(b_lumen_rc_counters[LUMEN_RC_COUNTER_FREE_LIST]);
	b_lumen_rc_counters[LUMEN_RC_COUNTER_FREE_LIST] = uint(clamp(free_list, 0, LUMEN_RADIANCE_CACHE_MAX_PROBES));
	b_lumen_rc_counters[LUMEN_RC_COUNTER_PROBES] =
	    min(b_lumen_rc_counters[LUMEN_RC_COUNTER_PROBES], uint(LUMEN_RADIANCE_CACHE_MAX_PROBES));
	uint traces = min(b_lumen_rc_counters[LUMEN_RC_COUNTER_TRACES], u_lumen_rc_trace_cap);
	b_lumen_rc_counters[LUMEN_RC_COUNTER_TRACE_COUNT] = traces;
	dispatchIndirect(b_lumen_rc_args, 0u, (traces + uint(LUMEN_RC_TILE_PASS_GROUP) - 1u) / uint(LUMEN_RC_TILE_PASS_GROUP), 1u, 1u);
}

void LumenRcWriteTraceArgs()
{
	uint tiles = b_lumen_rc_counters[LUMEN_RC_COUNTER_TILES];
	uint traces = b_lumen_rc_counters[LUMEN_RC_COUNTER_TRACE_COUNT];
	uint filter_groups = (uint(u_lumen_rc_final_res) + 7u) / 8u;
	uint rows = (tiles + LUMEN_RC_TRACE_DISPATCH_WIDTH - 1u) / LUMEN_RC_TRACE_DISPATCH_WIDTH;
	dispatchIndirect(b_lumen_rc_args, 1u, min(tiles, LUMEN_RC_TRACE_DISPATCH_WIDTH), rows, 1u);
	dispatchIndirect(b_lumen_rc_args, 2u, filter_groups, filter_groups, traces);
}

NUM_THREADS(1, 1, 1)
void main()
{
	int mode = u_lumen_rc_mode;
	if(mode == 0)
	{
		b_lumen_rc_counters[LUMEN_RC_COUNTER_FREE_LIST] = 0u;
		b_lumen_rc_counters[LUMEN_RC_COUNTER_PROBES] = 0u;
	}
	else if(mode == 1)
	{
		for(int i = LUMEN_RC_COUNTER_TRACES; i < LUMEN_RC_COUNTER_COUNT; ++i)
		{
			b_lumen_rc_counters[i] = 0u;
		}
	}
	else if(mode == 2)
	{
		LumenRcSelectBudget();
	}
	else if(mode == 3)
	{
		LumenRcClampAllocators();
	}
	else
	{
		LumenRcWriteTraceArgs();
	}
}
