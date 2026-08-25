#ifndef __GI_LIGHT_VOXELS_KERNEL_SH__
#define __GI_LIGHT_VOXELS_KERNEL_SH__

/*
 * Lights the surface voxels (gi_rewrite_plan.md 3.2): one thread per surface-list entry, direct
 * lighting with traced shadows per EXPOSED FACE, written straight into the light volume.
 *
 * SHARED KERNEL BODY: compiled twice, as cs_gi_light_voxels.sc (radiance) and as
 * cs_gi_light_voxels_debug.sc (GI_SUN_TIER_DEBUG_VARIANT - tier-attribution colors). The C++
 * pass selects the PROGRAM; see the variant note at u_light_voxel_debug_sun_tiers below.
 *
 * BUDGETED BY CONSTRUCTION: only listed surface voxels are processed, and each is re-lit every
 * GI_LIGHT_VOXEL_UPDATE_DENOM frames (entry index + frame phase), so the per-frame cost is a
 * quarter of the resident surface set regardless of scene size - the property the old 524k-slot
 * cache sweep lacked.
 *
 * NO temporal accumulation here, deliberately. Direct lighting with traced shadows is
 * deterministic - there is no variance to average - so the volume just holds the latest answer
 * and a light change propagates in at most one full rotation (4 frames). The stochastic
 * machinery lives where the stochastic rays are: the world probes (Phase 3). When the bounce
 * term arrives (Phase 4) it reads the probes' FILTERED irradiance, which is equally
 * deterministic per frame, so this stays a plain write.
 *
 * The dispatch covers every level's full segment and early-outs beyond each level's count; the
 * counts live on the GPU, so a tighter launch needs indirect dispatch args - a measured
 * optimisation for later, not a correctness matter (an early-out thread costs one buffer read).
 */

#include "bgfx_compute.sh"
#include "gi/sdf_common.sh"
#include "gi/gpu_lights.sh"
// Sun visibility from the sun's own cascade 0 where it covers, traced field beyond - see the
// tier note in gi_lighting.sh. Only this pass defines it: the debug direct view deliberately
// keeps showing the pure traced tier.
#define GI_SUN_SHADOWMAP_TIER
#include "gi/gi_lighting.sh"
#include "gi/gi_light_voxels.sh"
// The bounce term (gi_rewrite_plan.md Phase 4): last frame's world-probe irradiance closes the
// infinite-bounce loop - probes read voxels, voxels read probes, gain bounded by GI_MAX_ALBEDO.
#define GI_WORLD_PROBE_READ
#include "gi/gi_world_probes.sh"

/// Surface-voxel list, written by cs_gi_clipmap_attributes: a SDF_CLIPMAP_LEVEL_COUNT-entry
/// header of per-level counts (index = level), then one capacity-sized entry segment per
/// level. ONE buffer at a high stage ON PURPOSE: OpenGL guarantees only eight image units
/// (bindings 0-7), so the low stages are reserved for this pass's 3D IMAGES while buffers
/// and samplers tolerate the high ones - the old split count buffer occupied this pass's
/// last free stage.
BUFFER_RO(b_surface_list, uint, 10);
/// Attribute volumes: what the surface looks like.
SAMPLER3D(s_attr_albedo, 8);
SAMPLER3D(s_attr_emissive, 9);
/// The light volume this pass owns. Read+write: the radiance store folds each relight into
/// a per-voxel EMA (GI_LIGHT_VOXEL_EMA_BLEND) - the read is this pass's own previous answer
/// for the texel, never another consumer's concurrent write.
IMAGE3D_RW(s_light_voxels_out, rgba16f, 7);
/// The per-face memo: one texel per light-volume texel (same GiLightVoxelTexel
/// addressing), all 32 bits used (layout owned by GiWorldProbeVisMemoPack* in
/// gi_world_probes.sh). The PROBE half holds the bounce's near cage mask + generation +
/// level + the far-blend mask; the FACE half holds the cavity-cone visibility and the
/// culled bit, so a relit face pays the tunnel guard and the cavity march once per
/// generation instead of once per rotation. R32U because 32-bit typed UAV loads are the
/// only ones every backend guarantees - a 16-bit load on hardware without the optional cap
/// silently reads zero, which never matches a live generation and turns the memo into a
/// permanent miss. Every verdict here is a pure function of the field and the window, both
/// of which the generation tracks (field content changed, probe window scrolled) - see
/// GiBounceProbeIrradiance. Stage 6 is an IMAGE on purpose: OpenGL guarantees only eight
/// image units (bindings 0-7), which is why the surface list vacated it.
UIMAGE3D_RW(s_gi_vis_memo, r32ui, 6);

/// Defined locally rather than taken from lighting.sh, which this shader does not include. The
/// D3D backend happens to supply one anyway, so relying on it compiles there and fails on GLSL.
#define GI_PI 3.1415926535897932

