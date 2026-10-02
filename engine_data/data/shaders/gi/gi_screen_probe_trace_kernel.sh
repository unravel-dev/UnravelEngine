#ifndef __GI_SCREEN_PROBE_TRACE_KERNEL_SH__
#define __GI_SCREEN_PROBE_TRACE_KERNEL_SH__

/*
 * GI screen probe trace - the Lumen recipe. Compiled twice:
 *
 *  - cs_gi_screen_probe_trace_full.sc: one 8x8 group per traced probe, one thread per
 *    octahedral texel - all 64 rays fresh every frame. The default and the quality
 *    ceiling.
 *  - cs_gi_screen_probe_trace_adaptive.sc (GI_SCREEN_PROBE_TRACE_ADAPTIVE, the
 *    settings::adaptive_rays checkbox): Lumen's structured-importance-sampling shape at
 *    the same per-frame-complete contract - 2x2 blocks whose reprojected importance
 *    concentrates energy (ratio over the tile mean) or straddle the cull line trace at FULL
 *    per-texel detail (x2 samples on the very brightest), every other block traces one cell
 *    jittered across its quad with GI_ADAPTIVE_COARSE_SAMPLES samples and splats it. Both
 *    programs cull the same texels (GiTexelVisible) - 16 + 3K rays per
 *    probe plus the detail texels' supersamples, against the full program's 48-64. FOUR probes pack
 *    into each 64-lane group (16 lanes each); a 16-thread group alone would leave three
 *    quarters of every wave idle. The trade is per-frame variance and 4x4 angular granularity
 *    in DIM octants only - white, one-frame-lived, integrated by the resolve temporal.
 *
 * Both programs trace through a SAMPLE POOL: the group numbers every sample of its probes
 * (slot by slot, ray unit by ray unit) and lane L of the 64 traces samples L, L + 64, ...
 * A group lasts as long as its slowest lane, and a lane per ray unit would carry its unit's
 * whole chain - a supersampled texel's four samples beside lanes with one or none, a bright
 * probe's detail blocks beside a dim probe's quads - where the pool gives every lane
 * ceil(samples / 64) of them. The splat is integer atomics, so which lane traced a sample
 * cannot change the result.
 *
 * There is deliberately NO probe-space temporal accumulation in either form. Averaging in
 * probe space (e.g. direction-stratum windows blended 1/n into the tile) turns white
 * per-frame noise into probe-granular correlated drift - tiles serving differently-aged
 * strata, rare emitter arrivals living for ~cap frames as 16px-coherent blobs, walk
 * cadences stepping every anchor at once - and correlated drift is exactly what the
 * downstream per-pixel temporal cannot remove (it passes through as signal and its change
 * detector snaps on it). The per-frame gather stays white; the full-res dual-rate temporal
 * and the spatial denoiser own ALL accumulation. Ray budget scales with PROBE DENSITY
 * (settings::probe_spacing) first - the artifact-free knob, and the one Lumen ships -
 * and with adaptive_rays second.
 *
 * Probes ARE pixels: the anchor is a Halton-jittered G-buffer pixel of the probe's tile
 * (re-jittered every frame), its depth and normal taken as-is - no median selection, no
 * hysteresis, no layers. Stability is the downstream contract: world-anchored direction
 * indexing, plane-weighted integration, and the full-res temporal filter, which the
 * placement jitter deliberately feeds with a slightly different probe set each frame
 * [S21 s37-39].
 *
 * Rays are SHORTENED [S21 s69]: each establishes its own visibility out to
 * GI_SCREEN_PROBE_SHORT_RANGE (the same 8 m at every camera distance: mesh-exact over its first
 * GI_MESH_SDF_TRACE_RANGE metres, the cascade beyond), reads the light voxels at a hit, and
 * COMPLETES from the world probes'
 * radiance atlas on a miss (sphere-parallax corrected). Sky enters through the world probes or
 * directly past the outermost cascade. Every ray therefore measures something: the gather owes
 * nothing to a screen-space history or an environment fallback.
 *
 * SCREEN TRACE FIRST [S21 s66-68]: each ray first marches the Hi-Z depth pyramid - the same
 * machinery SSR/SSIL use - which resolves near-field occluders at PIXEL precision (an awning
 * half a metre above a wall occludes exactly the pixels in its shadow, which voxel-resolution
 * tracing cannot express). A confident on-screen hit commits, and its RADIANCE comes from
 * last frame's composited output (reprojected; the SSR scene-colour convention, the same
 * source the far field already trusts): the screen buys geometry AND full-resolution
 * lighting, which is what keeps the light-voxel lattice from imprinting converged voxel-scale
 * blotches onto every nearby receiver. The voxel read remains the commit's fallback when the
 * hit was off-screen last frame. Anything else - miss, left the screen, low confidence -
 * falls through to the SDF trace unchanged.
 * The march is BOUNDED by the ray's own short range (projected once per ray) and by the
 * viewport: the tests below reject every hit past either bound, so marching further would
 * only spend budget on answers that are then thrown away.
 *
 * Everything here is owned by gi_constants - the pass has no tuning surface beyond the
 * lattice descriptors and the adaptive_rays checkbox.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
// DecodeGBufferNormalMetalRoughnessLod and eval_radiance_sh live here.
#include "../lighting.sh"
#include "../hiz_trace.sh"

#include "gi/sdf_common.sh"
#include "gi/gi_emissive_nee.sh"
#define GI_LIGHT_VOXEL_READ
#include "gi/gi_light_voxels.sh"
#include "gi/gi_dirty_regions.sh"
#define GI_WORLD_PROBE_READ
#define GI_WORLD_PROBE_READ_RADIANCE
// Completion reads radiance + depth, never the irradiance cage - skipping it frees stage 11
// for the prev-color read below.
#define GI_WORLD_PROBE_SKIP_IRRADIANCE
// Completions REQUEST the sparse level-0 cages they read (stage 13, read-write): the cells
// around every completion point are what gets the 2 m probes allocated there.
#define GI_WORLD_PROBE_INDEX_RW
#include "gi/gi_world_probes.sh"
#include "gi/gi_noise.sh"
#include "gi/gi_env_sh.sh"
// The gather runs in the VIEW's pre-exposed space (Lumen's screen probe gather): store reads
// convert, history reads correct from last frame's scale.
#include "gi/gi_pre_exposure.sh"

/// LAST frame's composited output (the SSR convention, same source): the far-field radiance
/// for hits BEYOND the cascades, where the light voxels have nothing. Bound in place of the
/// world-probe irradiance cage the trace never read; the compacted probe list lives in the
/// probe buffer's list region (GiProbeTracedListBase), not in a stage of its own.
SAMPLER2D(s_gi_prev_color, 11);

/// rgb = radiance, a = hitT (negative = completed/sky). One 8x8 tile per probe,
/// fully rewritten every frame.
IMAGE2D_RW(s_probe_radiance_out, rgba16f, 5);
/// Probe records: reuses the existing layout (gi_probe_common.sh) so downstream plumbing holds.
BUFFER_RW(b_gi_probes, vec4, 7);

/// The Hi-Z depth pyramid when the screen trace is enabled, else the raw G-buffer depth.
/// Mip 0 is the device depth either way (cs_hiz_generate copies it verbatim), so the anchor
/// reconstruction below reads the same values from both.
SAMPLER2D(s_hiz, 8);
SAMPLER2D(s_gi_normal, 9);
/// This frame's velocity buffer (full camera resolution): RG = total uv-delta, BA = the
/// OBJECT-ONLY component. A screen hit on an object-motion pixel reprojects through it to
/// the mover's own last-frame pixel instead of being declined by the depth test (the
/// camera reprojection lands where the mover was NOT). The environment SH takes no stage:
/// it rides the probe buffer's SH block (GiProbeEnvShBase, staged by the args pass).
SAMPLER2D(s_gi_velocity, 14);

/// xyz = camera position (world-probe window centre), w = frame index.
uniform vec4 u_gi_camera;
/// zw = this frame's R2 offset for the sub-texel cone jitter (the direction cycle), computed in
/// DOUBLE on the CPU: fract(R2 x float(frame)) in float has 1/128 precision after ~1e5 frames,
/// which collapses the jitter to a few positions in long sessions. xy = the integrate's offset.
uniform vec4 u_gi_jitter;
/// x > 0 when s_hiz holds a full pyramid and the screen-trace tier runs. y = the young-pixel
/// threshold and z = the adaptive flag - consumed by the CLASSIFY pass, bound here only for layout
/// parity.
/// w > 0 when s_gi_prev_color holds last frame's composited output; > 1.5 when its alpha
/// also carries each pixel's view depth (the RGBA16F history), which GiReadHistory then
/// validates a reprojection against.
uniform vec4 u_gi_screen_trace;
/// Previous view projection: the anchor reprojects into LAST frame's lattice to read the
/// importance state the filter keeps in that probe's record slots.
uniform mat4 u_gi_prev_view_proj;

/// Probe slots per group: the adaptive program packs four 16-lane probes into one 64-lane
/// group; the full program's 8x8 group is one fully-busy probe already.
#if defined(GI_SCREEN_PROBE_TRACE_ADAPTIVE)
#	define GI_TRACE_SLOT_COUNT 4
#else
#	define GI_TRACE_SLOT_COUNT 1
#endif
/// Lanes per probe in the adaptive program = the 4x4 block count of the 8x8 tile.
// = (GI_PROBE_DIR_EDGE / 2) squared, written as a literal: it feeds NUM_THREADS, and a GLSL
// 430 layout id takes no expression (glslang: 'non-literal layout-id value').
#define GI_TRACE_ADAPTIVE_LANES 16
/// Lanes per group (both programs), and per probe slot.
#define GI_TRACE_GROUP_LANES 64
#if defined(GI_SCREEN_PROBE_TRACE_ADAPTIVE)
#	define GI_TRACE_SLOT_LANES GI_TRACE_ADAPTIVE_LANES
#else
#	define GI_TRACE_SLOT_LANES GI_TRACE_GROUP_LANES
#endif
/// Ray units per slot at most: 16 blocks x 4 detail texels, or the full program's 64 texels.
#define GI_TRACE_MAX_UNITS 64
/// Pool entries per slot: its units, then its total.
#define GI_TRACE_POOL_STRIDE (GI_TRACE_MAX_UNITS + 1)
/// A ray unit's samples at most, and phase-2 iterations per lane at most (a bound for the compiler).
#define GI_TRACE_UNIT_MAX_SAMPLES (GI_IMPORTANCE_SUPERSAMPLE_MAX + GI_EMISSIVE_NEE_SAMPLES)
#define GI_TRACE_POOL_GUARD (GI_TRACE_SLOT_COUNT * GI_TRACE_MAX_UNITS * GI_TRACE_UNIT_MAX_SAMPLES / GI_TRACE_GROUP_LANES + 1)
/// Binary-search steps over a slot's GI_TRACE_POOL_STRIDE prefix entries.
#define GI_TRACE_POOL_SEARCH_STEPS 7
/// The group's SAMPLE POOL. Per slot: one entry per ray unit - its exclusive sample prefix (low 16 bits) and its
/// cell (base x bits 16-18, base y bits 19-21, bit 22 = 2x2 span) - then one entry holding the slot's sample
/// total; plus the slot's unit count and packed probe coordinate.
SHARED uint s_pool_entry[GI_TRACE_SLOT_COUNT * GI_TRACE_POOL_STRIDE];
SHARED uint s_pool_units[GI_TRACE_SLOT_COUNT];
SHARED uint s_pool_probe[GI_TRACE_SLOT_COUNT];

SHARED vec3 s_anchor_normal[GI_TRACE_SLOT_COUNT];
SHARED vec3 s_origin[GI_TRACE_SLOT_COUNT];
SHARED float s_short_range[GI_TRACE_SLOT_COUNT];
/// Anchor in the screen tier's spaces, shared by every ray's Hi-Z march: full-res uv (self-hit
/// rejection), (uv, device z) screen-space origin, and the view-space origin.
SHARED vec2 s_anchor_uv[GI_TRACE_SLOT_COUNT];
SHARED vec3 s_ss_origin[GI_TRACE_SLOT_COUNT];
SHARED vec3 s_vs_origin[GI_TRACE_SLOT_COUNT];
/// Base record index of the reprojected PREVIOUS probe, or -1 when reprojection failed.
SHARED int s_history_record[GI_TRACE_SLOT_COUNT];
SHARED float s_importance_mean[GI_TRACE_SLOT_COUNT];
/// The reprojected probe's importance state (its 4x4 direction blocks), staged by the leader,
/// so the per-ray lookup does not re-read the four record vec4s the leader already loaded for
/// the mean.
SHARED vec4 s_importance_mip[GI_TRACE_SLOT_COUNT * 4];
/// The probe's importance PDF total - cosine to the anchor normal x the block's reprojected importance, over
/// the texels the BRDF cull keeps - staged by the leader for the full program's allocation (GiFullRayUnit).
SHARED float s_importance_pdf_sum[GI_TRACE_SLOT_COUNT];
/// Dispatch-uniform values hoisted out of the per-ray loop.
SHARED vec2 s_screen_size;
SHARED vec2 s_frame_r2;
/// EXPLICIT EMISSIVE SAMPLING (gi_emissive_nee.sh). Per slot: the aimed emitters' cone
/// axes and cosines (a cosine above 1 = no emitter), the aimed-ray count per emitter
/// (n_e), the jittered-ray count (n_c) and the aimed emitter per CELL (a texel, or a
/// coarse block's 2x2 - the block's mean is what its four texels store), and the
/// per-texel accumulators every sample splats into. Fixed point (GI_NEE_FIXED per unit)
/// because shared-memory atomics are integer only; the hit distance is the max of the
/// ordered float bits. One MIS sum per cell needs every technique's count before the first
/// ray is traced, hence the two extra barriers in main.
#define GI_NEE_K GI_EMISSIVE_NEE_PER_PROBE
/// Fixed-point scale of the accumulators. A unit is 1 / (GI_NEE_FIXED x omega) of RADIANCE,
/// not 1 / GI_NEE_FIXED - the resolve divides the sum by the cell's solid angle - and
/// GiSplatSample rounds each sample independently, so a ray under the rounding floor adds
/// EXACTLY ZERO. This scale puts that floor at 1.5e-5 to 4.1e-5 across an 8x8 tile, under
/// anything the light voxels carry: this engine works at radiance of order 1e-4, where auto
/// exposure's dark adaptation makes any loss visible, and the floor varies 5x with the
/// octahedral |d|_1, so a coarser scale prints the truncation as a direction-dependent
/// pattern. The ceiling stays safe because the cap below bounds the CONTRIBUTION: a cell
/// would need 409 capped samples to wrap 2^32, against the ~20 it actually receives.
#define GI_NEE_FIXED 131072.0
/// Per-sample cap on the CONTRIBUTION - L / pdf, the estimator's own output - not on L.
///
/// A cap on L would defeat the whole point of aiming rays. A JITTERED sample's denominator
/// is the cell PDF (3 to 16), so its contribution is about L and a cap on L is nearly a cap
/// on the contribution. An AIMED sample is divided by n_e / Omega_e, which for a small
/// emitter is enormous - a bulb subtending 6e-4 sr gives ~1600 - so its contribution is a
/// hundredth of L or less, far under any ceiling, while a cap on L would cut its radiance
/// directly: an under-estimate scaling as cap/L, worst for the smallest and brightest
/// emitters, which are exactly what explicit emissive sampling exists to find. The jittered
/// technique cannot make it back, because near the emitter the balance-heuristic
/// denominator is dominated by the aimed density.
///
/// The value is what one sample may contribute to the cell's MEAN radiance. A single sample
/// carrying a whole coarse cell that resolves at the store's GI_MAX_RAY_RADIANCE needs about
/// 46 (40 x the largest coarse omega), so twice the store clamp leaves headroom without
/// letting a genuine firefly through - the probe filter's governor still sees it.
#define GI_NEE_CONTRIBUTION_MAX (2.0 * GI_MAX_RAY_RADIANCE)
/// How far before its last screen-verified point a ray's SDF march resumes after a screen
/// march that left the viewport, ran out of iterations or found a crossing the validation
/// rejected (Lumen's HZB trace writes the last visible distance on a miss and its SDF trace
/// starts there). The verified point is the boundary of the last Hi-Z tile the march
/// skipped, so the margin only has to cover that tile's own depth spread. Owned here like
/// GI_NEE_FIXED: a single consumer. 1e6 turns the resume off.
#define GI_SCREEN_TRACE_RESUME_MARGIN 0.15
SHARED vec3 s_nee_axis[GI_TRACE_SLOT_COUNT * GI_NEE_K];
SHARED float s_nee_cos[GI_TRACE_SLOT_COUNT * GI_NEE_K];
SHARED uint s_nee_rays[GI_TRACE_SLOT_COUNT * GI_NEE_K];
SHARED uint s_cell_rays[GI_TRACE_SLOT_COUNT * GI_PROBE_DIR_COUNT];
SHARED int s_cell_nee[GI_TRACE_SLOT_COUNT * GI_PROBE_DIR_COUNT];
SHARED uint s_acc_r[GI_TRACE_SLOT_COUNT * GI_PROBE_DIR_COUNT];
SHARED uint s_acc_g[GI_TRACE_SLOT_COUNT * GI_PROBE_DIR_COUNT];
SHARED uint s_acc_b[GI_TRACE_SLOT_COUNT * GI_PROBE_DIR_COUNT];
SHARED uint s_acc_t[GI_TRACE_SLOT_COUNT * GI_PROBE_DIR_COUNT];
// Per probe: rays answered by the SCREEN tier over rays traced - the probe's screen share,
// stored to its record for the temporal's camera-motion collapse (gi_temporal_kernel.sh).
SHARED uint s_screen_rays[GI_TRACE_SLOT_COUNT];
SHARED uint s_traced_rays[GI_TRACE_SLOT_COUNT];
/// Rays that hit MOVING geometry: a screen hit on an object-motion
/// pixel, or an SDF hit on an instance displaced by more than GI_TEMPORAL_MOVING_SPEED of
/// the probe's depth since last frame. The temporal shortens the history of every pixel
/// this probe serves by the fraction (gi_temporal_kernel.sh) - the receivers of a mover's
/// shadow and bounce, which the per-pixel velocity buffer cannot see.
SHARED uint s_moving_rays[GI_TRACE_SLOT_COUNT];
/// The rest of the tier census (record [11], GI_PROBE_TIERS): rays the mesh SDF answered,
/// rays the clipmap answered, and completions that ended in the sky. The world-probe
/// completions are the remainder.
SHARED uint s_mesh_rays[GI_TRACE_SLOT_COUNT];
SHARED uint s_clipmap_rays[GI_TRACE_SLOT_COUNT];
SHARED uint s_sky_rays[GI_TRACE_SLOT_COUNT];
/// EMITTER SAMPLING CENSUS (record [7], GI_PROBE_EMITTER): the luminance of the MIS
/// contributions splatted by AIMED rays and by every ray (fixed point, GI_NEE_CENSUS_FIXED
/// per unit), the aimed-ray count and the emitters the leader selected. An instrument for
/// the gi_emitter_share view; nothing lit reads it.
#define GI_NEE_CENSUS_FIXED 1024.0
SHARED uint s_aimed_contribution[GI_TRACE_SLOT_COUNT];
SHARED uint s_total_contribution[GI_TRACE_SLOT_COUNT];
SHARED uint s_aimed_rays[GI_TRACE_SLOT_COUNT];
SHARED uint s_selected_emitters[GI_TRACE_SLOT_COUNT];

/// The probe's screen share (x) and moving share (y): rays the screen tier answered, and rays
/// that hit moving geometry, over rays traced (all counted where the tier is decided). The
/// moving share is the temporal's and the rough specular's moving-hit collapse weight.
/// Written by the slot leader after the trace barrier; interpolated probes get their
/// parents' mean from the interp pass. The full tier split goes to record [11] for the
/// gi_probe_tiers debug view.
void GiStoreScreenShare(int slot, uint record)
{
	uint traced = s_traced_rays[slot];
	float inv_traced = traced > 0u ? 1.0 / float(traced) : 0.0;
	float share = float(s_screen_rays[slot]) * inv_traced;
	float moving = float(s_moving_rays[slot]) * inv_traced;
	b_gi_probes[record + uint(GI_PROBE_SCREEN_SHARE)] = vec4(share, moving, 0.0, 0.0);
	// The debug records: only while their views are displayed (u_gi_probe_debug_census).
	if(u_gi_probe_debug_census)
	{
		b_gi_probes[record + uint(GI_PROBE_TIERS)] = vec4(share,
		                                                   float(s_mesh_rays[slot]) * inv_traced,
		                                                   float(s_clipmap_rays[slot]) * inv_traced,
		                                                   float(s_sky_rays[slot]) * inv_traced);
		float total_contribution = float(s_total_contribution[slot]) / GI_NEE_CENSUS_FIXED;
		float aimed_contribution = float(s_aimed_contribution[slot]) / GI_NEE_CENSUS_FIXED;
		b_gi_probes[record + uint(GI_PROBE_EMITTER)] =
		    vec4(total_contribution > 0.0 ? aimed_contribution / total_contribution : 0.0,
		         float(s_aimed_rays[slot]) * inv_traced,
		         float(s_selected_emitters[slot]) / float(GI_NEE_K),
		         total_contribution);
	}
}

/// 1 when the velocity buffer marks the pixel at @p hit_uv as OBJECT motion (the BA lanes),
/// 0 when static or when the buffer is not bound to this trace.
float GiHitObjectMotion(vec2 hit_uv)
{
	if(u_gi_screen_trace.w <= 2.5)
	{
		return 0.0;
	}
	vec4 vel4 = texture2DLod(s_gi_velocity, hit_uv, 0.0);
	vec2 vel_dim = vec2(textureSize(s_gi_velocity, 0));
	return smoothstep(0.5, 1.5, length(vel4.zw * vel_dim));
}

/// The environment radiance along @p dir from the SH block the args pass staged into the
/// probe buffer (GiProbeEnvShBase) - eval_radiance_sh's sum, with the coefficients read
/// from the buffer instead of the IRRADIANCE_SH texture. Ringing is clamped non-negative.
vec3 GiProbeEnvRadiance(vec3 dir)
{
	uint sh_base = GiProbeEnvShBase();
	vec3 radiance = vec3_splat(0.0);
	for(int k = 0; k < GI_ENV_SH_COEFFS; ++k)
	{
		radiance += b_gi_probes[sh_base + uint(k)].xyz * GiEnvShBasis(k, dir);
	}
	// The SH holds absolute environment radiance; the gather is pre-exposed.
	return max(radiance, vec3_splat(0.0)) * u_pre_exposure_value;
}

/*
 * HISTORY READ, validated. True when last frame's composite holds THIS surface at the
 * reprojected pixel: on screen last frame and, when the snapshot carries its view depth
 * (u_gi_screen_trace.w > 1.5), the stored depth agrees with the hit's reprojected depth
 * within GI_TEMPORAL_DEPTH_TOLERANCE of it - the temporal accumulation's own tolerance.
 * Without the test a disoccluded reprojection would read whatever surface last frame showed
 * at that pixel, and the first frame after a disocclusion would measure a different source
 * (the light voxels) than the frames after it (the composite) - a bias step the temporal
 * would carry. No stage is free for a depth history; the snapshot's alpha carries it instead.
 */
