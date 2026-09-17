/*
 * Sparse world-probe RELOCATION (gi_single_lighting_plan.md D8, audit section 22): one thread
 * per pool slot, acting on the slots the allocation pass claimed THIS frame (the FRESH count
 * sentinel). The slot's lattice point is moved out of geometry and to clearance by
 * GiWorldProbeRelocate (DDGI / RTXGI probe relocation, answered by the mesh fields); the
 * offset goes to the index's relocation lane for the readers and the trace (which refreshes
 * it every frame after this). A point that stays inside geometry is a DEAD probe for every
 * reader, so the slot goes straight back to the free stack, the cell's lane takes the BURIED
 * marker (the allocation pass then skips the cell until its re-test tick), and the claim's
 * allocation count is withdrawn so a static buried cell does not hold the gate open.
 *
 * A separate program on purpose: the 22 field samples of the relocation inlined into the
 * allocation kernel raised its temporaries from 5 to 18 and its cost from 0.06 to 0.18 ms at
 * rest - the 117649 index-cell threads that return at once paid the occupancy. Here only the
 * frame's fresh claims pay anything.
 */

#include "bgfx_compute.sh"
#include "gi/sdf_common.sh"
#include "gi/gi_light_voxels.sh"
#define GI_WORLD_PROBE_INDEX_RW
#define GI_WORLD_PROBE_RELOCATE
#include "gi/gi_world_probes.sh"

/// Per-slot cell ids and window counts, the trace's own buffers at the trace's stages.
BUFFER_RW(b_world_probe_cells, uint, 8);
BUFFER_RW(b_world_probe_counts, uint, 7);
/// The bounce vis-memo for its statistics slice alone (the allocation count withdrawn).
UIMAGE3D_RW(s_gi_vis_memo, r32ui, 6);

NUM_THREADS(64, 1, 1)
void main()
{
	int index = int(gl_GlobalInvocationID.x);
	if(index >= GI_WORLD_PROBE_POOL_L0)
	{
		return;
	}
	if(b_world_probe_counts[index] != GI_WORLD_PROBE_COUNT_FRESH)
	{
		return;
	}
	uint packed_word = b_world_probe_cells[index];
	if(packed_word == GI_WORLD_PROBE_NONE)
	{
		return;
	}
	ivec3 cell = GiWorldProbeUnpackCell(packed_word);
	int index_slot = GiWorldProbeIndexSlot(cell);
	vec3 nominal = GiWorldProbeCellPosition(cell, 0);
	vec3 relocation = GiWorldProbeRelocate(nominal, GiWorldProbeSpacing(0));
	if(SdfSampleInstancesPoint(nominal + relocation) < 0.0)
	{
		// Buried even relocated: the slot goes back (the allocation pass popped it this
		// frame in its own dispatch, so the free count's atomics are the only contention),
		// the cell is marked, the claim uncounted.
		b_world_probe_cells[index] = GI_WORLD_PROBE_NONE;
		b_world_probe_counts[index] = 0u;
		if(b_world_probe_index[GI_WORLD_PROBE_INDEX_SLOT_BASE + index_slot] == uint(index))
		{
			b_world_probe_index[GI_WORLD_PROBE_INDEX_SLOT_BASE + index_slot] = GI_WORLD_PROBE_NONE;
		}
		b_world_probe_index[GI_WORLD_PROBE_INDEX_OFFSET_BASE + index_slot] = GI_WORLD_PROBE_OFFSET_BURIED;
		uint position = 0u;
		atomicFetchAndAdd(b_world_probe_index[GI_WORLD_PROBE_INDEX_FREE_COUNT], 1u, position);
		b_world_probe_index[GI_WORLD_PROBE_INDEX_FREE_BASE + int(position)] = uint(index);
		imageAtomicAdd(s_gi_vis_memo, GiLightVoxelStatsTexel(0, GI_STATS_PROBES_ALLOCATED), 0xFFFFFFFFu);
		return;
	}
	b_world_probe_index[GI_WORLD_PROBE_INDEX_OFFSET_BASE + index_slot] =
	    GiWorldProbePackOffset(relocation, GiWorldProbeSpacing(0));
}
