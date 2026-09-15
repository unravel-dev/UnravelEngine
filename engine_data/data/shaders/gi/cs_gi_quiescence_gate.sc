/*
 * GPU-RESIDENT QUIESCENCE GATE.
 *
 * Decides, without a CPU readback, whether the light-voxel and world-probe dispatches have
 * anything left to write, and publishes the answer as indirect dispatch arguments the two
 * passes consume the same frame.
 *
 * This replaces a staging blit plus bgfx::readTexture per frame. That readback moved 32
 * bytes and cost about a GPU frame of render-thread time: bgfx's readTexture promises
 * frameNum + 2 latency at the API level but every desktop backend implements it as a
 * blocking sync (D3D11 Map without DO_NOT_WAIT, D3D12 CopyTextureRegion + finish, Vulkan
 * kick(true), GL glGetTextureSubImage), executed in the post-command buffer AFTER the
 * frame's submit - so the render thread waited for GPU idle at the tail of every frame the
 * gate was open, which is every frame the camera moves. The path is kept as a fallback for
 * backends without BGFX_CAPS_DRAW_INDIRECT; see gi_quiescence_gate_pass.
 *
 * The convergence tests mirror surface_cache_view::evaluate_relight_quiescence exactly.
 * One thread: the whole job is a handful of loads and two 40-entry rings.
 */

#include "bgfx_compute.sh"
#include "gi/gi_constants.sh"
#include "gi/sdf_common.sh"
#include "gi/gi_light_voxels.sh"

/// The bounce vis-memo, read+written for its statistics slice alone (GiLightVoxelStatsTexel):
/// the per-level sums the relight accumulated LAST frame, zeroed here for the next one. On
/// the fallback path cs_gi_light_voxel_stats.sc does this same copy-and-zero.
UIMAGE3D_RW(s_gi_vis_memo, r32ui, 0);

/// Sample ring and its counters, persistent across frames. Never touched by the CPU (bgfx
/// forbids updating a compute-writable buffer), so it self-initialises from the reset lane.
BUFFER_RW(s_gi_gate_ring, uint, 1);

/// The indirect argument buffer: one entry per gated dispatch, in gi_quiescence_gate_pass
/// entry order (light voxels, probe trace, probe convolve).
BUFFER_WO(s_gi_gate_indirect, uvec4, 2);

/// The relight's surface list (cs_gi_clipmap_attributes): its SDF_CLIPMAP_LEVEL_COUNT header
/// entries are this frame's per-level counts, which size the light-voxel launch below. Stage 5:
/// sdf_common.sh holds buffer stages 1-3 (b_sdf_instances is 3) in every includer.
BUFFER_RO(b_surface_list, uint, 5);

/// The world-probe scheduler's state (cs_gi_world_probe_select.sc), for its PENDING slot: probes
/// still owed a first window at the last selection. Bound when u_gi_gate_params.w is set.
BUFFER_RO(b_world_probe_select, uint, 6);
/// GI_WORLD_PROBE_SELECT_PENDING in gi_world_probes.sh (not included here: that header's
/// declarations would crowd this one-thread shader's stages).
#define GI_GATE_PROBE_PENDING_SLOT 19

/// One indirect entry per gated dispatch. STRUCTURAL, not tuned: it counts the dispatches the
/// gate owns, so it lives here rather than in the gi_constants tables. Must equal
/// gi_quiescence_gate_pass::entry_count, which static_asserts against this value's mirror.
#define GI_GATE_ENTRY_COUNT 6
/// The light-voxel entry's index and its kernel's lanes per group: gi_quiescence_gate_pass
/// entry_light_voxels and NUM_THREADS in gi_light_voxels_kernel.sh.
#define GI_GATE_ENTRY_LIGHT_VOXELS 0
#define GI_GATE_RELIGHT_THREADS    64u