bool GiReadHistory(vec3 hit_position, out vec3 radiance)
{
	radiance = vec3_splat(0.0);
	if(u_gi_screen_trace.w <= 0.0)
	{
		return false;
	}
	vec4 prev_clip = mul(u_gi_prev_view_proj, vec4(hit_position, 1.0));
	if(prev_clip.w <= 0.0)
	{
		return false;
	}
	vec3 ndc = clipTransform(prev_clip.xyz / prev_clip.w);
	vec2 prev_uv = ndc.xy * 0.5 + 0.5;
	if(any(lessThan(prev_uv, vec2_splat(0.0))) || any(greaterThan(prev_uv, vec2_splat(1.0))))
	{
		return false;
	}
	vec4 history = texture2DLod(s_gi_prev_color, prev_uv, 0.0);
	if(u_gi_screen_trace.w > 1.5 &&
	   abs(history.w - prev_clip.w) > GI_TEMPORAL_DEPTH_TOLERANCE * prev_clip.w)
	{
		return false;
	}
	// The snapshot was written under LAST frame's pre-exposure (UE P / Pprev).
	radiance = history.xyz * u_history_pre_exposure_correction;
	return true;
}

/*
 * Radiance for a hit whose light-voxel read failed, BEYOND the outermost cascade - where "no
 * data" must not mean "no light" (a distant wall rendering black): the Lumen
 * far-field recipe applies - reproject the hit into LAST frame's composited output, which
 * already carries that geometry's shadow-mapped lighting (the SSR scene-colour convention;
 * the temporal mean and the radiance clamp bound the feedback); off-screen or history-less,
 * the sky SH along the ray, the miss contract. WITHIN the cascades the callers answer honest
 * darkness directly - sub-voxel contact occlusion, sealed rooms - using the level search they
 * already ran, so this function does not re-run it.
 */
