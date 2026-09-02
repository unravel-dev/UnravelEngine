#ifndef __GI_SCREEN_PROBE_TRACE_KERNEL_SH__
#define __GI_SCREEN_PROBE_TRACE_KERNEL_SH__

/*
 * GI screen probe trace (plan 3.4) - the Lumen recipe. Compiled twice:
 *
 *  - cs_gi_screen_probe_trace_full.sc: one 8x8 group per traced probe, one thread per
 *    octahedral texel - all 64 rays fresh every frame. The default and the quality
 *    ceiling.
 *  - cs_gi_screen_probe_trace_adaptive.sc (GI_SCREEN_PROBE_TRACE_ADAPTIVE, the
 *    settings::adaptive_rays checkbox): Lumen's structured-importance-sampling shape at
 *    the same per-frame-complete contract - 2x2 blocks whose reprojected importance
 *    concentrates energy (ratio over the tile mean) trace at FULL per-texel detail (x2
 *    samples on the very brightest), every other block traces ONE cone jittered across
 *    its quad and splats it - 16 + 3K rays per probe instead of 64. FOUR probes pack
 *    into each 64-lane group (16 lanes each, rays pulled round-robin so one bright
 *    block never idles the wave); a 16-thread group alone would leave three quarters
 *    of every wave idle. The trade is per-frame variance and 4x4 angular granularity
 *    in DIM octants only - white, one-frame-lived, integrated by the resolve temporal.
 *
 * There is deliberately NO probe-space temporal accumulation in either form.
 * Direction-stratum amortization (16-ray windows blended 1/n into the tile) was built,
 * rebuilt as a blended adaptive schedule, and finally REMOVED: averaging in probe space
 * turns white per-frame noise into probe-granular correlated drift - tiles serving
 * differently-aged strata, rare emitter arrivals living for ~cap frames as 16px-coherent
 * blobs, walk cadences stepping every anchor at once - and correlated drift is exactly
 * what the downstream per-pixel temporal cannot remove (it passes through as signal and
 * its change detector snaps on it; measured as still-camera moving blobs across three
 * schemes). The per-frame gather stays white; the full-res dual-rate temporal and the
 * spatial denoiser own ALL accumulation. Ray budget scales with PROBE DENSITY
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
 * Everything here is owned by gi_constants - the pass has no tuning surface beyond the
 * lattice descriptors and the adaptive_rays checkbox.
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
// for the prev-color read below.
#define GI_WORLD_PROBE_SKIP_IRRADIANCE
#include "gi/gi_world_probes.sh"
#include "gi/gi_noise.sh"

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
SAMPLER2D(s_gi_env_sh, 14);

/// xyz = camera position (world-probe window centre), w = frame index.
uniform vec4 u_gi_camera;
/// x > 0 when s_hiz holds a full pyramid and the screen-trace tier runs.
/// y > 0 = RAY TIER debug: rays paint which tier answered instead of radiance
/// (green = screen commit, red = SDF hit, blue = world-probe/sky completion; the interp pass
/// paints interpolated tiles magenta under the same flag).
/// z = the adaptive flag - consumed by the CLASSIFY pass, bound here only for layout parity.
/// w > 0 when s_gi_prev_color holds last frame's composited output; > 1.5 when its alpha
/// also carries each pixel's view depth (the RGBA16F history), which GiReadHistory then
/// validates a reprojection against.
uniform vec4 u_gi_screen_trace;
/// Previous view projection: the anchor reprojects into LAST frame's lattice to read the
/// importance mip the filter stored in that probe's record slots.
uniform mat4 u_gi_prev_view_proj;

/// Probe slots per group: the adaptive program packs four 16-lane probes into one 64-lane
/// group; the full program's 8x8 group is one fully-busy probe already.
#if defined(GI_SCREEN_PROBE_TRACE_ADAPTIVE)
#	define GI_TRACE_SLOT_COUNT 4
#else
#	define GI_TRACE_SLOT_COUNT 1
#endif
/// Lanes per probe in the adaptive program = the 4x4 block count of the 8x8 tile.
#define GI_TRACE_ADAPTIVE_LANES ((GI_PROBE_DIR_EDGE / 2) * (GI_PROBE_DIR_EDGE / 2))

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
/// Dispatch-uniform values hoisted out of the per-ray loop.
SHARED vec2 s_screen_size;
SHARED vec2 s_frame_r2;

/*
 * HISTORY READ, validated. True when last frame's composite holds THIS surface at the
 * reprojected pixel: on screen last frame and, when the snapshot carries its view depth
 * (u_gi_screen_trace.w > 1.5), the stored depth agrees with the hit's reprojected depth
 * within GI_TEMPORAL_DEPTH_TOLERANCE of it - the temporal accumulation's own tolerance.
 * Without the test a disoccluded reprojection read whatever surface last frame showed at
 * that pixel, and the first frame after a disocclusion measured a different source (the
 * light voxels) than the frames after it (the composite) - a bias step the temporal then
 * carried. No stage was free for a depth history; the snapshot's alpha carries it instead.
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
	radiance = history.xyz;
	return true;
}

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
	vec3 history;
	BRANCH
	if(GiReadHistory(hit_position, history))
	{
		return history;
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

void GiStoreScreenProbeRay(int slot, ivec2 texel, vec3 radiance, float hit_t)
{
	vec3 averaged = min(radiance, vec3_splat(GI_MAX_RAY_RADIANCE));
	// FIREFLY GOVERNOR: a ray landing on a small bright emitter dominates the whole tile
	// when it enters at full weight - the probe's screen footprint pops for a frame. Each
	// new sample is capped at GI_GATHER_FIREFLY_CLAMP x its reference: LAST frame's value
	// of this texel (the tile is single-buffered, so it is still in place), FLOORED by the
	// reprojected previous tile's mean luminance (s_importance_mean - looked up by WORLD
	// position with a plane test, so it survives camera motion that leaves the texel
	// holding a nearby point's radiance). Without the floor, a dark stale texel crushed
	// legitimate arrivals to 8x darkness - measured as pumping noise in emissive-lit dark
	// scenes the moment the camera moved. A texel whose own history legitimately sees the
	// emitter raises its own ceiling and converges unbiased (per-texel, never ONLY the
	// tile mean - that would crush a lone bright texel to mean x k / 256). No meaningful
	// reference at all (fresh tile, failed reprojection): the first measurement stores
	// unclamped - progressive ramps from black would dim every disocclusion instead.
	vec4 hist = imageLoad(s_probe_radiance_out, texel);
	float reference = max(Luminance(hist.xyz), s_importance_mean[slot]);
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
	imageStore(s_probe_radiance_out, texel, vec4(averaged, hit_t));
}

/// The block's reprojected importance over the tile mean; 1.0 (neutral) when history is
/// absent or reprojection failed - uniform allocation, as ever.
float GiScreenProbeBlockRatio(int slot, int block)
{
	if(s_history_record[slot] < 0 || s_importance_mean[slot] <= 1e-4)
	{
		return 1.0;
	}
	vec4 mip = s_importance_mip[slot * 4 + block / 4];
	int lane = block % 4;
	float importance = lane == 0 ? mip.x : (lane == 1 ? mip.y : (lane == 2 ? mip.z : mip.w));
	return importance / s_importance_mean[slot];
}

/*
 * IMPORTANCE-PROPORTIONAL SAMPLE ALLOCATION for one 2x2 block: a cone whose reprojected
 * history reads brighter than the probe mean gets extra sub-cone samples on a geometric
 * ladder of the ratio - resolving an emitter smaller than the cone in proportion to how
 * much of the tile's energy it concentrates. The ladder is SELF-BUDGETING with no
 * reduction: ratios normalise by the tile MEAN, and the sixteen block importances sum to
 * sixteen means by definition - so however the energy is distributed, the extra samples
 * are bounded (about half the base ray count in the all-worst-case), and a uniformly lit
 * tile pays exactly one sample per cone as before.
 */
