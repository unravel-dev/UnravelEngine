#ifndef __GI_WORLD_PROBES_SH__
#define __GI_WORLD_PROBES_SH__

/*
 * World probe cascades (gi_rewrite_plan.md 3.3, revised): octahedral radiance/irradiance probes on a
 * TOROIDAL world-anchored lattice per SDF cascade. They carry offscreen and distant energy,
 * complete shortened gather rays, and feed the light voxels' bounce term.
 *
 * LATTICE. Probes live on an absolute world grid of spacing = cascade voxel *
 * GI_WORLD_PROBE_DIVISOR (2 m at level 0). A level's window covers GiWorldProbeAxis(level)^3
 * cells around the camera's cell (the axis is PER LEVEL, see the axis note below). Levels
 * 1-3 are DENSE: a probe's storage slot is its world cell index mod the axis, so camera
 * motion never moves or copies a probe - cells enter and leave the window, and a slot whose
 * cell changed is detected by the cell-id buffer and refilled. Camera ROTATION touches
 * nothing (R1 by construction).
 *
 * LEVEL 0 IS SPARSE (2026-09-13, gi_single_lighting_plan.md phase D, the shape of Lumen's
 * radiance cache): its window is wide (+-46 m) but only the cells a consumer ASKED FOR hold
 * a probe. A dense toroidal INDEX (one entry per window cell, GI_WORLD_PROBE_AXIS_L0^3) maps
 * a cell to a slot of a fixed POOL (GI_WORLD_PROBE_POOL_L0), or to none. Every reader that
 * resolves a level-0 cage stamps its base cell into the index's request lane
 * (GiWorldProbeRequest); an ungated allocation pass before the quiescence gate
 * (cs_gi_world_probe_alloc.sc) claims free pool slots for the eight cells around every
 * recent stamp, frees slots whose cell left the window or went unrequested, and the trace
 * seeds and traces a fresh slot the same frame. A cell without a probe reads as a DEAD cage
 * corner (no data, no verdict), so a cage arriving probe by probe renormalises onto what it
 * has and an all-absent cage falls through to the coarser lattice - exactly the buried-probe
 * contract. Why: the 2 m cage must answer in the alcove's own air from ANY camera distance
 * (the lion niche read 0.078 from 2.8 m and 1.41 from 21 m through the hall's 4 m probes,
 * gi_lighting_audit section 20), and a dense 2 m lattice over the same reach would be 36k
 * probes; Sponza's live set is a few thousand.
 *
 * UPDATE. Every probe, every frame, GI_WORLD_PROBE_RAYS_PER_FRAME rays at FIXED octahedral
 * texel centres: stratum s = frame mod GI_WORLD_PROBE_WINDOW covers texels where
 * (texel_index mod WINDOW) == s, so every direction refreshes exactly once per window and the
 * radiance atlas is a zero-variance windowed mean. Rays read the light voxels at hits and the
 * sky SH at miss.
 *
 * ATLASES (2D; every probe slot of every level in ONE row-major run of
 * GI_WORLD_PROBE_ATLAS_TILES_X tiles per row, addressed by the LINEAR slot index - level 0's
 * pool first, then the dense levels in order - GiWorldProbeTileBase):
 *  - radiance: probe tiles of OCT_RADIANCE^2 texels. rgb radiance, a hitT (negative =
 *    miss/sky). Point-fetched only, so no gutter.
 *  - irradiance: same tile grid at (OCT_IRRADIANCE+2)^2 texels - 1-texel octahedral gutter for
 *    hardware bilinear. rgb = E/pi at the texel's normal direction, a = sky fraction.
 *  - depth: same gutter layout, RG16F = (mean, mean^2) of hitT under a
 *    cos^GI_WORLD_PROBE_DEPTH_SHARPNESS lobe, for the Chebyshev visibility test.
 */

#include "gi_constants.sh"
#include "gi_probe_common.sh"

#ifndef GI_FINITE_OR_ZERO_DEFINED
#define GI_FINITE_OR_ZERO_DEFINED
/// Zero when non-finite (or absurdly large): the probe<->voxel feedback loop has no decay for
/// a NaN - each side re-ingests the other every cycle, so one poisoned texel converges the
/// whole field to NaN within a window (measured on Linux/Vulkan, where never-written texels
/// read as garbage: a white flash, then GI collapsing to black). An ordered comparison is
/// used rather than isnan(), which relaxed-math shader compilation may fold away; NaN fails
/// every ordered comparison, so it cannot pass this one.
vec3 GiFiniteOrZero(vec3 v)
{
	return all(lessThan(abs(v), vec3_splat(1e30))) ? v : vec3_splat(0.0);
}
#endif // GI_FINITE_OR_ZERO_DEFINED

/// Cells per axis of each level's window - odd, so a window has a centre cell. LEVEL 0 IS
/// WIDE ON PURPOSE (measured 2026-09-13, gi_lighting_audit sections 18 and 20): a gather ray
/// completes from the finest probe cage covering its completion point and the relight's
/// bounce reads the finest cage covering its cell, and only the 2 m lattice resolves the
/// courtyard's sky visibility or an alcove's own opening - a 4 m or 8 m cage blends gallery
/// probes into open-well queries and hall probes into the alcove, and the same floor read
/// E/pi 1.2 from 2.5 m and 0.25 from 15 m. Level 0 is the sparse pool's INDEX window: 49
/// cells reach +-46 m usable, so a surface up to ~40 m from the camera resolves its 2 m cage
/// at any camera distance, paid for only where probes were requested (see the header). The
/// coarser lattices answer where level 0 has no probe yet and beyond its reach (levels 1 and
/// 2 at 13: +-24 m / +-40 m; level 3 at 9: +-56 m). The lattice does not derive from the
/// cascade resolution: the SDF window and the probe window are independent extents. Mirrored
/// by global_sdf_clipmap_gpu::world_probe_axis / world_probe_pool_l0 /
/// world_probe_atlas_tiles_x (atlas and buffer sizes) and verified by the gi oracle suite.
#define GI_WORLD_PROBE_AXIS_L0 49
#define GI_WORLD_PROBE_AXIS_L1 13
#define GI_WORLD_PROBE_AXIS_L2 13
#define GI_WORLD_PROBE_AXIS_L3 9
/// Level 0's probe POOL: the slots the index hands out. Sponza's live set measured a few
/// thousand (the building's surface cells' cages plus the completion points 8 m off them);
/// the pool is sized for a margin over that, and the allocation pass tightens its eviction
/// age when fewer than a GI_WORLD_PROBE_POOL_PRESSURE_DIVISOR-th of it is free. A multiple of
/// the trace's four-probe groups.
#define GI_WORLD_PROBE_POOL_L0 8192
/// Tiles per atlas row (every level's tiles in one linear run): 128 x 16 = 2048 texels of
/// radiance per row, the pool alone filling 64 rows.
#define GI_WORLD_PROBE_ATLAS_TILES_X 128
/// The index buffer (stage 13 of every cage reader, read-write for the ones that request):
/// three lanes of GI_WORLD_PROBE_INDEX_CELLS entries - the pool slot serving the cell
/// (GI_WORLD_PROBE_NONE when unallocated), the packed cell of the last request stamped at
/// the entry, the clock tick of that stamp - then the allocation clock, the free stack's
/// count and the free stack itself. Cells are addressed by GiWorldProbeIndexSlot (cell mod
/// axis, toroidal like the dense windows).
#define GI_WORLD_PROBE_INDEX_CELLS        (GI_WORLD_PROBE_AXIS_L0 * GI_WORLD_PROBE_AXIS_L0 * GI_WORLD_PROBE_AXIS_L0)
#define GI_WORLD_PROBE_INDEX_SLOT_BASE    0
#define GI_WORLD_PROBE_INDEX_REQUEST_BASE GI_WORLD_PROBE_INDEX_CELLS
#define GI_WORLD_PROBE_INDEX_STAMP_BASE   (2 * GI_WORLD_PROBE_INDEX_CELLS)
#define GI_WORLD_PROBE_INDEX_CLOCK        (3 * GI_WORLD_PROBE_INDEX_CELLS)
#define GI_WORLD_PROBE_INDEX_FREE_COUNT   (3 * GI_WORLD_PROBE_INDEX_CELLS + 1)
#define GI_WORLD_PROBE_INDEX_FREE_BASE    (3 * GI_WORLD_PROBE_INDEX_CELLS + 2)
#define GI_WORLD_PROBE_INDEX_SIZE         (3 * GI_WORLD_PROBE_INDEX_CELLS + 2 + GI_WORLD_PROBE_POOL_L0)
/// An unallocated index entry, and the cell id of a FREE pool slot (the cell buffer's seed
/// sentinel, which the dense levels also start from).
#define GI_WORLD_PROBE_NONE 0xFFFFFFFFu
/// The window count the allocation pass writes into a freshly claimed pool slot: the trace
/// reads it as "seed and clear me" and replaces it with a real count the same frame.
#define GI_WORLD_PROBE_COUNT_FRESH 0xFFFFFFFFu