vec3 GiFarFieldRadiance(vec3 hit_position, vec3 sample_dir)
{
	vec3 history;
	BRANCH
	if(GiReadHistory(hit_position, history))
	{
		return history;
	}
	return GiProbeEnvRadiance(sample_dir);
}

/*
 * The screen tier's history read: a hit ON A MOVER (the velocity buffer's object-only lanes
 * at the hit pixel) reprojects through the mover's own velocity to the pixel it occupied
 * last frame - which holds the mover's own lit colour - while everything else takes the
 * camera reprojection with its depth validation (GiReadHistory). Without this every hit on
 * moving geometry would be declined (the camera reprojection lands where the mover was NOT
 * and the stored depth disagrees) and shaded from a voxel up to a relight rotation old. The
 * velocity buffer carries no depth, so a mover's pixel gets only a loose depth agreement
 * (twice the camera path's tolerance) against gross occlusion changes; the neighbourhood
 * the temporal accumulates over bounds the rest, as it does for the mover's own history.
 */
bool GiReadHistoryScreen(vec3 hit_position, vec2 hit_uv, out vec3 radiance)
{
	radiance = vec3_splat(0.0);
	BRANCH
	if(u_gi_screen_trace.w > 2.5)
	{
		vec4 vel4 = texture2DLod(s_gi_velocity, hit_uv, 0.0);
		vec2 vel_dim = vec2(textureSize(s_gi_velocity, 0));
		float object_w = smoothstep(0.5, 1.5, length(vel4.zw * vel_dim));
		BRANCH
		if(object_w >= 0.5)
		{
			vec2 prev_uv = hit_uv - vel4.xy;
			if(any(lessThan(prev_uv, vec2_splat(0.0))) || any(greaterThan(prev_uv, vec2_splat(1.0))))
			{
				return false;
			}
			vec4 prev_clip = mul(u_gi_prev_view_proj, vec4(hit_position, 1.0));
			vec4 history = texture2DLod(s_gi_prev_color, prev_uv, 0.0);
			if(prev_clip.w <= 0.0 ||
			   abs(history.w - prev_clip.w) > 2.0 * GI_TEMPORAL_DEPTH_TOLERANCE * prev_clip.w)
			{
				return false;
			}
			// Last frame's scale, as in GiReadHistory.
			radiance = history.xyz * u_history_pre_exposure_correction;
			return true;
		}
	}
	return GiReadHistory(hit_position, radiance);
}

/// The checked form for callers without a level search of their own (the screen tier).
vec3 GiFarFieldFallback(vec3 hit_position, vec3 sample_dir)
{
	float blend;
	float voxel;
	if(SdfFindClipmapLevel(hit_position, blend, voxel) < SDF_CLIPMAP_LEVEL_COUNT)
	{
		return vec3_splat(0.0);
	}
	return GiFarFieldRadiance(hit_position, sample_dir);
}

/// The reprojected probe's luminance for one of the 16 2x2 direction blocks, as the importance
/// state staged it (pre-exposure corrected).
float GiImportanceBlockLuma(int slot, int block)
{
	vec4 mip = s_importance_mip[slot * 4 + block / 4];
	int lane = block % 4;
	return lane == 0 ? mip.x : (lane == 1 ? mip.y : (lane == 2 ? mip.z : mip.w));
}

void GiStoreScreenProbeRay(int slot, ivec2 texel, vec3 radiance, float hit_t)
{
	// The cell stores what it measured. No absolute clamp on its radiance: a clamp at
	// GI_MAX_RAY_RADIANCE plateaus a bright emitter's spread well below its intensity, and an
	// emitter that fills the cell is not a firefly. The MIS contribution cap
	// (GI_NEE_CONTRIBUTION_MAX, per sample) bounds the estimator's step here; the firefly
	// governor caps each tap against the probe's importance state in the filter's first pass
	// (cs_gi_screen_probe_filter.sc), where the reference is a measurement the cap cannot
	// feed back into.
	imageStore(s_probe_radiance_out, texel, vec4(radiance, hit_t));
}

/// The block's importance over the tile mean - the reprojected state, or for a probe without
/// history the lighting prior; 1.0 (neutral) when neither exists - uniform allocation.
float GiScreenProbeBlockRatio(int slot, int block)
{
	if(s_history_record[slot] < 0 || s_importance_mean[slot] <= 1e-4)
	{
		return 1.0;
	}
	return GiImportanceBlockLuma(slot, block) / s_importance_mean[slot];
}

/*
 * ONE direction, answered Hi-Z -> SDF -> world-probe completion. Returns (radiance, hitT;
 * -1 = completed/sky). Returned, never out-parameters - the shaderc HLSL path miscompiles
 * out-params in .sc helpers silently. The sample loop lives in the caller (main), which
 * chooses each direction: a jitter inside the cell, or an aimed direction inside an
 * emitter's cone (gi_emissive_nee.sh) - one call site either way.
 *
 * Sub-texel DIRECTION jitter (the caller's). Fixed centre rays ALIAS small bright sources: a source
 * smaller than one cone is either skewered or missed by the grid, and which probes catch
 * it varies smoothly with anchor position - printing stationary whitish blobs across
 * walls that NO downstream filter can remove, because the per-probe estimates are BIASED,
 * not noisy. Jittering the sample within its cone per frame turns that bias into
 * per-frame variance the temporal chain integrates - each cone measures its whole solid
 * angle over the accumulation window. R2 low-discrepancy across frames; the pattern is
 * addressed by PROBE and cell (GiProbeCellNoise in gi_noise.sh - NOT by atlas texel, whose
 * stride-8 IGN prints moving waves). The multi-sample pattern is the first four points of
 * a shifted (0,2)-net: positions 0/1 are the exact antithetic pair, so counts one and two
 * reproduce the classic estimator.
 */