int GiScreenProbeSampleCount(int slot, int block)
{
	float ratio = GiScreenProbeBlockRatio(slot, block);
	int sample_count = 1;
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
	return sample_count;
}

/*
 * The sample loop: @p sample_count directions jittered inside the cone spanning
 * @p texel_span octahedral texels from @p texel_base (top-left, in tile texels), each
 * answered Hi-Z -> SDF -> world-probe completion. Returns (radiance mean, max hitT;
 * -1 = completed/sky only). Returned, never out-parameters - the shaderc HLSL path
 * miscompiles out-params in .sc helpers silently (tasks/lessons.md).
 *
 * Sub-texel DIRECTION jitter. Fixed centre rays ALIAS small bright sources: a source
 * smaller than one cone is either skewered or missed by the grid, and which probes catch
 * it varies smoothly with anchor position - printing stationary whitish blobs across
 * walls that NO downstream filter can remove, because the per-probe estimates are BIASED,
 * not noisy (measured: blobs immune to temporal off, denoise off, spacing, and every
 * writer-side fix). Jittering the sample within its cone per frame turns that bias into
 * per-frame variance the temporal chain integrates - each cone measures its whole solid
 * angle over the accumulation window. R2 low-discrepancy across frames; the pattern is
 * addressed by ATLAS texel so it decorrelates both the texels within a tile and the
 * same direction across neighbouring probes (two independent channels - see
 * gi_noise.sh). The multi-sample pattern is the first four points of a shifted
 * (0,2)-net: positions 0/1 are the exact antithetic pair, so counts one and two
 * reproduce the classic estimator.
 */