/// x = probe spacing of level 0 in world units (doubles per level), y = frame index,
/// z = non-zero when the probe atlases are resident, w = the cage-visibility variance gate
/// in probe spacings (std of the depth lobe; 0 = march every probe). The shipped default is
/// GI_WORLD_PROBE_CAGE_VIS_VARIANCE_GATE - a consumer that never sets the lane gets 0, the
/// march-always safe-slow extreme, never a leak.
uniform vec4 u_gi_world_probe_params;
#define u_world_probe_base_spacing  u_gi_world_probe_params.x
#define u_world_probe_frame         uint(u_gi_world_probe_params.y)
#define u_world_probe_ready         (u_gi_world_probe_params.z > 0.0)
#define u_world_probe_cage_vis_gate u_gi_world_probe_params.w

float GiWorldProbeSpacing(int level)
{
	return u_world_probe_base_spacing * float(1 << level);
}

/// Probes per axis of a level's window (see the axis note above).
int GiWorldProbeAxis(int level)
{
	return level == 0 ? GI_WORLD_PROBE_AXIS_L0
	                  : (level == 1 ? GI_WORLD_PROBE_AXIS_L1
	                                : (level == 2 ? GI_WORLD_PROBE_AXIS_L2 : GI_WORLD_PROBE_AXIS_L3));
}

/// Probe SLOTS of one level: the pool for the sparse level 0, the window's cells for the
/// dense levels.
int GiWorldProbeLevelCount(int level)
{
	if(level == 0)
	{
		return GI_WORLD_PROBE_POOL_L0;
	}
	int axis = GiWorldProbeAxis(level);
	return axis * axis * axis;
}

/// First linear slot of a level: the cell-id and count buffers are level-major.
int GiWorldProbeLevelBase(int level)
{
	int base = 0;
	if(level > 0)
	{
		base += GiWorldProbeLevelCount(0);
	}
	if(level > 1)
	{
		base += GiWorldProbeLevelCount(1);
	}
	if(level > 2)
	{
		base += GiWorldProbeLevelCount(2);
	}
	return base;
}

/// The level a linear slot belongs to; SDF_CLIPMAP_LEVEL_COUNT past the last probe (a
/// partial final dispatch group).
int GiWorldProbeLevelOfSlot(int slot_linear)
{
	int level = 0;
	int base = 0;
	LOOP
	for(int l = 0; l < SDF_CLIPMAP_LEVEL_COUNT; ++l)
	{
		base += GiWorldProbeLevelCount(l);
		if(slot_linear >= base)
		{
			level = l + 1;
		}
	}
	return level;
}

/// Usable half extent of a level's window around its centre, in world units: the outermost
/// cell on each side is excluded (the blend band, and the cells that may be mid-refill).
float GiWorldProbeHalfExtent(int level)
{
	return (float(GiWorldProbeAxis(level) - 1) * 0.5 - 1.0) * GiWorldProbeSpacing(level);
}

/// Wraps a world cell index onto its toroidal slot within @p level's window: the storage
/// slot of a dense level, the INDEX entry of the sparse level 0.
ivec3 GiWorldProbeSlot(ivec3 cell, int level)
{
	// True mathematical modulo for negative cells; HLSL/GLSL % is implementation-inconvenient
	// on negatives, so bias well into positives first (cells are bounded far below 1<<20).
	int axis = GiWorldProbeAxis(level);
	ivec3 biased = cell + ivec3(1048576, 1048576, 1048576);
	return ivec3(biased.x % axis, biased.y % axis, biased.z % axis);
}

/// The level-0 index entry of a world cell (a lane-relative offset into the index buffer).
int GiWorldProbeIndexSlot(ivec3 cell)
{
	ivec3 slot = GiWorldProbeSlot(cell, 0);
	return (slot.z * GI_WORLD_PROBE_AXIS_L0 + slot.y) * GI_WORLD_PROBE_AXIS_L0 + slot.x;
}

/// Top-left texel of a probe slot's tile in an atlas with @p tile_edge texels per tile:
/// one row-major run over the LINEAR slot index (level 0's pool, then the dense levels).
ivec2 GiWorldProbeTileBase(int slot_linear, int tile_edge)
{
	return ivec2((slot_linear % GI_WORLD_PROBE_ATLAS_TILES_X) * tile_edge,
	             (slot_linear / GI_WORLD_PROBE_ATLAS_TILES_X) * tile_edge);
}

/// World position of a probe cell's lattice point.
vec3 GiWorldProbeCellPosition(ivec3 cell, int level)
{
	return vec3(cell) * GiWorldProbeSpacing(level);
}

/// Linear slot index of a DENSE level's toroidal slot (levels 1-3): the cell-id buffer's and
/// the atlases' addressing. Level 0's slots come from the index (GiWorldProbeLookupSlot).
int GiWorldProbeSlotIndex(ivec3 slot, int level)
{
	int axis = GiWorldProbeAxis(level);
	return GiWorldProbeLevelBase(level) + (slot.z * axis + slot.y) * axis + slot.x;
}

/// Packs a world cell for the cell-id buffer. 10 bits per axis (biased), 2 bits of level - the
/// same one-word identity trick the surface list uses.
uint GiWorldProbePackCell(ivec3 cell, int level)
{
	ivec3 biased = cell + ivec3(512, 512, 512);
	return uint(biased.x & 0x3FF) | (uint(biased.y & 0x3FF) << 10u) | (uint(biased.z & 0x3FF) << 20u) |
	       (uint(level) << 30u);
}

/// The world cell of a packed id (the level bits dropped): how a sparse level-0 slot learns
/// which cell it was claimed for.
ivec3 GiWorldProbeUnpackCell(uint packed)
{
	return ivec3(int(packed & 0x3FFu), int((packed >> 10u) & 0x3FFu), int((packed >> 20u) & 0x3FFu)) -
	       ivec3(512, 512, 512);
}