/// xyz = camera position - the world-probe windows are centred on it, and the cascade chooser
/// needs the same centre the trace pass used. w mirrors the sun-tier debug state for GPU-debugger
/// inspection ONLY - the kernel does not read it.
uniform vec4 u_gi_light_voxel_camera;
/// x = the bounce visibility-memo generation (1..63; 0 = memo unavailable, fall back to the
/// gated march every other consumer runs). The CPU bumps it when the clipmap content epoch
/// changes or any probe window scrolls, so a stale texel can never serve.
/// y = the relight EMA blend for the radiance store (1 = write through). The CPU holds it at
/// 1 for a full rotation after any light-set or content change (and after debug-variant
/// writes), so real changes land in one relight and only the dither/limit-cycle noise is
/// integrated (GI_LIGHT_VOXEL_EMA_BLEND).
uniform vec4 u_gi_vis_memo_params;
#define u_vis_memo_generation uint(u_gi_vis_memo_params.x)
#define u_light_voxel_ema_blend u_gi_vis_memo_params.y
/// COMPILE-TIME variant switch, deliberately NOT a uniform. The debug write spent two hunts
/// dead behind runtime flags that provably left the CPU (two independent lanes, current
/// binaries, one camera, per-submit capture semantics) yet never steered the kernel - never
/// explained. Program selection cannot be stomped by the five other passes that set the same
/// uniform names, cannot go stale against constant-buffer reflection, and reproduces the
/// hardcoded-flag mode that verifiably worked. The uniform lanes (params.y, camera.w) still
/// CARRY the flag as telemetry, so a GPU debugger can settle the old mystery some day.
/// The dead branch compiles out of each variant; the ternaries below cost nothing.
#ifdef GI_SUN_TIER_DEBUG_VARIANT
#	define u_light_voxel_debug_sun_tiers true
#else
#	define u_light_voxel_debug_sun_tiers false
#endif
/// Third compiled variant (cs_gi_light_voxels_vis_memo_debug.sc): paints the LIVE bounce
/// visibility-memo transaction per face into the light volume instead of radiance - the
/// instrument for "is the memo hitting" (measured 2026-08-13: Light Voxels +0.5 ms over the
/// pre-memo build = the miss-every-rotation cost signature; the CPU-side links all verified,
/// exactly the situation the sun-tier saga taught to settle with a compiled variant, never a
/// runtime flag). It runs the real memo path - load, miss-march, restamp - so the view shows
/// the mechanism itself, not a simulation of it.
#ifdef GI_VIS_MEMO_DEBUG_VARIANT
#	define u_light_voxel_debug_vis_memo true
#else
#	define u_light_voxel_debug_vis_memo false
#endif

/*
 * CAVITY visibility for the bounce term - distance-field cone occlusion, the [DFAO] role:
 * sample the composed field at doubling distances along the face; wherever the field reads
 * less than the distance travelled, geometry encroaches on the face's hemisphere. This
 * measures EXACTLY the band the world probes cannot: from one attribute voxel (below which
 * the voxel's own surface dominates the field) out to about the probe spacing (beyond which
 * the probes' own Chebyshev visibility already handles occlusion). Without it, a voxel inside
 * a sub-spacing cavity - an awning's underside, a window reveal, a doorway - receives the
 * OPEN ambient of the probe cage around it and glows in exactly the places that should be
 * darkest; every gather ray that hits the cavity then reads that false brightness back.
 */
float GiBounceCavityVisibility(vec3 position, vec3 direction, float attr_voxel)
{
	float t = attr_voxel;
	float d1 = SdfSampleClipmap(position + direction * t);
	// EXACT early-outs from the field's 1-Lipschitz bound. The samples sit on one line at
	// t_i = 1/2/4 attribute voxels, so |d_i - d1| <= t_i - t1 and a sample's occlusion term
	// saturates to zero exactly when d_i >= t_i:
	//  - d1 >= 2*t_last - t1 (= 2*4 - 1 = 7 attribute voxels at the shipped 3 steps): then
	//    d_i >= d1 - (t_i - t1) >= t_i for EVERY farther sample - all terms provably zero,
	//    the march answers fully visible from this one sample. Open exteriors resolve here.
	//  - d1 <= 0: the first sample sits inside composed geometry - the face is buried, the
	//    march answers fully occluded.
	// In between, the remaining samples run exactly as before; the bounds trade nothing but
	// the redundant samples.
	float t_last = t * float(1 << (GI_BOUNCE_AO_STEPS - 1));
	if(d1 >= 2.0 * t_last - t)
	{
		return 1.0;
	}
	if(d1 <= 0.0)
	{
		return 0.0;
	}
	// Field >= travel distance: the cone is clear at this scale, no contribution. Field
	// negative (inside geometry): fully occluded. The weights halve so near encroachment
	// - the strongest visibility signal - dominates.
	float occlusion = saturate(1.0 - d1 / t);
	float weight = 0.5;
	float weight_sum = 1.0;
	LOOP for(int i = 1; i < GI_BOUNCE_AO_STEPS; ++i)
	{
		t *= 2.0;
		float d = SdfSampleClipmap(position + direction * t);
		occlusion += weight * saturate(1.0 - d / t);
		weight_sum += weight;
		weight *= 0.5;
		// With one sample left, if even a fully open reading cannot lift visibility past the
		// cull gate, the face is culled whatever that sample says - the value below the gate
		// is never consumed (the gate stores zero and the bounce attenuator only reads values
		// that cleared it), so returning early is exact. Occlusion only grows, and the bound
		// uses the largest weight_sum the final division could see.
		if(i == GI_BOUNCE_AO_STEPS - 2)
		{
			if(occlusion > (1.0 - GI_LIGHT_VOXEL_VISIBILITY_MIN) * (weight_sum + weight))
			{
				return 0.0;
			}
		}
	}
	return saturate(1.0 - occlusion / weight_sum);
}