/// x = gate mode (0 run, 1 measure, 2 skip - surface_cache_view::quiescence_mode),
/// y = non-zero to clear the ring (a tracked input changed), z = non-zero while the editor
/// census is armed (the census rows are cleared for accumulation only then), w = non-zero when the
/// world-probe scheduler's buffer is bound at stage 6.
uniform vec4 u_gi_gate_params;
#define u_gate_mode  int(u_gi_gate_params.x)
#define u_gate_reset (u_gi_gate_params.y > 0.0)

/// Group counts for each gated dispatch: xyz = numX, numY, numZ, w unused. Written verbatim
/// when the gate runs, zeroed when it does not.
uniform vec4 u_gi_gate_groups[GI_GATE_ENTRY_COUNT];

/// Ring header, then the samples as float bits (exact, and the buffer is typed uint): the
/// absolute change ring, then the signed drift ring, one head and count for both. The third
/// header slot is the sparse-probe HOLD: frames the gate stays open after an allocation.
#define GI_GATE_RING_COUNT_SLOT 0
#define GI_GATE_RING_HEAD_SLOT  1
#define GI_GATE_RING_HOLD_SLOT  2
#define GI_GATE_RING_BASE       3
/// The hold armed by an allocation: GI_WORLD_PROBE_ALLOC_HOLD_WINDOWS probe windows.
#define GI_GATE_ALLOC_HOLD_FRAMES (GI_WORLD_PROBE_ALLOC_HOLD_WINDOWS * GI_WORLD_PROBE_WINDOW)
#define GI_GATE_RING_SIZE       (GI_QUIESCENCE_COMPARE_FRAMES + GI_QUIESCENCE_WINDOW_FRAMES)
#define GI_GATE_DRIFT_RING_BASE (GI_GATE_RING_BASE + GI_GATE_RING_SIZE)

/// Mean of `count` samples ending `back` samples before the newest in the ring at `base` -
/// the same walk the CPU ring does, with `head` pointing one past the newest.
float GiGateRingMean(int base, uint head, int back, int count)
{
	float sum = 0.0;
	for(int i = 0; i < count; ++i)
	{
		int offset = back + i + 1;
		int slot = int((head + uint(GI_GATE_RING_SIZE) - uint(offset)) % uint(GI_GATE_RING_SIZE));
		sum += uintBitsToFloat(s_gi_gate_ring[base + slot]);
	}
	return sum / float(count);
}