/// The finest level COARSER than @p level whose window covers a point @p largest (Chebyshev
/// distance) from the window centre; SDF_CLIPMAP_LEVEL_COUNT when none does. The cascade
/// readers blend a cage into this one over its window's outer band. It used to be level + 1
/// by construction (each window enclosed the finer one); level 0's +-46 m index window now
/// reaches past levels 1 and 2, so its outer band blends into level 3.
int GiWorldProbeFarLevel(int level, float largest)
{
	LOOP
	for(int far = level + 1; far < SDF_CLIPMAP_LEVEL_COUNT; ++far)
	{
		if(largest <= GiWorldProbeHalfExtent(far))
		{
			return far;
		}
	}
	return SDF_CLIPMAP_LEVEL_COUNT;
}

/// The sparse index (see the layout defines). Stage 13, which the cull grid's second buffer
/// held until the two were merged (sdf_common.sh b_sdf_grid). Read-write for the consumers
/// that REQUEST probes (GI_WORLD_PROBE_INDEX_RW: the gather, the relight, the allocation pass
/// itself), read-only for the rest of the cage readers.
#if defined(GI_WORLD_PROBE_INDEX_RW)
BUFFER_RW(b_world_probe_index, uint, 13);
#define GI_WORLD_PROBE_INDEX_BOUND
#elif defined(GI_WORLD_PROBE_READ)
BUFFER_RO(b_world_probe_index, uint, 13);
#define GI_WORLD_PROBE_INDEX_BOUND
#endif

#if defined(GI_WORLD_PROBE_INDEX_BOUND)
/// The linear probe slot serving @p cell at @p level, or -1 when level 0 holds no probe for
/// it yet (the reader treats that corner as DEAD - no data, no verdict - and requests it).
/// The allocation pass keeps the invariant that a live index entry's slot holds exactly the
/// cell the entry stands for under this frame's window, so no cell check is needed here.
int GiWorldProbeLookupSlot(ivec3 cell, int level)
{
	int slot = -1;
	if(level == 0)
	{
		uint entry = b_world_probe_index[GI_WORLD_PROBE_INDEX_SLOT_BASE + GiWorldProbeIndexSlot(cell)];
		if(entry != GI_WORLD_PROBE_NONE)
		{
			slot = int(entry);
		}
	}
	else
	{
		slot = GiWorldProbeSlotIndex(GiWorldProbeSlot(cell, level), level);
	}
	return slot;
}

/// A level-0 cage reader asks for the eight probes around @p base_cell (its trilinear base):
/// one stamp per read, at the base cell; the allocation pass dilates it to the cage. Kept
/// cheap on the gather's completion path: a read first, the two stores only when the entry
/// does not already carry this frame's stamp for this cell (most completions in a frame
/// share cells). Requests also keep a live probe ALIVE: eviction is by stamp age. A no-op in
/// read-only consumers (the debug view).
void GiWorldProbeRequest(ivec3 base_cell)
{
#if defined(GI_WORLD_PROBE_INDEX_RW)
	int index_slot = GiWorldProbeIndexSlot(base_cell);
	uint clock = b_world_probe_index[GI_WORLD_PROBE_INDEX_CLOCK];
	uint packed = GiWorldProbePackCell(base_cell, 0);
	if(b_world_probe_index[GI_WORLD_PROBE_INDEX_STAMP_BASE + index_slot] != clock ||
	   b_world_probe_index[GI_WORLD_PROBE_INDEX_REQUEST_BASE + index_slot] != packed)
	{
		b_world_probe_index[GI_WORLD_PROBE_INDEX_REQUEST_BASE + index_slot] = packed;
		b_world_probe_index[GI_WORLD_PROBE_INDEX_STAMP_BASE + index_slot] = clock;
	}
#endif
}

/// Whether a level-0 @p cell (inside the index window) was requested within @p max_age clock
/// ticks: a stamp for any of the eight base cells whose cage contains it. The stamp's packed
/// cell must match - the toroidal entry may still hold a departed cell's request.
bool GiWorldProbeCellWanted(ivec3 cell, uint clock, uint max_age)
{
	LOOP
	for(int corner = 0; corner < 8; ++corner)
	{
		ivec3 base_cell = cell - ivec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
		int index_slot = GiWorldProbeIndexSlot(base_cell);
		if(b_world_probe_index[GI_WORLD_PROBE_INDEX_REQUEST_BASE + index_slot] ==
		   GiWorldProbePackCell(base_cell, 0))
		{
			uint age = clock - b_world_probe_index[GI_WORLD_PROBE_INDEX_STAMP_BASE + index_slot];
			if(age <= max_age)
			{
				return true;
			}
		}
	}
	return false;
}
#endif // GI_WORLD_PROBE_INDEX_BOUND

#if defined(GI_WORLD_PROBE_READ)

/// Irradiance + depth atlases for consumers. RESERVED STAGES 14 is the env SH; these use 11/15
/// unless the includer overrides beforehand. A consumer that only COMPLETES rays (radiance +
/// depth, never the irradiance cage) defines GI_WORLD_PROBE_SKIP_IRRADIANCE to leave stage 11
/// free - the screen probe trace hands that stage to its compacted probe list.
#ifndef GI_WORLD_PROBE_SKIP_IRRADIANCE
SAMPLER2D(s_world_probe_irradiance, 11);
#endif // GI_WORLD_PROBE_SKIP_IRRADIANCE
SAMPLER2D(s_world_probe_depth, 15);

/// xy = 1 / atlas size of the irradiance+depth atlases (they share a layout).
uniform vec4 u_gi_world_probe_atlas;

/**
 * Straight-segment visibility between a query point and one cage probe, asked of the global
 * SDF clipmap itself (every GI_WORLD_PROBE_READ consumer includes sdf_common.sh first, so
 * SdfSampleClipmap is in scope). Returns 1 when the field stays open along the segment, 0 when
 * a closed surface separates the pair.
 *
 * This is the reader-side defence the depth moments cannot provide at silhouettes: an 8x8
 * octahedral depth texel averages a ~22-degree cone, so from a probe OUTSIDE a room a
 * direction grazing a wall edge mixes "wall at 3 m" with "open to 12 m" - the mean lands
 * beyond the interior query (no Chebyshev test at all) and the variance explodes (Chebyshev
 * ~1 when tested). Measured: a sealed box read the exterior cage's sky at full weight through
 * exactly those texel wedges, at every level whose probes straddle the walls. The clipmap has
 * the wall itself; asking it is exact where the moments are statistical.
 *
 * Cost shape: one mid-segment sample proves an open cage (the common case); a blocked segment
 * convicts in the few samples it takes the sphere trace to reach the wall. The uniform
 * fallback step derived from GI_WORLD_PROBE_CAGE_VIS_STEPS keeps the walk skip-proof (it can
 * never cross the acceptance band around a sealing wall between samples), so exhaustion is
 * unreachable and the loop bound is a formality. Endpoint guards excuse the query's own
 * surface and a wall-hugging probe's contact zone; over-occlusion beyond that is the SAFE
 * direction - a wrongly-rejected probe renormalises the cage toward its visible members,
 * while a wrongly-accepted one imports the outdoors into a sealed room.
 */