/*
 * The bounce's world-probe read, memoised: the twin of GiWorldProbeIrradianceCascade
 * (gi_world_probes.sh - coverage test, all-dead fall-through and blend band kept in step BY
 * HAND) whose per-cage field verdicts come from the visibility memo instead of being marched
 * per rotation. The verdicts are geometry-static: they depend only on the field and on the
 * cage's lattice positions, both of which the generation tag tracks - so a face pays the
 * 8-corner march once per generation instead of once per relight.
 *
 *  - Memo HIT (generation and probe level match): the stored mask answers for every probe -
 *    verdicts this cheap need no variance gate, a strictly wider leak margin than gated
 *    marching (a confidently-wrong Chebyshev lobe cannot slip a wall past a stored verdict).
 *  - Memo MISS: march all 8 corners once (GiWorldProbeCageMask, ungated - the fill is
 *    amortised), and restamp the texel with the level that actually ANSWERED, so an all-dead
 *    finest cage never pins the tag to a level that returns nothing (its cage costs no
 *    marches anyway: dead probes exit before the field verdict).
 *  - Generation 0: the memo was never seeded (its clear shader missing) - run exactly the
 *    gated read every other consumer runs. Safe-slow, never a leak.
 *
 * The far-blend read (the outer half-cell of the window) is memoised the same way: its
 * cage mask (always level + 1 of the answering level) rides the texel's top byte, stamped
 * by the same transaction. Whether the band is open at all is a pure function of the
 * texel's position and the window - both frozen within a generation - so the byte needs no
 * flag of its own; where the band is closed it is stamped 0 and never consumed.
 *
 * The FACE half of the word (cavity visibility + culled bit) is owned by the caller: it is
 * computed before the gates and passed in as @p face_half, and every store here carries it,
 * so one transaction settles the whole word. On the no-cage-answered fall-out the face half
 * is still stamped (probe half unpopulated) when the generation was stale - otherwise those
 * faces would re-march their cavity cone every rotation.
 *
 * @p out_memo_state reports the transaction for the vis-memo debug variant:
 * GI_VIS_MEMO_STATE_OFF = generation 0 (memo unavailable, gated fallback ran),
 * _HIT = stored verdicts served, _MISS = marched and restamped, _NONE = no covering cage
 * answered (nothing added); the _FAR forms are their in-the-blend-band siblings (the L8
 * coverage instrument). Costs nothing in the radiance variant - dead writes fold away.
 */
