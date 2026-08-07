/*
 * GI v2 screen probe trace (plan 3.4) - the Lumen recipe, one thread group per probe, one
 * thread per octahedral direction.
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
 * Everything here is owned by gi_constants - the pass has no tuning surface.
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
#include "gi/gi_world_probes.sh"

/// rgb = radiance, a = hitT (negative = completed/sky). One 8x8 tile per probe.
IMAGE2D_WO(s_probe_radiance_out, rgba16f, 5);
/// Probe records: reuses the existing layout (gi_probe_common.sh) so downstream plumbing holds.
BUFFER_RW(b_gi_probes, vec4, 7);

/// The Hi-Z depth pyramid when the screen trace is enabled, else the raw G-buffer depth.
/// Mip 0 is the device depth either way (cs_hiz_generate copies it verbatim), so the anchor
/// reconstruction below reads the same values from both.
SAMPLER2D(s_hiz, 8);
SAMPLER2D(s_gi_normal, 9);
SAMPLER2D(s_gi_env_sh, 14);

/// xyz = camera position (world-probe window centre), w = frame index.
uniform vec4 u_gi_v2_camera;
/// x > 0 when s_hiz holds a full pyramid and the screen-trace tier runs.
/// y > 0 = RAY TIER debug: rays paint which tier answered instead of radiance
/// (green = screen commit, red = SDF hit, blue = world-probe/sky completion). zw unused.
uniform vec4 u_gi_screen_trace;
/// Previous view projection: the anchor reprojects into LAST frame's lattice to read the
/// importance mip the filter stored in that probe's record slots.
uniform mat4 u_gi_prev_view_proj;

SHARED vec3 s_anchor_position;
SHARED vec3 s_anchor_normal;
SHARED vec3 s_origin;
SHARED float s_short_range;
SHARED float s_valid;
/// Anchor in the screen tier's spaces, shared by every ray's Hi-Z march: full-res uv (self-hit
/// rejection), (uv, device z) screen-space origin, and the view-space origin.
SHARED vec2 s_anchor_uv;
SHARED vec3 s_ss_origin;
SHARED vec3 s_vs_origin;
/// Base record index of the reprojected PREVIOUS probe, or -1 when reprojection failed.
SHARED int s_history_record;
SHARED float s_importance_mean;

/// 2,3-Halton point of an 8-cycle, the placement jitter within the probe tile. A short cycle on
/// purpose: the temporal filter accumulates 10 frames, so the cycle must fit inside it.
vec2 GiHalton8(uint frame)
{
	uint index = frame % 8u;
	float h2 = 0.0;
	float f2 = 0.5;
	uint n2 = index + 1u;
	for(int i = 0; i < 4 && n2 > 0u; ++i)
	{
		h2 += f2 * float(n2 % 2u);
		n2 /= 2u;
		f2 *= 0.5;
	}
	float h3 = 0.0;
	float f3 = 1.0 / 3.0;
	uint n3 = index + 1u;
	for(int i = 0; i < 3 && n3 > 0u; ++i)
	{
		h3 += f3 * float(n3 % 3u);
		n3 /= 3u;
		f3 /= 3.0;
	}
	return vec2(h2, h3);
}

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 probe = ivec2(gl_WorkGroupID.xy);
	ivec2 local = ivec2(gl_LocalInvocationID.xy);
	if(probe.x >= u_gi_probe_count_x || probe.y >= u_gi_probe_count_y)
	{
		return;
	}
	uint record = (GiProbeRecord(probe.x, probe.y, 0) + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
	if(local.x == 0 && local.y == 0)
	{
		s_valid = 0.0;
		s_history_record = -1;
		s_importance_mean = 0.0;
		vec2 jitter = GiHalton8(uint(u_gi_v2_camera.w));
		vec2 pixel = (vec2(probe.xy) + jitter) * u_gi_probe_spacing;
		pixel = min(pixel, u_gi_probe_screen.xy - vec2_splat(1.0));
		vec2 uv = (floor(pixel) + vec2_splat(0.5)) * u_gi_probe_screen.zw;
		float depth = texture2DLod(s_hiz, uv, 0.0).x;
		if(depth < 1.0)
		{
			vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(depth)));
			vec3 world_position = clipToWorld(u_invViewProj, clip);
			GBufferDataNormalMetalRoughness nd = DecodeGBufferNormalMetalRoughnessLod(uv, s_gi_normal, 0.0);
			if(dot(nd.world_normal, nd.world_normal) >= 0.5)
			{
				vec3 world_normal = normalize(nd.world_normal);
				// Lift off the composed isosurface by what THIS point needs: its own burial
				// depth plus the trace's acceptance, in the answering level's voxels.
				float voxel;
				float d = SdfSampleClipmapEx(world_position, voxel);
				voxel = max(voxel, 0.01);
				float lift = max(0.0, -d) + GI_PROBE_TRACE_SURFACE_BIAS * voxel;
				s_anchor_position = world_position;
				s_anchor_normal = world_normal;
				s_origin = world_position + world_normal * lift;
				// Shortened-ray range: twice the local world-probe spacing covers the probe
				// cage's footprint plus the completion skip [S21 s71-72].
				float blend;
				float answered_voxel;
				int level = SdfFindClipmapLevel(world_position, blend, answered_voxel);
				int clamped = level >= SDF_CLIPMAP_LEVEL_COUNT ? SDF_CLIPMAP_LEVEL_COUNT - 1 : level;
				s_short_range = 2.0 * GiWorldProbeSpacing(clamped);
				s_anchor_uv = uv;
				s_ss_origin = vec3(uv, depth);
				s_vs_origin = HizComputeViewspacePosition(uv, depth);
				s_valid = 1.0;
				b_gi_probes[record + uint(GI_PROBE_META)] = vec4(world_position, 1.0);
				b_gi_probes[record + uint(GI_PROBE_META2)] =
				    vec4(world_normal, length(world_position - u_gi_v2_camera.xyz));
				// Reproject the anchor into LAST frame's lattice for the importance mip. A
				// failed or plane-rejected reprojection just means uniform allocation this
				// frame - importance is an optimisation, never a correctness dependency.
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
						   plane < 0.05 * max(length(world_position - u_gi_v2_camera.xyz), 0.1))
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
			}
		}
		if(s_valid < 0.5)
		{
			b_gi_probes[record + uint(GI_PROBE_META)] = vec4_splat(0.0);
			b_gi_probes[record + uint(GI_PROBE_META2)] = vec4_splat(0.0);
		}
	}
	barrier();
	ivec2 texel = GiProbeAtlasBase(probe.x, probe.y, 0) + local;
	if(s_valid < 0.5)
	{
		imageStore(s_probe_radiance_out, texel, vec4(0.0, 0.0, 0.0, -1.0));
		return;
	}
	// Fixed, world-anchored directions: texel centres of the shared octahedral parameterisation.
	// Deterministic per frame; the direction jitter arrives with importance sampling (Phase 6).
	vec2 tile_uv = (vec2(local.xy) + vec2_splat(0.5)) / float(GI_PROBE_DIR_EDGE);
	vec3 direction = GiOctDecode(tile_uv);
	// The hemisphere cap below the anchor's tangent plane cannot carry irradiance; skipped with
	// a small tolerance so grazing directions still trace (the integration weights by cosine).
	if(dot(direction, s_anchor_normal) < -0.2)
	{
		imageStore(s_probe_radiance_out, texel, vec4(0.0, 0.0, 0.0, -1.0));
		return;
	}
	// IMPORTANCE-DRIVEN SUPERSAMPLING: a cone whose reprojected history reads brighter than
	// GI_V2_IMPORTANCE_SUPERSAMPLE_RATIO x the probe mean gets a second sub-cone sample - the
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
		if(importance > GI_V2_IMPORTANCE_SUPERSAMPLE_RATIO * s_importance_mean)
		{
			sample_count = 2;
		}
	}
	vec2 sub_positions[2];
	sub_positions[0] = vec2(0.5, 0.5);
	sub_positions[1] = vec2(0.25, 0.25);
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
				bool marched = HizHierarchicalRaymarch(s_hiz, s_ss_origin, ss_dir, screen_size, 0,
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
								// Occluded but unmeasured: honest darkness. For sub-voxel
								// detail (railings, awning cloth) this IS the contact
								// occlusion the voxel tier cannot express.
								radiance = vec3_splat(0.0);
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
				if(!GiLightVoxelRead(hit_position, hit_normal, radiance))
				{
					// Occluded but unmeasured: honest darkness (the sealed-room branch).
					radiance = vec3_splat(0.0);
				}
			}
			else
			{
				// Completion: the world probes carry everything beyond the short range - scene
				// AND sky.
				if(!GiWorldProbeRadiance(s_origin + sample_dir * s_short_range, sample_dir,
				                         u_gi_v2_camera.xyz, radiance))
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
	vec3 averaged = min(radiance_sum / float(sample_count), vec3_splat(GI_MAX_RAY_RADIANCE));
	imageStore(s_probe_radiance_out, texel, vec4(averaged, hit_t));
}
