/*
 * WORLD-PROBE TRACE SCHEDULER (plan item 2.1, tasks/lumen57_deep_dive_2026-09-14.md; Lumen's
 * radiance-cache update budget, LumenRadianceCacheUpdate.usf). Every live slot gets a priority
 * bucket, and the probes of the most urgent buckets are listed for this frame's trace and convolve
 * up to u_gi_world_probe_select.y probes, so the world side's cost follows the budget instead of
 * the pool. Bucket 0: a sparse claim (GI_WORLD_PROBE_COUNT_FRESH) or a dense slot the window
 * scrolled onto a new cell - its tiles belong to another cell until traced. Buckets 1 to
 * GI_WORLD_PROBE_SELECT_FIRST_WINDOW_BUCKETS: probes still inside their first window, by the log2
 * of their age since the last trace. The buckets above: settled probes by the log2 of that age
 * divided by (level + 1) - a coarse cage feeds farther receivers and may age proportionally
 * longer. The oldest first within each band: a band-wide constant priority let an over-budget
 * band fill its quota in slot order and re-pick the same lowest slots every frame.
 *
 * One kernel, three phases (u_gi_world_probe_select.x), each dispatched through its own
 * quiescence-gate entry, so a closed gate stops the scheduler with the passes it feeds:
 *   0 HISTOGRAM - every live slot counts itself into its bucket;
 *   1 THRESHOLD - one thread walks the buckets to the budget, records the bucket it ends in and
 *     the quota left there, zeroes the histogram for the next frame, resets the list, and
 *     advances the level-0 allocation clock (once per frame the world side runs - a closed gate
 *     freezes every request age, see cs_gi_world_probe_alloc.sc);
 *   2 EMIT - every live slot in a bucket before the threshold appends itself to the list, and a
 *     slot in the threshold bucket while that bucket's quota lasts.
 */

#include "bgfx_compute.sh"
#include "gi/sdf_common.sh"
#define GI_WORLD_PROBE_INDEX_RW
#include "gi/gi_world_probes.sh"

BUFFER_RO(b_world_probe_counts, uint, 7);
BUFFER_RO(b_world_probe_cells, uint, 8);
BUFFER_RW(b_world_probe_select, uint, 9);
BUFFER_RW(b_world_probe_list, uint, 10);

/// x = phase (0 histogram, 1 threshold, 2 emit), y = the frame's budget in probes, z = the frame
/// index masked to the schedule word's 20 bits.
uniform vec4 u_gi_world_probe_select;
/// The trace's window centres (xyz = centre cell per level): a dense slot's cell this frame.
uniform vec4 u_gi_world_probe_window[SDF_CLIPMAP_LEVEL_COUNT];

#define SELECT_PHASE_HISTOGRAM 0
#define SELECT_PHASE_THRESHOLD 1
#define SELECT_PHASE_EMIT      2

/// The slot's priority bucket this frame (0 most urgent), -1 when the slot holds no probe.
int GiProbeSelectBucket(int slot_linear)
{
	int level = GiWorldProbeLevelOfSlot(slot_linear);
	if(level >= SDF_CLIPMAP_LEVEL_COUNT)
	{
		return -1;
	}
	uint word = b_world_probe_counts[slot_linear];
	if(level == 0)
	{
		if(b_world_probe_cells[slot_linear] == GI_WORLD_PROBE_NONE)
		{
			return -1;
		}
		if(word == GI_WORLD_PROBE_COUNT_FRESH)
		{
			return 0;
		}
	}
	else
	{
		ivec3 cell = GiWorldProbeDenseSlotCell(slot_linear, level, ivec3(u_gi_world_probe_window[level].xyz));
		if(b_world_probe_cells[slot_linear] != GiWorldProbePackCell(cell, level))
		{
			return 0;
		}
	}
	uint frame = uint(u_gi_world_probe_select.z);
	uint age = (frame - GiWorldProbeCountFrame(word)) & GI_WORLD_PROBE_COUNT_FRAME_MASK;
	float first_window_buckets = float(GI_WORLD_PROBE_SELECT_FIRST_WINDOW_BUCKETS);
	if(GiWorldProbeCountWindows(word) == 0u)
	{
		return int(clamp(first_window_buckets - floor(log2(max(float(age), 1.0))), 1.0, first_window_buckets));
	}
	float weighted_age = float(age) / float(level + 1);
	return int(clamp(15.0 - floor(log2(max(weighted_age, 1.0))), first_window_buckets + 1.0, 15.0));
}

NUM_THREADS(64, 1, 1)
void main()
{
	int phase = int(u_gi_world_probe_select.x + 0.5);
	uint budget = uint(u_gi_world_probe_select.y);
	if(phase == SELECT_PHASE_THRESHOLD)
	{
		if(gl_GlobalInvocationID.x != 0u)
		{
			return;
		}
		uint spent = 0u;
		uint threshold = uint(GI_WORLD_PROBE_SELECT_BUCKETS);
		uint quota = 0u;
		uint pending = 0u;
		for(int bucket = 0; bucket < GI_WORLD_PROBE_SELECT_BUCKETS; ++bucket)
		{
			uint count = b_world_probe_select[bucket];
			b_world_probe_select[bucket] = 0u;
			if(bucket <= GI_WORLD_PROBE_SELECT_FIRST_WINDOW_BUCKETS)
			{
				pending += count;
			}
			if(threshold == uint(GI_WORLD_PROBE_SELECT_BUCKETS))
			{
				if(spent + count > budget)
				{
					threshold = uint(bucket);
					quota = budget - spent;
				}
				else
				{
					spent += count;
				}
			}
		}
		b_world_probe_select[GI_WORLD_PROBE_SELECT_THRESHOLD] = threshold;
		b_world_probe_select[GI_WORLD_PROBE_SELECT_QUOTA] = quota;
		b_world_probe_select[GI_WORLD_PROBE_SELECT_COUNT] = 0u;
		b_world_probe_select[GI_WORLD_PROBE_SELECT_PENDING] = pending;
		b_world_probe_index[GI_WORLD_PROBE_INDEX_CLOCK] = b_world_probe_index[GI_WORLD_PROBE_INDEX_CLOCK] + 1u;
		return;
	}
	int slot_linear = int(gl_GlobalInvocationID.x);
	int bucket = GiProbeSelectBucket(slot_linear);
	if(bucket < 0)
	{
		return;
	}
	if(phase == SELECT_PHASE_HISTOGRAM)
	{
		uint counted = 0u;
		atomicFetchAndAdd(b_world_probe_select[bucket], 1u, counted);
		return;
	}
	uint threshold = b_world_probe_select[GI_WORLD_PROBE_SELECT_THRESHOLD];
	if(uint(bucket) > threshold)
	{
		return;
	}
	if(uint(bucket) == threshold)
	{
		// The threshold bucket's quota, taken one probe at a time; a take past zero is undone.
		uint left = 0u;
		atomicFetchAndAdd(b_world_probe_select[GI_WORLD_PROBE_SELECT_QUOTA], 0xFFFFFFFFu, left);
		if(left == 0u || left > budget)
		{
			uint restored = 0u;
			atomicFetchAndAdd(b_world_probe_select[GI_WORLD_PROBE_SELECT_QUOTA], 1u, restored);
			return;
		}
	}
	uint index = 0u;
	atomicFetchAndAdd(b_world_probe_select[GI_WORLD_PROBE_SELECT_COUNT], 1u, index);
	if(index < budget)
	{
		b_world_probe_list[int(index)] = uint(slot_linear);
	}
}