/// The march's field read: the FINEST covering level, deliberately UNBLENDED. The cross-fade
/// SdfSampleClipmapEx applies is what TRACING wants - one continuous function so consumers
/// resolve one surface - and exactly wrong for an occlusion VERDICT: inside the blend band the
/// coarse level contaminates the reading, so along the camera-locked handover shell a thin
/// wall's blended through-minimum floats above any conviction depth (measured: sky arcs on
/// sealed walls tracking the shell) and an on-surface query's blended reading dips below it
/// (measured: a dark low-sky ring on open ground at the shell radius). Each level alone is
/// conservative (test_clipmap_is_conservative), so the unblended verdict stays sound; the
/// step discontinuity between samples that the blend exists to remove is harmless to a walk
/// that never resolves a surface.
float GiCageVisibilitySample(vec3 position)
{
	float blend;
	float voxel;
	int level = SdfFindClipmapLevel(position, blend, voxel);
	if(level >= SDF_CLIPMAP_LEVEL_COUNT)
	{
		return SDF_CLIPMAP_OUTSIDE;
	}
	return SdfSampleClipmapLevel(level, position);
}

float GiWorldProbeCageVisibility(vec3 from, vec3 to, float spacing)
{
	vec3 delta = to - from;
	float segment_length = length(delta);
	float field_voxel = spacing / float(GI_WORLD_PROBE_DIVISOR);
	float guard = GI_WORLD_PROBE_CAGE_VIS_GUARD_VOXELS * field_voxel;
	float t = guard;
	float t_end = segment_length - guard;
	if(t >= t_end)
	{
		return 1.0;
	}
	vec3 direction = delta / segment_length;
	// One conservative sphere centred on the segment's midpoint covering both halves proves
	// the whole segment open - the field is an under-estimate, so this cannot false-pass.
	float half_length = 0.5 * segment_length;
	if(GiCageVisibilitySample(from + direction * half_length) >= half_length)
	{
		return 1.0;
	}
	float accept = GI_WORLD_PROBE_CAGE_VIS_ACCEPT_VOXELS * field_voxel;
	/*
	 * CROSSING conviction, alongside the negative-core one above.
	 *
	 * The negative test asks the field for a NEGATIVE INTERIOR, which a wall thinner than one
	 * voxel of the level answering the sample simply does not have: the trilinear
	 * reconstruction smooths its two faces into a dip that never reaches zero, so the march
	 * walks straight through a wall the probe rays themselves stop dead on (they convict at
	 * d < accept + expand = about +1.9 voxels - the two conventions disagree by ~19x AND by
	 * sign). Raising this threshold to a positive proximity is what cannot be done: legitimate
	 * cage segments run PARALLEL to the query's own surface by construction - on flat ground
	 * four of the eight cage probes lie in the floor plane and the biased query clears it by
	 * ~0.4 voxel - and a proximity test blocked those whole cages, painting the black
	 * rings/donuts on open ground that ACCEPT_VOXELS is negative to avoid.
	 *
	 * Proximity is the wrong question; the field's SHAPE along the segment separates the two
	 * cases outright. Because the field is 1-Lipschitz and a sphere trace steps by its own
	 * reading, the measured rate d(d)/dt along the walk is exactly the sine of the segment's
	 * incidence on the surface: a segment CROSSING a wall descends into a minimum and climbs
	 * out the other side at that rate, while a segment GRAZING one runs at near-constant
	 * clearance (rate ~0) and a segment ENDING on one (the flat-ground cage) descends
	 * monotonically and never climbs. So conviction takes a V: a minimum inside the surface
	 * band, followed by a rise at crossing rate. Grazes have no V, floor cages have no rise,
	 * and no threshold has to be calibrated against a wall thickness.
	 *
	 * Strictly additive - the negative core still convicts on its own - so the verdict can
	 * only ever get MORE conservative, which is the direction this reader must fail in. Costs
	 * nothing: the discriminator is arithmetic over samples the march already takes.
	 */
	float band = GI_WORLD_PROBE_CAGE_VIS_CROSS_VOXELS * field_voxel;
	float minimum_d = SDF_CLIPMAP_OUTSIDE;
	float minimum_t = t;
	float base_step = (t_end - t) / float(GI_WORLD_PROBE_CAGE_VIS_STEPS);
	LOOP for(int i = 0; i < GI_WORLD_PROBE_CAGE_VIS_STEPS; ++i)
	{
		float d = GiCageVisibilitySample(from + direction * t);
		if(d < accept)
		{
			return 0.0;
		}
		if(d < minimum_d)
		{
			minimum_d = d;
			minimum_t = t;
		}
		// Climbing away from a minimum that reached the surface band, at crossing rate: the
		// segment went through. Tested BEFORE the clearance break, which the far side of a
		// crossed wall would otherwise satisfy first.
		else if(minimum_d < band &&
		        (d - minimum_d) >= GI_WORLD_PROBE_CAGE_VIS_CROSS_SLOPE * (t - minimum_t))
		{
			return 0.0;
		}
		// The rest of the segment fits in this sample's clearance sphere: open, done.
		if(d >= t_end - t)
		{
			break;
		}
		t += max(d, base_step);
		if(t >= t_end)
		{
			break;
		}
	}
	return 1.0;
}

/// Self-shadow bias [DDGI21 Eq.2]: move the query toward the surface's clear side before any
/// visibility test. CAPPED at field-voxel scale: the spacing-proportional magnitude DDGI
/// publishes (0.225 x spacing = 0.45 m at the 2 m lattice) TUNNELS THROUGH any wall thinner
/// than it - the biased point lands outside, Chebyshev sees the exterior probe unoccluded,
/// and a sunlit exterior floods a closed room (measured on the thick-walled test room; the
/// documented DDGI thin-wall failure). Clearing the query surface's own field shadow is a
/// VOXEL-scale need, so two voxels of the level's field is enough - and stays below any
/// wall the field itself can resolve. Shared by the cage read and the bounce memo's mask
/// fill - the mask's bits must describe exactly the biased point the read walks from.
/// CPU transcription in gi_tests.cpp (world_probe_biased_query): keep in step by hand.
vec3 GiWorldProbeBiasedQuery(vec3 position, vec3 normal, vec3 view_direction, float spacing)
{
	float bias_magnitude = min(GI_SELF_SHADOW_BIAS_SCALE * GI_SELF_SHADOW_BIAS_K * spacing,
	                           GI_SELF_SHADOW_BIAS_MAX_VOXELS * spacing / float(GI_WORLD_PROBE_DIVISOR));
	return position + (normal * GI_SELF_SHADOW_BIAS_NORMAL + view_direction * GI_SELF_SHADOW_BIAS_VIEW) *
	                      bias_magnitude;
}

/**
 * The bounce memo's FILL: the field's verdict for every corner of the cage that
 * GiWorldProbeIrradiance would walk at @p level - bit i set when cage corner i (the read's
 * corner enumeration: offset = (i & 1, i >> 1 & 1, i >> 2 & 1) from the biased point's base
 * cell) is field-visible from the biased query. Marches ALL 8 corners, deliberately ungated:
 * the fill is amortised across the memo's lifetime, and an honest bit for every corner is
 * what lets the masked read apply the verdicts to every probe (a strictly wider leak margin
 * than gated marching). CPU transcription in gi_tests.cpp (cage_mask): keep in step by hand.
 */