#define GI_VIS_MEMO_STATE_OFF      0
#define GI_VIS_MEMO_STATE_HIT      1
#define GI_VIS_MEMO_STATE_MISS     2
#define GI_VIS_MEMO_STATE_NONE     3
#define GI_VIS_MEMO_STATE_HIT_FAR  4
#define GI_VIS_MEMO_STATE_MISS_FAR 5
bool GiBounceProbeIrradiance(vec3 position, vec3 face_direction, ivec3 memo_texel,
                             uint memo_word, uint face_half,
                             out vec3 out_irradiance, out float out_sky_fraction,
                             out int out_memo_state)
{
	out_irradiance = vec3_splat(0.0);
	out_sky_fraction = 0.0;
	out_memo_state = GI_VIS_MEMO_STATE_NONE;
	bool memo_live = u_vis_memo_generation != 0u;
	bool generation_ok = memo_live &&
	                     GiWorldProbeVisMemoGeneration(memo_word) == u_vis_memo_generation;
	LOOP for(int level = 0; level < SDF_CLIPMAP_LEVEL_COUNT; ++level)
	{
		float spacing = GiWorldProbeSpacing(level);
		float half_extent = (float(GI_WORLD_PROBE_AXIS - 1) * 0.5 - 1.0) * spacing;
		vec3 delta = abs(position - u_gi_light_voxel_camera.xyz);
		float largest = max(delta.x, max(delta.y, delta.z));
		if(largest > half_extent)
		{
			continue;
		}
		// The blend band, computed up front: the far mask below belongs to the stamp.
		float band = 0.5 * spacing;
		float blend = saturate((largest - (half_extent - band)) / band);
		bool wants_far = blend > 0.0 && level + 1 < SDF_CLIPMAP_LEVEL_COUNT;
		vec3 near_irradiance;
		float near_sky;
		bool answered;
		bool restamp = false;
		uint mask = 0u;
		if(!memo_live)
		{
			answered = GiWorldProbeIrradiance(position, face_direction, face_direction, level,
			                                  near_irradiance, near_sky);
		}
		else
		{
			bool hit = generation_ok && GiWorldProbeVisMemoProbeValid(memo_word) &&
			           GiWorldProbeVisMemoLevel(memo_word) == level;
			// A real BRANCH, never a ternary: HLSL's ?: is a SELECT that may evaluate BOTH
			// operands, and with the 8-corner march on the miss side the fill executed on
			// every face and was discarded on hits - the memo classified perfectly (view 28
			// solid green) while the pass still paid march-every-rotation prices (measured:
			// the entire +0.5 ms the memo was built to reclaim).
			BRANCH if(hit)
			{
				mask = GiWorldProbeVisMemoMask(memo_word);
			}
			else
			{
				mask = GiWorldProbeCageMask(position, face_direction, face_direction, level);
			}
			restamp = !hit;
			answered = GiWorldProbeIrradianceMasked(position, face_direction, face_direction,
			                                        level, mask, near_irradiance, near_sky);
		}
		if(!answered)
		{
			// All-dead cage: no data is not a verdict - the coarser level answers, marched
			// and sealed like this one (the round-2 fall-through contract). The far mask is
			// deliberately not marched yet: on fall-through it would duplicate the next
			// level's own near march.
			continue;
		}
		out_memo_state = !memo_live ? GI_VIS_MEMO_STATE_OFF
		                            : (restamp ? GI_VIS_MEMO_STATE_MISS : GI_VIS_MEMO_STATE_HIT);
		if(memo_live && wants_far)
		{
			out_memo_state = restamp ? GI_VIS_MEMO_STATE_MISS_FAR : GI_VIS_MEMO_STATE_HIT_FAR;
		}
		if(restamp)
		{
			// Near verdicts stamped now; the far mask fills LAZILY on the first hit (see the
			// layout note in gi_world_probes.sh) - an eager far march here regressed motion
			// frames, where every rotation is a miss and this store is per-face per-frame.
			imageStore(s_gi_vis_memo, memo_texel,
			           uvec4(GiWorldProbeVisMemoPackProbe(mask, u_vis_memo_generation, level,
			                                              0u, false) |
			                     face_half,
			                 0u, 0u, 0u));
		}
		// Blend toward the next level over the outer half of the last usable cell, exactly as
		// the cascade read does.
		if(wants_far)
		{
			vec3 far_irradiance;
			float far_sky;
			bool far_answered;
			BRANCH if(!memo_live || restamp)
			{
				// Memo off, or a miss rotation: the plain gated read - exactly the pre-memo
				// cost, so churning generations (window re-snaps under camera motion) never
				// pay the 8-corner far march on top of the near one they already marched.
				far_answered = GiWorldProbeIrradiance(position, face_direction, face_direction,
				                                      level + 1, far_irradiance, far_sky);
			}
			else
			{
				uint far_mask;
				BRANCH if(GiWorldProbeVisMemoFarFilled(memo_word))
				{
					far_mask = GiWorldProbeVisMemoFarMask(memo_word);
				}
				else
				{
					// First hit with the band open: this generation survived a full rotation,
					// so it is stable enough to amortise - march the far cage once and seal
					// it into the word (near mask, level and face half preserved).
					far_mask = GiWorldProbeCageMask(position, face_direction, face_direction,
					                                level + 1);
					imageStore(s_gi_vis_memo, memo_texel,
					           uvec4(GiWorldProbeVisMemoPackProbe(mask, u_vis_memo_generation,
					                                              level, far_mask, true) |
					                     face_half,
					                 0u, 0u, 0u));
				}
				far_answered = GiWorldProbeIrradianceMasked(position, face_direction,
				                                            face_direction, level + 1, far_mask,
				                                            far_irradiance, far_sky);
			}
			if(far_answered)
			{
				near_irradiance = mix(near_irradiance, far_irradiance, blend);
				near_sky = mix(near_sky, far_sky, blend);
			}
		}
		out_irradiance = GiFiniteOrZero(near_irradiance);
		out_sky_fraction = near_sky;
		return true;
	}
	// No covering cage answered. A stale generation still stamps the caller's fresh face
	// half (probe half unpopulated) so the cavity march is not repaid every rotation; a
	// matching generation already holds it.
	if(memo_live && !generation_ok)
	{
		imageStore(s_gi_vis_memo, memo_texel,
		           uvec4(GiWorldProbeVisMemoPackFaceOnly(face_half, u_vis_memo_generation),
		                 0u, 0u, 0u));
	}
	return false;
}