vec4 GiTraceScreenProbeDirection(int slot, vec3 sample_dir)
{
	float hit_t = -1.0;
	{
		vec3 radiance = vec3_splat(0.0);
		bool committed = false;
		// The screen march ran the WHOLE short range in front of the depth buffer: the segment
		// is verified empty of rendered geometry, so the ray completes from the world probes
		// without an SDF march (Lumen's bReachedRadianceCache).
		bool screen_reached = false;
		// Where the SDF march starts: 0, or the last screen-verified distance less a margin
		// when the march left the screen, ran out of iterations or found a crossing the
		// validation rejected (Lumen writes that distance on a miss and its SDF trace resumes
		// there).
		float sdf_t_min = 0.0;
		// The hit landed on moving geometry (see s_moving_rays).
		bool moving = false;
		// 1 = screen commit, 2 = SDF hit, 3 = completion; 1 feeds the probe's screen share.
		int answered_tier = 3;
		// The census detail under tier 2 / 3: a mesh-exact SDF hit, and a completion the
		// world probes could not answer (the sky SH did).
		bool mesh_hit = false;
		bool sky_completion = false;
		// SCREEN TIER: Hi-Z march from the anchor pixel. A confident on-screen hit inside the
		// ray's own range commits at PIXEL precision; everything else falls through to the SDF.
		BRANCH
		if(u_gi_screen_trace.x > 0.0)
		{
			vec3 vs_dir = mul(u_view, vec4(sample_dir, 0.0)).xyz;
			vec3 ss_dir = HizProjectVsDirToSsDir(s_vs_origin[slot], vs_dir, s_ss_origin[slot]);
			BRANCH
			if(dot(ss_dir.xy, ss_dir.xy) >= 1e-12)
			{
				// Parametric bound at the ray's own short range, mirroring the projection
				// HizProjectVsDirToSsDir applies: any hit past it is rejected by the range
				// test below, so the march may stop there instead of spending its budget.
				// 5% margin - the authority stays the exact test on the reconstructed hit.
				// An endpoint behind the near plane leaves the march unbounded (the rare
				// toward-camera ray); a negative parameter skips the tier, which is the
				// watertight SDF answer anyway.
				float t_limit = FFX_SSSR_FLOAT_MAX;
				vec4 end_pj4 =
				    mul(u_proj, vec4(s_vs_origin[slot] + vs_dir * s_short_range[slot], 1.0));
				if(end_pj4.w > 1e-6)
				{
					vec3 end_pj = clipTransform(end_pj4.xyz / end_pj4.w);
					end_pj.xy = end_pj.xy * 0.5 + 0.5;
					end_pj.z = toDepthTextureZ(end_pj.z);
					vec3 ss_delta = end_pj - s_ss_origin[slot];
					t_limit = 1.05 * dot(ss_delta, ss_dir) / max(dot(ss_dir, ss_dir), 1e-12);
				}
				vec3 ss_hit = vec3_splat(0.0);
				vec3 ss_last_above = vec3_splat(0.0);
				// Mip-1 floor (GI_SCREEN_TRACE_MIN_MIP): these rays are cone-amortized over a
				// probe tile, so a mip-0 walk buys sub-pixel precision below the cone footprint
				// at twice the traversal cost. Validation still reads mip-0 depth; a lost
				// commit falls through to the SDF, which is the watertight answer anyway.
				int march_code = HizHierarchicalRaymarchCode(s_hiz, s_ss_origin[slot], ss_dir,
				                                             s_screen_size, GI_SCREEN_TRACE_MIN_MIP,
				                                             GI_SCREEN_TRACE_MAX_STEPS, t_limit, true,
				                                             ss_hit, ss_last_above);
				bool marched = march_code == 1;
				screen_reached = march_code == 2;
				BRANCH
				if(marched)
				{
					vec3 vs_hit = HizComputeViewspacePosition(ss_hit.xy, ss_hit.z);
					float hit_dist = length(vs_hit - s_vs_origin[slot]);
					// Beyond the short range the world probes own the answer, exactly as they
					// do for an SDF miss - a far screen hit must not override that contract.
					BRANCH
					if(hit_dist < s_short_range[slot])
					{
						float tolerance = GI_SCREEN_TRACE_DEPTH_TOLERANCE +
						                  GI_SCREEN_TRACE_THICKNESS * (hit_dist / s_short_range[slot]);
						float confidence = HizValidateHit(s_hiz, s_gi_normal, ss_hit,
						                                  s_anchor_uv[slot], s_vs_origin[slot],
						                                  vs_hit, tolerance);
						BRANCH
						if(confidence >= GI_SCREEN_TRACE_CONFIDENCE_MIN)
						{
							vec3 hit_position = mul(u_invView, vec4(vs_hit, 1.0)).xyz;
							GBufferDataNormalMetalRoughness hd =
							    DecodeGBufferNormalMetalRoughnessLod(ss_hit.xy, s_gi_normal, 0.0);
							vec3 hit_normal = normalize(hd.world_normal);
							if(dot(hit_normal, sample_dir) > 0.0)
							{
								hit_normal = -hit_normal;
							}
							// SCREEN-HIT LIGHTING from last frame's composited output: the
							// screen tier resolves geometry at pixel precision, but reading
							// the 0.25 m light voxels at that hit would re-imprint the voxel
							// lattice onto every nearby receiver as CONVERGED voxel-scale
							// blotches larger than any downstream kernel's footprint - the
							// denoiser cannot reach them (a 0.5 m voxel at 3 m spans ~100 px
							// against a ~16 px a-trous reach). Last frame's composite
							// carries this surface's radiance at FULL pixel resolution and
							// is ALREADY this kernel's trusted source for the far field
							// (GiFarFieldRadiance) - the same source at nearer range, the
							// same feedback bounds (albedo < 1 closes the loop; the ray
							// clamp and the firefly governor bound spikes). The read is
							// validated against the depth the snapshot carries (GiReadHistory):
							// a disoccluded reprojection declines instead of reading whatever
							// surface last frame showed there. Off-screen last frame, no
							// history, or a depth mismatch: the voxel read answers.
							// DIRTY-REGION CUT: where a placement just moved, appeared or
							// vanished (an emissive one: out to its light's reach), last
							// frame's composite still carries the light it left, and
							// reading it here would feed that light back into the gather -
							// with the temporal's memory on top, a moved emissive's pool
							// would decay over seconds. Inside a region the voxel read
							// answers (relit within a rotation) until the hold expires.
							moving = GiHitObjectMotion(ss_hit.xy) >= 0.5;
							bool screen_lit = GiDirtyRegionFactor(hit_position) < 0.5 &&
							                  GiReadHistoryScreen(hit_position, ss_hit.xy, radiance);
							if(!screen_lit)
							{
								if(GiLightVoxelReadBlend(hit_position, hit_normal,
								                         GI_LIGHT_VOXEL_FADE_VOXELS, radiance))
								{
									// Cached lighting into the gather's pre-exposed space.
									radiance = GiCachedToView(radiance);
								}
								else
								{
									// Occluded but unmeasured: honest darkness within the
									// cascades - for sub-voxel detail (railings, awning cloth)
									// this IS the contact occlusion the voxel tier cannot
									// express; past them, the far-field fallback.
									radiance = GiFarFieldFallback(hit_position, sample_dir);
								}
							}
							hit_t = max(hit_t, hit_dist);
							committed = true;
							answered_tier = 1;
						}
					}
				}
				if(!committed && !screen_reached)
				{
					vec3 vs_last = HizComputeViewspacePosition(ss_last_above.xy, ss_last_above.z);
					sdf_t_min = max(length(vs_last - s_vs_origin[slot]) - GI_SCREEN_TRACE_RESUME_MARGIN, 0.0);
				}
			}
		}
		BRANCH
		if(!committed)
		{
			// Mesh-exact over GI_MESH_SDF_TRACE_RANGE from wherever the screen march stopped
			// vouching, then the cascade the way Lumen traces its global distance field
			// (SdfTraceRayGather, SdfTraceClipmapLumen): the exact first metres are the thin-wall
			// defence and the contact detail, and past them the per-instance walk would dominate
			// this pass's cost (GI_MESH_SDF_TRACE_RANGE).
			SdfRayHit hit = SdfMakeMiss();
			BRANCH
			if(!screen_reached)
			{
				hit = SdfTraceRayGather(s_origin[slot], sample_dir, min(sdf_t_min, s_short_range[slot]),
				                        s_short_range[slot], GI_MESH_SDF_TRACE_RANGE, GI_TRACE_MAX_STEPS,
				                        GI_PROBE_TRACE_SURFACE_BIAS, GI_PROBE_TRACE_RELAXATION, true);
			}
			if(hit.hit)
			{
				answered_tier = 2;
				mesh_hit = hit.instance_index != SDF_NO_INSTANCE;
				hit_t = max(hit_t, hit.t);
				// A mesh-exact hit knows its instance: moving when the instance's
				// displacement since last frame exceeds GI_TEMPORAL_MOVING_SPEED of the
				// probe's depth (a relative-speed test). Cascade hits carry no
				// instance and never flag - an accepted limitation.
				if(hit.instance_index != SDF_NO_INSTANCE)
				{
					vec4 instance_velocity = SdfInstanceVelocity(hit.instance_index);
					float speed = max(length(instance_velocity.xyz), instance_velocity.w);
					float probe_depth = max(length(s_origin[slot] - u_gi_camera.xyz), 1.0);
					moving = speed > GI_TEMPORAL_MOVING_SPEED * probe_depth;
				}
				vec3 hit_position = s_origin[slot] + sample_dir * hit.t;
				// A CASCADE hit is pulled back by the march's surface expand; the light voxels are a
				// SURFACE store, so the read steps onto the surface estimate (hit_field).
				if(hit.instance_index == SDF_NO_INSTANCE)
				{
					hit_position += sample_dir * max(hit.hit_field, 0.0);
				}
				vec3 hit_normal = hit.normal;
				if(dot(hit_normal, sample_dir) > 0.0)
				{
					hit_normal = -hit_normal;
				}
				float hit_blend;
				float hit_voxel;
				int hit_level = SdfFindClipmapLevel(hit_position, hit_blend, hit_voxel);
				// One level search serves all three of its consumers: the buried-hit
				// guard, the within/beyond-cascade split, and (by making the beyond branch
				// explicit) the far-field fallback's own coverage test.
				if(hit_level < SDF_CLIPMAP_LEVEL_COUNT)
				{
					// Sub-surface hit guard: a hit the field itself reports as INSIDE geometry
					// (beyond half the covering voxel under the surface - normal acceptance
					// stops strictly above it) is a trace pathology: the launch-suppression
					// walk can release through thin geometry around the mesh<->clipmap
					// handover shell. Reading ANYTHING there imports the far side - the
					// light-voxel read selects a face slab by normal, and a tunneled hit
					// inside a sunlit wall hands the EXTERIOR face's radiance to an interior
					// ray. Honest darkness is the only answer that cannot leak. Unblended
					// finest-level reading, for the same reason the cage-visibility march
					// uses it: a verdict, not a surface resolve.
					if(SdfSampleClipmapLevel(hit_level, hit_position) < -0.5 * hit_voxel)
					{
						radiance = vec3_splat(0.0);
					}
					// Cross-faded across cascade levels (GI_LIGHT_VOXEL_FADE_VOXELS): a
					// first-success walk would switch from 0.25 m to 0.5 m voxels at a knife
					// edge the camera drags across every surface, popping at each level-0
					// re-snap. The blend mixes two MEASURED answers only; a hole still
					// falls through to the walk.
					else if(GiLightVoxelReadBlend(hit_position, hit_normal,
					                              GI_LIGHT_VOXEL_FADE_VOXELS, radiance))
					{
						// Cached lighting into the gather's pre-exposed space.
						radiance = GiCachedToView(radiance);
					}
					else
					{
						// Occluded but unmeasured within the cascades: honest darkness (the
						// sealed-room branch).
						radiance = vec3_splat(0.0);
					}
				}
				else
				{
					// Beyond every cascade the light-voxel volume has nothing by
					// construction; the far-field recipe answers directly.
					radiance = GiFarFieldRadiance(hit_position, sample_dir);
				}
			}
			else
			{
				// Completion: the world probes carry everything beyond the short range - scene
				// AND sky.
				if(GiWorldProbeRadiance(s_origin[slot] + sample_dir * s_short_range[slot],
				                        sample_dir, u_gi_camera.xyz, radiance))
				{
					// The world-probe atlas is a persistent store (cached lighting).
					radiance = GiCachedToView(radiance);
				}
				else
				{
					radiance = GiProbeEnvRadiance(sample_dir);
					sky_completion = true;
				}
			}
		}
		atomicAdd(s_traced_rays[slot], 1u);
		if(answered_tier == 1)
		{
			atomicAdd(s_screen_rays[slot], 1u);
		}
		else if(u_gi_probe_debug_census)
		{
			// The rest of the tier split feeds only the Probe Tiers view.
			if(answered_tier == 2)
			{
				if(mesh_hit)
				{
					atomicAdd(s_mesh_rays[slot], 1u);
				}
				else
				{
					atomicAdd(s_clipmap_rays[slot], 1u);
				}
			}
			else if(sky_completion)
			{
				atomicAdd(s_sky_rays[slot], 1u);
			}
		}
		if(moving)
		{
			atomicAdd(s_moving_rays[slot], 1u);
		}
		return vec4(radiance, hit_t);
	}
}

