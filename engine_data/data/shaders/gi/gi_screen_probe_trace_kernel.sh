#ifndef __GI_SCREEN_PROBE_TRACE_KERNEL_SH__
#define __GI_SCREEN_PROBE_TRACE_KERNEL_SH__

/*
 * GI screen probe trace (plan 3.4) - the Lumen recipe, one thread group per probe SLOT.
 *
 * SHARED KERNEL BODY: compiled twice. cs_gi_screen_probe_trace.sc is the compacted
 * program (GI_SCREEN_PROBE_TRACE_COMPACT) used while probe-space temporal is on:
 * 16 threads per probe, one per this frame's Bayer stratum - and FOUR probes packed
 * into each 64-lane group, because a 16-thread group still allocates a whole wave
 * (half of a wave32, three quarters of a wave64 idle). The indirect args pass emits
 * ceil(count / 4) groups and pads the list with 0xFFFFFFFF sentinels so every slot
 * read is defined. cs_gi_screen_probe_trace_full.sc is NUM_THREADS(8,8,1), one probe
 * per group - the A/B-off / first-frame path that traces all 64 octahedral texels in
 * parallel; its lanes are already fully busy, so it does not pack.
 *
 * Probes ARE pixels: the anchor is a Halton-jittered G-buffer pixel of the probe's tile, its
 * depth and normal taken as-is - no median selection, no hysteresis, no layers. Stability is
 * the downstream contract: world-anchored direction indexing, plane-weighted integration, and
 * the full-res temporal filter, which the placement jitter deliberately feeds with a slightly
 * different probe set each frame [S21 s37-39].
 *
 * Rays are SHORTENED [S21 s69]: each establishes its own visibility out to twice the local
 * world-probe spacing, reads the light voxels at a hit, and COMPLETES from the world probes'
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
 * viewport: hits past either bound were unconditionally rejected by the tests below, so the
 * old unbounded march only ever spent budget on answers it then threw away.
 *
 * PROBE-SPACE TEMPORAL (windowed): each frame traces a 16-ray 2x2 Bayer stratum
 * and blends those texels (1/n) into this probe's own previous tile. The other 48
 * stay as that tile. Placement stays sticky while the origin is still in-tile so
 * the sphere is one visibility field; a Halton walk resets the blend count.
 * A miss keeps the previous tile (never black). Window 1 traces every texel.
 *
 * SINGLE-BUFFERED ATLAS: the atlas holds each probe's tile at a lattice-fixed position, so
 * the old A/B ping-pong's "history pass" was a texel-for-texel identity copy - 33 MB of
 * traffic at 4K to move every tile onto itself. The trace now blends in place; the history
 * pass's surviving duties live in the slot leader below (the keep/count bookkeeping) and in
 * the not-in-stratum clear (a fresh tile's untraced texels must not show a stale atlas).
 *
 * Everything here is owned by gi_constants - the pass has no tuning surface beyond
 * gi_resolve_pass::settings::probe_space_temporal.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
// DecodeGBufferNormalMetalRoughnessLod and eval_radiance_sh live here.
#include "../lighting.sh"
#include "../hiz_trace.sh"

#include "gi/sdf_common.sh"
#define GI_LIGHT_VOXEL_READ
#include "gi/gi_light_voxels.sh"
#define GI_WORLD_PROBE_READ
#define GI_WORLD_PROBE_READ_RADIANCE
// Completion reads radiance + depth, never the irradiance cage - skipping it frees stage 11
// for the compacted probe list below.
#define GI_WORLD_PROBE_SKIP_IRRADIANCE
#include "gi/gi_world_probes.sh"
#include "gi/gi_noise.sh"

/// LAST frame's composited output (the SSR convention, same source): the far-field radiance
/// for hits BEYOND the cascades, where the light voxels have nothing. Bound in place of the
/// world-probe irradiance cage the trace never read; the compacted probe list lives in the
/// probe buffer's list region (GiProbeTracedListBase), not in a stage of its own.
SAMPLER2D(s_gi_prev_color, 11);

/// rgb = radiance, a = hitT (negative = completed/sky). One 8x8 tile per probe.
/// RW: single-buffered - traced texels blend onto this probe's own previous tile in place.
IMAGE2D_RW(s_probe_radiance_out, rgba16f, 5);
/// Probe records: reuses the existing layout (gi_probe_common.sh) so downstream plumbing holds.
BUFFER_RW(b_gi_probes, vec4, 7);

/// The Hi-Z depth pyramid when the screen trace is enabled, else the raw G-buffer depth.
/// Mip 0 is the device depth either way (cs_hiz_generate copies it verbatim), so the anchor
/// reconstruction below reads the same values from both.
SAMPLER2D(s_hiz, 8);
SAMPLER2D(s_gi_normal, 9);
SAMPLER2D(s_gi_env_sh, 14);

/// xyz = camera position (world-probe window centre), w = frame index.
uniform vec4 u_gi_camera;
/// x > 0 when s_hiz holds a full pyramid and the screen-trace tier runs.
/// y > 0 = RAY TIER debug: rays paint which tier answered instead of radiance
/// (green = screen commit, red = SDF hit, blue = world-probe/sky completion; the interp pass
/// paints interpolated tiles magenta under the same flag).
/// z = the adaptive flag - consumed by the CLASSIFY pass, bound here only for layout parity.
/// w > 0 when s_gi_prev_color holds last frame's composited output.
uniform vec4 u_gi_screen_trace;
/// Previous view projection: the anchor reprojects into LAST frame's lattice to read the
/// importance mip the filter stored in that probe's record slots.
uniform mat4 u_gi_prev_view_proj;

/// Probe slots per group: the compact program packs four 16-thread probes into one 64-lane
/// group; the full program's 8x8 group is one fully-busy probe already.
#if defined(GI_SCREEN_PROBE_TRACE_COMPACT)
#	define GI_TRACE_SLOT_COUNT 4
#else
#	define GI_TRACE_SLOT_COUNT 1
#endif

/// The compacted list's padding sentinel (see cs_gi_screen_probe_args.sc).
#define GI_TRACE_LIST_SENTINEL 0xFFFFFFFFu

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
/// The reprojected probe's 4x4 importance mip, staged by the leader: the per-ray lookup used
/// to re-read the same four record vec4s the leader had already loaded for the mean.
SHARED vec4 s_importance_mip[GI_TRACE_SLOT_COUNT * 4];
SHARED float s_blend_alpha[GI_TRACE_SLOT_COUNT];
/// Fresh history under the single-buffered atlas: untraced texels must be cleared rather
/// than left showing whatever tile the atlas held before.
SHARED bool s_tile_clear[GI_TRACE_SLOT_COUNT];
/// Atlas base of the tile this probe INHERITS from ((-1,-1) = none): when a camera slide
/// re-anchors the probe (same-origin fails on a frame that scheduled no walk), the
/// world-reprojected previous-lattice probe - already located, plane-tested, for the
/// importance mip - holds the CURRENT accumulated estimate of the world point this probe
/// now covers. Blending into and copying from THAT tile is what lets probe-space
/// accumulation survive camera rotation and travel; without it every slide reset the count
/// AND left the tile serving strata traced from wherever the probe pointed windows ago,
/// which the convolve integrated into a churning mean no pixel temporal can settle
/// (measured as blotch noise the moment the camera moves, worst in emissive-lit darkness).
SHARED ivec2 s_inherit_base[GI_TRACE_SLOT_COUNT];
/// Dispatch-uniform values hoisted out of the per-ray loop.
SHARED vec2 s_screen_size;
SHARED vec2 s_frame_r2;

/*
 * Radiance for a hit whose light-voxel read failed, BEYOND the outermost cascade - where "no
 * data" must not mean "no light" (the black wall at the end of the street): the Lumen
 * far-field recipe applies - reproject the hit into LAST frame's composited output, which
 * already carries that geometry's shadow-mapped lighting (the SSR scene-colour convention;
 * the temporal mean and the radiance clamp bound the feedback); off-screen or history-less,
 * the sky SH along the ray, the miss contract. WITHIN the cascades the callers answer honest
 * darkness directly - sub-voxel contact occlusion, sealed rooms - using the level search they
 * already ran, so this function no longer re-runs it.
 */