/*
 * SUN-TIER DEBUG (the sealed-box leak hunt): classify each face by WHICH TIER answers sun
 * visibility and WHAT it answers, written into the light volume in place of radiance so the
 * SDF debug view (mode: sun_tiers) shows the injection tier per voxel face in one screenshot.
 *
 *   GREEN -> the CSM quadrature answered; brightness = lit fraction, so a face the map calls
 *            fully shadowed reads dim green and one it calls lit reads bright green. Bright
 *            green on a sealed interior IS the leak, attributed to the shadow-map tier.
 *   RED   -> the traced field answered with a resolved hit: fully occluded, injects nothing.
 *   WHITE -> the traced field answered lit; brightness = clearance visibility, so penumbra
 *            greys. White on a sealed interior means rays thread the field (corner gaps).
 *   BLUE  -> sun visibility never queried: no directional sun resident, or the face points
 *            away from it (the cosine already zeroes the energy, so no tier runs).
 *   Faces the guards below cull keep their zero write (alpha 0), which the debug view paints
 *   its usual dark blue - those faces inject nothing either way.
 *
 * ALPHA IS THE PROVENANCE MARKER: every tier write carries a = 0.5, radiance writes carry 1,
 * the guards write 0. The debug view classifies by it, so a texel still holding a = 1 content
 * after the rotation window is PROOF the lighting pass never rewrites it (painted magenta) -
 * a stale-texel population that matters on its own: frozen radiance keeps emitting whatever
 * light it captured last, forever, into every consumer.
 *
 * If EVERY interior face reads red / dim green yet the box still washes out, no tier stamps
 * sun directly and the energy enters through the bounce path (probe completions, screen-trace
 * commits) instead - the view discriminates that case too, by elimination.
 *
 * While active this REPLACES the volume's radiance, so the probes and the screen gather
 * ingest tier colors for as long as the view is up; the volume relights within the usual
 * rotation and temporal windows once it is switched off. Diagnostic only.
 */
#define GI_SUN_TIER_DEBUG_ALPHA 0.5
vec4 GiDebugSunTierColor(vec3 world_position, vec3 world_normal, float voxel_size, float near_field)
{
	int sun_index = int(u_gi_sun_index);
	if(sun_index < 0)
	{
		// Tier disabled (no map rendered, VSM sun): attribute against the first directional
		// light so the traced classification still answers.
		for(int i = 0; i < u_gpu_light_count; ++i)
		{
			if(GpuLoadLight(i).type == GPU_LIGHT_TYPE_DIRECTIONAL)
			{
				sun_index = i;
				break;
			}
		}
	}
	if(sun_index < 0)
	{
		return vec4(0.0, 0.2, 1.0, GI_SUN_TIER_DEBUG_ALPHA);
	}
	GpuLight sun = GpuLoadLight(sun_index);
	vec3 unshadowed = GpuEvalLightUnshadowed(sun, world_position, world_normal);
	if(dot(unshadowed, unshadowed) <= 0.0)
	{
		return vec4(0.0, 0.2, 1.0, GI_SUN_TIER_DEBUG_ALPHA);
	}
	// The exact tier order of GiEvalLight: the map answers where it covers, the trace beyond.
	float lit;
	if(u_gi_sun_index >= 0.0 && GiSunShadowmapVisibility(world_position, world_normal, voxel_size, lit))
	{
		return vec4(0.0, 0.25 + 0.75 * lit, 0.0, GI_SUN_TIER_DEBUG_ALPHA);
	}
	float visibility = GiTraceShadow(world_position, world_normal, -sun.direction,
	                                 u_gi_shadow_distance, voxel_size, near_field);
	if(visibility <= 0.0)
	{
		return vec4(1.0, 0.0, 0.0, GI_SUN_TIER_DEBUG_ALPHA);
	}
	float shade = 0.25 + 0.75 * visibility;
	return vec4(shade, shade, shade, GI_SUN_TIER_DEBUG_ALPHA);
}