/// The one cull both programs share: a texel is traced and stored only when its centre direction
/// rises GI_IMPORTANCE_MIN_COSINE above the anchor's tangent plane. A ray under the plane runs back
/// into the probe's own surface and returns that surface's radiance, which the SH3 irradiance
/// projection then spreads into the directions that do light the surface.
bool GiTexelVisible(int slot, ivec2 local)
{
	vec2 tile_uv = (vec2(local) + vec2_splat(0.5)) / float(GI_PROBE_DIR_EDGE);
	return dot(GiOctDecode(tile_uv), s_anchor_normal[slot]) >= GI_IMPORTANCE_MIN_COSINE;
}

#if defined(GI_SCREEN_PROBE_TRACE_ADAPTIVE)

/// Rays the adaptive schedule grants a block this frame: 4 per-texel DETAIL rays when the
/// block concentrates energy or straddles the cull line (each texel then keeps or culls
/// itself, as in the full program - a coarse cone across the line would spread samples from
/// under the surface over its visible texels), 1 coarse cone otherwise (a block wholly under
/// the line keeps its single "ray" as the coarse executor's zero-store walk - no trace).
int GiScreenProbeBlockRays(int slot, int block)
{
	if(GiScreenProbeBlockRatio(slot, block) > GI_IMPORTANCE_SUPERSAMPLE_RATIO)
	{
		return 4;
	}
	ivec2 quad = ivec2((block % 4) * 2, (block / 4) * 2);
	int visible = 0;
	for(int t = 0; t < 4; ++t)
	{
		visible += GiTexelVisible(slot, quad + ivec2(t & 1, t >> 1)) ? 1 : 0;
	}
	return visible == 0 || visible == 4 ? 1 : 4;
}

#endif // GI_SCREEN_PROBE_TRACE_ADAPTIVE

/// A ray unit: the CELL it estimates (top-left texel + span: one texel, or a coarse
/// block's 2x2), its jittered sample count and whether it is traced at all.
struct GiRayUnit
{
	ivec2 base;
	int span;
	int samples;
	bool traced;
};

int GiCellIndex(int slot, ivec2 texel)
{
	return slot * GI_PROBE_DIR_COUNT + texel.y * GI_PROBE_DIR_EDGE + texel.x;
}

/// The cell a texel belongs to this frame: itself, or (adaptive coarse block) its 2x2.
GiRayUnit GiCellOfTexel(int slot, ivec2 local)
{
	GiRayUnit unit;
	unit.base = local;
	unit.span = 1;
	unit.samples = 0;
	unit.traced = false;
#if defined(GI_SCREEN_PROBE_TRACE_ADAPTIVE)
	int block = (local.y / 2) * 4 + (local.x / 2);
	if(GiScreenProbeBlockRays(slot, block) == 1)
	{
		unit.base = (local / 2) * 2;
		unit.span = 2;
	}
#endif
	return unit;
}

/// Evaluated per call on purpose: a per-texel table staged in shared memory (one evaluation
/// per lane, reads here) is slower on the gather - the recompute is ALU the wave hides, the
/// staging is not.
float GiCellSolidAngle(ivec2 base, int span)
{
	float omega = 0.0;
	for(int y = 0; y < span; ++y)
	{
		for(int x = 0; x < span; ++x)
		{
			omega += GiOctTexelSolidAngle(base + ivec2(x, y), GI_PROBE_DIR_EDGE);
		}
	}
	return omega;
}

ivec2 GiTexelOfDirection(vec3 direction)
{
	ivec2 texel = ivec2(floor(GiOctEncode(direction) * float(GI_PROBE_DIR_EDGE)));
	return clamp(texel, ivec2(0, 0), ivec2(GI_PROBE_DIR_EDGE - 1, GI_PROBE_DIR_EDGE - 1));
}

/// The first (brightest) aimed emitter whose cone touches the cell's footprint, or -1.
/// The test is the cone half angle plus the cell's angular half-diagonal, in cosines.
int GiSelectNeeForCell(int slot, ivec2 base, int span)
{
	vec3 centre = GiOctDecode((vec2(base) + vec2_splat(0.5 * float(span))) / float(GI_PROBE_DIR_EDGE));
	float half_cos = 1.0;
	for(int c = 0; c < 4; ++c)
	{
		vec2 corner_uv = (vec2(base) + vec2(float(c & 1), float(c >> 1)) * float(span)) / float(GI_PROBE_DIR_EDGE);
		half_cos = min(half_cos, dot(centre, GiOctDecode(corner_uv)));
	}
	float half_sin = sqrt(max(1.0 - half_cos * half_cos, 0.0));
	for(int k = 0; k < GI_NEE_K; ++k)
	{
		float cos_k = s_nee_cos[slot * GI_NEE_K + k];
		if(cos_k > 1.0)
		{
			continue;
		}
		float sin_k = sqrt(max(1.0 - cos_k * cos_k, 0.0));
		float cos_total = cos_k * half_cos - sin_k * half_sin;
		if(dot(centre, s_nee_axis[slot * GI_NEE_K + k]) >= cos_total)
		{
			return k;
		}
	}
	return -1;
}

/// Publishes a cell's jittered-sample count and aimed emitter to every texel it covers.
void GiPublishCell(int slot, ivec2 base, int span, int jittered_samples, int nee)
{
	for(int y = 0; y < span; ++y)
	{
		for(int x = 0; x < span; ++x)
		{
			int idx = GiCellIndex(slot, base + ivec2(x, y));
			s_cell_rays[idx] = uint(jittered_samples);
			s_cell_nee[idx] = nee;
		}
	}
}

/// The balance-heuristic denominator for a direction landing in a cell: the cell's own
/// jittered density plus every aimed cone that contains the direction. @p own_cone is the cone
/// an aimed sample was drawn from (-1 for a jittered sample) and always counts: a far emitter's
/// cone is a few float ulps wide in cosine, so a direction drawn at its rim can test as outside
/// it, and without its own density the aimed sample would be weighted as a jittered one - orders
/// of magnitude too heavy, a probe-sized spike wherever the aimed ray hits a lit surface.
float GiSampleDenominator(int slot, ivec2 base, int span, vec3 direction, int own_cone)
{
	float denominator = float(s_cell_rays[GiCellIndex(slot, base)]) *
	                    GiOctCellDirectionalPdf(direction, span, GI_PROBE_DIR_EDGE);
	for(int k = 0; k < GI_NEE_K; ++k)
	{
		float cos_k = s_nee_cos[slot * GI_NEE_K + k];
		if(cos_k <= 1.0 && (k == own_cone || dot(direction, s_nee_axis[slot * GI_NEE_K + k]) >= cos_k))
		{
			denominator += float(s_nee_rays[slot * GI_NEE_K + k]) / max(GiConeSolidAngle(cos_k), 1e-6);
		}
	}
	return max(denominator, 1e-6);
}