vec3 GiFarFieldRadiance(vec3 hit_position, vec3 sample_dir)
{
	BRANCH
	if(u_gi_screen_trace.w > 0.0)
	{
		vec4 prev_clip = mul(u_gi_prev_view_proj, vec4(hit_position, 1.0));
		if(prev_clip.w > 0.0)
		{
			vec3 ndc = clipTransform(prev_clip.xyz / prev_clip.w);
			vec2 prev_uv = ndc.xy * 0.5 + 0.5;
			if(all(greaterThanEqual(prev_uv, vec2_splat(0.0))) &&
			   all(lessThanEqual(prev_uv, vec2_splat(1.0))))
			{
				return texture2DLod(s_gi_prev_color, prev_uv, 0.0).xyz;
			}
		}
	}
	return eval_radiance_sh(s_gi_env_sh, sample_dir);
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

void GiStoreScreenProbeRay(int slot, ivec2 texel, ivec2 local, vec3 radiance, float hit_t)
{
	vec3 averaged = min(radiance, vec3_splat(GI_MAX_RAY_RADIANCE));
	// The blend history and the FIREFLY GOVERNOR's reference: the INHERITED tile's texel
	// when a slide re-anchor adopted the world-reprojected previous probe (s_inherit_base),
	// the texel's own previous value otherwise. Loaded unconditionally - the governor
	// applies in every accumulation mode (a probe with temporal OFF overwrites its tile
	// each frame and pops just as hard). The inherited read may race the source probe's
	// own trace this frame: either side of that race is a valid radiance estimate of the
	// same texel (old blend or new blend), and RGBA16F texel stores do not tear, so the
	// worst case is one window of blend uncertainty - never corruption.
	ivec2 inherit_base = s_inherit_base[slot];
	vec4 hist;
	BRANCH
	if(inherit_base.x >= 0)
	{
		hist = imageLoad(s_probe_radiance_out, inherit_base + local);
	}
	else
	{
		hist = imageLoad(s_probe_radiance_out, texel);
	}
	float hist_luma = Luminance(hist.xyz);
	// FIREFLY GOVERNOR: a ray landing on a small bright emitter dominates the whole tile,
	// and a reset accumulation (Halton walk, temporal off) ingests it at full weight - the
	// probe's screen footprint pops for a frame and fades. Each new sample is capped at
	// GI_GATHER_FIREFLY_CLAMP x its reference. The reference is the texel's own blended
	// history, FLOORED by the reprojected previous tile's mean luminance
	// (s_importance_mean - looked up by WORLD position with a plane test, so it survives
	// the camera-slide re-anchor that leaves the tile holding a NEARBY point's radiance).
	// Without the floor, a dark stale texel crushed legitimate arrivals to 8x darkness,
	// then let them ramp, then crushed again on the next slide - measured as pumping
	// noise in emissive-lit dark scenes the moment the camera moved. A texel whose own
	// history legitimately sees the emitter still raises its own ceiling and converges
	// unbiased (per-texel, never ONLY the tile mean - that would crush a lone bright
	// texel to mean x k / 256). No meaningful reference at all (fresh tile, failed
	// reprojection): the first measurement stores unclamped - progressive ramps from
	// black would dim every disocclusion instead.
	float reference = max(hist_luma, s_importance_mean[slot]);
	BRANCH
	if(reference > 1e-3)
	{
		float ceiling = GI_GATHER_FIREFLY_CLAMP * reference;
		float luma = Luminance(averaged);
		if(luma > ceiling)
		{
			averaged *= ceiling / luma;
		}
	}
	BRANCH
	if(u_gi_probe_blend && s_blend_alpha[slot] < 0.999)
	{
		averaged = mix(hist.xyz, averaged, s_blend_alpha[slot]);
	}
	imageStore(s_probe_radiance_out, texel, vec4(averaged, hit_t));
}

/// One octahedral texel: Hi-Z then SDF then world-probe completion, written to the atlas.
void GiTraceScreenProbeRay(int slot, ivec2 probe, ivec2 local)
{
	ivec2 texel = GiProbeAtlasBase(probe.x, probe.y, 0) + local;
	// World-anchored direction CONES: each thread owns one texel of the shared octahedral
	// parameterisation; the cone centre is used for the below-tangent cull and the importance
	// lookup, while the traced sample directions jitter WITHIN the cone (see the sample loop).
	vec2 tile_uv = (vec2(local.xy) + vec2_splat(0.5)) / float(GI_PROBE_DIR_EDGE);
	vec3 direction = GiOctDecode(tile_uv);
	// The hemisphere cap below the anchor's tangent plane cannot carry irradiance; skipped with
	// a small tolerance so grazing directions still trace (the integration weights by cosine).
	// Stored as EXACT zero, not blended: the cap's converged value is zero by definition, and
	// the old read-modify-write only ever decayed toward it at 1/count per re-trace.
	if(dot(direction, s_anchor_normal[slot]) < -0.2)
	{
		imageStore(s_probe_radiance_out, texel, vec4(0.0, 0.0, 0.0, -1.0));
		return;
	}
	// IMPORTANCE-PROPORTIONAL SAMPLE ALLOCATION: a cone whose reprojected history reads
	// brighter than the probe mean gets extra sub-cone samples on a geometric ladder of the
	// ratio - the refinement of the old binary 2x gate that resolves an emitter smaller than
	// the cone in proportion to how much of the tile's energy it concentrates. The ladder is
	// SELF-BUDGETING with no reduction: ratios normalise by the tile MEAN, and the sixteen
	// block importances sum to sixteen means by definition - so however the energy is
	// distributed, a stratum's extra samples are bounded (about half the base ray count in
	// the all-worst-case), and a uniformly lit tile pays exactly one sample per cone as
	// before. History absent or reprojection failed: uniform allocation, as ever.
	int sample_count = 1;
	if(s_history_record[slot] >= 0 && s_importance_mean[slot] > 1e-4)
	{
		int block = (local.y / 2) * 4 + (local.x / 2);
		vec4 mip = s_importance_mip[slot * 4 + block / 4];
		int lane = block % 4;
		float importance = lane == 0 ? mip.x : (lane == 1 ? mip.y : (lane == 2 ? mip.z : mip.w));
		float ratio = importance / s_importance_mean[slot];
		if(ratio > GI_IMPORTANCE_SUPERSAMPLE_RATIO)
		{
			sample_count = 2;
		}
		if(ratio > GI_IMPORTANCE_SUPERSAMPLE_RATIO * GI_IMPORTANCE_SUPERSAMPLE_RATIO)
		{
			sample_count = 3;
		}
		if(ratio > GI_IMPORTANCE_SUPERSAMPLE_RATIO * GI_IMPORTANCE_SUPERSAMPLE_RATIO *
		               GI_IMPORTANCE_SUPERSAMPLE_RATIO)
		{
			sample_count = GI_IMPORTANCE_SUPERSAMPLE_MAX;
		}
	}
	// Sub-texel DIRECTION jitter. Fixed centre rays ALIAS small bright sources: a source
	// smaller than one cone is either skewered or missed by the grid, and which probes catch
	// it varies smoothly with anchor position - printing stationary whitish blobs across
	// walls that NO downstream filter can remove, because the per-probe estimates are BIASED,
	// not noisy (measured: blobs immune to temporal off, denoise off, spacing, and every
	// writer-side fix). Jittering the sample within its cone per frame turns that bias into
	// per-frame variance the temporal chain integrates - each cone measures its whole solid
	// angle over the accumulation window. R2 low-discrepancy across frames; the pattern is
	// addressed by ATLAS texel so it decorrelates both the texels within a tile and the
	// same direction across neighbouring probes (two independent channels - see
	// gi_noise.sh). The multi-sample pattern is the first four points of a shifted
	// (0,2)-net: positions 0/1 are the exact antithetic pair the binary gate traced, so
	// counts one and two reproduce the previous estimator.
	vec2 sub_positions[GI_IMPORTANCE_SUPERSAMPLE_MAX];
	sub_positions[0] = fract(s_frame_r2 + GiIgnNoise(texel));
	sub_positions[1] = fract(sub_positions[0] + vec2(0.5, 0.5));
	sub_positions[2] = fract(sub_positions[0] + vec2(0.25, 0.75));
	sub_positions[3] = fract(sub_positions[0] + vec2(0.75, 0.25));
	vec3 radiance_sum = vec3_splat(0.0);
	float hit_t = -1.0;
	for(int s = 0; s < sample_count; ++s)
	{
		vec2 sample_uv = (vec2(local.xy) + sub_positions[s]) / float(GI_PROBE_DIR_EDGE);
		vec3 sample_dir = GiOctDecode(sample_uv);
		vec3 radiance = vec3_splat(0.0);
		bool committed = false;
		// 1 = screen commit, 2 = SDF hit, 3 = completion; consumed by the tier debug view.
		int answered_tier = 3;
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
#if BGFX_SHADER_LANGUAGE_GLSL
					end_pj.z = end_pj.z * 0.5 + 0.5;
#endif
					vec3 ss_delta = end_pj - s_ss_origin[slot];
					t_limit = 1.05 * dot(ss_delta, ss_dir) / max(dot(ss_dir, ss_dir), 1e-12);
				}
				vec3 ss_hit = vec3_splat(0.0);
				// Mip-1 floor (GI_SCREEN_TRACE_MIN_MIP): these rays are cone-amortized over a
				// probe tile, so a mip-0 walk buys sub-pixel precision below the cone footprint
				// at twice the traversal cost. Validation still reads mip-0 depth; a lost
				// commit falls through to the SDF, which is the watertight answer anyway.
				bool marched = HizHierarchicalRaymarchEx(s_hiz, s_ss_origin[slot], ss_dir,
				                                         s_screen_size, GI_SCREEN_TRACE_MIN_MIP,
				                                         GI_SCREEN_TRACE_MAX_STEPS, t_limit, true,
				                                         ss_hit);
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
							// the 0.25 m light voxels at that hit re-imprinted the voxel
							// lattice onto every nearby receiver as CONVERGED voxel-scale
							// blotches larger than any downstream kernel's footprint - the
							// denoiser provably cannot reach them (measured: parameter
							// changes did nothing; a 0.5 m voxel at 3 m spans ~100 px
							// against a ~16 px a-trous reach). Last frame's composite
							// carries this surface's radiance at FULL pixel resolution and
							// is ALREADY this kernel's trusted source for the far field
							// (GiFarFieldRadiance) - the same source at nearer range, the
							// same feedback bounds (albedo < 1 closes the loop; the ray
							// clamp and the firefly governor bound spikes). No prev-depth
							// stage is free for reprojection validation, so a disoccluded
							// reprojection can read a wrong surface for a frame - bounded
							// by the same clamps and the temporal, accepted as Lumen does.
							// Off-screen last frame or no history: the voxel read answers
							// exactly as before.
							bool screen_lit = false;
							BRANCH
							if(u_gi_screen_trace.w > 0.0)
							{
								vec4 prev_clip = mul(u_gi_prev_view_proj, vec4(hit_position, 1.0));
								if(prev_clip.w > 0.0)
								{
									vec3 prev_ndc = clipTransform(prev_clip.xyz / prev_clip.w);
									vec2 prev_hit_uv = prev_ndc.xy * 0.5 + 0.5;
									if(all(greaterThanEqual(prev_hit_uv, vec2_splat(0.0))) &&
									   all(lessThanEqual(prev_hit_uv, vec2_splat(1.0))))
									{
										radiance =
										    texture2DLod(s_gi_prev_color, prev_hit_uv, 0.0).xyz;
										screen_lit = true;
									}
								}
							}
							if(!screen_lit && !GiLightVoxelRead(hit_position, hit_normal, radiance))
							{
								// Occluded but unmeasured: honest darkness within the
								// cascades - for sub-voxel detail (railings, awning cloth)
								// this IS the contact occlusion the voxel tier cannot
								// express; past them, the far-field fallback.
								radiance = GiFarFieldFallback(hit_position, sample_dir);
							}
							hit_t = max(hit_t, hit_dist);
							committed = true;
							answered_tier = 1;
						}
					}
				}
			}
		}
		BRANCH
		if(!committed)
		{
			SdfRayHit hit = SdfTraceRayEx(s_origin[slot], sample_dir, s_short_range[slot],
			                              GI_MESH_SDF_TRACE_RANGE, GI_TRACE_MAX_STEPS,
			                              GI_PROBE_TRACE_SURFACE_BIAS, GI_PROBE_TRACE_RELAXATION,
			                              true, 0.0);
			if(hit.hit)
			{
				answered_tier = 2;
				hit_t = max(hit_t, hit.t);
				vec3 hit_position = s_origin[slot] + sample_dir * hit.t;
				vec3 hit_normal = hit.normal;
				if(dot(hit_normal, sample_dir) > 0.0)
				{
					hit_normal = -hit_normal;
				}
				float hit_blend;
				float hit_voxel;
				int hit_level = SdfFindClipmapLevel(hit_position, hit_blend, hit_voxel);
				// One level search serves all three of its consumers now: the buried-hit
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
					else if(!GiLightVoxelRead(hit_position, hit_normal, radiance))
					{
						// Occluded but unmeasured within the cascades: honest darkness (the
						// sealed-room branch) - exactly what the old fallback's covered
						// branch returned, minus its second level search.
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
				if(!GiWorldProbeRadiance(s_origin[slot] + sample_dir * s_short_range[slot],
				                         sample_dir, u_gi_camera.xyz, radiance))
				{
					radiance = eval_radiance_sh(s_gi_env_sh, sample_dir);
				}
			}
		}
		BRANCH
		if(u_gi_screen_trace.y > 0.5)
		{
			radiance = answered_tier == 1 ? vec3(0.0, 1.0, 0.0)
			                              : (answered_tier == 2 ? vec3(1.0, 0.0, 0.0)
			                                                    : vec3(0.0, 0.0, 1.0));
		}
		radiance_sum += radiance;
	}
	GiStoreScreenProbeRay(slot, texel, local, radiance_sum / float(sample_count), hit_t);
}

