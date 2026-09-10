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
 * One thread: the whole job is a handful of loads and a 40-entry ring.
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

/// One indirect entry per gated dispatch. STRUCTURAL, not tuned: it counts the dispatches the
/// gate owns, so it lives here rather than in the gi_constants tables. Must equal
/// gi_quiescence_gate_pass::entry_count, which static_asserts against this value's mirror.
#define GI_GATE_ENTRY_COUNT 3

/// x = gate mode (0 run, 1 measure, 2 skip - surface_cache_view::quiescence_mode),
/// y = non-zero to clear the ring (a tracked input changed), z, w unused.
uniform vec4 u_gi_gate_params;
#define u_gate_mode  int(u_gi_gate_params.x)
#define u_gate_reset (u_gi_gate_params.y > 0.0)

/// Group counts for each gated dispatch: xyz = numX, numY, numZ, w unused. Written verbatim
/// when the gate runs, zeroed when it does not.
uniform vec4 u_gi_gate_groups[GI_GATE_ENTRY_COUNT];

/// Ring header, then the samples as float bits (exact, and the buffer is typed uint).
#define GI_GATE_RING_COUNT_SLOT 0
#define GI_GATE_RING_HEAD_SLOT  1
#define GI_GATE_RING_BASE       2
#define GI_GATE_RING_SIZE       (GI_QUIESCENCE_COMPARE_FRAMES + GI_QUIESCENCE_WINDOW_FRAMES)

/// Mean of `count` samples ending `back` samples before the newest - the same walk the CPU
/// ring does, with `head` pointing one past the newest.
float GiGateRingMean(uint head, int back, int count)
{
	float sum = 0.0;
	for(int i = 0; i < count; ++i)
	{
		int offset = back + i + 1;
		int slot = int((head + uint(GI_GATE_RING_SIZE) - uint(offset)) % uint(GI_GATE_RING_SIZE));
		sum += uintBitsToFloat(s_gi_gate_ring[GI_GATE_RING_BASE + slot]);
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
	for(int level = 0; level < SDF_CLIPMAP_LEVEL_COUNT; ++level)
	{
		ivec3 change_texel = GiLightVoxelStatsTexel(level, 0);
		ivec3 faces_texel = GiLightVoxelStatsTexel(level, 1);
		change += float(imageLoad(s_gi_vis_memo, change_texel).x) / GI_QUIESCENCE_STATS_SCALE;
		faces += float(imageLoad(s_gi_vis_memo, faces_texel).x);
		imageStore(s_gi_vis_memo, change_texel, uvec4(0u, 0u, 0u, 0u));
		imageStore(s_gi_vis_memo, faces_texel, uvec4(0u, 0u, 0u, 0u));
	}
	float mean = faces > 0.0 ? change / faces : 0.0;

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
	head = (head + 1u) % uint(GI_GATE_RING_SIZE);
	count = min(count + 1u, uint(GI_GATE_RING_SIZE));
	s_gi_gate_ring[GI_GATE_RING_COUNT_SLOT] = count;
	s_gi_gate_ring[GI_GATE_RING_HEAD_SLOT] = head;

	// Converged when the mean relative change per relit face is below what any reader can
	// distinguish, or when it has stopped falling (a stationary dithered equilibrium at
	// shadow edges never reaches the floor, while a decaying tail shrinks between the two
	// windows). surface_cache_view::evaluate_relight_quiescence is the same test.
	bool converged = false;
	if(count >= uint(GI_QUIESCENCE_WINDOW_FRAMES))
	{
		float recent = GiGateRingMean(head, 0, GI_QUIESCENCE_WINDOW_FRAMES);
		if(recent < GI_QUIESCENCE_CONVERGED_MEAN)
		{
			converged = true;
		}
		else if(count >= uint(GI_QUIESCENCE_COMPARE_FRAMES + GI_QUIESCENCE_WINDOW_FRAMES))
		{
			float earlier = GiGateRingMean(head, GI_QUIESCENCE_COMPARE_FRAMES, GI_QUIESCENCE_WINDOW_FRAMES);
			converged = recent >= GI_QUIESCENCE_STATIONARY_FRACTION * earlier;
		}
	}

	// The CPU settled everything except convergence: mode 0 forces the dispatches, mode 2
	// (GI_QUIESCENCE_MAX_FRAMES) forces them off, mode 1 defers to the measurement.
	bool run = u_gate_mode == 0 || (u_gate_mode == 1 && !converged);
	for(int entry = 0; entry < GI_GATE_ENTRY_COUNT; ++entry)
	{
		uvec3 groups = run ? uvec3(u_gi_gate_groups[entry].xyz) : uvec3(0u, 0u, 0u);
		dispatchIndirect(s_gi_gate_indirect, entry, groups.x, groups.y, groups.z);
	}
	// The census rows (GI_STATS_RELIGHT_FACES_MOVED onward) are zeroed only when the passes
	// are about to accumulate a fresh one, so a snapshot taken while the gate is closed still
	// reads the last frame that did any work - the frame the ledger wants.
	if(run)
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