uint GiWorldProbeCageMask(vec3 position, vec3 normal, vec3 view_direction, int level,
                          uint keep_mask, uint stored_mask)
{
	float spacing = GiWorldProbeSpacing(level);
	vec3 biased = GiWorldProbeBiasedQuery(position, normal, view_direction, spacing);
	ivec3 base_cell = ivec3(floor(biased / spacing));
	uint mask = 0u;
	LOOP for(int corner = 0; corner < 8; ++corner)
	{
		uint bit = 1u << uint(corner);
		// SEGMENT-LOCAL KEEP (gi_light_voxels_kernel.sh, GiSegmentTouchesBox): a corner the
		// caller has proven untouched by every changed region since it was stamped keeps its
		// stored verdict - the field along its segment is byte-identical, so the march would
		// return the same bit. keep_mask 0 is the plain full march.
		BRANCH
		if((keep_mask & bit) != 0u)
		{
			mask |= stored_mask & bit;
		}
		else
		{
			ivec3 offset = ivec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
			vec3 probe_position = GiWorldProbeCellPosition(base_cell + offset, level);
			if(GiWorldProbeCageVisibility(biased, probe_position, spacing) > 0.0)
			{
				mask |= bit;
			}
		}
	}
	return mask;
}

/// Bounce visibility-memo texel layout (an R32U volume, all 32 bits now spoken for):
///   bits  0-7  = the 8-bit NEAR cage mask above,
///   bits  8-13 = a wrapping generation tag shared by both halves (0 reserved as "never
///                stamped" - the volume clears to 0 and the CPU hands out generations 1..63),
///   bits 14-15 = the probe LEVEL the near mask was computed for,
///   bits 16-21 = the face's cavity visibility quantised to 6 bits (error <= 1/126, consumed
///                only as the bounce attenuator and to skip the cavity march). Quantised 0
///                doubles as the CULLED sentinel: a stored non-culled face passed the
///                GI_LIGHT_VOXEL_VISIBILITY_MIN gate with at least one escaping ray of
///                GI_BOUNCE_ESCAPE_RAYS (1/16 quantises to 4), so the encodings never collide.
///   bit  22    = the FAR mask (below) is filled. Filled LAZILY, on the first probe-half HIT
///                whose blend band is open - never on a miss: a miss marches enough already
///                (the near cage), and on churning generations (camera motion re-snapping
///                windows every few frames) an eager ungated far march on every miss cost
///                MORE than the gated read it replaced. A generation that survives to its
///                first hit has proven stable enough to amortise.
///   bit  23    = the PROBE half (near mask, level) is populated. A culled or zero-radiance
///                face stamps only the face half; fabricating mask 0 + level 0 instead would
///                decode as a valid "all-dead level 0" verdict and pin the texel's cage
///                fall-through to the wrong level for the whole generation.
///   bits 24-31 = the FAR-blend cage mask, for the far level of the near tag
///                (GiWorldProbeFarLevel - a pure function of position and window). Whether the
///                blend band is open is a pure function of the texel's position and the
///                window, both frozen within a generation.
/// Every store writes the whole word (both halves share the one generation), and validity is
/// generation match + the half's own populated semantics. These functions are the layout's
/// single source of truth; the CPU transcriptions in gi_oracle.cpp pin them by hand.
#define GI_VIS_MEMO_FACE_HALF_BITS 0x003F0000u

/// The probe half of a full stamp: near mask + level, marked populated; the far mask and its
/// filled bit only when the caller actually marched it (the lazy fill). OR the preserved (or
/// freshly packed) face half on top - the caller owns that half.
uint GiWorldProbeVisMemoPackProbe(uint mask, uint generation, int level, uint far_mask,
                                  bool far_filled)
{
	return (mask & 0xFFu) | ((generation & 0x3Fu) << 8u) | (uint(level) << 14u) |
	       (1u << 23u) | (far_filled ? (1u << 22u) : 0u) | ((far_mask & 0xFFu) << 24u);
}

/// The face half's payload bits (16-21) alone - combine with PackProbe or PackFaceOnly.
/// Culled faces store quantised visibility 0 (see the sentinel note above).
uint GiWorldProbeVisMemoPackFace(float visibility, bool culled)
{
	uint quantised = culled ? 0u : uint(saturate(visibility) * 63.0 + 0.5);
	return quantised << 16u;
}

/// A face-half-only stamp (culled and zero-radiance faces, and the no-cage-answered
/// fall-out): generation + face payload, probe half left unpopulated.
uint GiWorldProbeVisMemoPackFaceOnly(uint face_half, uint generation)
{
	return face_half | ((generation & 0x3Fu) << 8u);
}

uint GiWorldProbeVisMemoMask(uint texel_value)
{
	return texel_value & 0xFFu;
}

uint GiWorldProbeVisMemoGeneration(uint texel_value)
{
	return (texel_value >> 8u) & 0x3Fu;
}

int GiWorldProbeVisMemoLevel(uint texel_value)
{
	return int((texel_value >> 14u) & 0x3u);
}

float GiWorldProbeVisMemoFaceVisibility(uint texel_value)
{
	return float((texel_value >> 16u) & 0x3Fu) * (1.0 / 63.0);
}

bool GiWorldProbeVisMemoFaceCulled(uint texel_value)
{
	return ((texel_value >> 16u) & 0x3Fu) == 0u;
}

bool GiWorldProbeVisMemoFarFilled(uint texel_value)
{
	return (texel_value & (1u << 22u)) != 0u;
}

bool GiWorldProbeVisMemoProbeValid(uint texel_value)
{
	return (texel_value & (1u << 23u)) != 0u;
}

uint GiWorldProbeVisMemoFarMask(uint texel_value)
{
	return (texel_value >> 24u) & 0xFFu;
}

#ifndef GI_WORLD_PROBE_SKIP_IRRADIANCE
/**
 * Irradiance around @p normal at @p position from the 8-probe cage of @p level, with the full
 * DDGI weight chain: trilinear x wrap-shading backface x Chebyshev visibility (cubed, floored)
 * x perception crush, evaluated at the self-shadow-biased point - then the field's own verdict,
 * which a blocked probe does not survive. Returns false when the level's window does not cover
 * the position or the cage never carried weight; a cage whose every probe the FIELD blocked
 * returns TRUE with zero irradiance - sealed is a measurement, and it must not fall through to
 * a coarser cage or the environment term.
 *
 * The field verdict has two forms, chosen by @p use_mask: marched here for the statistically
 * ambiguous band (every consumer's default - see GiWorldProbeIrradiance), or SUPPLIED by the
 * caller as a per-corner bitmask (the light-voxel bounce memo - see
 * GiWorldProbeIrradianceMasked). Everything else in the chain is identical between the two.
 *
 * All the constants are the published DDGI/RTXGI values, owned by gi_constants
 * (tasks/research/research_probe_systems.md section 1.3 quotes the chain verbatim).
 */