/// Adds one traced sample to the accumulators of the cell its direction lands in. @p own_cone is
/// the aimed emitter the sample was drawn toward, -1 for a jittered sample.
void GiSplatSample(int slot, ivec2 base, int span, vec3 direction, vec3 radiance, float hit_t,
                   int own_cone)
{
	bool is_aimed = own_cone >= 0;
	// The cap goes on the ESTIMATOR OUTPUT, after the division - see GI_NEE_CONTRIBUTION_MAX.
	vec3 contribution = min(radiance / GiSampleDenominator(slot, base, span, direction, own_cone),
	                        vec3_splat(GI_NEE_CONTRIBUTION_MAX));
	uvec3 fixed_point = uvec3(max(contribution, vec3_splat(0.0)) * GI_NEE_FIXED + vec3_splat(0.5));
	uint hit_bits = floatBitsToUint(max(hit_t, 0.0));
	// The census (record [7]): luminance of this contribution, by technique - only while the
	// Emitter Share view (or Probe Tiers) is displayed.
	if(u_gi_probe_debug_census)
	{
		uint census = uint(max(dot(contribution, vec3(0.2126, 0.7152, 0.0722)), 0.0) * GI_NEE_CENSUS_FIXED + 0.5);
		atomicAdd(s_total_contribution[slot], census);
		if(is_aimed)
		{
			atomicAdd(s_aimed_contribution[slot], census);
			atomicAdd(s_aimed_rays[slot], 1u);
		}
	}
	for(int y = 0; y < span; ++y)
	{
		for(int x = 0; x < span; ++x)
		{
			int idx = GiCellIndex(slot, base + ivec2(x, y));
			atomicAdd(s_acc_r[idx], fixed_point.x);
			atomicAdd(s_acc_g[idx], fixed_point.y);
			atomicAdd(s_acc_b[idx], fixed_point.z);
			atomicMax(s_acc_t[idx], hit_bits);
		}
	}
}

/// Resolves one texel from its cell's accumulators and stores it.
void GiFinalizeTexel(int slot, ivec2 atlas_base, ivec2 local)
{
	ivec2 texel = atlas_base + local;
	int idx = GiCellIndex(slot, local);
	uint acc_r = s_acc_r[idx];
	uint acc_g = s_acc_g[idx];
	uint acc_b = s_acc_b[idx];
	// The culled texel's contract: exact zero, negative hitT (its converged value by
	// definition); a cell no ray served this frame stores the same.
	if(!GiTexelVisible(slot, local) || (s_cell_rays[idx] == 0u && (acc_r | acc_g | acc_b) == 0u))
	{
		imageStore(s_probe_radiance_out, texel, vec4(0.0, 0.0, 0.0, -1.0));
		return;
	}
	GiRayUnit cell = GiCellOfTexel(slot, local);
	float omega = max(GiCellSolidAngle(cell.base, cell.span), 1e-6);
	vec3 radiance = vec3(float(acc_r), float(acc_g), float(acc_b)) / (GI_NEE_FIXED * omega);
	uint hit_bits = s_acc_t[idx];
	float hit_t = hit_bits == 0u ? -1.0 : uintBitsToFloat(hit_bits);
	GiStoreScreenProbeRay(slot, texel, radiance, hit_t);
}

/// Traces sample @p k of one ray unit - its jittered samples first, then the aimed ones - through the
/// single trace call site, splatting it into the cell it lands in. A k past the unit's samples, and an
/// aimed direction landing in a culled texel, trace nothing.
void GiTraceUnitSample(int slot, ivec2 probe, GiRayUnit unit, int k)
{
	int idx = GiCellIndex(slot, unit.base);
	int jittered = int(s_cell_rays[idx]);
	int nee = s_cell_nee[idx];
	int aimed = nee >= 0 ? GI_EMISSIVE_NEE_SAMPLES : 0;
	if(k >= jittered + aimed)
	{
		return;
	}
	vec3 nee_axis = nee >= 0 ? s_nee_axis[slot * GI_NEE_K + nee] : vec3(0.0, 1.0, 0.0);
	float nee_cos = nee >= 0 ? s_nee_cos[slot * GI_NEE_K + nee] : 1.0;
	// The multi-sample pattern is the first four points of a shifted (0,2)-net: positions
	// 0/1 are the exact antithetic pair, so counts one and two reproduce the classic
	// estimator. Addressed by PROBE and cell (GiProbeCellNoise): well spread across adjacent
	// probes for the same cell, decorrelated across the cells of one tile. The probe's lattice
	// key follows the surfaces through a camera turn, so a surface keeps its pattern.
	int cell_index = unit.base.y * GI_PROBE_DIR_EDGE + unit.base.x;
	vec2 first_position = fract(s_frame_r2 + GiProbeCellNoise(GiProbeLatticeKey(probe), cell_index));
	int position = min(k, GI_IMPORTANCE_SUPERSAMPLE_MAX - 1);
	vec2 net_offset = position == 1 ? vec2(0.5, 0.5) : (position == 2 ? vec2(0.25, 0.75) : vec2(0.75, 0.25));
	vec2 xi = position == 0 ? first_position : fract(first_position + net_offset);
	bool is_aimed = k >= jittered;
	vec3 direction =
	    is_aimed ? GiSampleCone(nee_axis, nee_cos, xi)
	             : GiOctDecode((vec2(unit.base) + xi * float(unit.span)) / float(GI_PROBE_DIR_EDGE));
	// An aimed direction landing in a culled texel would be stored nowhere (the cull the
	// jittered rays get per cell).
	if(is_aimed && !GiTexelVisible(slot, GiTexelOfDirection(direction)))
	{
		return;
	}
	vec4 traced = GiTraceScreenProbeDirection(slot, direction);
	ivec2 land_base = unit.base;
	int land_span = unit.span;
	if(is_aimed)
	{
		GiRayUnit land = GiCellOfTexel(slot, GiTexelOfDirection(direction));
		land_base = land.base;
		land_span = land.span;
	}
	GiSplatSample(slot, land_base, land_span, direction, traced.xyz, traced.w, is_aimed ? nee : -1);
}

/// Publishes ray unit @p r of a slot to the pool: its sample count (GiPoolScan turns it into the exclusive
/// prefix) and its cell.
void GiPoolPublishUnit(int slot, int r, GiRayUnit unit, int samples)
{
	s_pool_entry[slot * GI_TRACE_POOL_STRIDE + r] = uint(samples) | (uint(unit.base.x) << 16u) |
	                                                (uint(unit.base.y) << 19u) |
	                                                (unit.span == 2 ? (1u << 22u) : 0u);
}

/// The slot leader's scan: every unit's count becomes its exclusive prefix, entry @p units the slot's total.
void GiPoolScan(int slot, int units)
{
	uint running = 0u;
	LOOP
	for(int r = 0; r < units; ++r)
	{
		uint entry = s_pool_entry[slot * GI_TRACE_POOL_STRIDE + r];
		s_pool_entry[slot * GI_TRACE_POOL_STRIDE + r] = (entry & 0xFFFF0000u) | running;
		running += entry & 0xFFFFu;
	}
	s_pool_entry[slot * GI_TRACE_POOL_STRIDE + units] = running;
}

/// Ray unit @p r of a slot, from its pool entry.
GiRayUnit GiPoolUnit(int slot, int r)
{
	uint entry = s_pool_entry[slot * GI_TRACE_POOL_STRIDE + r];
	GiRayUnit unit;
	unit.base = ivec2(int((entry >> 16u) & 7u), int((entry >> 19u) & 7u));
	unit.span = (entry & (1u << 22u)) != 0u ? 2 : 1;
	unit.samples = 0;
	unit.traced = true;
	return unit;
}

/// The sample prefix of pool entry @p r of a slot (the slot's total at r = its unit count).
uint GiPoolPrefix(int slot, int r)
{
	return s_pool_entry[slot * GI_TRACE_POOL_STRIDE + r] & 0xFFFFu;
}

/// The probe a slot traces, from its packed pool coordinate.
ivec2 GiPoolProbe(int slot)
{
	uint packed_probe = s_pool_probe[slot];
	return ivec2(int(packed_probe & 0xFFFFu), int(packed_probe >> 16u));
}

/// PHASE 2 for lane @p lane of the group's 64: traces pool samples lane, lane + 64, ... - each found as its
/// slot (the slot totals in order) and its unit (a binary search over the slot's prefix) - through the one
/// trace call site.
void GiTraceSamplePool(int lane)
{
	uint pool_index = uint(lane);
	LOOP
	for(int guard = 0; guard < GI_TRACE_POOL_GUARD; ++guard)
	{
		uint local_index = pool_index;
		int work_slot = 0;
		for(int walk_slot = 0; walk_slot < GI_TRACE_SLOT_COUNT - 1; ++walk_slot)
		{
			uint walk_total = GiPoolPrefix(walk_slot, int(s_pool_units[walk_slot]));
			if(work_slot == walk_slot && local_index >= walk_total)
			{
				local_index -= walk_total;
				work_slot = walk_slot + 1;
			}
		}
		int work_units = int(s_pool_units[work_slot]);
		if(local_index >= GiPoolPrefix(work_slot, work_units))
		{
			break;
		}
		int lo = 0;
		int hi = work_units;
		LOOP
		for(int search = 0; search < GI_TRACE_POOL_SEARCH_STEPS && hi - lo > 1; ++search)
		{
			int mid = (lo + hi) / 2;
			if(GiPoolPrefix(work_slot, mid) <= local_index)
			{
				lo = mid;
			}
			else
			{
				hi = mid;
			}
		}
		GiTraceUnitSample(work_slot, GiPoolProbe(work_slot), GiPoolUnit(work_slot, lo),
		                  int(local_index - GiPoolPrefix(work_slot, lo)));
		pool_index += uint(GI_TRACE_GROUP_LANES);
	}
}

