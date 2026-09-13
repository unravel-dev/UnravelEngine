/*
 * SPARSE LEVEL-0 PROBE ALLOCATION (gi_world_probes.sh, the sparse-level note; the shape of
 * Lumen's radiance-cache allocation). Three phases, one dispatch each, ungated and run BEFORE
 * the quiescence gate every frame so a probe requested last frame is traced this frame:
 *  - INIT (once, on a fresh index buffer): every index entry unallocated, every stamp void,
 *    the free stack full, the clock at 1.
 *  - EVICT, one thread per pool slot: frees a live slot whose cell left the index window, or
 *    whose cage nobody stamped within the eviction age - GI_WORLD_PROBE_EVICT_IDLE_FRAMES
 *    while the pool is comfortable, GI_WORLD_PROBE_EVICT_PRESSURE_FRAMES once fewer than a
 *    GI_WORLD_PROBE_POOL_PRESSURE_DIVISOR-th of it is free (a parked camera with the relight
 *    gated off stamps nothing from the relight for minutes; its cages must not churn). The
 *    index entry still pointing at the slot is cleared and the slot pushed on the free stack.
 *    Thread 0 advances the allocation clock the request stamps carry (the ages this phase
 *    reads are off by at most one tick, which the ages tolerate).
 *  - ALLOCATE, one thread per index entry: an unallocated cell that any of the eight base
 *    cells around it stamped within GI_WORLD_PROBE_REQUEST_AGE ticks pops a free slot, writes
 *    its cell id and the FRESH count sentinel (cs_gi_world_probe_trace.sc seeds and traces it
 *    the same frame) and binds the entry to it. An empty stack leaves the cell unallocated
 *    and its readers on the coarser cage until pressure eviction frees something.
 * Pushes and pops never share a dispatch, so the stack needs only its counter's atomics; the
 * pool slot of an in-window cell is unique, so no two threads claim the same entry. The census
 * rows GI_STATS_PROBES_ALLOCATED / _EVICTED (level-0 column) feed the gate's hold and the
 * waste ledger.
 */

#include "bgfx_compute.sh"
#include "gi/sdf_common.sh"
#include "gi/gi_light_voxels.sh"
#define GI_WORLD_PROBE_INDEX_RW
#include "gi/gi_world_probes.sh"

/// Per-slot cell ids and window counts, the trace's own buffers at the trace's stages.
BUFFER_RW(b_world_probe_cells, uint, 8);
BUFFER_RW(b_world_probe_counts, uint, 7);
/// The bounce vis-memo for its statistics slice alone (an image stage must stay below 8 for
/// OpenGL's image units).
UIMAGE3D_RW(s_gi_vis_memo, r32ui, 6);

/// x = phase (0 init, 1 evict, 2 allocate), yzw = the level-0 window's centre cell (the
/// trace's u_gi_world_probe_window[0].xyz).
uniform vec4 u_gi_world_probe_alloc;
#define u_alloc_phase  int(u_gi_world_probe_alloc.x)
#define u_alloc_center ivec3(u_gi_world_probe_alloc.yzw)

#define ALLOC_PHASE_INIT     0
#define ALLOC_PHASE_EVICT    1
#define ALLOC_PHASE_ALLOCATE 2