vec4 GiTraceScreenProbeCone(int slot, vec2 texel_base, float texel_span, int sample_count,
                            ivec2 noise_texel)
{
	vec2 sub_positions[GI_IMPORTANCE_SUPERSAMPLE_MAX];
	sub_positions[0] = fract(s_frame_r2 + GiIgnNoise(noise_texel));
	sub_positions[1] = fract(sub_positions[0] + vec2(0.5, 0.5));
	sub_positions[2] = fract(sub_positions[0] + vec2(0.25, 0.75));
	sub_positions[3] = fract(sub_positions[0] + vec2(0.75, 0.25));
	vec3 radiance_sum = vec3_splat(0.0);
	float hit_t = -1.0;
	for(int s = 0; s < sample_count; ++s)
	{
		vec2 sample_uv = (texel_base + sub_positions[s] * texel_span) / float(GI_PROBE_DIR_EDGE);
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
							// clamp and the firefly governor bound spikes). The read is
							// validated against the depth the snapshot carries (GiReadHistory):
							// a disoccluded reprojection declines instead of reading whatever
							// surface last frame showed there. Off-screen last frame, no
							// history, or a depth mismatch: the voxel read answers exactly as
							// before.
							bool screen_lit = GiReadHistory(hit_position, radiance);
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
	return vec4(radiance_sum / float(sample_count), hit_t);
}

/// One octahedral texel at @p sample_count sub-cone samples: traced and stored, with the
/// below-tangent cull (cap texels hold exact zero - their converged value by definition).
void GiTraceScreenProbeDetail(int slot, ivec2 probe, ivec2 local, int sample_count)
{
	ivec2 texel = GiProbeAtlasBase(probe.x, probe.y, 0) + local;
	vec2 tile_uv = (vec2(local.xy) + vec2_splat(0.5)) / float(GI_PROBE_DIR_EDGE);
	vec3 direction = GiOctDecode(tile_uv);
	if(dot(direction, s_anchor_normal[slot]) < -0.2)
	{
		imageStore(s_probe_radiance_out, texel, vec4(0.0, 0.0, 0.0, -1.0));
		return;
	}
	vec4 traced = GiTraceScreenProbeCone(slot, vec2(local.xy), 1.0, sample_count, texel);
	GiStoreScreenProbeRay(slot, texel, traced.xyz, traced.w);
}

/// The full-program form: one texel with the whole importance ladder.
void GiTraceScreenProbeRay(int slot, ivec2 probe, ivec2 local)
{
	GiTraceScreenProbeDetail(slot, probe, local,
	                         GiScreenProbeSampleCount(slot, (local.y / 2) * 4 + (local.x / 2)));
}

#if defined(GI_SCREEN_PROBE_TRACE_ADAPTIVE)

/// Rays the adaptive schedule grants a block this frame: 4 per-texel DETAIL rays when the
/// block concentrates energy, 1 coarse cone otherwise (a culled block's single "ray" is
/// the coarse executor's zero-store walk - no trace).
int GiScreenProbeBlockRays(int slot, int block)
{
	return GiScreenProbeBlockRatio(slot, block) > GI_IMPORTANCE_SUPERSAMPLE_RATIO ? 4 : 1;
}

// The coarse-block executor was FOLDED into the scheduler below: keeping it as a second
// function gave the cone body a second inlined instantiation, which alone quadrupled this
// program's compile time (see the note at the scheduler's trace call).

#endif // GI_SCREEN_PROBE_TRACE_ADAPTIVE

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
			s_frame_r2 = fract(vec2(0.754877666, 0.569840291) * u_gi_camera.w);
		}
		if(probe_active)
		{
			s_history_record[slot] = -1;
			s_importance_mean[slot] = 0.0;
			// Placement computed the anchor, classification put this probe on the traced
			// list - the records are valid by construction; this thread only unpacks them
			// and reprojects the anchor for the importance mip.
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
			// Reproject the anchor into LAST frame's lattice for the importance mip. The
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
		}
	}
	barrier();
	if(!probe_active)
	{
		return;
	}