bool GiWorldProbeIrradianceInternal(vec3 position, vec3 normal, vec3 view_direction, int level,
                                    bool use_mask, uint visibility_mask,
                                    out vec3 out_irradiance, out float out_sky_fraction, out float out_visible)
{
	out_irradiance = vec3_splat(0.0);
	out_sky_fraction = 0.0;
	// The cage's VISIBLE fraction: the weight that survived the field's verdict over the weight
	// the statistical chain granted. 0 for a sealed cage. The cascade readers scale their
	// blend toward the coarser cage by it, so a fine cage the field sealed never admits a
	// coarse cage that straddles the wall (measured: the completion blend lit a thin-walled
	// sealed room and a narrow corridor from the level-1 cages outside them).
	out_visible = 0.0;
	float spacing = GiWorldProbeSpacing(level);
	vec3 biased = GiWorldProbeBiasedQuery(position, normal, view_direction, spacing);
	vec3 grid = biased / spacing;
	ivec3 base_cell = ivec3(floor(grid));
	vec3 frac = grid - vec3(base_cell);
	// The sparse level: ask for this cage (a no-op in read-only consumers), whether or not it
	// is allocated yet - the stamp is also what keeps a live cage from being evicted.
	if(level == 0)
	{
		GiWorldProbeRequest(base_cell);
	}
	int tile_edge = GI_WORLD_PROBE_OCT_IRRADIANCE + 2;
	vec2 oct_uv = GiOctEncode(normal);
	vec3 sum = vec3_splat(0.0);
	float sky_sum = 0.0;
	float weight_sum = 0.0;
	// What the DDGI chain alone would have granted the cage. Diverging from weight_sum only
	// through the field-visibility term below, it distinguishes "no cage here" from "the cage
	// is here and every probe of it is behind a wall" - two failures with opposite answers.
	float covered_sum = 0.0;
	// LOOP: the body carries the (gated) cage-visibility march; unrolled it multiplies the
	// largest instruction footprint of every caller by eight.
	LOOP
	for(int corner = 0; corner < 8; ++corner)
	{
		ivec3 offset = ivec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
		ivec3 cell = base_cell + offset;
		vec3 probe_position = GiWorldProbeCellPosition(cell, level);
		// Trilinear, floored so a query exactly on a probe plane cannot zero the whole cage.
		vec3 tri = mix(vec3_splat(1.0) - frac, frac, vec3(offset));
		float weight = max(tri.x, 0.001) * max(tri.y, 0.001) * max(tri.z, 0.001);
		// Wrap-shading backface term [RTXGI]: soft, so detail geometry does not reject the
		// whole cage.
		vec3 to_probe = probe_position - position;
		float to_probe_length = max(length(to_probe), 1e-4);
		vec3 dir_to_probe = to_probe / to_probe_length;
		float wrap = (dot(dir_to_probe, normal) + 1.0) * 0.5;
		weight *= wrap * wrap + 0.2;
		// Chebyshev visibility from the depth moments, tested from the BIASED point.
		vec3 biased_to_probe = probe_position - biased;
		float distance_to_probe = max(length(biased_to_probe), 1e-4);
		// An unallocated level-0 cell is a DEAD corner: no data, no verdict, no covered_sum
		// (the same contract as the buried-probe gate below), so an all-absent cage falls
		// through to the coarser level while the requested probes arrive.
		int probe_slot = GiWorldProbeLookupSlot(cell, level);
		if(probe_slot < 0)
		{
			continue;
		}
		ivec2 tile = GiWorldProbeTileBase(probe_slot, tile_edge);
		vec2 depth_oct = GiOctEncode(-biased_to_probe / distance_to_probe);
		vec2 depth_uv =
		    (vec2(tile) + vec2_splat(1.0) + depth_oct * float(GI_WORLD_PROBE_OCT_DEPTH)) *
		    u_gi_world_probe_atlas.xy;
		vec2 moments = texture2DLod(s_world_probe_depth, depth_uv, 0.0).xy;
		// DEAD probe (the dead-probe gate zeroed it): no data is not a verdict - skip without
		// counting toward covered_sum, so an all-dead cage still reports false and the
		// cascade falls through to a level that HAS data, never to blackness.
		if(moments.x <= 1e-4)
		{
			continue;
		}
		// The moments' trustworthiness decides everything below: low variance means the
		// depth lobe saw ONE thing (a wall, or open space) and can be trusted BOTH ways;
		// high variance means a silhouette wedge - the 8x8 oct texel mixes near-wall with
		// far-open, the mean lands anywhere, Chebyshev saturates, and only the field march
		// can answer (the measured sealed-box import channel). The gate is the live setting
		// carried in u_gi_world_probe_params.w (default: the constant of the same name).
		float variance = abs(moments.y - moments.x * moments.x);
		bool moments_ambiguous = variance > u_world_probe_cage_vis_gate *
		                                        u_world_probe_cage_vis_gate * spacing * spacing;
		if(distance_to_probe > moments.x)
		{
			float difference = distance_to_probe - moments.x;
			float chebyshev = variance / (variance + difference * difference);
			chebyshev = chebyshev * chebyshev * chebyshev;
			// CONFIDENTLY blocked: a low-variance lobe whose weight would have sat at the
			// floor is a wall the probe actually measured - contribute exactly nothing.
			// The floor exists so STATISTICAL rejection cannot zero a cage, but flooring
			// confident blocks lets normalisation launder an all-blocked cage's texels back
			// to full amplitude; the covered_sum contract makes the zeroed cage safe (it
			// answers sealed-dark, not sky).
			if(!moments_ambiguous && chebyshev < GI_CHEBYSHEV_WEIGHT_FLOOR)
			{
				covered_sum += weight;
				continue;
			}
			weight *= max(chebyshev, GI_CHEBYSHEV_WEIGHT_FLOOR);
		}
		// Perception crush [DDGI19]: fade dim contributions faster than linear, which is what
		// keeps barely-weighted leaks below visibility.
		weight = max(weight, 1e-6);
		if(weight < GI_PERCEPTION_CRUSH_THRESHOLD)
		{
			weight *= (weight * weight) / (GI_PERCEPTION_CRUSH_THRESHOLD * GI_PERCEPTION_CRUSH_THRESHOLD);
		}
		covered_sum += weight;
		// Field visibility, unfloored and never renormalised around: a wall the clipmap
		// itself reports between the pair is not statistics - a blocked probe contributes
		// exactly nothing, no matter how loud its moments say otherwise (the silhouette-wedge
		// leak). Masked callers apply their pre-paid verdicts to EVERY probe (strictly wider
		// leak margin); marching callers gate to the ambiguous band, which is what keeps the
		// pass affordable - ungated the march walked 8 probes per query and tripled the
		// light-voxel pass, while flat-wall and open lobes never needed it.
		if(use_mask)
		{
			if((visibility_mask & (1u << uint(corner))) == 0u)
			{
				continue;
			}
		}
		else
		{
			// Nested, never an && chain: HLSL && may evaluate both operands (FXC does), and
			// the right operand is the 40-step field march the variance gate exists to skip.
			BRANCH
			if(moments_ambiguous)
			{
				if(GiWorldProbeCageVisibility(biased, probe_position, spacing) <= 0.0)
				{
					continue;
				}
			}
		}
		vec2 irradiance_uv =
		    (vec2(tile) + vec2_splat(1.0) + oct_uv * float(GI_WORLD_PROBE_OCT_IRRADIANCE)) *
		    u_gi_world_probe_atlas.xy;
		vec4 texel = texture2DLod(s_world_probe_irradiance, irradiance_uv, 0.0);
		sum += texel.xyz * weight;
		sky_sum += texel.w * weight;
		weight_sum += weight;
	}
	if(weight_sum <= 1e-5)
	{
		// The window covers the query and the cage carried weight, but the field blocked every
		// probe: the point is SEALED off from its entire cage. The measured answer is darkness
		// - returning false instead would hand the query to a coarser cage or the environment
		// SH, which is precisely the import this term exists to stop.
		return covered_sum > 1e-5;
	}
	out_irradiance = sum / weight_sum;
	out_sky_fraction = sky_sum / weight_sum;
	out_visible = covered_sum > 1e-5 ? saturate(weight_sum / covered_sum) : 1.0;
	return true;
}