NUM_THREADS(64, 1, 1)
void main()
{
	int index = int(gl_GlobalInvocationID.x);
	int half_axis = (GI_WORLD_PROBE_AXIS_L0 - 1) / 2;
	if(u_alloc_phase == ALLOC_PHASE_INIT)
	{
		if(index < GI_WORLD_PROBE_INDEX_CELLS)
		{
			b_world_probe_index[GI_WORLD_PROBE_INDEX_SLOT_BASE + index] = GI_WORLD_PROBE_NONE;
			b_world_probe_index[GI_WORLD_PROBE_INDEX_REQUEST_BASE + index] = GI_WORLD_PROBE_NONE;
			b_world_probe_index[GI_WORLD_PROBE_INDEX_STAMP_BASE + index] = 0u;
		}
		if(index < GI_WORLD_PROBE_POOL_L0)
		{
			// The stack pops from its top: slot 0 goes out first, so a small live set packs
			// into the atlas's first rows.
			b_world_probe_index[GI_WORLD_PROBE_INDEX_FREE_BASE + index] = uint(GI_WORLD_PROBE_POOL_L0 - 1 - index);
		}
		if(index == 0)
		{
			b_world_probe_index[GI_WORLD_PROBE_INDEX_CLOCK] = 1u;
			b_world_probe_index[GI_WORLD_PROBE_INDEX_FREE_COUNT] = uint(GI_WORLD_PROBE_POOL_L0);
		}
		return;
	}
	uint clock = b_world_probe_index[GI_WORLD_PROBE_INDEX_CLOCK];
	if(u_alloc_phase == ALLOC_PHASE_EVICT)
	{
		if(index == 0)
		{
			b_world_probe_index[GI_WORLD_PROBE_INDEX_CLOCK] = clock + 1u;
		}
		if(index >= GI_WORLD_PROBE_POOL_L0)
		{
			return;
		}
		uint packed = b_world_probe_cells[index];
		if(packed == GI_WORLD_PROBE_NONE)
		{
			return;
		}
		ivec3 cell = GiWorldProbeUnpackCell(packed);
		ivec3 delta = abs(cell - u_alloc_center);
		bool in_window = max(delta.x, max(delta.y, delta.z)) <= half_axis;
		uint free_count = b_world_probe_index[GI_WORLD_PROBE_INDEX_FREE_COUNT];
		bool pressure = free_count * uint(GI_WORLD_PROBE_POOL_PRESSURE_DIVISOR) < uint(GI_WORLD_PROBE_POOL_L0);
		uint evict_age = pressure ? uint(GI_WORLD_PROBE_EVICT_PRESSURE_FRAMES) : uint(GI_WORLD_PROBE_EVICT_IDLE_FRAMES);
		if(in_window && GiWorldProbeCellWanted(cell, clock, evict_age))
		{
			return;
		}
		b_world_probe_cells[index] = GI_WORLD_PROBE_NONE;
		b_world_probe_counts[index] = 0u;
		// Only an entry that still names this slot is cleared: a departed cell's entry may
		// already serve the in-window cell that took its toroidal place.
		int index_slot = GiWorldProbeIndexSlot(cell);
		if(b_world_probe_index[GI_WORLD_PROBE_INDEX_SLOT_BASE + index_slot] == uint(index))
		{
			b_world_probe_index[GI_WORLD_PROBE_INDEX_SLOT_BASE + index_slot] = GI_WORLD_PROBE_NONE;
		}
		uint position = 0u;
		atomicFetchAndAdd(b_world_probe_index[GI_WORLD_PROBE_INDEX_FREE_COUNT], 1u, position);
		b_world_probe_index[GI_WORLD_PROBE_INDEX_FREE_BASE + int(position)] = uint(index);
		imageAtomicAdd(s_gi_vis_memo, GiLightVoxelStatsTexel(0, GI_STATS_PROBES_EVICTED), 1u);
		return;
	}
	// ALLOCATE: this thread's index entry and the window cell it stands for - the unique cell
	// in [centre - half, centre + half] whose mod-axis equals the entry's coordinates (the
	// trace's dense decode).
	if(index >= GI_WORLD_PROBE_INDEX_CELLS)
	{
		return;
	}
	if(b_world_probe_index[GI_WORLD_PROBE_INDEX_SLOT_BASE + index] != GI_WORLD_PROBE_NONE)
	{
		return;
	}
	int axis = GI_WORLD_PROBE_AXIS_L0;
	ivec3 entry = ivec3(index % axis, (index / axis) % axis, index / (axis * axis));
	ivec3 window_base = u_alloc_center - ivec3(half_axis, half_axis, half_axis);
	ivec3 base_slot = GiWorldProbeSlot(window_base, 0);
	ivec3 offset = ivec3((entry.x - base_slot.x + axis) % axis,
	                     (entry.y - base_slot.y + axis) % axis,
	                     (entry.z - base_slot.z + axis) % axis);
	ivec3 cell = window_base + offset;
	if(!GiWorldProbeCellWanted(cell, clock, uint(GI_WORLD_PROBE_REQUEST_AGE)))
	{
		return;
	}
	// Pop. A decrement past zero is undone: the pool is exhausted and the cell waits.
	uint previous = 0u;
	atomicFetchAndAdd(b_world_probe_index[GI_WORLD_PROBE_INDEX_FREE_COUNT], 0xFFFFFFFFu, previous);
	if(previous == 0u || previous > uint(GI_WORLD_PROBE_POOL_L0))
	{
		uint restored = 0u;
		atomicFetchAndAdd(b_world_probe_index[GI_WORLD_PROBE_INDEX_FREE_COUNT], 1u, restored);
		return;
	}
	uint slot = b_world_probe_index[GI_WORLD_PROBE_INDEX_FREE_BASE + int(previous) - 1];
	b_world_probe_cells[int(slot)] = GiWorldProbePackCell(cell, 0);
	b_world_probe_counts[int(slot)] = GI_WORLD_PROBE_COUNT_FRESH;
	b_world_probe_index[GI_WORLD_PROBE_INDEX_SLOT_BASE + index] = slot;
	imageAtomicAdd(s_gi_vis_memo, GiLightVoxelStatsTexel(0, GI_STATS_PROBES_ALLOCATED), 1u);
}
