/*
 * Traces every world probe's per-frame direction stratum (GI v2 plan 3.3, revised design - see
 * gi_world_probes.sh). One thread group per probe SLOT, one thread per ray:
 * thread t traces octahedral texel t * WINDOW + (frame mod WINDOW), so over one window every
 * texel of the 16x16 radiance atlas refreshes exactly once - the atlas IS the windowed mean,
 * with zero steady-state variance on a static scene (R1) and one-window reaction latency (R4).
 *
 * Rays sphere-trace the global cascade only (near_field 0): a probe is a coarse world-scale
 * structure and mesh-exact contact detail is the screen gather's job. Hits read the light
 * voxels; misses read the sky SH. A slot whose world cell changed (the window scrolled) is
 * claimed by zeroing every stratum but this frame's, so stale radiance from the departed cell
 * can never be read at the new position; the probe then refills over one window.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
// eval_radiance_sh lives here.
#include "../lighting.sh"

#include "gi/sdf_common.sh"
#define GI_LIGHT_VOXEL_READ
#include "gi/gi_light_voxels.sh"
#include "gi/gi_world_probes.sh"

/// rgb = radiance the ray saw, a = hitT (negative = sky/miss).
IMAGE2D_WO(s_world_probe_radiance_out, rgba16f, 5);
/// One packed cell id per probe slot across all cascades (GiWorldProbePackCell).
BUFFER_RW(b_world_probe_cells, uint, 6);
/// The lighting pass's environment SH probe, for the sky at ray miss.
SAMPLER2D(s_gi_env_sh, 14);

/// x = window centre CELL of level 0 (int as float) per axis... levels each get a vec4:
/// xyz = centre cell, w = ray max distance for that level's probes.
uniform vec4 u_gi_world_probe_window[SDF_CLIPMAP_LEVEL_COUNT];

NUM_THREADS(GI_WORLD_PROBE_RAYS_PER_FRAME, 1, 1)
void main()
{
	int slot_linear = int(gl_WorkGroupID.x);
	int per_level = GI_WORLD_PROBE_AXIS * GI_WORLD_PROBE_AXIS * GI_WORLD_PROBE_AXIS;
	int level = slot_linear / per_level;
	if(level >= SDF_CLIPMAP_LEVEL_COUNT)
	{
		return;
	}
	int in_level = slot_linear % per_level;
	ivec3 slot = ivec3(in_level % GI_WORLD_PROBE_AXIS,
	                   (in_level / GI_WORLD_PROBE_AXIS) % GI_WORLD_PROBE_AXIS,
	                   in_level / (GI_WORLD_PROBE_AXIS * GI_WORLD_PROBE_AXIS));
	// The world cell this slot represents under the current window: the unique cell in
	// [centre - half, centre + half] whose mod-AXIS equals the slot.
	ivec3 center_cell = ivec3(u_gi_world_probe_window[level].xyz);
	int half_axis = (GI_WORLD_PROBE_AXIS - 1) / 2;
	ivec3 window_base = center_cell - ivec3(half_axis, half_axis, half_axis);
	ivec3 base_slot = GiWorldProbeSlot(window_base);
	ivec3 offset = ivec3((slot.x - base_slot.x + GI_WORLD_PROBE_AXIS) % GI_WORLD_PROBE_AXIS,
	                     (slot.y - base_slot.y + GI_WORLD_PROBE_AXIS) % GI_WORLD_PROBE_AXIS,
	                     (slot.z - base_slot.z + GI_WORLD_PROBE_AXIS) % GI_WORLD_PROBE_AXIS);
	ivec3 cell = window_base + offset;
	vec3 origin = GiWorldProbeCellPosition(cell, level);
	int thread = int(gl_LocalInvocationID.x);
	// FAST-REFRESH WINDOW (GI v2 plan section 8, the DDGI event pattern adapted): while the
	// light set is changing, each frame covers TWO strata instead of one, halving the window to
	// 8 frames so a moved or toggled light propagates through the probes at double speed. The
	// stratum formula stays exhaustive either way: count consecutive strata per frame cover
	// every direction once per (WINDOW / count) frames.
	int stratum_count = int(max(u_gi_world_probe_params.w, 1.0));
	uint stratum_base = (u_world_probe_frame * uint(stratum_count)) % uint(GI_WORLD_PROBE_WINDOW);
	ivec2 tile = GiWorldProbeTileBase(slot, level, GI_WORLD_PROBE_OCT_RADIANCE);
	// Scroll claim: the slot's stored cell is compared by every thread (uniform read), thread 0
	// rewrites it, and every thread zeroes the OTHER strata of its own texel column so no stale
	// direction survives into the new cell's window. The claim and the zeroing are idempotent,
	// so the race between thread 0's write and other groups' reads next frame is harmless.
	uint packed_cell = GiWorldProbePackCell(cell, level);
	int slot_index = GiWorldProbeSlotIndex(slot, level);
	// DEAD PROBE gate (the problem RTXGI answers with relocation/classification, answered here
	// by the field itself): a lattice point inside geometry is poison. The trace's launch-slab
	// walk lets its rays exit on EITHER side of the wall it is buried in, so its atlas mixes
	// both sides' light - including sky through the miss fallback - while its depth moments in
	// room-facing directions measure the open ROOM; Chebyshev then trusts it at FULL weight.
	// Visibility-true, content-false: one embedded junction probe floods a sealed interior
	// with the sunlit exterior (measured - no weight floor or bias tuning can reject it,
	// because the visibility math is being told the truth about the wrong point). Writing
	// zero radiance with ZERO-DISTANCE hits collapses its convolved depth, and the visibility
	// test itself then kills it at every read. Cost: one field sample per probe slice.
	bool buried = SdfSampleClipmap(origin) < 0.0;
	bool fresh = b_world_probe_cells[slot_index] != packed_cell;
	if(fresh)
	{
		if(thread == 0)
		{
			b_world_probe_cells[slot_index] = packed_cell;
		}
		for(int s = 0; s < GI_WORLD_PROBE_WINDOW; ++s)
		{
			if(uint(s) >= stratum_base && uint(s) < stratum_base + uint(stratum_count))
			{
				continue;
			}
			int clear_index = thread * GI_WORLD_PROBE_WINDOW + s;
			ivec2 clear_texel = tile + ivec2(clear_index % GI_WORLD_PROBE_OCT_RADIANCE,
			                                 clear_index / GI_WORLD_PROBE_OCT_RADIANCE);
			imageStore(s_world_probe_radiance_out, clear_texel,
			           vec4(0.0, 0.0, 0.0, buried ? 0.0 : -1.0));
		}
	}
	float t_max = u_gi_world_probe_window[level].w;
	for(int si = 0; si < stratum_count; ++si)
	{
		int texel_index = thread * GI_WORLD_PROBE_WINDOW + int(stratum_base) + si;
		ivec2 texel = tile + ivec2(texel_index % GI_WORLD_PROBE_OCT_RADIANCE,
		                           texel_index / GI_WORLD_PROBE_OCT_RADIANCE);
		if(buried)
		{
			imageStore(s_world_probe_radiance_out, texel, vec4_splat(0.0));
			continue;
		}
		vec2 tile_uv = (vec2(texel - tile) + vec2_splat(0.5)) / float(GI_WORLD_PROBE_OCT_RADIANCE);
		vec3 direction = GiOctDecode(tile_uv);
		// Coarse world structure: cascade tier only (near_field 0), the shared trace defaults
		// for acceptance and budget - all owned by gi_constants, no settings.
		SdfRayHit hit = SdfTraceRay(origin, direction, t_max, 0.0, GI_TRACE_MAX_STEPS,
		                            GI_PROBE_TRACE_SURFACE_BIAS, GI_PROBE_TRACE_RELAXATION, true);
		vec3 radiance;
		float hit_t;
		if(!hit.hit)
		{
			radiance = eval_radiance_sh(s_gi_env_sh, direction);
			hit_t = -1.0;
		}
		else
		{
			hit_t = hit.t;
			vec3 hit_position = origin + direction * hit.t;
			vec3 hit_normal = hit.normal;
			if(dot(hit_normal, direction) > 0.0)
			{
				hit_normal = -hit_normal;
			}
			vec3 voxel_radiance;
			if(!GiLightVoxelRead(hit_position, hit_normal, voxel_radiance))
			{
				// Occluded but unmeasured: honest darkness, never fabricated energy - the
				// sealed room converges black through exactly this branch.
				voxel_radiance = vec3_splat(0.0);
			}
			radiance = voxel_radiance;
		}
		imageStore(s_world_probe_radiance_out, texel, vec4(radiance, hit_t));
	}
}