/// The default cage read: field verdicts marched here, gated to the Chebyshev-ambiguous band.
bool GiWorldProbeIrradiance(vec3 position, vec3 normal, vec3 view_direction, int level,
                            out vec3 out_irradiance, out float out_sky_fraction, out float out_visible)
{
	return GiWorldProbeIrradianceInternal(position, normal, view_direction, level, false, 0u,
	                                      out_irradiance, out_sky_fraction, out_visible);
}

/// The memoised cage read (light-voxel bounce): field verdicts supplied as the per-corner
/// bitmask GiWorldProbeCageMask filled, applied to every probe in place of the gated march.
bool GiWorldProbeIrradianceMasked(vec3 position, vec3 normal, vec3 view_direction, int level,
                                  uint visibility_mask,
                                  out vec3 out_irradiance, out float out_sky_fraction, out float out_visible)
{
	return GiWorldProbeIrradianceInternal(position, normal, view_direction, level, true,
	                                      visibility_mask, out_irradiance, out_sky_fraction,
	                                      out_visible);
}

/**
 * As @ref GiWorldProbeIrradiance, choosing the finest level whose window covers the position
 * and cross-fading into the next over the outermost probe cell - the DDGI 2021 cascade blend,
 * tightened by one cell so a scrolled plane cannot pop. The light-voxel bounce runs its own
 * memoised twin of this walk (GiBounceProbeIrradiance, gi_light_voxels_kernel.sh): coverage
 * test, all-dead fall-through and blend band must stay in step BY HAND.
 */
bool GiWorldProbeIrradianceCascade(vec3 position, vec3 normal, vec3 view_direction,
                                   vec3 window_center,
                                   out vec3 out_irradiance, out float out_sky_fraction)
{
	for(int level = 0; level < SDF_CLIPMAP_LEVEL_COUNT; ++level)
	{
		float spacing = GiWorldProbeSpacing(level);
		float half_extent = GiWorldProbeHalfExtent(level);
		vec3 delta = abs(position - window_center);
		float largest = max(delta.x, max(delta.y, delta.z));
		if(largest > half_extent)
		{
			continue;
		}
		vec3 near_irradiance;
		float near_sky;
		float near_visible;
		if(!GiWorldProbeIrradiance(position, normal, view_direction, level, near_irradiance, near_sky,
		                           near_visible))
		{
			continue;
		}
		// Blend toward the next COVERING level over the outer half of the last usable cell.
		float band = GI_WORLD_PROBE_BLEND_BAND * spacing;
		float blend = saturate((largest - (half_extent - band)) / band);
		int far_level = GiWorldProbeFarLevel(level, largest);
		if(blend > 0.0 && far_level < SDF_CLIPMAP_LEVEL_COUNT)
		{
			vec3 far_irradiance;
			float far_sky;
			float far_visible;
			if(GiWorldProbeIrradiance(position, normal, view_direction, far_level, far_irradiance, far_sky,
			                          far_visible))
			{
				// Scaled by the NEAR cage's visible fraction: the finer field is the authority, so
				// a sealed fine cage admits nothing from a coarse one that straddles the wall.
				float far_mix = blend * near_visible;
				near_irradiance = mix(near_irradiance, far_irradiance, far_mix);
				near_sky = mix(near_sky, far_sky, far_mix);
			}
		}
		out_irradiance = GiFiniteOrZero(near_irradiance);
		out_sky_fraction = near_sky;
		return true;
	}
	out_irradiance = vec3_splat(0.0);
	out_sky_fraction = 0.0;
	return false;
}
#endif // GI_WORLD_PROBE_SKIP_IRRADIANCE

#ifdef GI_WORLD_PROBE_READ_RADIANCE

/// The raw radiance atlas, for ray COMPLETION - a consumer that reads it binds stage 6.
SAMPLER2D(s_world_probe_radiance_read, 6);

/// xy = 1 / radiance atlas size.
uniform vec4 u_gi_world_probe_radiance_atlas;

/**
 * Radiance arriving from @p direction at @p position, read from the probe cage of the finest
 * covering cascade - what a SHORTENED gather ray completes into after establishing its own
 * near-field visibility [S21 s69-74].
 *
 * Each cage probe is read at its SPHERE-PARALLAX-corrected direction [S21 s73]: the query ray
 * is intersected with a sphere of the probe's stored hit distance around the probe, and the
 * texel toward that intersection is read - removing the positional gap between the ray's origin
 * and the probe's at the price of directional error, which is the trade Lumen ships. Cage
 * weights are trilinear x Chebyshev (no facing terms: radiance has no receiver normal), then
 * the field-visibility verdict, exactly as the irradiance cage applies it - and the same
 * all-blocked contract: TRUE with zero radiance, so a sealed completion answers darkness
 * rather than handing the ray to the environment fallback.
 */
