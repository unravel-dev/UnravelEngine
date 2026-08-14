#ifndef __GI_SCREEN_PROBE_TRACE_KERNEL_SH__
#define __GI_SCREEN_PROBE_TRACE_KERNEL_SH__

/*
 * GI screen probe trace (plan 3.4) - the Lumen recipe, one thread group per probe.
 *
 * SHARED KERNEL BODY: compiled twice. cs_gi_screen_probe_trace.sc is the compacted
 * 16-thread group (GI_SCREEN_PROBE_TRACE_COMPACT) used while probe-space temporal is
 * on: one thread per this frame's Bayer stratum, no idle lanes waiting on SDF rays.
 * cs_gi_screen_probe_trace_full.sc is NUM_THREADS(8,8,1) - the A/B-off / first-frame
 * path that traces all 64 octahedral texels in parallel. The C++ pass selects the
 * PROGRAM; both share the same compacted probe-list dispatch (one group per live
 * probe). Window 1 on the compact program still covers the atlas (each of 16
 * threads walks the 4 Bayer phases) so a missing full variant cannot leave holes.
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
 * tracing cannot express). A confident on-screen hit commits: the ray reads the light voxels
 * at the reconstructed world hit, so radiance stays in the SDF path's units and the two tiers
 * never disagree on energy - the screen buys geometry, not a second lighting source. Anything
 * else - miss, left the screen, low confidence - falls through to the SDF trace unchanged.
 *
 * PROBE-SPACE TEMPORAL (windowed): each frame traces a 16-ray 2x2 Bayer stratum
 * and blends those texels (1/n) into this probe's own previous tile. The other 48
 * stay as that tile. Placement stays sticky while the origin is still in-tile so
 * the sphere is one visibility field; a Halton walk resets the blend count.
 * A miss copies the previous tile (never black). Window 1 traces every texel.
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

/// LAST frame's composited output (the SSR convention, same source): the far-field radiance
/// for hits BEYOND the cascades, where the light voxels have nothing. Bound in place of the
/// world-probe irradiance cage the trace never read; the compacted probe list lives in the
/// probe buffer's list region (GiProbeTracedListBase), not in a stage of its own.
SAMPLER2D(s_gi_prev_color, 11);

/// rgb = radiance, a = hitT (negative = completed/sky). One 8x8 tile per probe.
/// RW: the history pass writes the previous tile first; traced texels blend on top.
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

SHARED vec3 s_anchor_position;
SHARED vec3 s_anchor_normal;
SHARED vec3 s_origin;
SHARED float s_short_range;
/// Anchor in the screen tier's spaces, shared by every ray's Hi-Z march: full-res uv (self-hit
/// rejection), (uv, device z) screen-space origin, and the view-space origin.
SHARED vec2 s_anchor_uv;
SHARED vec3 s_ss_origin;
SHARED vec3 s_vs_origin;
/// Base record index of the reprojected PREVIOUS probe, or -1 when reprojection failed.
SHARED int s_history_record;
SHARED float s_importance_mean;
SHARED float s_blend_alpha;

/*
 * Radiance for a hit whose light-voxel read failed. WITHIN the cascades that is honest
 * darkness - sub-voxel contact occlusion, sealed rooms - and must stay black. BEYOND the
 * outermost window "no data" must not mean "no light" (the black wall at the end of the
 * street): the Lumen far-field recipe applies - reproject the hit into LAST frame's
 * composited output, which already carries that geometry's shadow-mapped lighting (the SSR
 * scene-colour convention; the temporal mean and the radiance clamp bound the feedback);
 * off-screen or history-less, the sky SH along the ray, the miss contract.
 */