NUM_THREADS(1, 1, 1)
void main()
{
	// LAST frame's relight, summed per level and drained so the next frame measures only its
	// own writes. Drained unconditionally: a frame whose dispatches the gate zeroed adds
	// nothing, so the slice reads 0 and the verdict latches - exactly the fixed point the CPU
	// path reaches, and only a CPU-side change (the reset lane) leaves it.
	float change = 0.0;
	float faces = 0.0;
	float rise = 0.0;
	for(int level = 0; level < SDF_CLIPMAP_LEVEL_COUNT; ++level)
	{
		ivec3 change_texel = GiLightVoxelStatsTexel(level, GI_STATS_RELIGHT_CHANGE);
		ivec3 faces_texel = GiLightVoxelStatsTexel(level, GI_STATS_RELIGHT_FACES);
		ivec3 rise_texel = GiLightVoxelStatsTexel(level, GI_STATS_RELIGHT_RISE);
		change += float(imageLoad(s_gi_vis_memo, change_texel).x) / GI_QUIESCENCE_STATS_SCALE;
		faces += float(imageLoad(s_gi_vis_memo, faces_texel).x);
		rise += float(imageLoad(s_gi_vis_memo, rise_texel).x) / GI_QUIESCENCE_STATS_SCALE;
		imageStore(s_gi_vis_memo, change_texel, uvec4(0u, 0u, 0u, 0u));
		imageStore(s_gi_vis_memo, faces_texel, uvec4(0u, 0u, 0u, 0u));
		imageStore(s_gi_vis_memo, rise_texel, uvec4(0u, 0u, 0u, 0u));
	}
	float mean = faces > 0.0 ? change / faces : 0.0;
	// Signed: the rising share minus the falling one (change - rise), per relit face.
	float drift = faces > 0.0 ? (2.0 * rise - change) / faces : 0.0;
	// SPARSE PROBES (cs_gi_world_probe_alloc.sc ran just before this gate): any allocation
	// this frame arms a hold that keeps every gated dispatch running for
	// GI_WORLD_PROBE_ALLOC_HOLD_WINDOWS windows, whatever the relight census says and even
	// under the CPU's skip mode - that mode caps a PARKED shot's cost, and a fresh probe means
	// something new came into view (a camera turn needs no window scroll to reveal a room).
	// Both rows are drained every frame like the relight rows above.
	ivec3 allocated_texel = GiLightVoxelStatsTexel(0, GI_STATS_PROBES_ALLOCATED);
	ivec3 evicted_texel = GiLightVoxelStatsTexel(0, GI_STATS_PROBES_EVICTED);
	uint allocated = imageLoad(s_gi_vis_memo, allocated_texel).x;
	imageStore(s_gi_vis_memo, allocated_texel, uvec4(0u, 0u, 0u, 0u));
	imageStore(s_gi_vis_memo, evicted_texel, uvec4(0u, 0u, 0u, 0u));
	uint hold = s_gi_gate_ring[GI_GATE_RING_HOLD_SLOT];
	// A never-written buffer reads as garbage on some backends: anything past the arm value
	// is not a hold.
	if(hold > uint(GI_GATE_ALLOC_HOLD_FRAMES))
	{
		hold = 0u;
	}
	if(allocated > 0u)
	{
		hold = uint(GI_GATE_ALLOC_HOLD_FRAMES);
	}
	else if(hold > 0u)
	{
		hold -= 1u;
	}
	s_gi_gate_ring[GI_GATE_RING_HOLD_SLOT] = hold;

	uint count = s_gi_gate_ring[GI_GATE_RING_COUNT_SLOT];
	uint head = s_gi_gate_ring[GI_GATE_RING_HEAD_SLOT];
	if(u_gate_reset)
	{
		count = 0u;
		head = 0u;
	}
	// A never-written buffer reads as allocation garbage on some backends, so the head is
	// wrapped rather than trusted; the count is clamped by the same bound below.
	head = head % uint(GI_GATE_RING_SIZE);
	s_gi_gate_ring[GI_GATE_RING_BASE + int(head)] = floatBitsToUint(mean);
	s_gi_gate_ring[GI_GATE_DRIFT_RING_BASE + int(head)] = floatBitsToUint(drift);
	head = (head + 1u) % uint(GI_GATE_RING_SIZE);
	count = min(count + 1u, uint(GI_GATE_RING_SIZE));
	s_gi_gate_ring[GI_GATE_RING_COUNT_SLOT] = count;
	s_gi_gate_ring[GI_GATE_RING_HEAD_SLOT] = head;

	// Converged when the mean relative change per relit face is below what any reader can
	// distinguish, or when it has stopped falling (a stationary dithered equilibrium at
	// shadow edges never reaches the floor, while a decaying tail shrinks between the two
	// windows) AND is not trending: a volume climbing through its bounce loop is also a
	// steady change (GI_QUIESCENCE_DRIFT_FRACTION), which the ratio alone called rest.
	// surface_cache_view::evaluate_relight_quiescence is the same test.
	bool converged = false;
	if(count >= uint(GI_QUIESCENCE_WINDOW_FRAMES))
	{
		float recent = GiGateRingMean(GI_GATE_RING_BASE, head, 0, GI_QUIESCENCE_WINDOW_FRAMES);
		if(recent < GI_QUIESCENCE_CONVERGED_MEAN)
		{
			converged = true;
		}
		else if(count >= uint(GI_QUIESCENCE_COMPARE_FRAMES + GI_QUIESCENCE_WINDOW_FRAMES))
		{
			float earlier = GiGateRingMean(GI_GATE_RING_BASE, head, GI_QUIESCENCE_COMPARE_FRAMES, GI_QUIESCENCE_WINDOW_FRAMES);
			float recent_drift = abs(GiGateRingMean(GI_GATE_DRIFT_RING_BASE, head, 0, GI_QUIESCENCE_WINDOW_FRAMES));
			converged = recent >= GI_QUIESCENCE_STATIONARY_FRACTION * earlier &&
			            recent_drift <= GI_QUIESCENCE_DRIFT_FRACTION * recent;
		}
	}

	// The CPU settled everything except convergence: mode 0 forces the dispatches, mode 2
	// (GI_QUIESCENCE_MAX_FRAMES) forces them off, mode 1 defers to the measurement - and the
	// sparse-probe hold overrides both closed answers.
	// PENDING PROBES: under the trace budget a claim completes its first window over as many
	// frames as the budget needs to reach it again; closing on the relight's convergence alone
	// froze such probes with their seeded texels (the thick sealed cell, 0.0017 -> 0.0048). Like
	// the allocation hold, this overrides both closed answers.
	uint pending_probes = u_gi_gate_params.w > 0.5 ? b_world_probe_select[GI_GATE_PROBE_PENDING_SLOT] : 0u;
	bool run = u_gate_mode == 0 || hold > 0u || pending_probes > 0u || (u_gate_mode == 1 && !converged);
	// TIGHT RELIGHT LAUNCH: the CPU can only size the light-voxel entry for a full volume (a
	// rotation slice of the whole capacity at every level), and every lane past a level's
	// count returns at once - on Sponza most of the 262,144 lanes per open frame. The kernel
	// maps lane i to entry i x GI_LIGHT_VOXEL_UPDATE_DENOM + phase and the level rides the
	// group row, so ceil(count / denom) lanes of the largest level cover every level's due
	// entries whatever the phase. The counts are the ones the attribute pass wrote this frame.
	uint relight_entries = 0u;
	for(int count_level = 0; count_level < SDF_CLIPMAP_LEVEL_COUNT; ++count_level)
	{
		relight_entries = max(relight_entries, b_surface_list[count_level]);
	}
	uint relight_denom = uint(GI_LIGHT_VOXEL_UPDATE_DENOM);
	uint relight_lanes = (relight_entries + relight_denom - 1u) / relight_denom;
	uint relight_groups = (relight_lanes + GI_GATE_RELIGHT_THREADS - 1u) / GI_GATE_RELIGHT_THREADS;
	for(int entry = 0; entry < GI_GATE_ENTRY_COUNT; ++entry)
	{
		uvec3 groups = run ? uvec3(u_gi_gate_groups[entry].xyz) : uvec3(0u, 0u, 0u);
		if(entry == GI_GATE_ENTRY_LIGHT_VOXELS)
		{
			groups.x = min(groups.x, relight_groups);
		}
		dispatchIndirect(s_gi_gate_indirect, entry, groups.x, groups.y, groups.z);
	}
	// The census rows (GI_STATS_RELIGHT_FACES_MOVED onward) are zeroed only when the passes
	// are about to accumulate a fresh one, so a snapshot taken while the gate is closed still
	// reads the last frame that did any work - the frame the ledger wants.
	if(run && u_gi_gate_params.z > 0.5)
	{
		for(int census_level = 0; census_level < SDF_CLIPMAP_LEVEL_COUNT; ++census_level)
		{
			for(int quantity = GI_STATS_RELIGHT_FACES_MOVED; quantity < GI_STATS_QUANTITY_COUNT; ++quantity)
			{
				imageStore(s_gi_vis_memo, GiLightVoxelStatsTexel(census_level, quantity), uvec4(0u, 0u, 0u, 0u));
			}
		}
	}
}