/// Phase 1 for one ray unit: the cell's jittered count (one supersample gives way to the
/// aimed ray, never the last) and its aimed emitter, published for the MIS sums. Returns the
/// unit's sample count (jittered + aimed).
int GiAllocateRayUnit(int slot, GiRayUnit unit)
{
	int nee = unit.traced ? GiSelectNeeForCell(slot, unit.base, unit.span) : -1;
	int aimed = nee >= 0 ? GI_EMISSIVE_NEE_SAMPLES : 0;
	int jittered = !unit.traced ? 0 : (nee >= 0 ? max(unit.samples - aimed, 1) : unit.samples);
	GiPublishCell(slot, unit.base, unit.span, jittered, nee);
	if(nee >= 0)
	{
		atomicAdd(s_nee_rays[slot * GI_NEE_K + nee], uint(aimed));
	}
	return jittered + aimed;
}

#if defined(GI_SCREEN_PROBE_TRACE_ADAPTIVE)
/// Ray @p r of the adaptive schedule: its block (walked by the per-block ray counts), its
/// cell (a detail texel or the whole coarse quad), sample count and cull.
GiRayUnit GiAdaptiveRayUnit(int slot, int r)
{
	int scan = r;
	int block = 0;
	LOOP
	for(int b = 0; b < 16; ++b)
	{
		int block_rays = GiScreenProbeBlockRays(slot, b);
		if(scan < block_rays)
		{
			block = b;
			break;
		}
		scan -= block_rays;
	}
	float ratio = GiScreenProbeBlockRatio(slot, block);
	// The schedule's own verdict: a dim block across the cull line is detail too.
	bool detail = GiScreenProbeBlockRays(slot, block) == 4;
	ivec2 quad = ivec2((block % 4) * 2, (block / 4) * 2);
	GiRayUnit unit;
	// DETAIL: ray `scan` in [0,4) owns one texel of the 2x2 quad, on the full program's
	// supersampling ladder - 2 samples past the ratio squared, GI_IMPORTANCE_SUPERSAMPLE_MAX
	// past its cube: the bright texels are where this program's extra noise lives, so the
	// extra samples go there rather than to the dim blocks. COARSE: one cell spanning the
	// whole quad footprint, sampled GI_ADAPTIVE_COARSE_SAMPLES times with its mean stored to
	// all four texels.
	unit.base = detail ? quad + ivec2(scan & 1, scan >> 1) : quad;
	unit.span = detail ? 1 : 2;
	float ratio_squared = GI_IMPORTANCE_SUPERSAMPLE_RATIO * GI_IMPORTANCE_SUPERSAMPLE_RATIO;
	unit.samples = detail ? ((ratio > ratio_squared * GI_IMPORTANCE_SUPERSAMPLE_RATIO)
	                             ? GI_IMPORTANCE_SUPERSAMPLE_MAX
	                             : (ratio > ratio_squared ? 2 : 1))
	                      : GI_ADAPTIVE_COARSE_SAMPLES;
	// The full program's cull, per texel: a coarse quad never straddles the line, so its first
	// texel speaks for all four.
	unit.traced = GiTexelVisible(slot, unit.base);
	return unit;
}
#else
/*
 * The full program's ray unit: one texel, sampled by STRUCTURED IMPORTANCE [Lumen screen-probe importance
 * sampling]. The BRDF culls the texels whose centre cosine to the anchor normal is under
 * GI_IMPORTANCE_MIN_COSINE, and a fixed budget of GI_IMPORTANCE_SAMPLE_BUDGET jittered samples is shared by the
 * rest in proportion to cosine x the block's reprojected importance (GiScreenProbeBlockRatio), 1 to
 * GI_IMPORTANCE_SUPERSAMPLE_MAX per texel. Every texel's estimate stays unbiased at any count - the balance
 * heuristic divides by the texel's own count - so the cull is the one bias, a darkening accepted for less
 * per-frame change at the same trace cost.
 */
GiRayUnit GiFullRayUnit(int slot, ivec2 local)
{
	GiRayUnit unit;
	unit.base = local;
	unit.span = 1;
	vec2 tile_uv = (vec2(local.xy) + vec2_splat(0.5)) / float(GI_PROBE_DIR_EDGE);
	float cosine = dot(GiOctDecode(tile_uv), s_anchor_normal[slot]);
	unit.traced = GiTexelVisible(slot, local);
	float pdf = max(cosine, 0.0) * GiScreenProbeBlockRatio(slot, (local.y / 2) * 4 + (local.x / 2));
	float share = GI_IMPORTANCE_SAMPLE_BUDGET * pdf / max(s_importance_pdf_sum[slot], 1e-6);
	unit.samples = int(clamp(floor(share + 0.5), 1.0, float(GI_IMPORTANCE_SUPERSAMPLE_MAX)));
	return unit;
}
#endif