NUM_THREADS(64, 1, 1)
void main()
{
	// One thread per entry due for relight THIS frame. The 4-frame rotation used to be a
	// `(entry + frame) % 4` test over the dense list, which left exactly 8 of every 32 lanes
	// alive in a live warp while the warp still issued the full body - the trace, the cage
	// chain, all six faces - for a quarter of the output. Folding the rotation into the
	// launch (entry = denom * id + phase selects the identical set) makes every lane of a
	// live warp do real work, with the entry footprint per wave unchanged in spirit: still
	// a contiguous stretch of the same compacted list.
	//
	// The level rides gl_WorkGroupID.y, which keeps it provably wave-uniform (scalar loads
	// for the per-level state) and removes two integer divisions by a non-constant.
	uint level = gl_WorkGroupID.y;
	if(level >= uint(SDF_CLIPMAP_LEVEL_COUNT))
	{
		return;
	}
	// The phase that selects the set `(entry + frame) % denom == 0`.
	uint denom = uint(GI_LIGHT_VOXEL_UPDATE_DENOM);
	uint phase = (denom - (u_light_voxel_frame % denom)) % denom;
	uint entry = (gl_WorkGroupID.x * 64u + gl_LocalInvocationID.x) * denom + phase;
	if(entry >= b_surface_list[level])
	{
		return;
	}
	uint capacity = uint(u_light_voxel_resolution * u_light_voxel_resolution * u_light_voxel_resolution);
	// packed_slot, not `packed`: that word is a GLSL layout-qualifier keyword and a variable
	// named after it fails the OpenGL backend outright.
	uint packed_slot = b_surface_list[uint(SDF_CLIPMAP_LEVEL_COUNT) + level * capacity + entry];
	ivec3 slot = ivec3(int(packed_slot & 0xFFu),
	                   int((packed_slot >> 8u) & 0xFFu),
	                   int((packed_slot >> 16u) & 0xFFu));
	vec4 level_data = u_sdf_clipmap_levels[level];
	float attr_voxel = level_data.w * 2.0;
	// Toroidal reconstruction: the slot's world cell under the current window (the same math the
	// attribute composer used to place it - the origin is attr-voxel aligned by the snap).
	int attr_res = u_light_voxel_resolution;
	ivec3 window_base = ivec3(floor(level_data.xyz / attr_voxel + vec3_splat(0.5)));
	ivec3 base_slot = GiLightVoxelSlot(window_base);
	ivec3 offset = ivec3((slot.x - base_slot.x + attr_res) % attr_res,
	                     (slot.y - base_slot.y + attr_res) % attr_res,
	                     (slot.z - base_slot.z + attr_res) % attr_res);
	ivec3 cell = window_base + offset;
	vec3 center = (vec3(cell) + vec3_splat(0.5)) * attr_voxel;
	ivec3 attr_texel = ivec3(slot.x, slot.y, slot.z + int(level) * attr_res);
	vec4 albedo = texelFetch(s_attr_albedo, attr_texel, 0);
	vec3 emissive = texelFetch(s_attr_emissive, attr_texel, 0).xyz;
	if(albedo.a <= 0.0)
	{
		// De-listed between compose and this slice's turn: nothing to light.
		return;
	}
	// The gain clamp that closes the (future) bounce recursion below 1 lives at the one place
	// radiance is produced, exactly as the old cache update kept it.
	vec3 bounded_albedo = min(albedo.xyz, vec3_splat(GI_MAX_ALBEDO));
	// A voxel that can emit nothing produces zero radiance on every measurable face no matter
	// what the lights say, so the lighting below is skipped for it. The gates still run: the
	// zero-with-alpha-0 provenance of a culled face and the zero-with-alpha-1 of a measured
	// black face are different answers, and the readers distinguish them.
	bool zero_radiance = dot(bounded_albedo, bounded_albedo) <= 0.0 && dot(emissive, emissive) <= 0.0;
	float d_center = SdfSampleClipmapLevel(int(level), center);
	// Mesh-exact shadow detail fades with level, like every near-field consumer: level 0 sees
	// full contact shadowing, level 1 half range, beyond that the cascade alone answers.
	float near_scale = level == 0u ? 1.0 : (level == 1u ? 0.5 : 0.0);
	// Per-voxel memo for the traced directional visibility (GiEvalDirectLightingVoxel): the
	// sun-facing faces trace one shared ray instead of one each.
	float cached_dir_visibility = 0.0;
	int cached_dir_index = -1;
	float center_lift = max(0.0, -d_center) + 0.5 * attr_voxel;
	// DIRECT-LIGHTING DITHER (GI_LIGHT_VOXEL_SUN_DITHER): the evaluation point walks within
	// the voxel per relight, so shadow edges land in the volume as temporal dither instead of
	// a voxel staircase - the probes' stratum window and the gather temporal integrate it
	// into penumbra. Per-VOXEL (hoisted, shared by all six faces and the directional memo);
	// the tunnel guard, cavity march and bounce read stay un-dithered - their verdicts are
	// memoised as pure functions of the field, and the bounce lattice is smooth anyway.
	vec3 dither_seed = fract(vec3(cell) * vec3(0.1031, 0.1030, 0.0973) +
	                         vec3(0.9151, 0.8380, 0.7548) * float(u_light_voxel_frame));
	vec3 light_jitter =
	    (dither_seed - vec3_splat(0.5)) * (2.0 * GI_LIGHT_VOXEL_SUN_DITHER * attr_voxel);
	// LOOP: unrolled, this replicates the largest body in the GI frame - the light loop with
	// its sphere traces, the cavity march, the 8-corner probe chain - six times, with the
	// register pressure that implies.
	LOOP
	for(int face = 0; face < 6; ++face)
	{
		vec3 direction = GiLightVoxelFaceDirection(face);
		ivec3 texel = GiLightVoxelTexel(slot, int(level), face);
		// The memo word, loaded ONCE per face: the FACE half (cavity verdict) consults it
		// before the gates, the PROBE half inside the bounce read.
		bool memo_live = u_vis_memo_generation != 0u;
		uint memo_word = 0u;
		if(memo_live)
		{
			memo_word = imageLoad(s_gi_vis_memo, texel).x;
		}
		// The face fast path serves only the radiance variant: the debug views exist to
		// watch the full mechanism run, and the sun-tier view's provenance contract requires
		// every visited texel to be freshly stored. Compile-time flags - this folds to
		// memo_live in the shipping variant.
		bool face_memo_live = memo_live && !u_light_voxel_debug_sun_tiers &&
		                      !u_light_voxel_debug_vis_memo;
		bool face_hit = face_memo_live &&
		                GiWorldProbeVisMemoGeneration(memo_word) == u_vis_memo_generation;
		// Launch point clear of the surface: out by however deep the centre sits, plus half an
		// attribute voxel - in the units of the thing being cleared. Loop-invariant, hoisted
		// as center_lift above (the per-voxel sun memo launches by the same amount).
		float lift = center_lift;
		vec3 position = center + direction * lift;
		float visibility = 0.0;
		uint face_half = 0u;
		BRANCH
		if(face_hit)
		{
			// FACE MEMO HIT: the tunnel guard and the cavity march are pure functions of the
			// field and the window, both frozen within a generation - the stored verdict
			// answers. A culled face's light texel already holds the zero this generation's
			// miss rotation stored, so even that store is skipped.
			if(GiWorldProbeVisMemoFaceCulled(memo_word))
			{
				continue;
			}
			visibility = GiWorldProbeVisMemoFaceVisibility(memo_word);
			face_half = memo_word & GI_VIS_MEMO_FACE_HALF_BITS;
		}
		else
		{
			// TUNNEL GUARD: walking out of your OWN surface along the face rises monotonically
			// (1-Lipschitz from inside the band); a lift whose midpoint reads DEEPER than the
			// centre crossed the slab core - it exited through the FAR side, and everything
			// measured from there (direct sun, exterior ambient) belongs to the wrong side of
			// the wall. Un-guarded, buried faces near walls were lit by the sunlit exterior
			// and stamped white into enclosed rooms. One field sample, only for deep lifts.
			bool culled = false;
			if(lift > attr_voxel)
			{
				float d_mid = SdfSampleClipmap(center + direction * (0.5 * lift));
				if(d_mid < d_center - 0.25 * attr_voxel)
				{
					culled = true;
				}
			}
			// A face is MEASURABLE when enough of its cavity cone escapes - the same
			// multi-scale visibility the ambient below is weighted by, computed once and
			// shared. This replaced a single-step field-rise test, which cannot see past a
			// coarse level's blob plateau: small geometry merges into blobs whose shell voxels
			// sit a voxel or more deep, the one-voxel step stays inside, and every face read
			// as unexposed - whole objects went black wherever only coarse levels covered them
			// (the far-distance failure). The march at 1/2/4 voxels from the LIFTED point sees
			// past the plateau; a face pointing into real interior still reads closed at every
			// scale and stays dark, both sides of a thin wall still measure open through their
			// own slabs.
			if(!culled)
			{
				visibility = GiBounceCavityVisibility(position, direction, attr_voxel);
				culled = visibility < GI_LIGHT_VOXEL_VISIBILITY_MIN;
			}
			if(culled)
			{
				// The verdict is stamped so every later rotation of this generation skips the
				// march AND the zero store below.
				if(face_memo_live)
				{
					imageStore(s_gi_vis_memo, texel,
					           uvec4(GiWorldProbeVisMemoPackFaceOnly(
					                     GiWorldProbeVisMemoPackFace(0.0, true),
					                     u_vis_memo_generation),
					                 0u, 0u, 0u));
				}
				// In debug mode the cull carries the provenance alpha too: with it, EVERY texel
				// this dispatch visits is marked 0.5, so any alpha-1 texel left on screen is
				// PROOF of a radiance-path write (flag not arriving), not of a stale texel.
				imageStore(s_light_voxels_out, texel,
				           u_light_voxel_debug_sun_tiers
				               ? vec4(0.0, 0.0, 0.0, GI_SUN_TIER_DEBUG_ALPHA)
				               : vec4_splat(0.0));
				continue;
			}
			face_half = GiWorldProbeVisMemoPackFace(visibility, false);
		}
		// After the gates on purpose: a culled face writes zero in both modes, so the debug
		// view only ever attributes faces that can actually inject energy.
		if(u_light_voxel_debug_sun_tiers)
		{
			imageStore(s_light_voxels_out, texel,
			           GiDebugSunTierColor(position, direction, max(level_data.w, 0.01),
			                               u_gi_shadow_near_field * near_scale));
			continue;
		}
		// VIS-MEMO DEBUG: run the real memo transaction and paint its outcome (categorical,
		// displayed nearest through the sun-tiers view path). GREEN = stored verdicts served
		// (healthy steady state), RED = miss -> marched + restamped (one red sweep right
		// after a generation bump is the fill; PERSISTENT red with a quiet generation log is
		// the defect), BLUE = the kernel sees generation 0 (memo never seeded, or the
		// uniform never arrived), DARK GREY = no covering cage answered. Alpha carries the
		// same 0.5 provenance marker as the sun-tier writes.
		if(u_light_voxel_debug_vis_memo)
		{
			vec4 memo_color = vec4(0.15, 0.15, 0.15, GI_SUN_TIER_DEBUG_ALPHA);
			if(u_world_probe_ready)
			{
				vec3 memo_irradiance;
				float memo_sky;
				int memo_state;
				GiBounceProbeIrradiance(position, direction, texel, memo_word, face_half,
				                        memo_irradiance, memo_sky, memo_state);
				if(memo_state == GI_VIS_MEMO_STATE_OFF)
				{
					memo_color = vec4(0.1, 0.3, 1.0, GI_SUN_TIER_DEBUG_ALPHA);
				}
				else if(memo_state == GI_VIS_MEMO_STATE_HIT)
				{
					memo_color = vec4(0.0, 0.8, 0.0, GI_SUN_TIER_DEBUG_ALPHA);
				}
				else if(memo_state == GI_VIS_MEMO_STATE_MISS)
				{
					memo_color = vec4(1.0, 0.0, 0.0, GI_SUN_TIER_DEBUG_ALPHA);
				}
				// The far-blend band, the L8 coverage instrument: TEAL = probe hit in the
				// band (stored far verdicts served, or the one-time lazy fill), ORANGE =
				// miss in the band (the gated far read; the mask fills on the first hit).
				// Their area is the share of faces paying (or saving) the level+1 read.
				else if(memo_state == GI_VIS_MEMO_STATE_HIT_FAR)
				{
					memo_color = vec4(0.0, 0.7, 0.7, GI_SUN_TIER_DEBUG_ALPHA);
				}
				else if(memo_state == GI_VIS_MEMO_STATE_MISS_FAR)
				{
					memo_color = vec4(1.0, 0.5, 0.0, GI_SUN_TIER_DEBUG_ALPHA);
				}
			}
			imageStore(s_light_voxels_out, texel, memo_color);
			continue;
		}
		// The zero-radiance skip lands after every gate, so provenance alphas are unchanged.
		BRANCH
		if(zero_radiance)
		{
			// Stamp the face half (probe half unpopulated) so a black voxel's faces still
			// skip the cavity march on later rotations; zero-radiance is generation-stable
			// (an albedo or emissive change recomposes attributes, which bumps the epoch).
			if(face_memo_live && !face_hit)
			{
				imageStore(s_gi_vis_memo, texel,
				           uvec4(GiWorldProbeVisMemoPackFaceOnly(face_half,
				                                                 u_vis_memo_generation),
				                 0u, 0u, 0u));
			}
			imageStore(s_light_voxels_out, texel, vec4(0.0, 0.0, 0.0, 1.0));
			continue;
		}
		vec3 irradiance = GiEvalDirectLightingVoxel(position + light_jitter,
		                                            direction,
		                                            max(level_data.w, 0.01),
		                                            u_gi_shadow_near_field * near_scale,
		                                            center + light_jitter,
		                                            center_lift,
		                                            cached_dir_visibility,
		                                            cached_dir_index);
		// Bounce: LAST frame's world-probe irradiance around this face (the probes traced after
		// this pass last frame, so the loop advances one bounce per frame). The probes' E/pi
		// convention converts back with pi so one albedo/pi below serves the whole sum. The
		// "view" direction of the self-shadow bias is the face itself - a voxel has no camera,
		// and biasing purely along the face normal is the direction that clears its own surface.
		//
		// Branched, never an && chain: the right operand does an imageStore (the memo restamp),
		// and HLSL's && does not guarantee short-circuiting - the ternary lesson's sibling.
		vec3 probe_value;
		float sky_fraction;
		int memo_state_unused;
		BRANCH
		if(u_world_probe_ready)
		{
			if(GiBounceProbeIrradiance(position, direction, texel, memo_word, face_half,
			                           probe_value, sky_fraction, memo_state_unused))
			{
				// Attenuated by the face's own sub-probe-spacing visibility: the probes' ambient
				// is measured on a lattice that cannot see this cavity. The SAME value gated the
				// face above, so the gate costs nothing extra.
				irradiance += probe_value * GI_PI * visibility;
			}
		}
		vec3 radiance = bounded_albedo * irradiance / GI_PI + emissive;
		// RELIGHT EMA (GI_LIGHT_VOXEL_EMA_BLEND): the relight is SAMPLED - one dithered
		// evaluation point per rotation (light_jitter above) - so near shadow edges and
		// 1/r^2 falloffs the raw store is a limit cycle at the rotation period. The gather
		// and probes are contracted to integrate that; MIRRORS read the volume raw and
		// showed it as shimmer. Folding each relight into the voxel's own history makes the
		// volume the integrator. Blend 1 (CPU-held on light/content change, debug writes,
		// first frames) writes through; a previous texel with alpha 0 was culled or never
		// measured - its zero is provenance, not radiance, and is never blended in.
		float ema = u_light_voxel_ema_blend;
		BRANCH
		if(ema < 1.0)
		{
			vec4 previous = imageLoad(s_light_voxels_out, texel);
			if(previous.w > 0.0)
			{
				radiance = mix(previous.xyz, radiance, ema);
			}
		}
		imageStore(s_light_voxels_out, texel, vec4(radiance, 1.0));
	}
}

#endif // __GI_LIGHT_VOXELS_KERNEL_SH__