vec3 GiFarFieldFallback(vec3 hit_position, vec3 sample_dir)
{
	float blend;
	float voxel;
	if(SdfFindClipmapLevel(hit_position, blend, voxel) < SDF_CLIPMAP_LEVEL_COUNT)
	{
		return vec3_splat(0.0);
	}
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

void GiStoreScreenProbeRay(ivec2 texel, vec3 radiance, float hit_t)
{
	vec3 averaged = min(radiance, vec3_splat(GI_MAX_RAY_RADIANCE));
	BRANCH
	if(u_gi_probe_blend && s_blend_alpha < 0.999)
	{
		vec4 hist = imageLoad(s_probe_radiance_out, texel);
		averaged = mix(hist.xyz, averaged, s_blend_alpha);
	}
	imageStore(s_probe_radiance_out, texel, vec4(averaged, hit_t));
}

/// One octahedral texel: Hi-Z then SDF then world-probe completion, written to the atlas.
void GiTraceScreenProbeRay(ivec2 probe, ivec2 local)
{
	ivec2 texel = GiProbeAtlasBase(probe.x, probe.y, 0) + local;
	// World-anchored direction CONES: each thread owns one texel of the shared octahedral
	// parameterisation; the cone centre is used for the below-tangent cull and the importance
	// lookup, while the traced sample directions jitter WITHIN the cone (see the sample loop).
	vec2 tile_uv = (vec2(local.xy) + vec2_splat(0.5)) / float(GI_PROBE_DIR_EDGE);
	vec3 direction = GiOctDecode(tile_uv);
	// The hemisphere cap below the anchor's tangent plane cannot carry irradiance; skipped with
	// a small tolerance so grazing directions still trace (the integration weights by cosine).
	if(dot(direction, s_anchor_normal) < -0.2)
	{
		GiStoreScreenProbeRay(texel, vec3_splat(0.0), -1.0);
		return;
	}
	// IMPORTANCE-DRIVEN SUPERSAMPLING: a cone whose reprojected history reads brighter than
	// GI_IMPORTANCE_SUPERSAMPLE_RATIO x the probe mean gets a second sub-cone sample - the
	// smallest step that resolves an emitter smaller than the cone, funded only where the
	// history says energy is concentrated. Deterministic sub-positions, so a static scene still
	// produces identical probe input every frame.
	int sample_count = 1;
	if(s_history_record >= 0 && s_importance_mean > 1e-4)
	{
		int block = (local.y / 2) * 4 + (local.x / 2);
		vec4 mip = b_gi_probes[uint(s_history_record) + uint(block / 4)];
		int lane = block % 4;
		float importance = lane == 0 ? mip.x : (lane == 1 ? mip.y : (lane == 2 ? mip.z : mip.w));
		if(importance > GI_IMPORTANCE_SUPERSAMPLE_RATIO * s_importance_mean)
		{
			sample_count = 2;
		}
	}
	// Sub-texel DIRECTION jitter. Fixed centre rays ALIAS small bright sources: a source
	// smaller than one cone is either skewered or missed by the grid, and which probes catch
	// it varies smoothly with anchor position - printing stationary whitish blobs across
	// walls that NO downstream filter can remove, because the per-probe estimates are BIASED,
	// not noisy (measured: blobs immune to temporal off, denoise off, spacing, and every
	// writer-side fix). Jittering the sample within its cone per frame turns that bias into
	// per-frame variance the temporal chain integrates - each cone measures its whole solid
	// angle over the accumulation window. R2 low-discrepancy across frames, IGN-decorrelated
	// across texels; the supersample takes the antithetic offset.
	vec2 r2 = fract(vec2(0.754877666, 0.569840291) * u_gi_camera.w);
	float texel_ign =
	    fract(52.9829189 * fract(0.06711056 * float(local.x) + 0.00583715 * float(local.y)));
	vec2 sub_positions[2];
	sub_positions[0] = fract(r2 + vec2(texel_ign, fract(texel_ign * 1.618033989)));
	sub_positions[1] = fract(sub_positions[0] + vec2_splat(0.5));
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
			vec3 ss_dir = HizProjectVsDirToSsDir(s_vs_origin, vs_dir, s_ss_origin);
			BRANCH
			if(dot(ss_dir.xy, ss_dir.xy) >= 1e-12)
			{
				vec2 screen_size = HizGetDepthMipResolution(s_hiz, 0);
				vec3 ss_hit = vec3_splat(0.0);
				// Mip-1 floor (GI_SCREEN_TRACE_MIN_MIP): these rays are cone-amortized over a
				// probe tile, so a mip-0 walk buys sub-pixel precision below the cone footprint
				// at twice the traversal cost. Validation still reads mip-0 depth; a lost
				// commit falls through to the SDF, which is the watertight answer anyway.
				bool marched = HizHierarchicalRaymarch(s_hiz, s_ss_origin, ss_dir, screen_size,
				                                       GI_SCREEN_TRACE_MIN_MIP,
				                                       GI_SCREEN_TRACE_MAX_STEPS, ss_hit);
				BRANCH
				if(marched)
				{
					vec3 vs_hit = HizComputeViewspacePosition(ss_hit.xy, ss_hit.z);
					float hit_dist = length(vs_hit - s_vs_origin);
					// Beyond the short range the world probes own the answer, exactly as they
					// do for an SDF miss - a far screen hit must not override that contract.
					BRANCH
					if(hit_dist < s_short_range)
					{
						float tolerance = GI_SCREEN_TRACE_DEPTH_TOLERANCE +
						                  GI_SCREEN_TRACE_THICKNESS * (hit_dist / s_short_range);
						float confidence = HizValidateHit(s_hiz, s_gi_normal, ss_hit, s_anchor_uv,
						                                  s_vs_origin, vs_hit, tolerance);
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
							if(!GiLightVoxelRead(hit_position, hit_normal, radiance))
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
			SdfRayHit hit = SdfTraceRayEx(s_origin, sample_dir, s_short_range, GI_MESH_SDF_TRACE_RANGE,
			                              GI_TRACE_MAX_STEPS, GI_PROBE_TRACE_SURFACE_BIAS,
			                              GI_PROBE_TRACE_RELAXATION, true, 0.0);
			if(hit.hit)
			{
				answered_tier = 2;
				hit_t = max(hit_t, hit.t);
				vec3 hit_position = s_origin + sample_dir * hit.t;
				vec3 hit_normal = hit.normal;
				if(dot(hit_normal, sample_dir) > 0.0)
				{
					hit_normal = -hit_normal;
				}
				// Sub-surface hit guard: a hit the field itself reports as INSIDE geometry
				// (beyond half the covering voxel under the surface - normal acceptance stops
				// strictly above it) is a trace pathology: the launch-suppression walk can
				// release through thin geometry around the mesh<->clipmap handover shell.
				// Reading ANYTHING there imports the far side - the light-voxel read selects
				// a face slab by normal, and a tunneled hit inside a sunlit wall hands the
				// EXTERIOR face's radiance to an interior ray. Honest darkness is the only
				// answer that cannot leak. Unblended finest-level reading, for the same
				// reason the cage-visibility march uses it: a verdict, not a surface resolve.
				float hit_blend;
				float hit_voxel;
				int hit_level = SdfFindClipmapLevel(hit_position, hit_blend, hit_voxel);
				if(hit_level < SDF_CLIPMAP_LEVEL_COUNT &&
				   SdfSampleClipmapLevel(hit_level, hit_position) < -0.5 * hit_voxel)
				{
					radiance = vec3_splat(0.0);
				}
				else if(!GiLightVoxelRead(hit_position, hit_normal, radiance))
				{
					// Occluded but unmeasured: honest darkness within the cascades (the
					// sealed-room branch); past them - exhaustion at the boundary, coarse
					// fringe hits - the far-field fallback.
					radiance = GiFarFieldFallback(hit_position, sample_dir);
				}
			}
			else
			{
				// Completion: the world probes carry everything beyond the short range - scene
				// AND sky.
				if(!GiWorldProbeRadiance(s_origin + sample_dir * s_short_range, sample_dir,
				                         u_gi_camera.xyz, radiance))
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
	GiStoreScreenProbeRay(texel, radiance_sum / float(sample_count), hit_t);
}

#if defined(GI_SCREEN_PROBE_TRACE_COMPACT)
NUM_THREADS(GI_SCREEN_PROBE_RAYS_PER_FRAME, 1, 1)
#else
NUM_THREADS(8, 8, 1)
#endif
void main()
{
	// COMPACTED dispatch: exactly the traced count of groups launches (indirect args from the
	// classify pass), each reading its probe coordinate from the dense list - interpolated,
	// dead and sky probes never occupy a wavefront here. Their tiles are the interp pass's
	// job (parent blend or black clear). The list lives in the probe buffer's list region,
	// one bit-cast coordinate per vec4 (GiProbeTracedListBase).
	uint packed_probe = floatBitsToUint(b_gi_probes[GiProbeTracedListBase() + gl_WorkGroupID.x].x);
	ivec2 probe = ivec2(int(packed_probe & 0xFFFFu), int(packed_probe >> 16u));
	uint record = (GiProbeRecord(probe.x, probe.y, 0) + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
    if(int(gl_LocalInvocationID.x) == 0 && int(gl_LocalInvocationID.y) == 0)
	{
		s_history_record = -1;
		s_importance_mean = 0.0;
		s_blend_alpha = 1.0;
		// Placement computed the anchor, classification put this probe on the traced list -
		// the records are valid by construction; this thread only unpacks them and
		// reprojects the anchor for the importance mip.
		vec4 meta = b_gi_probes[record + uint(GI_PROBE_META)];
		vec4 meta2 = b_gi_probes[record + uint(GI_PROBE_META2)];
		vec3 world_position = meta.xyz;
		vec3 world_normal = meta2.xyz;
		vec4 origin_range = b_gi_probes[record + uint(GI_PROBE_ORIGIN)];
		vec4 anchor = b_gi_probes[record + uint(GI_PROBE_ANCHOR)];
		s_anchor_position = world_position;
		s_anchor_normal = world_normal;
		s_origin = origin_range.xyz;
		s_short_range = origin_range.w;
		s_anchor_uv = anchor.xy;
		s_ss_origin = vec3(anchor.xy, anchor.z);
		s_vs_origin = HizComputeViewspacePosition(anchor.xy, anchor.z);
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
					s_history_record = int(history_base);
					float total = 0.0;
					for(int m = 0; m < 4; ++m)
					{
						vec4 mip = b_gi_probes[history_base + uint(m)];
						total += mip.x + mip.y + mip.z + mip.w;
					}
					s_importance_mean = total / 16.0;
				}
			}
		}
		BRANCH
		if(u_gi_probe_blend)
		{
			vec4 hist = b_gi_probes[record + uint(GI_PROBE_HISTORY)];
			float count = 1.0;
			if(hist.w > 0.5)
			{
				count = min(hist.x + 1.0, u_gi_probe_max_accum);
			}
			b_gi_probes[record + uint(GI_PROBE_HISTORY)] = vec4(count, 1.0 / count, 0.0, hist.w);
			s_blend_alpha = 1.0 / count;
		}
	}
	barrier();
#if defined(GI_SCREEN_PROBE_TRACE_COMPACT)
	// 16 busy lanes. Window 4: one Bayer texel each. Window 1 (A/B-off fallback): each
	// thread walks the 4 phases so the atlas still fills when the 8x8 program is missing.
	int thread = int(gl_LocalInvocationID.x);
	int rays_per_thread = u_gi_probe_window <= 1u ? GI_SCREEN_PROBE_WINDOW : 1;
	uint base_phase = u_gi_probe_window <= 1u ? 0u : (u_gi_probe_frame % u_gi_probe_window);
	for(int ri = 0; ri < rays_per_thread; ++ri)
	{
		GiTraceScreenProbeRay(probe, GiScreenProbeStratumLocal(thread, base_phase + uint(ri)));
	}
#else
	ivec2 local = ivec2(gl_LocalInvocationID.xy);
	// Stratum early-out: the history pass already wrote this texel from last frame. Returning
	// without a store is what makes the 4x ray cut real on this 8x8 path - a write here
	// would erase it. Window 1 takes every texel, the parallel A/B-off path.
	if(!GiScreenProbeInStratum(local, u_gi_probe_frame, u_gi_probe_window))
	{
		return;
	}
	GiTraceScreenProbeRay(probe, local);
#endif
}

#endif // __GI_SCREEN_PROBE_TRACE_KERNEL_SH__