#if defined(GI_SCREEN_PROBE_TRACE_ADAPTIVE)
NUM_THREADS(GI_TRACE_ADAPTIVE_LANES, GI_TRACE_SLOT_COUNT, 1)
#else
NUM_THREADS(8, 8, 1)
#endif
void main()
{
	// COMPACTED dispatch (indirect args from the args pass): the full program launches one
	// group per traced probe, the adaptive program ceil(count / 4) groups of four probes -
	// interpolated, dead and sky probes never occupy a lane here. Their tiles are the
	// interp pass's job (parent blend or black clear). The list lives in the probe
	// buffer's list region, one bit-cast coordinate per vec4 (GiProbeTracedListBase); the
	// head's y lane carries the traced COUNT (staged by the args pass) so a partial final
	// adaptive group bounds-checks its tail slots.
#if defined(GI_SCREEN_PROBE_TRACE_ADAPTIVE)
	int slot = int(gl_LocalInvocationID.y);
	uint list_index = gl_WorkGroupID.x * uint(GI_TRACE_SLOT_COUNT) + uint(slot);
	bool leader = int(gl_LocalInvocationID.x) == 0;
	float traced_count = b_gi_probes[GiProbeTracedListBase()].y;
	bool probe_active = float(list_index) < traced_count;
#else
	int slot = 0;
	uint list_index = gl_WorkGroupID.x;
	bool leader = gl_LocalInvocationID.x == 0u && gl_LocalInvocationID.y == 0u;
	// One group per traced probe by construction - every list read is in bounds.
	bool probe_active = true;
#endif
	// Coordinates start at the list base; the head entry's .x IS the first coordinate and
	// its .y lane carries the staged count (the args pass preserves .x when it stages).
	uint packed_probe =
	    probe_active ? floatBitsToUint(b_gi_probes[GiProbeTracedListBase() + list_index].x) : 0u;
	ivec2 probe = ivec2(int(packed_probe & 0xFFFFu), int(packed_probe >> 16u));
	uint record = (GiProbeRecord(probe.x, probe.y, 0) + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
	if(leader)
	{
		if(slot == 0)
		{
			// Dispatch-uniform hoists: the mip-0 resolution the screen tier reads per
			// sample, and the frame's R2 offset the jitter derives per ray.
			s_screen_size = HizGetDepthMipResolution(s_hiz, 0);
			s_frame_r2 = u_gi_jitter.zw;
		}
		if(probe_active)
		{
			s_history_record[slot] = -1;
			s_importance_mean[slot] = 0.0;
			s_screen_rays[slot] = 0u;
			s_traced_rays[slot] = 0u;
			s_moving_rays[slot] = 0u;
			s_mesh_rays[slot] = 0u;
			s_clipmap_rays[slot] = 0u;
			s_sky_rays[slot] = 0u;
			s_aimed_contribution[slot] = 0u;
			s_total_contribution[slot] = 0u;
			s_aimed_rays[slot] = 0u;
			s_selected_emitters[slot] = 0u;
			// Placement computed the anchor, classification put this probe on the traced
			// list - the records are valid by construction; this thread only unpacks them
			// and reprojects the anchor for the importance state.
			vec4 meta = b_gi_probes[record + uint(GI_PROBE_META)];
			vec4 meta2 = b_gi_probes[record + uint(GI_PROBE_META2)];
			vec3 world_position = meta.xyz;
			vec3 world_normal = meta2.xyz;
			vec4 origin_range = b_gi_probes[record + uint(GI_PROBE_ORIGIN)];
			vec4 anchor = b_gi_probes[record + uint(GI_PROBE_ANCHOR)];
			s_anchor_normal[slot] = world_normal;
			s_origin[slot] = origin_range.xyz;
			s_short_range[slot] = origin_range.w;
			s_anchor_uv[slot] = anchor.xy;
			s_ss_origin[slot] = vec3(anchor.xy, anchor.z);
			s_vs_origin[slot] = HizComputeViewspacePosition(anchor.xy, anchor.z);
			// EXPLICIT EMISSIVE SAMPLING: the K brightest emitters this probe can aim at,
			// by luminance x subtended solid angle (gi_emissive_nee.sh). Cones wider than
			// GI_EMISSIVE_NEE_MIN_CONE_COS and emitters under the anchor's tangent cap are
			// left to the jittered rays.
			{
				float nee_score[GI_NEE_K];
				vec3 nee_axis[GI_NEE_K];
				float nee_cos[GI_NEE_K];
				for(int k0 = 0; k0 < GI_NEE_K; ++k0)
				{
					nee_score[k0] = 0.0;
					nee_axis[k0] = vec3(0.0, 1.0, 0.0);
					nee_cos[k0] = 2.0;
				}
				int emitter_count = min(u_sdf_emitter_count, GI_EMISSIVE_NEE_MAX_EMITTERS);
				LOOP
				for(int ei = 0; ei < emitter_count; ++ei)
				{
					GiEmitter e = GiLoadEmitter(ei);
					GiEmitterCone cone = GiEmitterConeFrom(e, origin_range.xyz);
					if(!cone.valid || cone.cos_max < GI_EMISSIVE_NEE_MIN_CONE_COS)
					{
						continue;
					}
					float sin_max = sqrt(max(1.0 - cone.cos_max * cone.cos_max, 0.0));
					if(dot(cone.axis, world_normal) < -0.2 - sin_max)
					{
						continue;
					}
					float score = GiEmitterLuminance(e) * GiConeSolidAngle(cone.cos_max);
					LOOP
					for(int k = 0; k < GI_NEE_K; ++k)
					{
						if(score > nee_score[k])
						{
							for(int j = GI_NEE_K - 1; j > k; --j)
							{
								nee_score[j] = nee_score[j - 1];
								nee_axis[j] = nee_axis[j - 1];
								nee_cos[j] = nee_cos[j - 1];
							}
							nee_score[k] = score;
							nee_axis[k] = cone.axis;
							nee_cos[k] = cone.cos_max;
							break;
						}
					}
				}
				for(int k2 = 0; k2 < GI_NEE_K; ++k2)
				{
					s_nee_axis[slot * GI_NEE_K + k2] = nee_axis[k2];
					s_nee_cos[slot * GI_NEE_K + k2] = nee_cos[k2];
					s_nee_rays[slot * GI_NEE_K + k2] = 0u;
					if(u_gi_probe_debug_census && nee_cos[k2] <= 1.0)
					{
						s_selected_emitters[slot] += 1u;
					}
				}
			}
			// Reproject the anchor into LAST frame's lattice for the importance state. The
			// lookup CLAMPS to the border instead of requiring an on-screen reprojection:
			// content revealed by panning or rotation often shares a surface with the
			// nearest screen-edge probe of the previous frame, and the plane test below is
			// the arbiter. A failed or plane-rejected reprojection just means uniform
			// allocation this frame - importance is an optimisation, never a correctness
			// dependency. Gated on trusted records so freshly allocated garbage is never
			// read as history.
			BRANCH
			if(u_gi_probe_trusted)
			{
				vec4 prev_clip4 = mul(u_gi_prev_view_proj, vec4(world_position, 1.0));
				if(prev_clip4.w > 0.0)
				{
					vec3 prev_clip = clipTransform(prev_clip4.xyz / prev_clip4.w);
					vec2 prev_uv = clamp(prev_clip.xy * 0.5 + 0.5, vec2_splat(0.0), vec2_splat(1.0));
					vec2 prev_probe = floor(GiProbeLatticeOfPrevPixel(prev_uv * u_gi_probe_screen.xy));
					int hx = int(clamp(prev_probe.x, 0.0, float(u_gi_probe_count_x - 1)));
					int hy = int(clamp(prev_probe.y, 0.0, float(u_gi_probe_count_y - 1)));
					uint history_base =
					    (GiProbeRecord(hx, hy, 0) + u_gi_probe_read_offset) * uint(GI_PROBE_STRIDE);
					vec4 history_meta = b_gi_probes[history_base + uint(GI_PROBE_META)];
					float plane = abs(dot(history_meta.xyz - world_position, world_normal));
					if(history_meta.w > 0.5 &&
					   plane < 0.05 * max(length(world_position - u_gi_camera.xyz), 0.1))
					{
						s_history_record[slot] = int(history_base);
						float total = 0.0;
						for(int m = 0; m < 4; ++m)
						{
							vec4 mip = b_gi_probes[history_base + uint(m)] * u_history_pre_exposure_correction;
							s_importance_mip[slot * 4 + m] = mip;
							total += mip.x + mip.y + mip.z + mip.w;
						}
						s_importance_mean[slot] = total / 16.0;
					}
				}
			}
			// THE LIGHTING PRIOR (cs_gi_screen_probe_prior.sc): a probe without history at its
			// surface allocates by the world-probe cache's luminance per block - the radiance
			// cache as the lighting PDF of a probe without history, as Lumen's - instead of one
			// coarse ray per block, which leaves a revealed region's bright openings to a coin
			// flip per probe.
			if(s_history_record[slot] < 0 || s_importance_mean[slot] <= 1e-4)
			{
				float prior_age = b_gi_probes[record + uint(GI_PROBE_IMPORTANCE_FRAMES)].z;
				if(prior_age >= 1.0 && prior_age < float(GI_IMPORTANCE_STATE_FRAMES))
				{
					float prior_total = 0.0;
					for(int prior_m = 0; prior_m < 4; ++prior_m)
					{
						vec4 prior_mip = b_gi_probes[record + uint(GI_PROBE_FLOOR + prior_m)];
						s_importance_mip[slot * 4 + prior_m] = prior_mip;
						prior_total += prior_mip.x + prior_mip.y + prior_mip.z + prior_mip.w;
					}
					s_importance_mean[slot] = prior_total / 16.0;
					s_history_record[slot] = int(record);
				}
			}
#if !defined(GI_SCREEN_PROBE_TRACE_ADAPTIVE)
			// The importance PDF total GiFullRayUnit divides by, over the texels the BRDF cull keeps.
			float pdf_sum = 0.0;
			for(int pdf_texel = 0; pdf_texel < GI_PROBE_DIR_COUNT; ++pdf_texel)
			{
				ivec2 pdf_local = ivec2(pdf_texel % GI_PROBE_DIR_EDGE, pdf_texel / GI_PROBE_DIR_EDGE);
				vec2 pdf_uv = (vec2(pdf_local) + vec2_splat(0.5)) / float(GI_PROBE_DIR_EDGE);
				float pdf_cosine = dot(GiOctDecode(pdf_uv), world_normal);
				if(pdf_cosine >= GI_IMPORTANCE_MIN_COSINE)
				{
					pdf_sum += max(pdf_cosine, 0.0) *
					           GiScreenProbeBlockRatio(slot, (pdf_local.y / 2) * 4 + (pdf_local.x / 2));
				}
			}
			s_importance_pdf_sum[slot] = pdf_sum;
#endif
		}
	}
	// Every lane clears the accumulators of the texels it will finalize (phase 3 below),
	// before the barrier that publishes the leader's staging.
#if defined(GI_SCREEN_PROBE_TRACE_ADAPTIVE)
	int thread = int(gl_LocalInvocationID.x);
	for(int clear_i = 0; clear_i < GI_PROBE_DIR_COUNT / GI_TRACE_ADAPTIVE_LANES; ++clear_i)
	{
		int clear_t = thread + clear_i * GI_TRACE_ADAPTIVE_LANES;
		int clear_idx = slot * GI_PROBE_DIR_COUNT + clear_t;
		s_acc_r[clear_idx] = 0u;
		s_acc_g[clear_idx] = 0u;
		s_acc_b[clear_idx] = 0u;
		s_acc_t[clear_idx] = 0u;
		s_cell_rays[clear_idx] = 0u;
		s_cell_nee[clear_idx] = -1;
	}
#else
	{
		int clear_idx = int(gl_LocalInvocationID.y) * GI_PROBE_DIR_EDGE + int(gl_LocalInvocationID.x);
		s_acc_r[clear_idx] = 0u;
		s_acc_g[clear_idx] = 0u;
		s_acc_b[clear_idx] = 0u;
		s_acc_t[clear_idx] = 0u;
		s_cell_rays[clear_idx] = 0u;
		s_cell_nee[clear_idx] = -1;
	}
#endif
	barrier();
	// No early return for an inactive slot (a partial final adaptive group): the phase
	// barriers below must stay in uniform flow control, so the work is guarded instead.
	ivec2 atlas_base = GiProbeAtlasBase(probe.x, probe.y, 0);
	// THREE PHASES, three barriers: (1) every ray unit publishes its cell's jittered count
	// and aimed emitter, so the balance heuristic knows every technique's sample count, and
	// its sample count to the pool, which each slot's leader turns into prefixes; (2) the
	// group traces the pool (GiTraceSamplePool) through the ONE trace call site (fxc fully
	// inlines every call site of the trace body - Hi-Z + SDF march + completion, thousands
	// of instructions - and this program's s_5_0 compile time grows superlinearly with each
	// instantiation) and splats; (3) every texel resolves its cell's accumulators.
	// Every texel is written every frame - by its cell's samples, or by the cull's zero store.
#if defined(GI_SCREEN_PROBE_TRACE_ADAPTIVE)
	// ADAPTIVE SCHEDULE (see the header): 16 + 3K ray units for K detail blocks, allocated
	// round-robin across the slot's 16 lanes. The block walk is pure shared-memory
	// arithmetic per lane; with sixteen blocks a scan beats any prefix machinery.
	int total_rays = 0;
	if(probe_active)
	{
		LOOP
		for(int b = 0; b < 16; ++b)
		{
			total_rays += GiScreenProbeBlockRays(slot, b);
		}
		LOOP
		for(int r = thread; r < total_rays; r += GI_TRACE_ADAPTIVE_LANES)
		{
			GiRayUnit unit = GiAdaptiveRayUnit(slot, r);
			GiPoolPublishUnit(slot, r, unit, GiAllocateRayUnit(slot, unit));
		}
	}
	int pool_units = total_rays;
#else
	ivec2 local = ivec2(gl_LocalInvocationID.xy);
	int thread = local.y * GI_PROBE_DIR_EDGE + local.x;
	{
		GiRayUnit unit = GiFullRayUnit(slot, local);
		GiPoolPublishUnit(slot, thread, unit, GiAllocateRayUnit(slot, unit));
	}
	int pool_units = GI_PROBE_DIR_COUNT;
#endif
	if(leader)
	{
		s_pool_units[slot] = uint(pool_units);
		s_pool_probe[slot] = packed_probe;
	}
	barrier();
	if(leader)
	{
		GiPoolScan(slot, pool_units);
	}
	barrier();
	GiTraceSamplePool(slot * GI_TRACE_SLOT_LANES + thread);
	barrier();
#if defined(GI_SCREEN_PROBE_TRACE_ADAPTIVE)
	if(probe_active)
	{
		if(leader)
		{
			GiStoreScreenShare(slot, record);
		}
		for(int final_i = 0; final_i < GI_PROBE_DIR_COUNT / GI_TRACE_ADAPTIVE_LANES; ++final_i)
		{
			int final_t = thread + final_i * GI_TRACE_ADAPTIVE_LANES;
			GiFinalizeTexel(slot, atlas_base, ivec2(final_t % GI_PROBE_DIR_EDGE, final_t / GI_PROBE_DIR_EDGE));
		}
	}
#else
	if(leader)
	{
		GiStoreScreenShare(slot, record);
	}
	GiFinalizeTexel(slot, atlas_base, local);
#endif
}

#endif // __GI_SCREEN_PROBE_TRACE_KERNEL_SH__
