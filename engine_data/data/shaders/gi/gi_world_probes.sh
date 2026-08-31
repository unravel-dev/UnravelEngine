#ifndef __GI_WORLD_PROBES_SH__
#define __GI_WORLD_PROBES_SH__

/*
 * World probe cascades (gi_rewrite_plan.md 3.3, revised): octahedral radiance/irradiance probes on a
 * TOROIDAL world-anchored lattice per SDF cascade. They carry offscreen and distant energy,
 * complete shortened gather rays, and feed the light voxels' bounce term.
 *
 * LATTICE. Probes live on an absolute world grid of spacing = cascade voxel *
 * GI_WORLD_PROBE_DIVISOR (2 m at level 0). The window covers GI_WORLD_PROBE_AXIS^3 cells around
 * the cascade; a probe's storage slot is its world cell index mod GI_WORLD_PROBE_AXIS, so
 * camera motion never moves or copies a probe - cells enter and leave the window, and a slot
 * whose cell changed is detected by the cell-id buffer and refilled. Camera ROTATION touches
 * nothing (R1 by construction).
 *
 * UPDATE. Every probe, every frame, GI_WORLD_PROBE_RAYS_PER_FRAME rays at FIXED octahedral
 * texel centres: stratum s = frame mod GI_WORLD_PROBE_WINDOW covers texels where
 * (texel_index mod WINDOW) == s, so every direction refreshes exactly once per window and the
 * radiance atlas is a zero-variance windowed mean. Rays read the light voxels at hits and the
 * sky SH at miss.
 *
 * ATLASES (2D, cascades stacked vertically):
 *  - radiance: GI_WORLD_PROBE_AXIS^2 probe tiles of OCT_RADIANCE^2 texels, probe-major
 *    (tile x = slot.x + slot.z * AXIS, tile y = slot.y + level * AXIS). rgb radiance, a hitT
 *    (negative = miss/sky). Point-fetched only, so no gutter.
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

/// Probes per axis of one cascade's window: cascade resolution / divisor + 1 (lattice includes
/// both endpoints). Runtime resolution 128 => 9.
#define GI_WORLD_PROBE_AXIS 9

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

/// Wraps a world cell index onto its storage slot.
ivec3 GiWorldProbeSlot(ivec3 cell)
{
	// True mathematical modulo for negative cells; HLSL/GLSL % is implementation-inconvenient
	// on negatives, so bias well into positives first (cells are bounded far below 1<<20).
	ivec3 biased = cell + ivec3(1048576, 1048576, 1048576);
	return ivec3(biased.x % GI_WORLD_PROBE_AXIS,
	             biased.y % GI_WORLD_PROBE_AXIS,
	             biased.z % GI_WORLD_PROBE_AXIS);
}

/// Top-left texel of a probe slot's tile in an atlas with @p tile_edge texels per tile.
ivec2 GiWorldProbeTileBase(ivec3 slot, int level, int tile_edge)
{
	return ivec2((slot.x + slot.z * GI_WORLD_PROBE_AXIS) * tile_edge,
	             (slot.y + level * GI_WORLD_PROBE_AXIS) * tile_edge);
}

/// World position of a probe cell's lattice point.
vec3 GiWorldProbeCellPosition(ivec3 cell, int level)
{
	return vec3(cell) * GiWorldProbeSpacing(level);
}

/// Linear index of a probe slot within one cascade, for the cell-id buffer.
int GiWorldProbeSlotIndex(ivec3 slot, int level)
{
	return ((level * GI_WORLD_PROBE_AXIS + slot.z) * GI_WORLD_PROBE_AXIS + slot.y) * GI_WORLD_PROBE_AXIS +
	       slot.x;
}

/// Packs a world cell for the cell-id buffer. 10 bits per axis (biased), 2 bits of level - the
/// same one-word identity trick the surface list uses.
uint GiWorldProbePackCell(ivec3 cell, int level)
{
	ivec3 biased = cell + ivec3(512, 512, 512);
	return uint(biased.x & 0x3FF) | (uint(biased.y & 0x3FF) << 10u) | (uint(biased.z & 0x3FF) << 20u) |
	       (uint(level) << 30u);
}

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
uint GiWorldProbeCageMask(vec3 position, vec3 normal, vec3 view_direction, int level)
{
	float spacing = GiWorldProbeSpacing(level);
	vec3 biased = GiWorldProbeBiasedQuery(position, normal, view_direction, spacing);
	ivec3 base_cell = ivec3(floor(biased / spacing));
	uint mask = 0u;
	LOOP for(int corner = 0; corner < 8; ++corner)
	{
		ivec3 offset = ivec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
		vec3 probe_position = GiWorldProbeCellPosition(base_cell + offset, level);
		if(GiWorldProbeCageVisibility(biased, probe_position, spacing) > 0.0)
		{
			mask |= 1u << uint(corner);
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
///                GI_LIGHT_VOXEL_VISIBILITY_MIN (0.25) gate, so its quantised value is >= 16
///                and the encodings can never collide.
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
///   bits 24-31 = the FAR-blend cage mask, always for level + 1 of the near tag. Whether the
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
                                    out vec3 out_irradiance, out float out_sky_fraction)
{
	out_irradiance = vec3_splat(0.0);
	out_sky_fraction = 0.0;
	float spacing = GiWorldProbeSpacing(level);
	vec3 biased = GiWorldProbeBiasedQuery(position, normal, view_direction, spacing);
	vec3 grid = biased / spacing;
	ivec3 base_cell = ivec3(floor(grid));
	vec3 frac = grid - vec3(base_cell);
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
		ivec3 slot = GiWorldProbeSlot(cell);
		ivec2 tile = GiWorldProbeTileBase(slot, level, tile_edge);
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
	return true;
}

/// The default cage read: field verdicts marched here, gated to the Chebyshev-ambiguous band.
bool GiWorldProbeIrradiance(vec3 position, vec3 normal, vec3 view_direction, int level,
                            out vec3 out_irradiance, out float out_sky_fraction)
{
	return GiWorldProbeIrradianceInternal(position, normal, view_direction, level, false, 0u,
	                                      out_irradiance, out_sky_fraction);
}

/// The memoised cage read (light-voxel bounce): field verdicts supplied as the per-corner
/// bitmask GiWorldProbeCageMask filled, applied to every probe in place of the gated march.
bool GiWorldProbeIrradianceMasked(vec3 position, vec3 normal, vec3 view_direction, int level,
                                  uint visibility_mask,
                                  out vec3 out_irradiance, out float out_sky_fraction)
{
	return GiWorldProbeIrradianceInternal(position, normal, view_direction, level, true,
	                                      visibility_mask, out_irradiance, out_sky_fraction);
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
		// The window covers AXIS cells centred on the camera's cell; usable extent excludes the
		// outermost cell on each side (the blend band, and the cells that may be mid-refill).
		float half_extent = (float(GI_WORLD_PROBE_AXIS - 1) * 0.5 - 1.0) * spacing;
		vec3 delta = abs(position - window_center);
		float largest = max(delta.x, max(delta.y, delta.z));
		if(largest > half_extent)
		{
			continue;
		}
		vec3 near_irradiance;
		float near_sky;
		if(!GiWorldProbeIrradiance(position, normal, view_direction, level, near_irradiance, near_sky))
		{
			continue;
		}
		// Blend toward the next level over the outer half of the last usable cell.
		float band = 0.5 * spacing;
		float blend = saturate((largest - (half_extent - band)) / band);
		if(blend > 0.0 && level + 1 < SDF_CLIPMAP_LEVEL_COUNT)
		{
			vec3 far_irradiance;
			float far_sky;
			if(GiWorldProbeIrradiance(position, normal, view_direction, level + 1, far_irradiance, far_sky))
			{
				near_irradiance = mix(near_irradiance, far_irradiance, blend);
				near_sky = mix(near_sky, far_sky, blend);
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
	// LOOP on both levels and corners, exactly as the irradiance cage: the corner body
	// carries the (gated) cage-visibility march, and unrolled it multiplied the largest
	// instruction footprint of every completing trace kernel by eight per level.
	LOOP
	for(int level = 0; level < SDF_CLIPMAP_LEVEL_COUNT; ++level)
	{
		float spacing = GiWorldProbeSpacing(level);
		float half_extent = (float(GI_WORLD_PROBE_AXIS - 1) * 0.5 - 1.0) * spacing;
		vec3 delta = abs(position - window_center);
		if(max(delta.x, max(delta.y, delta.z)) > half_extent)
		{
			continue;
		}
		vec3 grid = position / spacing;
		ivec3 base_cell = ivec3(floor(grid));
		vec3 frac = grid - vec3(base_cell);
		vec3 sum = vec3_splat(0.0);
		float weight_sum = 0.0;
		// Same bookkeeping as the irradiance cage: the weight the statistical chain granted,
		// before the field's verdict, so an all-blocked cage can answer darkness instead of
		// falling through to the environment term.
		float covered_sum = 0.0;
		LOOP
		for(int corner = 0; corner < 8; ++corner)
		{
			ivec3 offset = ivec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
			ivec3 cell = base_cell + offset;
			vec3 probe_position = GiWorldProbeCellPosition(cell, level);
			vec3 tri = mix(vec3_splat(1.0) - frac, frac, vec3(offset));
			float weight = max(tri.x, 0.001) * max(tri.y, 0.001) * max(tri.z, 0.001);
			ivec3 slot = GiWorldProbeSlot(cell);
			// Chebyshev visibility of the QUERY POINT from the probe, exactly as the
			// irradiance read tests it - a probe behind a wall must not complete rays through
			// it.
			vec3 to_query = position - probe_position;
			float query_distance = max(length(to_query), 1e-4);
			ivec2 depth_tile = GiWorldProbeTileBase(slot, level, GI_WORLD_PROBE_OCT_IRRADIANCE + 2);
			vec2 depth_uv = (vec2(depth_tile) + vec2_splat(1.0) +
			                 GiOctEncode(to_query / query_distance) * float(GI_WORLD_PROBE_OCT_DEPTH)) *
			                u_gi_world_probe_atlas.xy;
			vec2 moments = texture2DLod(s_world_probe_depth, depth_uv, 0.0).xy;
			// DEAD probe: no data, not a verdict - and no covered_sum, so all-dead cages fall
			// through to the coarser level (see the irradiance cage).
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
				// CONFIDENTLY blocked - zero, never floored: this reader has no perception
				// crush, so floored probes renormalise to FULL amplitude when the whole cage
				// is blocked, completing interior rays with the sunlit far side of the wall.
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
			// Nested, never an && chain: HLSL && may evaluate both operands (FXC does), and
			// the right operand is the 40-step field march the gate exists to skip.
			BRANCH
			if(moments_ambiguous)
			{
				if(GiWorldProbeCageVisibility(position, probe_position, spacing) <= 0.0)
				{
					continue;
				}
			}
			// Sphere parallax: read toward where the ray meets the probe's visibility sphere.
			// The stored mean depth toward the RAY direction is the sphere radius estimate.
			ivec2 tile = GiWorldProbeTileBase(slot, level, GI_WORLD_PROBE_OCT_RADIANCE);
			vec2 radius_uv = (vec2(depth_tile) + vec2_splat(1.0) +
			                  GiOctEncode(direction) * float(GI_WORLD_PROBE_OCT_DEPTH)) *
			                 u_gi_world_probe_atlas.xy;
			float radius = max(texture2DLod(s_world_probe_depth, radius_uv, 0.0).x, 0.25 * spacing);
			vec3 corrected = normalize(position + direction * radius - probe_position);
			vec2 radiance_uv = (vec2(tile) + (GiOctEncode(corrected) * float(GI_WORLD_PROBE_OCT_RADIANCE))) *
			                   u_gi_world_probe_radiance_atlas.xy;
			// Manual clamp inside the tile: the radiance atlas has no gutter, and a bilinear tap
			// crossing into a neighbour probe's tile would mix unrelated probes.
			vec2 tile_min = (vec2(tile) + vec2_splat(0.5)) * u_gi_world_probe_radiance_atlas.xy;
			vec2 tile_max = (vec2(tile) + vec2_splat(float(GI_WORLD_PROBE_OCT_RADIANCE) - 0.5)) *
			                u_gi_world_probe_radiance_atlas.xy;
			radiance_uv = clamp(radiance_uv, tile_min, tile_max);
			vec3 sample_radiance = texture2DLod(s_world_probe_radiance_read, radiance_uv, 0.0).xyz;
			sum += sample_radiance * weight;
			weight_sum += weight;
		}
		if(weight_sum <= 1e-5)
		{
			// Cage present but field-blocked in every direction: the completion is darkness
			// (out_radiance stays zero), never the environment fallback - the ray is INSIDE
			// something sealed.
			if(covered_sum > 1e-5)
			{
				return true;
			}
			// The cage never carried weight at all - every probe DEAD (buried lattice points
			// near dense geometry). No data is not an answer: let the next level's cage try,
			// marched and sealed like this one. Returning false here handed these queries to
			// the environment SH - measured as sky patches inside sealed rooms wherever the
			// finest covering cage was fully buried.
			continue;
		}
		out_radiance = GiFiniteOrZero(sum / weight_sum);
		return true;
	}
	return false;
}

#endif // GI_WORLD_PROBE_READ_RADIANCE

#endif // GI_WORLD_PROBE_READ

#endif // __GI_WORLD_PROBES_SH__
