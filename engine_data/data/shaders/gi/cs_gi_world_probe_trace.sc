/*
 * Traces the per-frame direction stratum of every world probe the scheduler listed (see
 * gi_world_probes.sh). PROBE_TRACE_SLOTS probes per thread group, one thread per ray:
 * thread t traces octahedral texel t * WINDOW + the probe's stratum cursor, so over one window
 * every texel of the 16x16 radiance atlas refreshes exactly once - with the direction jitter off
 * the atlas IS the windowed mean, with zero steady-state variance on a static scene and
 * one-window reaction latency.
 *
 * Rays march the mesh-exact fields over GI_WORLD_PROBE_MESH_RANGE and the global cascade
 * beyond. Hits read the light voxels; misses read the sky SH. A dense-level slot whose world
 * cell changed (the window scrolled) is claimed by zeroing every stratum but this frame's, so
 * stale radiance from the departed cell can never be read at the new position; the probe then
 * refills over one window. A SPARSE level-0 slot (gi_world_probes.sh) is claimed by the
 * allocation pass, which writes its cell and the FRESH count sentinel; this kernel seeds it
 * the same way on that sentinel, and a free slot does nothing at all.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
// eval_radiance_sh lives here.
#include "../lighting.sh"

#include "gi/sdf_common.sh"
#define GI_LIGHT_VOXEL_READ
#include "gi/gi_light_voxels.sh"
/// The sparse index (stage 13, read-write): the relocation lane, refreshed here once per window
/// of each probe's traces.
#define GI_WORLD_PROBE_INDEX_RW
#define GI_WORLD_PROBE_RELOCATE
#include "gi/gi_world_probes.sh"
#include "gi/gi_noise.sh"
#include "gi/gi_emissive_nee.sh"
// The atlas stores CACHED lighting (absolute), but the coverage bound below is expressed on
// pre-exposed values - Lumen's radiosity rule (MaxRayIntensity x View.OneOverPreExposure).
#include "gi/gi_pre_exposure.sh"

/// Solid angle of one radiance-atlas texel (the octahedral map is near equal-area).
#define GI_WORLD_PROBE_TEXEL_SOLID_ANGLE (4.0 * 3.1415926535897932 / float(GI_WORLD_PROBE_OCT_RADIANCE * GI_WORLD_PROBE_OCT_RADIANCE))

/// How much of a texel's cone the emitter table covers around @p direction, seen from
/// @p origin: the solid angles of every piece whose bounding cone intersects the texel's,
/// over the texel's solid angle, saturated. A centre ray that skewers a small emitter
/// would otherwise hold the emitter's full radiance as the texel's mean. The bound is the
/// geometry, not a radiance cap, which would also cap every large emitter's spread: a big
/// panel's pieces add up to the whole texel (1, energy-preserving), a lone small piece gives
/// the cone's mean. The union is taken because the single hit piece's own fraction darkens
/// a segmented panel's far texels. Pieces come from the emitter table the probe tracers aim
/// at (gi_emissive_nee.sh; the instance buffer and the count are bound to this pass); a
/// direction no piece covers is left unscaled.
float GiWorldProbeEmitterCoverage(vec3 direction, vec3 origin)
{
	int emitter_count = min(u_sdf_emitter_count, GI_EMISSIVE_NEE_MAX_EMITTERS);
	// The texel's half angle: its solid angle as a cone.
	float texel_cos = 1.0 - GI_WORLD_PROBE_TEXEL_SOLID_ANGLE / (2.0 * 3.1415926535897932);
	float texel_sin = sqrt(max(1.0 - texel_cos * texel_cos, 0.0));
	float covered = 0.0;
	bool any_piece = false;
	LOOP
	for(int ei = 0; ei < emitter_count; ++ei)
	{
		GiEmitter e = GiLoadEmitter(ei);
		GiEmitterCone cone = GiEmitterConeFrom(e, origin);
		if(!cone.valid)
		{
			continue;
		}
		// The piece's cone intersects the texel's when the axes are within the sum of the half angles.
		float piece_sin = sqrt(max(1.0 - cone.cos_max * cone.cos_max, 0.0));
		float sum_cos = cone.cos_max * texel_cos - piece_sin * texel_sin;
		if(dot(direction, cone.axis) < sum_cos)
		{
			continue;
		}
		any_piece = true;
		covered += GiConeSolidAngle(cone.cos_max);
	}
	return any_piece ? saturate(covered / GI_WORLD_PROBE_TEXEL_SOLID_ANGLE) : 1.0;
}

/// rgb = radiance, a = hitT (the clamped depth: 0 = never measured, the clamp = miss/sky).
/// READ-write: both are a converging running mean over windows (GI_WORLD_PROBE_EMA_WINDOWS) -
/// the read is this texel's own previous value.
IMAGE2D_RW(s_world_probe_radiance_out, rgba16f, 5);
/// One packed cell id per probe slot across all cascades (GiWorldProbePackCell). Stage 8: a
/// buffer may sit past 7 on every backend; an IMAGE may not on OpenGL (eight image units,
/// 0-7), so the vis-memo image below holds this kernel's stage 6.
BUFFER_RW(b_world_probe_cells, uint, 8);
/// Complete windows accumulated per probe slot since its claim or the last fast window - the
/// running mean's count (saturating at GI_WORLD_PROBE_EMA_WINDOWS).
BUFFER_RW(b_world_probe_counts, uint, 7);
/// The scheduler's list of this frame's probes and its state (cs_gi_world_probe_select.sc): a
/// trace group's lanes serve list entries, not pool slots.
BUFFER_RO(b_world_probe_list, uint, 9);
BUFFER_RO(b_world_probe_select, uint, 15);
/// xy = this window's R2 offset for the sub-texel direction jitter (double on the CPU, from
/// the window index). z = the jitter/mean enable. w = 1 while the editor's GI census is armed:
/// the occupancy classification and the texel census run only then.
uniform vec4 u_gi_world_probe_jitter;
#define u_world_probe_census (u_gi_world_probe_jitter.w > 0.5)
/// The lighting pass's environment SH probe, for the sky at ray miss.
SAMPLER2D(s_gi_env_sh, 14);
/// The PARENT cascade's convolved irradiance, for scroll-in seeding. Read-only and a
/// DIFFERENT texture from the radiance atlas being written, so sampling it here is legal.
SAMPLER2D(s_world_probe_irradiance_seed, 11);
/// The bounce vis-memo, bound for its statistics slice alone: the world-probe census
/// (GI_STATS_PROBES_*, GI_STATS_PROBE_TEXELS_*) the waste ledger reads back on demand.
/// Stage 6 (an image stage must stay below 8 for OpenGL's image units); the free stages
/// are 9 and 15.
UIMAGE3D_RW(s_gi_vis_memo, r32ui, 6);
/// xy = 1 / irradiance-depth atlas size (the seeding read shares the irradiance tile layout).
/// z = strata per frame (the fast-refresh window). Carried HERE, in a trace-only uniform,
/// rather than in u_gi_world_probe_params.w: that lane is the cage-visibility variance gate
/// for every reading consumer, and aliasing the two would leave the gate one added #define
/// away from silently becoming 1.0 or 2.0 and the sealed-box leak defence never marching.
uniform vec4 u_gi_world_probe_seed_atlas;

/// x = window centre CELL of level 0 (int as float) per axis... levels each get a vec4:
/// xyz = centre cell, w = ray max distance for that level's probes.
uniform vec4 u_gi_world_probe_window[SDF_CLIPMAP_LEVEL_COUNT];

/// Probes per 64-lane group: four 16-ray probes, so a wave runs full instead of one probe's
/// 16 lanes leaving half (32-wide) or three quarters (64-wide) of it idle. Each probe keeps
/// its own leader lane and claim logic; the group barrier stays uniform. Mirror of
/// gi_world_probe_pass.cpp's dispatch (ceil(probe count / this)).
#define PROBE_TRACE_SLOTS 4
/// Per-slot census accumulators and the lane-completion counter that elects the lane
/// flushing them: a barrier cannot follow the partial-group early return below, so the
/// last of a slot's lanes to finish its texels does the image atomics (four per slot,
/// against tens of thousands per frame if every lane flushed its own).
SHARED uint s_census_texels[PROBE_TRACE_SLOTS];
SHARED uint s_census_moved[PROBE_TRACE_SLOTS];
SHARED uint s_census_visible[PROBE_TRACE_SLOTS];
SHARED uint s_census_done[PROBE_TRACE_SLOTS];
/// The slot's relocation offset, computed by its leader lane and shared before the trace.
SHARED vec3 s_probe_offset[PROBE_TRACE_SLOTS];

// = GI_WORLD_PROBE_RAYS_PER_FRAME x PROBE_TRACE_SLOTS: a literal, as the OpenGL backend
// rejects expressions in local_size.
NUM_THREADS(64, 1, 1)
void main()
{
	int slot_in_group = int(gl_LocalInvocationID.x) / GI_WORLD_PROBE_RAYS_PER_FRAME;
	// THE SCHEDULED PROBE: the group's lanes serve list entries; an entry past the scheduler's
	// count maps one slot past the last level, which the partial-group path below already treats
	// as inactive.
	int list_index = int(gl_WorkGroupID.x) * PROBE_TRACE_SLOTS + slot_in_group;
	int slot_linear = list_index < int(b_world_probe_select[GI_WORLD_PROBE_SELECT_COUNT])
	                      ? int(b_world_probe_list[list_index])
	                      : GiWorldProbeSlotTotal();
	int level = GiWorldProbeLevelOfSlot(slot_linear);
	// A partial final group: the lanes past the last probe do no work but must still reach
	// the barrier below (a return here is varying flow, which the barrier forbids), so the
	// level is clamped for the uniform-array reads and every store is guarded. A FREE sparse
	// slot is inactive the same way.
	bool probe_active = level < SDF_CLIPMAP_LEVEL_COUNT;
	level = min(level, SDF_CLIPMAP_LEVEL_COUNT - 1);
	int slot_index = slot_linear;
	ivec3 cell = ivec3(0, 0, 0);
	uint packed_cell = GI_WORLD_PROBE_NONE;
	bool fresh = false;
	if(level == 0)
	{
		// SPARSE level 0: the slot's cell is whatever the allocation pass claimed it for; a
		// claim is announced by the FRESH count sentinel (the cell buffer already holds the
		// new cell, so the dense levels' cell comparison cannot see it).
		packed_cell = b_world_probe_cells[slot_index];
		probe_active = probe_active && packed_cell != GI_WORLD_PROBE_NONE;
		cell = GiWorldProbeUnpackCell(packed_cell);
		fresh = b_world_probe_counts[slot_index] == GI_WORLD_PROBE_COUNT_FRESH;
	}
	else
	{
		// The world cell this slot represents under the current window.
		cell = GiWorldProbeDenseSlotCell(slot_linear, level, ivec3(u_gi_world_probe_window[level].xyz));
		packed_cell = GiWorldProbePackCell(cell, level);
		fresh = b_world_probe_cells[slot_index] != packed_cell;
	}
	// THE SCHEDULE: the probe's own stratum cursor, not the frame, picks the directions it
	// traces - a probe traced on non-consecutive frames still covers its sixteen strata in order.
	// A fast window's strata per frame align the cursor down to their multiple, so a window never
	// spills into the next texel row. A fresh slot starts at stratum 0.
	uint schedule_word = b_world_probe_counts[slot_index];
	int stratum_count = int(max(u_gi_world_probe_seed_atlas.z, 1.0));
	uint cursor = fresh ? 0u : GiWorldProbeCountCursor(schedule_word);
	uint stratum_base = (cursor / uint(stratum_count)) * uint(stratum_count);
	vec3 nominal = GiWorldProbeCellPosition(cell, level);
	int thread = int(gl_LocalInvocationID.x) % GI_WORLD_PROBE_RAYS_PER_FRAME;
	// PROBE RELOCATION (GiWorldProbeRelocate, sparse level only): the leader lane shares the
	// probe's offset from its lattice point with the probe's other lanes. The relocation
	// pass computed it at claim; the trace re-runs it once per window of the probe's own traces
	// (at its first stratum: a mover or a field streamed in after the claim moves the origin
	// within one window) and reads the stored lane otherwise, which keeps the relocation's field
	// samples off most traces. The barrier is uniform: no lane has returned yet.
	if(thread == 0)
	{
		vec3 relocation = vec3_splat(0.0);
		if(probe_active && level == 0)
		{
			bool refresh = stratum_base == 0u;
			BRANCH
			if(refresh)
			{
				relocation = GiWorldProbeRelocate(nominal, GiWorldProbeSpacing(0));
				b_world_probe_index[GI_WORLD_PROBE_INDEX_OFFSET_BASE + GiWorldProbeIndexSlot(cell)] =
				    GiWorldProbePackOffset(relocation, GiWorldProbeSpacing(0));
			}
			else
			{
				relocation = GiWorldProbeOffset(cell, 0);
			}
		}
		s_probe_offset[slot_in_group] = relocation;
	}
	barrier();
	vec3 origin = nominal + s_probe_offset[slot_in_group];
	// FAST-REFRESH WINDOW (the DDGI event pattern adapted): while the light set is changing, each
	// frame covers TWO strata instead of one, halving the window to 8 frames so a moved or toggled
	// light propagates through the probes at double speed. The stratum formula stays exhaustive
	// either way: count consecutive strata per frame cover every direction once per
	// (WINDOW / count) frames.
	// (stratum_count and stratum_base: the probe's schedule, decoded above.)
	ivec2 tile = GiWorldProbeTileBase(slot_linear, GI_WORLD_PROBE_OCT_RADIANCE);
	// Scroll claim (dense levels): the slot's stored cell is compared by every thread (uniform
	// read), thread 0 rewrites it, and every thread zeroes the OTHER strata of its own texel
	// column so no stale direction survives into the new cell's window. The claim and the
	// zeroing are idempotent, so the race between thread 0's write and other groups' reads
	// next frame is harmless. The sparse level's claim (above) runs the same path.
	// DEAD PROBE gate (the problem RTXGI answers with relocation/classification, answered here
	// by the field itself): a lattice point inside geometry is poison. The trace's launch-slab
	// walk lets its rays exit on EITHER side of the wall it is buried in, so its atlas mixes
	// both sides' light - including sky through the miss fallback - while its depth moments in
	// room-facing directions measure the open ROOM; Chebyshev then trusts it at FULL weight.
	// Visibility-true, content-false: one embedded junction probe floods a sealed interior
	// with the sunlit exterior (no weight floor or bias tuning can reject it, because the
	// visibility math is being told the truth about the wrong point). Writing zero radiance
	// with ZERO-DISTANCE hits collapses its convolved depth, and the visibility test itself
	// then kills it at every read. Cost: one field sample per probe slice.
	//
	// The MESH fields decide, not the clipmap: the clipmap's finest level at a probe is the
	// CAMERA's, which from a distance is too coarse to see the floor slab or wall a lattice
	// point sits in. A clipmap verdict would read such a probe alive from far and dead from
	// near, and alive-but-buried it would trace from inside the geometry, mixing the sky
	// beneath a floor into its atlas. The verdict is a function of the probe's position alone.
	bool buried = probe_active && SdfSampleInstancesPoint(origin) < 0.0;
	// OCCUPANCY CLASSIFICATION (RTXGI's probe classification, answered by the field like the gate
	// above): a level-0 probe with no geometry within GI_WORLD_PROBE_SLEEP_SPACINGS of it cannot
	// be a cage corner for any ON-SURFACE query. It is COUNTED, not slept: it goes to the census
	// (GI_STATS_PROBES_ASLEEP) and the Probe Lattice view, and keeps tracing. Putting such probes
	// to sleep - zero radiance and depth, like a buried probe - saves next to nothing, because
	// the trace's four-probe groups are latency-bound whatever their lanes do, and it would
	// change the completion read of screen-probe rays, which queries the lattice in the AIR
	// (origin + direction x short range), where such a probe IS a legitimate cage corner.
	//
	// Sampled from the COARSEST level, not the one covering the probe. The cascade is a narrow
	// band saturating at mesh_sdf::encode_range (4) VOXELS, so level 0's own field certifies only
	// half a metre of clearance while the coarsest level's 1 m voxel certifies four - which is
	// what the 3.5 m threshold needs. That is also why only LEVEL 0 is classified: every coarser
	// level's threshold is beyond what any level can certify.
	// Editor statistics only (the Probe Lattice view classifies for itself): census armed.
	bool asleep = false;
	if(level == 0 && u_world_probe_census)
	{
		float clearance = SdfSampleClipmapLevel(SDF_CLIPMAP_LEVEL_COUNT - 1, origin);
		// A level that did not answer (outside its window, or no cascade at all) reports the
		// give-up value and proves nothing; stay awake on it.
		asleep = clearance < SDF_CLIPMAP_OUTSIDE &&
		         clearance >= GI_WORLD_PROBE_SLEEP_SPACINGS * GiWorldProbeSpacing(0);
	}
	// One word for "this probe writes nothing this frame". Only buried probes are inactive;
	// the occupancy classification above is a count, never a skip (see it).
	bool inactive = buried;
	// CONVERGING MEAN (GI_WORLD_PROBE_EMA_WINDOWS). A windowed mean over FIXED texel-centre
	// directions has zero variance but is BIASED per probe - a small emitter is skewered or
	// missed per direction and neighbouring probes disagree, which enters the voxel bounce as a
	// blotch field on emissive-lit walls. With the jitter on, directions jitter inside their
	// texel per window and each texel is a running mean over the last windows; a fresh claim or
	// a fast (light/content change) window resets the count so changes still land in one window
	// at write-through. Every thread reads the count before thread 0 advances it.
	uint windows_seen = fresh ? 0u : GiWorldProbeCountWindows(schedule_word);
	// The stored count is windows since the claim or the last fast window WHATEVER the jitter
	// setting: the trace scheduler's first-window bucket reads it. The running mean below
	// still restarts every window while the jitter is off.
	uint windows_counted = windows_seen;
	bool fast_window = stratum_count > 1;
	// The jitter/mean is a SETTING (u_gi_world_probe_jitter.z, gi_resolve_pass::settings::
	// world_probe_jitter): off, every window is a fast one in this sense - texel centres at
	// write-through, the deterministic atlas that settles the instant the scene does.
	if(fast_window || u_gi_world_probe_jitter.z < 0.5)
	{
		windows_seen = 0u;
	}
	float mean_blend = 1.0 / float(min(windows_seen + 1u, uint(GI_WORLD_PROBE_EMA_WINDOWS)));
	if(thread == 0)
	{
		s_census_texels[slot_in_group] = 0u;
		s_census_moved[slot_in_group] = 0u;
		s_census_visible[slot_in_group] = 0u;
		s_census_done[slot_in_group] = 0u;
	}
	barrier();
	if(!probe_active)
	{
		return;
	}
	if(thread == 0 && u_world_probe_census)
	{
		// The probe's state this frame, one atomic per probe. Inactive probes never reach
		// the texel flush below, so their state is the whole of their census.
		int state_quantity = asleep ? GI_STATS_PROBES_ASLEEP
		                            : (buried ? GI_STATS_PROBES_BURIED : GI_STATS_PROBES_ACTIVE);
		imageAtomicAdd(s_gi_vis_memo, GiLightVoxelStatsTexel(level, state_quantity), 1u);
	}
	if(thread == 0)
	{
		// A window completes on the frame that traces its last strata.
		bool window_end = stratum_base + uint(stratum_count) >= uint(GI_WORLD_PROBE_WINDOW);
		uint next_cursor = (stratum_base + uint(stratum_count)) % uint(GI_WORLD_PROBE_WINDOW);
		b_world_probe_counts[slot_index] = GiWorldProbePackCount(
		    fast_window ? 0u : (window_end ? windows_counted + 1u : windows_counted), next_cursor,
		    u_world_probe_frame);
	}
	if(fresh)
	{
		if(thread == 0)
		{
			b_world_probe_cells[slot_index] = packed_cell;
		}
		// SCROLL-IN SEEDING (the SDFGI cascade trick): a freshly claimed slot starts from the
		// PARENT cascade's view of the same position instead of black. The parent scrolls at
		// half the rate, so its data is valid here; the seed is its convolved IRRADIANCE at
		// each texel's direction - the cosine-weighted mean of what the parent sees that way,
		// in exactly the E/pi = mean-radiance units a radiance texel holds on average - soft,
		// energy-consistent, refined stratum by stratum over the window. Without it every
		// window edge would drag a dark frontier that takes a full window (a quarter second) to
		// converge. The outermost level has no parent and keeps the dark clear (energy loss,
		// never invention); buried probes stay dead.
		//
		// The seed is RADIANCE ONLY. hitT is left at 0 - the "never measured" value the atlas
		// clear writes - never at the miss marker (the clamp), which would be a lie about
		// GEOMETRY with no relation to the seed: the convolve turns every miss texel into
		// depth = GI_WORLD_PROBE_DEPTH_CLAMP x spacing at near-zero variance, so a just-claimed
		// slot would advertise "confidently open in every direction" for a whole window. A cage
		// corner is at most sqrt(3) x spacing away against a 1.5 x spacing clamp, so most
		// corners would take the readers' no-test path (distance <= mean, low variance =
		// neither Chebyshev nor the field march runs) at FULL trilinear weight - reading the
		// COARSER cascade's irradiance, which straddles walls worse by construction. That is a
		// cascade-down import into sealed interiors, re-arming every time the camera crosses a
		// probe cell, and it bypasses every defence in the reader. Leaving hitT at 0 drags the
		// convolved mean toward zero instead, which over-occludes: the fresh cage is rejected
		// and the query falls through to the coarser level - the same energy, by the legitimate
		// path that still gets a visibility test. Untraced strata also do not count as sky, so a
		// fresh interior probe does not report sky_fraction 1.
		int parent_level = level + 1;
		bool parent_valid = false;
		ivec2 parent_tile = ivec2(0, 0);
		// The SPARSE level seeds BLACK (energy loss for one window, never invention): its
		// parent is the 4 m lattice, whose probes straddle every wall thinner than their
		// spacing, and a sparse slot is claimed for exactly the cells that matter - a sealed
		// room's own air. Seeding those from the exterior cage would put the sunlit outdoors
		// into a thin-walled room for a window after every claim. Until the slot's first window
		// completes the cage reads it as dead (depth 0) and the coarser lattice answers, exactly
		// as for a cell that holds no slot.
		if(!inactive && level > 0 && parent_level < SDF_CLIPMAP_LEVEL_COUNT)
		{
			float parent_spacing = GiWorldProbeSpacing(parent_level);
			ivec3 parent_cell = ivec3(floor(origin / parent_spacing + vec3_splat(0.5)));
			int parent_slot = GiWorldProbeSlotIndex(GiWorldProbeSlot(parent_cell, parent_level), parent_level);
			// The parent slot must still HOLD that cell - it is toroidal too, and a slot
			// serving a different region would seed someone else's lighting.
			parent_valid = b_world_probe_cells[parent_slot] == GiWorldProbePackCell(parent_cell, parent_level);
			parent_tile = GiWorldProbeTileBase(parent_slot, GI_WORLD_PROBE_OCT_IRRADIANCE + 2);
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
			vec4 clear_value = vec4(0.0, 0.0, 0.0, 0.0);
			if(parent_valid)
			{
				vec2 clear_uv = (vec2(clear_texel - tile) + vec2_splat(0.5)) /
				                float(GI_WORLD_PROBE_OCT_RADIANCE);
				vec3 clear_direction = GiOctDecode(clear_uv);
				vec2 seed_uv = (vec2(parent_tile) + vec2_splat(1.0) +
				                GiOctEncode(clear_direction) * float(GI_WORLD_PROBE_OCT_IRRADIANCE)) *
				               u_gi_world_probe_seed_atlas.xy;
				clear_value.xyz =
				    max(texture2DLod(s_world_probe_irradiance_seed, seed_uv, 0.0).xyz,
				        vec3_splat(0.0));
			}
			imageStore(s_world_probe_radiance_out, clear_texel, clear_value);
		}
	}
	float t_max = u_gi_world_probe_window[level].w;
	uint census_texels = 0u;
	uint census_moved = 0u;
	uint census_visible = 0u;
	for(int si = 0; si < stratum_count; ++si)
	{
		int texel_index = thread * GI_WORLD_PROBE_WINDOW + int(stratum_base) + si;
		ivec2 texel = tile + ivec2(texel_index % GI_WORLD_PROBE_OCT_RADIANCE,
		                           texel_index / GI_WORLD_PROBE_OCT_RADIANCE);
		if(inactive)
		{
			imageStore(s_world_probe_radiance_out, texel, vec4_splat(0.0));
			continue;
		}
		// Sub-texel direction jitter, per window (R2) and per atlas texel (IGN, so neighbouring
		// probes and texels decorrelate): the running mean integrates the texel's whole solid
		// angle instead of its centre ray. FAST windows (a light or content change) sample the
		// texel CENTRE at write-through instead - deterministic and reactive, as with the jitter
		// off - because a jittered sample written through every 4 frames is a flickering probe,
		// and content churn (movers) keeps the fast window open for as long as it lasts. The
		// converging mean resumes from that deterministic base when the scene settles.
		// A probe's FIRST window (fresh claim, or the window after a fast one) samples the
		// centres too: the mean has no base yet and a lone jittered sample written through is
		// a noisy probe for a whole window.
		bool deterministic_window = fast_window || windows_seen == 0u || u_gi_world_probe_jitter.z < 0.5;
		vec2 texel_jitter = deterministic_window ? vec2_splat(0.5)
		                                         : fract(GiIgnNoise(texel) + u_gi_world_probe_jitter.xy);
		vec2 tile_uv = (vec2(texel - tile) + texel_jitter) / float(GI_WORLD_PROBE_OCT_RADIANCE);
		vec3 direction = GiOctDecode(tile_uv);
		// MESH-EXACT NEAR FIELD: the first GI_WORLD_PROBE_MESH_RANGE metres of every probe ray
		// march the per-instance fields, as the gather's rays do. The exact tier is the sealed-box
		// defence - the leak channel is the cascade's porous field, which the exact tier does not
		// have. The cascade takes over beyond (see the far tier's note); the near tier's steps,
		// exhaustion and clearance are carried into the far hit (SdfTraceRayEx does the same) so
		// the open-exhaustion miss below reads the whole ray.
		float near_field = min(GI_WORLD_PROBE_MESH_RANGE, t_max);
		SdfRayHit near_hit = SdfTraceInstances(origin, direction, 0.0, near_field,
		                                       GI_WORLD_PROBE_TRACE_STEPS, GI_WORLD_PROBE_TRACE_BIAS,
		                                       GI_WORLD_PROBE_TRACE_RELAXATION, true);
		// Coarse world structure beyond the near field: the cascade tier called DIRECTLY with NO
		// surface expand (expand_start -1) and the probe-specific suppression hardening
		// (expand_full). Behind the mesh-exact near field the expand is not needed against the
		// sealed-box leak, and it would fatten every surface by up to 0.87 voxel and darken the
		// lit image (GI_WORLD_PROBE_MESH_RANGE). Acceptance is the tracing default and the trace
		// is EXACT (no cone): a full voxel of acceptance plus the expand, with a cone capped at a
		// voxel beyond 20 voxels of travel, would accept anything within 1.87 voxels of a probe
		// ray's path, so steep rays from wall cages would resolve such hits and no world probe
		// would carry the sky. The half-voxel acceptance, the exact trace and the
		// open-exhaustion miss below let those rays see the sky, so a change of sky intensity
		// carries into the shadowed walls' GI (derivations in gi_constants.h).
		SdfRayHit hit = near_hit;
		BRANCH
		if(!near_hit.hit)
		{
			hit = SdfTraceClipmap(origin, direction, near_field, t_max, GI_WORLD_PROBE_TRACE_STEPS,
			                      GI_WORLD_PROBE_TRACE_BIAS, GI_WORLD_PROBE_TRACE_RELAXATION,
			                      true, -1.0, true);
			hit.steps += near_hit.steps;
			hit.exhausted = hit.exhausted || near_hit.exhausted;
			hit.clearance = min(hit.clearance, near_hit.clearance);
		}
		vec3 radiance;
		float hit_t;
		// The stored distance is the CLAMPED depth the convolve consumes (misses store the clamp
		// itself, hits at most GI_WORLD_PROBE_HIT_DEPTH_CAP of it: a miss and a hit beyond the
		// clamp are the same moments within one percent but DISTINCT sky shares), always
		// positive, so it can ride the running mean like the radiance - under the direction
		// jitter a "latest sample" depth would make the Chebyshev moments flicker per window and
		// trip the cage-visibility marches. Zero stays the never-measured mark (a fresh clear or
		// a buried probe).
		float depth_clamp = GI_WORLD_PROBE_DEPTH_CLAMP * GiWorldProbeSpacing(level);
		// EXHAUSTION IN OPEN AIR IS A MISS for a probe ray. The trace's exhaustion hit is an
		// occlusion contract for surface-born rays; a budget-dead radiance-cache ray that never
		// came within two voxels of composed geometry (RAW clearance - the expand does not
		// count) took at least 1.13 voxels per step while its level answered, so it crossed
		// about 18 m of open air at level 0 before dying: not an occluder, it reads the sky, as
		// GiTraceShadow grades its own exhaustion. A ray hugging a wall stays a hit - inside a
		// sealed room that is the ray that must not launder into sky, which a one-voxel
		// clearance threshold lets it do.
		float probe_voxel = u_sdf_clipmap_levels[level].w;
		bool open_exhaustion =
		    hit.exhausted && hit.clearance >= GI_WORLD_PROBE_OPEN_CLEARANCE_VOXELS * probe_voxel;
		if(!hit.hit || open_exhaustion)
		{
			radiance = eval_radiance_sh(s_gi_env_sh, direction);
			hit_t = depth_clamp;
		}
		else
		{
			// The stored DEPTH lands on the surface: the trace accepted the hit bias + expand
			// voxels short of it, and the raw field reading there is a lower bound on the
			// remaining distance (SdfRayHit::hit_field) - exact for a head-on hit,
			// conservative at grazing incidence, never beyond the first surface. Without it
			// every live probe over a flat floor would measure the floor nearer than the floor's
			// own biased query, and the Chebyshev test would reject the whole cage with
			// confidence, leaving the floor with zero irradiance.
			// Capped UNDER the clamp: the clamp itself is the miss marker the convolve's sky
			// share reads (GI_WORLD_PROBE_HIT_DEPTH_CAP).
			hit_t = min(hit.t + hit.hit_field, depth_clamp * GI_WORLD_PROBE_HIT_DEPTH_CAP);
			vec3 hit_position = origin + direction * hit.t;
			vec3 hit_normal = hit.normal;
			if(dot(hit_normal, direction) > 0.0)
			{
				hit_normal = -hit_normal;
			}
			vec3 voxel_radiance;
			// Cross-faded across cascade levels like the gather's read (GI_LIGHT_VOXEL_FADE_VOXELS).
			if(!GiLightVoxelReadBlend(hit_position, hit_normal, GI_LIGHT_VOXEL_FADE_VOXELS, voxel_radiance))
			{
				// Occluded but unmeasured: honest darkness, never fabricated energy - the
				// sealed room converges black through exactly this branch.
				voxel_radiance = vec3_splat(0.0);
			}
			radiance = voxel_radiance;
			// The cone bound only for a hit brighter than GI_MAX_RAY_RADIANCE: the emitter table
			// walk is too costly to pay on every hit.
			//
			// The threshold is ABSOLUTE, not the view-relative GiViewToCached form Lumen's
			// radiosity rule would use. This atlas is a PERSISTENT store shared across views,
			// and an exposure-relative gate makes its contents follow whatever the camera was
			// metering when each probe last traced; under a high pre-exposure the relative
			// threshold also drops low enough to fire on ordinary emitter-lit texels.
			if(GiStatsLuminance(voxel_radiance) > GI_MAX_RAY_RADIANCE)
			{
				radiance *= GiWorldProbeEmitterCoverage(direction, origin);
			}
		}
		// No absolute radiance clamp: a small emitter's texel is bounded by its cone fraction
		// above (the geometry), a large one keeps its radiance, which a clamp would cap.
		vec3 stored = radiance;
		float stored_t = hit_t;
		// The texel's previous value: the running mean's base below, and the census's
		// "did this ray change anything" reference either way.
		vec4 previous = imageLoad(s_world_probe_radiance_out, texel);
		BRANCH
		if(mean_blend < 1.0)
		{
			// Running mean over windows: the previous value is this texel's own, written by
			// this thread's slot one window ago (the seed or the clear on a fresh claim -
			// both replaced at write-through while the count is zero). A never-measured depth
			// (0) is not averaged into.
			stored = mix(GiFiniteOrZero(previous.xyz), stored, mean_blend);
			if(previous.w > 0.0)
			{
				stored_t = mix(previous.w, hit_t, mean_blend);
			}
		}
		imageStore(s_world_probe_radiance_out, texel, vec4(stored, stored_t));
		// The texel census (editor statistics): census armed only; the flush below then
		// never runs either.
		if(u_world_probe_census)
		{
			float relative_change = GiStatsRelativeChange(GiStatsLuminance(stored),
			                                              GiStatsLuminance(GiFiniteOrZero(previous.xyz)));
			census_texels += 1u;
			census_moved += relative_change > GI_QUIESCENCE_CONVERGED_MEAN ? 1u : 0u;
			census_visible += relative_change > GI_STATS_VISIBLE_CHANGE ? 1u : 0u;
		}
	}
	// CENSUS FLUSH. Shared atomics from every lane, then the last lane of the slot to arrive
	// (the completion counter) publishes the slot's sums. Reads go through atomics too, so
	// they queue behind the other lanes' adds in the shared-memory pipeline without a group
	// barrier - which the early return above forbids.
	if(census_texels > 0u)
	{
		atomicAdd(s_census_texels[slot_in_group], census_texels);
		atomicAdd(s_census_moved[slot_in_group], census_moved);
		atomicAdd(s_census_visible[slot_in_group], census_visible);
		uint arrived = 0u;
		atomicFetchAndAdd(s_census_done[slot_in_group], 1u, arrived);
		if(arrived == uint(GI_WORLD_PROBE_RAYS_PER_FRAME - 1))
		{
			uint texels = 0u;
			uint moved = 0u;
			uint visible = 0u;
			atomicFetchAndAdd(s_census_texels[slot_in_group], 0u, texels);
			atomicFetchAndAdd(s_census_moved[slot_in_group], 0u, moved);
			atomicFetchAndAdd(s_census_visible[slot_in_group], 0u, visible);
			imageAtomicAdd(s_gi_vis_memo, GiLightVoxelStatsTexel(level, GI_STATS_PROBE_TEXELS), texels);
			imageAtomicAdd(s_gi_vis_memo, GiLightVoxelStatsTexel(level, GI_STATS_PROBE_TEXELS_MOVED), moved);
			imageAtomicAdd(s_gi_vis_memo, GiLightVoxelStatsTexel(level, GI_STATS_PROBE_TEXELS_VISIBLE), visible);
		}
	}
}