#if defined(GI_SCREEN_PROBE_TRACE_ADAPTIVE)
	// ADAPTIVE SCHEDULE (see the header): 16 + 3K rays for K detail blocks, pulled
	// round-robin across the 16 lanes so one bright block never idles the wave (lane time
	// = ceil(rays / 16) iterations, not one block's whole cost). Every texel is written
	// every frame - by its own detail ray, by its block's coarse splat, or by the cull's
	// zero store. The block walk below is pure shared-memory arithmetic per lane; with
	// sixteen blocks a scan beats any prefix machinery.
	int thread = int(gl_LocalInvocationID.x);
	int total_rays = 0;
	LOOP
	for(int b = 0; b < 16; ++b)
	{
		total_rays += GiScreenProbeBlockRays(slot, b);
	}
	LOOP
	for(int r = thread; r < total_rays; r += GI_TRACE_ADAPTIVE_LANES)
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
		// ONE cone-body instantiation for both ray kinds, by PARAMETER selection: fxc
		// fully inlines every call site of the trace body (Hi-Z + SDF march + completion,
		// thousands of instructions), and a second instantiation alone took this program's
		// s_5_0 compile from ~4 s to ~17 s - fxc codegen is superlinear in inlined
		// mega-bodies, independent of the -O level (measured; [loop] attributes and
		// unrolling were ruled out).
		float ratio = GiScreenProbeBlockRatio(slot, block);
		bool detail = ratio > GI_IMPORTANCE_SUPERSAMPLE_RATIO;
		ivec2 quad = ivec2((block % 4) * 2, (block / 4) * 2);
		// DETAIL: ray `scan` in [0,4) owns one texel of the 2x2 quad, double-sampled on
		// the ladder's top rung. COARSE: one cone jittered across the whole quad footprint,
		// splatted to all four texels (blend-free, the splat lives one frame - a boundary
		// quad's bright/dark coin flip is white noise the resolve temporal integrates).
		ivec2 cone_base = detail ? quad + ivec2(scan & 1, scan >> 1) : quad;
		float cone_span = detail ? 1.0 : 2.0;
		int sample_count = (detail && ratio > GI_IMPORTANCE_SUPERSAMPLE_RATIO *
		                                          GI_IMPORTANCE_SUPERSAMPLE_RATIO *
		                                          GI_IMPORTANCE_SUPERSAMPLE_RATIO)
		                       ? 2
		                       : 1;
		ivec2 atlas_base = GiProbeAtlasBase(probe.x, probe.y, 0);
		// Cone-centre cull: the detail texel's own threshold, or the quad centre loosened
		// by its cosine half-span (~0.25) - a quad it rejects has every texel at or under
		// the tangent cap, where cosine weights vanish anyway.
		vec2 centre_uv = (vec2(cone_base) + vec2_splat(0.5 * cone_span)) / float(GI_PROBE_DIR_EDGE);
		bool cone_traced =
		    dot(GiOctDecode(centre_uv), s_anchor_normal[slot]) >= (detail ? -0.2 : -0.45);
		vec4 traced = vec4(0.0, 0.0, 0.0, -1.0);
		BRANCH
		if(cone_traced)
		{
			traced = GiTraceScreenProbeCone(slot, vec2(cone_base), cone_span, sample_count,
			                                atlas_base + cone_base);
		}
		int store_count = detail ? 1 : 4;
		LOOP
		for(int i = 0; i < store_count; ++i)
		{
			ivec2 local = cone_base + (detail ? ivec2(0, 0) : ivec2(i & 1, i >> 1));
			ivec2 texel = atlas_base + local;
			vec2 tile_uv = (vec2(local.xy) + vec2_splat(0.5)) / float(GI_PROBE_DIR_EDGE);
			// The cap texel's contract, exactly as the full path stores it.
			if(!cone_traced || dot(GiOctDecode(tile_uv), s_anchor_normal[slot]) < -0.2)
			{
				imageStore(s_probe_radiance_out, texel, vec4(0.0, 0.0, 0.0, -1.0));
			}
			else
			{
				GiStoreScreenProbeRay(slot, texel, traced.xyz, traced.w);
			}
		}
	}
#else
	GiTraceScreenProbeRay(slot, probe, ivec2(gl_LocalInvocationID.xy));
#endif
}

#endif // __GI_SCREEN_PROBE_TRACE_KERNEL_SH__
