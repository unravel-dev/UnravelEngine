/*
 * Screen probe gather, composite. One group per probe, one thread per ray (GI_PROBE_TRACE_RES^2).
 *
 * Each ray's radiance, its max channel clamped to GI_MAX_RAY_INTENSITY (pre-exposed, stateless), is
 * re-binned to ONE of the 2x2 octahedral texels around its jittered direction by a blue-noise bilinear
 * dither independent of the ray jitter (GiProbeRebinDither): the stored texel then stands for its centre
 * direction whatever this frame's ray jitter, which
 * is what lets the filter mix texels of neighbouring probes and the SH projection use texel centres.
 * A texel holds the SUM of the rays that landed in it (all rays shift together, so most texels receive
 * exactly one); a texel no ray reached stores hit distance -1, which the filter reads as "no sample".
 *
 * The probe's moving fraction: the solid angle share of its rays the trace marked moving (GiEncodeTraceDistance),
 * read by the filter and the temporal.
 *
 * Writes rgb = radiance, a = the smallest hit distance of the texel's rays, or -1; and the probe's moving fraction.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "gi/gi_common.sh"

SAMPLER2D(s_gi_trace_radiance, 0);
SAMPLER2D(s_gi_probe_records, 1);
IMAGE2D_WO(i_gi_probe_radiance, rgba16f, 2);
/// The probes' importance-sampled ray slots (cs_gi_probe_generate_rays.sc).
SAMPLER2D(s_gi_ray_info, 3);
/// One texel per probe: the fraction of its rays that hit a moving surface.
IMAGE2D_WO(i_gi_probe_moving, r8, 4);

#define GI_PROBE_TEXELS (GI_PROBE_TRACE_RES * GI_PROBE_TRACE_RES)
/// Fixed-point units per unit radiance: all of a probe's rays at the clamp sum to exactly 2^32 - 1.
#define GI_COMPOSITE_FIXED_SCALE (4294967295.0 / (float(GI_PROBE_TEXELS) * GI_MAX_RAY_INTENSITY))
/// Bit pattern of a hit distance no ray reaches (f16 max, 65504).
#define GI_COMPOSITE_NO_HIT 0x477FE000u
/// Fixed-point units of a moving ray's solid angle: a refined ray (a quarter texel) counts 1, a full texel 4.
#define GI_COMPOSITE_MOVING_UNITS 4.0

SHARED uint s_acc_r[GI_PROBE_TEXELS];
SHARED uint s_acc_g[GI_PROBE_TEXELS];
SHARED uint s_acc_b[GI_PROBE_TEXELS];
SHARED uint s_acc_hit[GI_PROBE_TEXELS];
SHARED uint s_acc_rays[GI_PROBE_TEXELS];
SHARED uint s_acc_moving;

/// The texel (0..GI_PROBE_TRACE_RES - 1 per axis) a ray traced at @p texel + @p jitter of a @p resolution map
/// lands in: floor or ceil of its continuous position in the probe's map per axis, picked by @p dither against the
/// fraction, wrapped across the octahedron's folds.
ivec2 GiRebinTexel(ivec2 texel, int resolution, vec2 jitter, vec2 dither)
{
	vec2 position = (vec2(texel) + jitter) * (float(GI_PROBE_TRACE_RES) / float(resolution)) - 0.5;
	vec2 base = floor(position);
	vec2 target = base + step(dither, position - base);
	return GiOctahedralMapWrapBorder(ivec2(target) + ivec2(1, 1), GI_PROBE_TRACE_RES + 2, 1);
}

NUM_THREADS(GI_PROBE_TRACE_RES, GI_PROBE_TRACE_RES, 1)
void main()
{
	ivec2 tile = ivec2(gl_WorkGroupID.xy);
	ivec2 texel = ivec2(gl_LocalInvocationID.xy);
	int index = texel.y * GI_PROBE_TRACE_RES + texel.x;
	ivec2 atlas_texel = tile * GI_PROBE_TRACE_RES + texel;
	// Barriers stay in uniform flow control: an invalid probe guards its work instead of returning.
	vec4 record = texelFetch(s_gi_probe_records, tile, 0);
	bool valid = record.x > 0.0;
	s_acc_r[index] = 0u;
	s_acc_g[index] = 0u;
	s_acc_b[index] = 0u;
	s_acc_hit[index] = GI_COMPOSITE_NO_HIT;
	s_acc_rays[index] = 0u;
	if(index == 0)
	{
		s_acc_moving = 0u;
	}
	barrier();
	BRANCH
	if(valid)
	{
		vec4 ray = texelFetch(s_gi_trace_radiance, atlas_texel, 0);
		ivec3 slot = GiProbeRaySlot(texel, texelFetch(s_gi_ray_info, atlas_texel, 0).x);
		int resolution = GiRayResolution(slot.z);
		// A refined ray covers a quarter of a texel's solid angle.
		float slot_weight = float(GI_PROBE_TRACE_RES * GI_PROBE_TRACE_RES) / float(resolution * resolution);
		vec3 radiance = max(ray.xyz, vec3_splat(0.0)) * slot_weight;
		float peak = max(radiance.x, max(radiance.y, radiance.z));
		if(peak > GI_MAX_RAY_INTENSITY)
		{
			radiance *= GI_MAX_RAY_INTENSITY / peak;
		}
		ivec2 screen_tile = GiProbeScreenTile(tile, record);
		vec2 jitter = GiProbeRayJitter(screen_tile, u_gi_frame_mod);
		vec2 dither = u_gi_jitter_slice_dither
		                  ? GiProbeRayJitter(screen_tile, u_gi_frame_mod + float(GI_COMPOSITE_DITHER_SLICE_OFFSET))
		                  : GiProbeRebinDither(screen_tile, u_gi_frame_mod);
		ivec2 target = GiRebinTexel(slot.xy, resolution, jitter, dither);
		int target_index = target.y * GI_PROBE_TRACE_RES + target.x;
		atomicAdd(s_acc_r[target_index], uint(radiance.x * GI_COMPOSITE_FIXED_SCALE));
		atomicAdd(s_acc_g[target_index], uint(radiance.y * GI_COMPOSITE_FIXED_SCALE));
		atomicAdd(s_acc_b[target_index], uint(radiance.z * GI_COMPOSITE_FIXED_SCALE));
		atomicMin(s_acc_hit[target_index], floatBitsToUint(max(GiTraceDistance(ray.w), 0.0)));
		atomicAdd(s_acc_rays[target_index], 1u);
		if(ray.w < 0.0)
		{
			atomicAdd(s_acc_moving, uint(slot_weight * GI_COMPOSITE_MOVING_UNITS + 0.5));
		}
	}
	barrier();
	if(index == 0)
	{
		float moving = float(s_acc_moving) / (GI_COMPOSITE_MOVING_UNITS * float(GI_PROBE_TEXELS));
		imageStore(i_gi_probe_moving, tile, vec4(moving, 0.0, 0.0, 0.0));
	}
	vec3 sum = vec3(float(s_acc_r[index]), float(s_acc_g[index]), float(s_acc_b[index])) *
	           (1.0 / GI_COMPOSITE_FIXED_SCALE);
	float hit_distance = s_acc_rays[index] > 0u ? uintBitsToFloat(s_acc_hit[index]) : -1.0;
	imageStore(i_gi_probe_radiance, atlas_texel, vec4(sum, hit_distance));
}