#if defined(GI_SCREEN_PROBE_TRACE_COMPACT)
NUM_THREADS(GI_SCREEN_PROBE_RAYS_PER_FRAME, GI_TRACE_SLOT_COUNT, 1)
#else
NUM_THREADS(8, 8, 1)
#endif
void main()
{
	// COMPACTED dispatch: ceil(traced count / slots) groups launch (indirect args from the
	// args pass), each slot reading its probe coordinate from the dense list - interpolated,
	// dead and sky probes never occupy a lane here. Their tiles are the interp pass's job
	// (parent blend or black clear). The list lives in the probe buffer's list region, one
	// bit-cast coordinate per vec4 (GiProbeTracedListBase); the args pass pads the tail with
	// sentinels so a partial final group reads defined values.
#if defined(GI_SCREEN_PROBE_TRACE_COMPACT)
	int slot = int(gl_LocalInvocationID.y);
	uint list_index = gl_WorkGroupID.x * uint(GI_TRACE_SLOT_COUNT) + uint(slot);
	bool leader = int(gl_LocalInvocationID.x) == 0;
#else
	int slot = 0;
	uint list_index = gl_WorkGroupID.x;
	bool leader = gl_LocalInvocationID.x == 0u && gl_LocalInvocationID.y == 0u;
#endif
	uint packed_probe = floatBitsToUint(b_gi_probes[GiProbeTracedListBase() + list_index].x);
	// (`active` is a reserved word in GLSL)
	bool probe_active = packed_probe != GI_TRACE_LIST_SENTINEL;
	ivec2 probe = ivec2(int(packed_probe & 0xFFFFu), int(packed_probe >> 16u));
	uint record = (GiProbeRecord(probe.x, probe.y, 0) + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
	if(leader)
	{
		if(slot == 0)
		{
			// Dispatch-uniform hoists: the mip-0 resolution the screen tier reads per sample,
			// and the frame's R2 offset the jitter derives per ray.
			s_screen_size = HizGetDepthMipResolution(s_hiz, 0);
			s_frame_r2 = fract(vec2(0.754877666, 0.569840291) * u_gi_camera.w);
		}
		if(probe_active)
		{
			s_history_record[slot] = -1;
			s_importance_mean[slot] = 0.0;
			s_blend_alpha[slot] = 1.0;
			s_tile_clear[slot] = false;
			s_inherit_base[slot] = ivec2(-1, -1);
			ivec2 history_probe = ivec2(-1, -1);
			// Placement computed the anchor, classification put this probe on the traced list -
			// the records are valid by construction; this thread only unpacks them and
			// reprojects the anchor for the importance mip.
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
			// Reproject the anchor into LAST frame's lattice for the importance mip. A failed or
			// plane-rejected reprojection just means uniform allocation this frame - importance
			// is an optimisation, never a correctness dependency.
			vec4 prev_clip4 = mul(u_gi_prev_view_proj, vec4(world_position, 1.0));
			if(prev_clip4.w > 0.0)
			{
				vec3 prev_clip = clipTransform(prev_clip4.xyz / prev_clip4.w);
				vec2 prev_uv = prev_clip.xy * 0.5 + 0.5;
				if(all(greaterThanEqual(prev_uv, vec2_splat(0.0))) &&
				   all(lessThanEqual(prev_uv, vec2_splat(1.0))))
				{
					vec2 prev_probe = floor(prev_uv * u_gi_probe_screen.xy / u_gi_probe_spacing);
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
						history_probe = ivec2(hx, hy);
						float total = 0.0;
						for(int m = 0; m < 4; ++m)
						{
							vec4 mip = b_gi_probes[history_base + uint(m)];
							s_importance_mip[slot * 4 + m] = mip;
							total += mip.x + mip.y + mip.z + mip.w;
						}
						s_importance_mean[slot] = total / 16.0;
					}
				}
			}
			// The old history pass's bookkeeping, merged here: decide from the READ half
			// whether this probe's previous tile is still the same visibility field (same
			// origin, or a scheduled walk), and stamp the WRITE half's count accordingly.
			// The tile itself needs no copy - the atlas is single-buffered and the tile
			// position is lattice-fixed, so the previous tile is already in place.
			BRANCH
			if(u_gi_probe_blend)
			{
				uint read_record =
				    (GiProbeRecord(probe.x, probe.y, 0) + u_gi_probe_read_offset) * uint(GI_PROBE_STRIDE);
				vec4 history_meta = b_gi_probes[read_record + uint(GI_PROBE_META)];
				float keep = 0.0;
				float prev_count = 0.0;
				bool tile_valid = false;
				if(u_gi_probe_history_cap > 0.5)
				{
					if(history_meta.w > 0.5)
					{
						tile_valid = true;
						if(GiScreenProbeSameOrigin(meta.xyz, history_meta.xyz, meta2.w) ||
						   anchor.w > 0.5)
						{
							keep = 1.0;
							prev_count = b_gi_probes[read_record + uint(GI_PROBE_HISTORY)].x;
						}
					}
				}
				float count = keep > 0.5 ? min(prev_count + 1.0, u_gi_probe_max_accum) : 1.0;
				// TILE INHERITANCE (see s_inherit_base): a slide re-anchor - same-origin
				// failed on a frame that scheduled no walk - adopts the world-reprojected
				// previous probe's tile and count instead of resetting. The plane test
				// already vetted it as this world point's estimate, so no ghosting enters:
				// this is the CURRENT accumulation of the right point, maintained live by
				// its own probe (or the interp pass). Scheduled walks normally never reach
				// this branch at all - ANCHOR.w keeps their count upstream (the walk is a
				// 1/n fade, never a reset); the walk guard here only stops a walk that
				// landed on an INVALID tile from inheriting content its own move is meant
				// to leave behind. The count still clamps to u_gi_probe_max_accum, so the
				// lighting-change fast-flush caps inherited depth exactly like kept depth.
				BRANCH
				if(keep < 0.5 && history_probe.x >= 0 && !GiScreenProbeWalkThisFrame())
				{
					float inherited =
					    b_gi_probes[uint(s_history_record[slot]) + uint(GI_PROBE_HISTORY)].x;
					count = min(inherited + 1.0, u_gi_probe_max_accum);
					s_inherit_base[slot] = GiProbeAtlasBase(history_probe.x, history_probe.y, 0);
					s_tile_clear[slot] = false;
					b_gi_probes[record + uint(GI_PROBE_HISTORY)] =
					    vec4(count, 1.0 / count, 0.0, 0.0);
					s_blend_alpha[slot] = 1.0 / count;
				}
				else
				{
					b_gi_probes[record + uint(GI_PROBE_HISTORY)] = vec4(count, 1.0 / count, 0.0, keep);
					s_blend_alpha[slot] = 1.0 / count;
					s_tile_clear[slot] = !tile_valid;
				}
			}
		}
	}
	barrier();
	if(!probe_active)
	{
		return;
	}
#if defined(GI_SCREEN_PROBE_TRACE_COMPACT)
	// 16 busy lanes per slot. Window 4: one Bayer texel each. Window 1 (A/B-off fallback):
	// each thread walks the 4 phases so the atlas still fills when the 8x8 program is missing.
	//
	// ADAPTIVE REINVESTMENT: the adaptive classifier frees 40-60% of the lattice's ray
	// budget on flat content, and that budget was banked as idle lanes. The traced count is
	// already staged in the list head (the args pass's bounds value), so the kernel widens
	// its own stratum where the launch stays within GI_SCREEN_PROBE_REINVEST_BUDGET of the
	// full lattice's rays - HALF, not all of it, because the probes that remain traced are
	// the expensive ones (geometry breaks, near-field marches; the skipped flat probes are
	// what diluted the average ray cost), so spending the whole nominal budget on them cost
	// MORE time than the uniform lattice and adaptive stopped being a perf win (measured:
	// +0.15 ms). Under the half budget, dense scenes reinvest nothing and keep the full
	// adaptive saving, while sparse lattices - flat dim walls, where arrival density is the
	// far-emissive flicker - still re-measure every direction two to four times as fast.
	// base advances by the stratum width, so phases never overlap and a flip near the
	// threshold costs nothing (untraced phases keep the single-buffered tile).
	int thread = int(gl_LocalInvocationID.x);
	int rays_per_thread;
	uint base_phase;
	if(u_gi_probe_window <= 1u)
	{
		rays_per_thread = GI_SCREEN_PROBE_WINDOW;
		base_phase = 0u;
	}
	else
	{
		float traced_count = b_gi_probes[GiProbeTracedListBase()].y;
		float budget = GI_SCREEN_PROBE_REINVEST_BUDGET * float(u_gi_probe_count_x) *
		               float(u_gi_probe_count_y);
		if(traced_count * 4.0 <= budget)
		{
			rays_per_thread = GI_SCREEN_PROBE_WINDOW;
		}
		else if(traced_count * 2.0 <= budget)
		{
			rays_per_thread = 2;
		}
		else
		{
			rays_per_thread = 1;
		}
		// Per-probe phase stagger (GiScreenProbePhaseOffset): the base stays a multiple of
		// the stratum width, so consecutive frames' phases still tile the window without
		// overlap exactly as before - only WHICH probe refreshes which phase decorrelates.
		base_phase = ((u_gi_probe_frame + GiScreenProbePhaseOffset(probe)) *
		              uint(rays_per_thread)) % u_gi_probe_window;
	}
	// Texels outside this frame's stratum: an INHERITED tile copies them from the
	// world-reprojected source (they would otherwise keep serving strata traced from
	// wherever this probe pointed windows ago - the churning mean under camera motion);
	// a fresh (invalid) history clears them, the duty the old history-pass copy carried.
	if(rays_per_thread < int(u_gi_probe_window))
	{
		ivec2 inherit_base = s_inherit_base[slot];
		if(inherit_base.x >= 0)
		{
			for(uint p = 0u; p < u_gi_probe_window; ++p)
			{
				if(p < base_phase || p >= base_phase + uint(rays_per_thread))
				{
					ivec2 stratum_local = GiScreenProbeStratumLocal(thread, p);
					imageStore(s_probe_radiance_out,
					           GiProbeAtlasBase(probe.x, probe.y, 0) + stratum_local,
					           imageLoad(s_probe_radiance_out, inherit_base + stratum_local));
				}
			}
		}
		else if(s_tile_clear[slot])
		{
			for(uint p = 0u; p < u_gi_probe_window; ++p)
			{
				if(p < base_phase || p >= base_phase + uint(rays_per_thread))
				{
					imageStore(s_probe_radiance_out,
					           GiProbeAtlasBase(probe.x, probe.y, 0) + GiScreenProbeStratumLocal(thread, p),
					           vec4(0.0, 0.0, 0.0, -1.0));
				}
			}
		}
	}
	LOOP
	for(int ri = 0; ri < rays_per_thread; ++ri)
	{
		GiTraceScreenProbeRay(slot, probe, GiScreenProbeStratumLocal(thread, base_phase + uint(ri)));
	}
#else
	ivec2 local = ivec2(gl_LocalInvocationID.xy);
	// Stratum early-out: untraced texels keep this probe's previous tile in place (the atlas
	// is single-buffered) - unless the tile is INHERITED (copied from the world-reprojected
	// source, see s_inherit_base) or the history is fresh (cleared, the duty the old
	// history-pass copy carried). Window 1 takes every texel, the parallel A/B-off path.
	if(!GiScreenProbeInStratum(local,
	                           u_gi_probe_frame + GiScreenProbePhaseOffset(probe),
	                           u_gi_probe_window))
	{
		ivec2 inherit_base = s_inherit_base[slot];
		if(inherit_base.x >= 0)
		{
			imageStore(s_probe_radiance_out, GiProbeAtlasBase(probe.x, probe.y, 0) + local,
			           imageLoad(s_probe_radiance_out, inherit_base + local));
		}
		else if(s_tile_clear[slot])
		{
			imageStore(s_probe_radiance_out, GiProbeAtlasBase(probe.x, probe.y, 0) + local,
			           vec4(0.0, 0.0, 0.0, -1.0));
		}
		return;
	}
	GiTraceScreenProbeRay(slot, probe, local);
#endif
}

#endif // __GI_SCREEN_PROBE_TRACE_KERNEL_SH__