bool GiWorldProbeRadiance(vec3 position, vec3 direction, vec3 window_center, out vec3 out_radiance)
{
	out_radiance = vec3_splat(0.0);
	// LOOP on levels, cages and corners, exactly as the irradiance cage: the corner body
	// carries the (gated) cage-visibility march, and unrolled it multiplied the largest
	// instruction footprint of every completing trace kernel by eight per level.
	LOOP
	for(int level = 0; level < SDF_CLIPMAP_LEVEL_COUNT; ++level)
	{
		float level_spacing = GiWorldProbeSpacing(level);
		float half_extent = GiWorldProbeHalfExtent(level);
		vec3 delta = abs(position - window_center);
		float largest = max(delta.x, max(delta.y, delta.z));
		if(largest > half_extent)
		{
			continue;
		}
		// CASCADE BLEND, the irradiance cascade's band (GiWorldProbeIrradianceCascade): the
		// completion used to take the finest covering level outright, so every gather ray's far
		// energy switched cages at a knife edge 6 / 12 / 24 m from the camera cell - an edge the
		// camera drags across every surface (measured as brightness pops on translation). The
		// far cage is evaluated by the SAME loop body (k = 1) so fxc instantiates the corner
		// walk once (the one-call-site contract of the trace mega-bodies).
		float band = GI_WORLD_PROBE_BLEND_BAND * level_spacing;
		float blend = saturate((largest - (half_extent - band)) / band);
		int far_level = GiWorldProbeFarLevel(level, largest);
		bool wants_far = blend > 0.0 && far_level < SDF_CLIPMAP_LEVEL_COUNT;
		vec3 near_radiance = vec3_splat(0.0);
		vec3 far_radiance = vec3_splat(0.0);
		bool near_answered = false;
		bool far_answered = false;
		float near_visible = 0.0;
		LOOP
		for(int k = 0; k < 2; ++k)
		{
			if(k == 1 && !wants_far)
			{
				break;
			}
			int cage_level = k == 0 ? level : far_level;
			float spacing = GiWorldProbeSpacing(cage_level);
			vec3 grid = position / spacing;
			ivec3 base_cell = ivec3(floor(grid));
			vec3 frac = grid - vec3(base_cell);
			// The sparse level: request this cage (see the irradiance read).
			if(cage_level == 0)
			{
				GiWorldProbeRequest(base_cell);
			}
			vec3 sum = vec3_splat(0.0);
			float weight_sum = 0.0;
			// Same bookkeeping as the irradiance cage: the weight the statistical chain
			// granted, before the field's verdict, so an all-blocked cage can answer darkness
			// instead of falling through to the environment term.
			float covered_sum = 0.0;
			LOOP
			for(int corner = 0; corner < 8; ++corner)
			{
				ivec3 offset = ivec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
				ivec3 cell = base_cell + offset;
				vec3 probe_position = GiWorldProbeCellPosition(cell, cage_level);
				vec3 tri = mix(vec3_splat(1.0) - frac, frac, vec3(offset));
				float weight = max(tri.x, 0.001) * max(tri.y, 0.001) * max(tri.z, 0.001);
				// An unallocated level-0 cell: a DEAD corner (see the irradiance read).
				int probe_slot = GiWorldProbeLookupSlot(cell, cage_level);
				if(probe_slot < 0)
				{
					continue;
				}
				// Chebyshev visibility of the QUERY POINT from the probe, exactly as the
				// irradiance read tests it - a probe behind a wall must not complete rays
				// through it.
				vec3 to_query = position - probe_position;
				float query_distance = max(length(to_query), 1e-4);
				ivec2 depth_tile = GiWorldProbeTileBase(probe_slot, GI_WORLD_PROBE_OCT_IRRADIANCE + 2);
				vec2 depth_uv = (vec2(depth_tile) + vec2_splat(1.0) +
				                 GiOctEncode(to_query / query_distance) * float(GI_WORLD_PROBE_OCT_DEPTH)) *
				                u_gi_world_probe_atlas.xy;
				vec2 moments = texture2DLod(s_world_probe_depth, depth_uv, 0.0).xy;
				// DEAD probe: no data, not a verdict - and no covered_sum, so all-dead cages
				// fall through to the coarser level (see the irradiance cage).
				if(moments.x <= 1e-4)
				{
					continue;
				}
				float variance = abs(moments.y - moments.x * moments.x);
				bool moments_ambiguous = variance > u_world_probe_cage_vis_gate *
				                                        u_world_probe_cage_vis_gate * spacing * spacing;
				if(query_distance > moments.x)
				{
					float difference = query_distance - moments.x;
					float chebyshev = variance / (variance + difference * difference);
					chebyshev = chebyshev * chebyshev * chebyshev;
					// CONFIDENTLY blocked - zero, never floored: this reader has no
					// perception crush, so floored probes renormalise to FULL amplitude when
					// the whole cage is blocked, completing interior rays with the sunlit far
					// side of the wall.
					if(!moments_ambiguous && chebyshev < GI_CHEBYSHEV_WEIGHT_FLOOR)
					{
						covered_sum += weight;
						continue;
					}
					weight *= max(chebyshev, GI_CHEBYSHEV_WEIGHT_FLOOR);
				}
				if(weight <= 1e-5)
				{
					continue;
				}
				covered_sum += weight;
				// Field visibility for the AMBIGUOUS band only, unfloored (see the irradiance
				// cage): the depth moments blur silhouettes into ~22-degree wedges that pass
				// exterior probes at full weight, and a completed ray carries that import into
				// every gather cone - the dominant sealed-room leak once the trace side was
				// watertight. The gate keeps completions affordable: most miss-ray cages are
				// open-lobe or flat-wall cases the moments already answer.
				//
				// Nested, never an && chain: HLSL && may evaluate both operands (FXC does),
				// and the right operand is the 40-step field march the gate exists to skip.
				BRANCH
				if(moments_ambiguous)
				{
					if(GiWorldProbeCageVisibility(position, probe_position, spacing) <= 0.0)
					{
						continue;
					}
				}
				// Sphere parallax: read toward where the ray meets the probe's visibility
				// sphere. The stored mean depth toward the RAY direction is the sphere radius
				// estimate - EXCEPT at the depth clamp, which marks "beyond" (the sky, or a hit
				// past GI_WORLD_PROBE_DEPTH_CLAMP spacings), not a radius: treating the clamp as
				// one pulled a vertical sky completion 2 m beside a level-0 probe 34 degrees off
				// the zenith into the walls, so mid-cell completions never read a probe's sky
				// texels (measured 2026-09-12: the courtyard floor's completions returned
				// exactly zero under a sky of E(up) 3). Beyond the clamp the ray's own
				// direction is the better estimate.
				ivec2 tile = GiWorldProbeTileBase(probe_slot, GI_WORLD_PROBE_OCT_RADIANCE);
				vec2 radius_uv = (vec2(depth_tile) + vec2_splat(1.0) +
				                  GiOctEncode(direction) * float(GI_WORLD_PROBE_OCT_DEPTH)) *
				                 u_gi_world_probe_atlas.xy;
				float radius = texture2DLod(s_world_probe_depth, radius_uv, 0.0).x;
				vec3 corrected = direction;
				if(radius < 0.999 * GI_WORLD_PROBE_DEPTH_CLAMP * spacing)
				{
					corrected = normalize(position + direction * max(radius, 0.25 * spacing) - probe_position);
				}
				vec2 radiance_uv =
				    (vec2(tile) + (GiOctEncode(corrected) * float(GI_WORLD_PROBE_OCT_RADIANCE))) *
				    u_gi_world_probe_radiance_atlas.xy;
				// Manual clamp inside the tile: the radiance atlas has no gutter, and a
				// bilinear tap crossing into a neighbour probe's tile would mix unrelated
				// probes.
				vec2 tile_min = (vec2(tile) + vec2_splat(0.5)) * u_gi_world_probe_radiance_atlas.xy;
				vec2 tile_max = (vec2(tile) + vec2_splat(float(GI_WORLD_PROBE_OCT_RADIANCE) - 0.5)) *
				                u_gi_world_probe_radiance_atlas.xy;
				radiance_uv = clamp(radiance_uv, tile_min, tile_max);
				vec3 sample_radiance = texture2DLod(s_world_probe_radiance_read, radiance_uv, 0.0).xyz;
				sum += sample_radiance * weight;
				weight_sum += weight;
			}
			// Cage present but field-blocked in every direction: the completion is darkness
			// (zero radiance), never the environment fallback - the ray is INSIDE something
			// sealed. A cage that never carried weight at all (every probe DEAD) is no answer.
			bool answered = weight_sum > 1e-5 || covered_sum > 1e-5;
			vec3 cage_radiance = weight_sum > 1e-5 ? sum / weight_sum : vec3_splat(0.0);
			if(k == 0)
			{
				near_answered = answered;
				near_radiance = cage_radiance;
				// The fine cage's visible fraction gates the far blend (see the irradiance cascade).
				near_visible = covered_sum > 1e-5 ? saturate(weight_sum / covered_sum) : 0.0;
			}
			else
			{
				far_answered = answered;
				far_radiance = cage_radiance;
			}
		}
		if(!near_answered)
		{
			// Every probe of the near cage DEAD (buried lattice points near dense geometry).
			// No data is not an answer: let the next level's cage try, marched and sealed like
			// this one. Returning false here handed these queries to the environment SH -
			// measured as sky patches inside sealed rooms wherever the finest covering cage
			// was fully buried.
			continue;
		}
		if(far_answered)
		{
			near_radiance = mix(near_radiance, far_radiance, blend * near_visible);
		}
		out_radiance = GiFiniteOrZero(near_radiance);
		return true;
	}
	return false;
}

#endif // GI_WORLD_PROBE_READ_RADIANCE

#endif // GI_WORLD_PROBE_READ

#endif // __GI_WORLD_PROBES_SH__
