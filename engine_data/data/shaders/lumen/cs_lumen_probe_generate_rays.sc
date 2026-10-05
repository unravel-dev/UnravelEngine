/*
 * Lumen screen probe gather, structured importance sampling: UE 5.8 ScreenProbeGatherScreenDataCS
 * (LumenScreenProbeGatherScreenData.usf:77-310), ScreenProbeComputeLightingProbabilityDensityFunctionCS
 * (LumenScreenProbeImportanceSampling.usf:36-193) and ScreenProbeGenerateRaysCS (:306-495), fused into one
 * group per probe of one thread per octahedral texel, N x N = LUMEN_PROBE_TRACE_RES^2 (an adaptive probe's
 * footprint is centred on its own pixel):
 *  1. BRDF PDF: the mean cosine lobe (SH3) of the 8x8 footprint pixels around the probe that lie on its
 *     plane - the pixels that will read it - and the disocclusion flag: at least 40% of them have fewer than
 *     4 frames of history. The footprint is 8x8 at every resolution: each thread takes its share of it.
 *  2. Lighting PDF per octahedral texel: last frame's filtered radiance of the 2x2 history probes around
 *     the probe's reprojection (a moving surface's from where it was, lumen_motion.sh) that lie on its plane,
 *     completed from the radiance cache where history is missing.
 *  3. Rays: texel PDF = BRDF x lighting x N^2; texels with a BRDF PDF of at least LUMEN_IS_MIN_PDF_TO_TRACE
 *     keep at least that. The N^2 texels are rank-sorted; every three lowest below the threshold are given
 *     to the highest remaining texel, which then traces four rays at the 2N x 2N level instead of one.
 * Writes the probe's N^2 ray slots (texel x | y << 6 | level << 12; level 1 = N x N texel, 0 = 2N x 2N) and the
 * probe's disocclusion flag.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "lumen/lumen_common.sh"
#include "lumen/lumen_radiance_cache_common.sh"
#include "gi/gi_pre_exposure.sh"

IMAGE2D_WO(i_lumen_ray_info, r32f, 0);
IMAGE2D_WO(i_lumen_screen_data, rgba16f, 1);
SAMPLER2D(s_lumen_depth, 2);
SAMPLER2D(s_lumen_normal, 3);
SAMPLER2D(s_lumen_probe_records, 4);
/// Last frame's probe records and final filtered probe radiance.
SAMPLER2D(s_lumen_history_records, 5);
SAMPLER2D(s_lumen_history_radiance, 6);
BUFFER_RO(b_lumen_rc_indirection, uint, 7);
SAMPLER2D(s_lumen_rc_final, 8);
/// Last frame's per-pixel history and device depth (the disocclusion test).
SAMPLER2D(s_lumen_history, 10);
SAMPLER2D(s_lumen_prev_depth, 11);

/// This frame's velocity buffer (where moving surfaces were last frame).
#define LUMEN_VELOCITY_STAGE 9
#include "lumen/lumen_history.sh"
#include "lumen/lumen_radiance_cache_sample.sh"

/// x > 0 when last frame's probes are valid history, y > 0 when the per-pixel history is, z > 0 when the
/// radiance cache was updated this frame; w unused.
uniform vec4 u_lumen_ray_gen;
/// xy = last frame's probe placement jitter in pixels.
uniform vec4 u_lumen_prev_probe;

#define u_lumen_probe_history (u_lumen_ray_gen.x > 0.0)
#define u_lumen_pixel_history (u_lumen_ray_gen.y > 0.0)
#define u_lumen_cache_valid   (u_lumen_ray_gen.z > 0.0)

#define LUMEN_PROBE_TEXELS (LUMEN_PROBE_TRACE_RES * LUMEN_PROBE_TRACE_RES)
/// The footprint's pixels per axis (UE gathers 64 pixels per probe whatever its tracing resolution).
#define LUMEN_FOOTPRINT_EDGE 8
#define LUMEN_FOOTPRINT_SAMPLES (LUMEN_FOOTPRINT_EDGE * LUMEN_FOOTPRINT_EDGE)
/// Reduced per thread: 9 SH3 coefficients, the sample count, the young-sample count.
#define LUMEN_REDUCE_STRIDE 11

SHARED float s_reduce[LUMEN_PROBE_TEXELS * LUMEN_REDUCE_STRIDE];
SHARED float s_sum[LUMEN_PROBE_TEXELS];
SHARED float s_pdf[LUMEN_PROBE_TEXELS];
SHARED uint s_ray[LUMEN_PROBE_TEXELS];
SHARED float s_sorted_pdf[LUMEN_PROBE_TEXELS];
SHARED uint s_sorted_ray[LUMEN_PROBE_TEXELS];
SHARED uint s_num_subdivide;

void LumenStoreFootprint(int index, LumenSH3 sh, float count, float young)
{
	int base = index * LUMEN_REDUCE_STRIDE;
	s_reduce[base + 0] = sh.v0.x;
	s_reduce[base + 1] = sh.v0.y;
	s_reduce[base + 2] = sh.v0.z;
	s_reduce[base + 3] = sh.v0.w;
	s_reduce[base + 4] = sh.v1.x;
	s_reduce[base + 5] = sh.v1.y;
	s_reduce[base + 6] = sh.v1.z;
	s_reduce[base + 7] = sh.v1.w;
	s_reduce[base + 8] = sh.v2;
	s_reduce[base + 9] = count;
	s_reduce[base + 10] = young;
}

/// One footprint sample's contribution (UE ScreenData): x = 1 when the pixel lies on the probe's plane
/// (or is the probe's own), y = 1 when it is young, and its normal for the BRDF SH. @p footprint_texel is the
/// sample's place in the 8x8 footprint.
vec4 LumenFootprintSample(ivec2 probe_pixel, vec3 probe_position, float probe_depth, ivec2 footprint_texel)
{
	bool center = footprint_texel.x == LUMEN_FOOTPRINT_EDGE / 2 && footprint_texel.y == LUMEN_FOOTPRINT_EDGE / 2;
	vec2 offset = center ? vec2_splat(0.0)
	                     : ((vec2(footprint_texel) + 0.5) / float(LUMEN_FOOTPRINT_EDGE) * 2.0 - 1.0) * u_lumen_downsample;
	ivec2 pixel = clamp(probe_pixel + ivec2(offset), ivec2(0, 0), ivec2(u_lumen_view_size) - ivec2(1, 1));
	float depth01 = texelFetch(s_lumen_depth, pixel, 0).x;
	if(depth01 >= 1.0)
	{
		return vec4_splat(0.0);
	}
	vec3 position = LumenWorldFromDepth(LumenPixelUv(pixel), depth01);
	vec3 normal = decodeNormalOctahedron(texelFetch(s_lumen_normal, pixel, 0).xy);
	float plane_distance = abs(dot(probe_position - position, normal)) / probe_depth;
	if(!center && exp2(-LUMEN_INTERP_DEPTH_WEIGHT * plane_distance * plane_distance) <= LUMEN_INTERP_MIN_WEIGHT)
	{
		return vec4_splat(0.0);
	}
	float young = 1.0;
	if(u_lumen_pixel_history)
	{
		young = LumenReadHistory(pixel, position, normal).w < float(LUMEN_IS_DISOCCLUSION_MAX_FRAMES) ? 1.0 : 0.0;
	}
	return vec4(1.0, young, encodeNormalOctahedron(normal));
}

/// Last frame's world position of a history probe from its record.
vec3 LumenHistoryProbePosition(vec4 record)
{
	vec2 uv = LumenPixelUv(LumenProbeRecordPixel(record));
	return clipToWorld(u_lumen_prev_inv_view_proj, clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(record.w))));
}

/// The incoming radiance the lighting PDF uses for one texel (pre-exposed), for a probe at @p position this frame
/// and @p prev_position last frame, completed from the radiance cache @p clipmap where history is missing.
vec3 LumenLightingPrior(ivec2 texel, vec3 position, vec3 prev_position, vec3 normal, float depth, vec3 direction,
                        int clipmap)
{
	vec3 lighting = vec3_splat(0.0);
	float transparency = 1.0;
	vec4 prev_clip = mul(u_lumen_prev_view_proj, vec4(prev_position, 1.0));
	BRANCH
	if(u_lumen_probe_history && prev_clip.w > 0.0)
	{
		vec2 history_uv = clipToUv((prev_clip.xy / prev_clip.w) * 0.5 + 0.5);
		if(all(greaterThanEqual(history_uv, vec2_splat(0.0))) && all(lessThanEqual(history_uv, vec2_splat(1.0))))
		{
			vec2 tile_coord = (history_uv * u_lumen_view_size - u_lumen_prev_probe.xy) / u_lumen_downsample;
			ivec2 base = ivec2(floor(tile_coord));
			vec3 sum = vec3_splat(0.0);
			float weight_sum = 0.0;
			for(int corner = 0; corner < 4; ++corner)
			{
				ivec2 tile = clamp(base + ivec2(corner & 1, corner >> 1), ivec2(0, 0), u_lumen_probe_count - ivec2(1, 1));
				vec4 record = texelFetch(s_lumen_history_records, tile, 0);
				if(record.x <= 0.0)
				{
					continue;
				}
				float plane_distance = abs(dot(LumenHistoryProbePosition(record) - prev_position, normal)) / depth;
				if(exp2(-LUMEN_INTERP_DEPTH_WEIGHT * plane_distance * plane_distance) > LUMEN_IS_HISTORY_MIN_WEIGHT)
				{
					sum += texelFetch(s_lumen_history_radiance, tile * LUMEN_PROBE_TRACE_RES + texel, 0).xyz;
					weight_sum += 1.0;
				}
			}
			if(weight_sum > 0.0)
			{
				lighting = sum / weight_sum * u_history_pre_exposure_correction;
			}
			transparency = 1.0 - saturate(weight_sum / 4.0);
		}
	}
	if(transparency > 0.0)
	{
		if(clipmap < LUMEN_RADIANCE_CACHE_CLIPMAPS)
		{
			lighting += GiCachedToView(LumenRcSampleInterpolated(position, direction, clipmap).radiance) * transparency;
		}
		else
		{
			lighting = vec3_splat(1.0);
		}
	}
	return lighting;
}

/// Sums s_sum over the group (tree reduction); the result is in s_sum[0] after the last barrier.
void LumenReduceSum(int index)
{
	for(int stride = LUMEN_PROBE_TEXELS / 2; stride > 0; stride >>= 1)
	{
		if(index < stride)
		{
			s_sum[index] += s_sum[index + stride];
		}
		barrier();
	}
}

NUM_THREADS(LUMEN_PROBE_TRACE_RES, LUMEN_PROBE_TRACE_RES, 1)
void main()
{
	ivec2 tile = ivec2(gl_WorkGroupID.xy);
	ivec2 local = ivec2(gl_LocalInvocationID.xy);
	int index = local.y * LUMEN_PROBE_TRACE_RES + local.x;
	// Barriers stay in uniform flow control: an invalid probe runs the same steps on neutral values.
	vec4 record = texelFetch(s_lumen_probe_records, tile, 0);
	bool valid = record.x > 0.0;
	ivec2 probe_pixel = LumenProbeRecordPixel(record);
	float probe_depth = max(record.x, 1e-4);
	vec3 probe_position = LumenWorldFromDepth(LumenPixelUv(probe_pixel), record.w);
	vec3 probe_normal = LumenProbeNormal(record);
	// This thread's footprint samples: one at 8 x 8, four at 4 x 4, the first 64 threads' at 16 x 16.
	LumenSH3 lobe;
	lobe.v0 = vec4_splat(0.0);
	lobe.v1 = vec4_splat(0.0);
	lobe.v2 = 0.0;
	float footprint_count = 0.0;
	float footprint_young = 0.0;
	BRANCH
	if(valid)
	{
		for(int footprint_index = index; footprint_index < LUMEN_FOOTPRINT_SAMPLES; footprint_index += LUMEN_PROBE_TEXELS)
		{
			ivec2 footprint_texel = ivec2(footprint_index % LUMEN_FOOTPRINT_EDGE, footprint_index / LUMEN_FOOTPRINT_EDGE);
			vec4 footprint = LumenFootprintSample(probe_pixel, probe_position, probe_depth, footprint_texel);
			LumenSH3 sample_lobe = LumenDiffuseTransferSH3(decodeNormalOctahedron(footprint.zw));
			lobe.v0 += sample_lobe.v0 * footprint.x;
			lobe.v1 += sample_lobe.v1 * footprint.x;
			lobe.v2 += sample_lobe.v2 * footprint.x;
			footprint_count += footprint.x;
			footprint_young += footprint.x * footprint.y;
		}
	}
	LumenStoreFootprint(index, lobe, footprint_count, footprint_young);
	barrier();
	for(int stride = LUMEN_PROBE_TEXELS / 2; stride > 0; stride >>= 1)
	{
		if(index < stride)
		{
			for(int k = 0; k < LUMEN_REDUCE_STRIDE; ++k)
			{
				s_reduce[index * LUMEN_REDUCE_STRIDE + k] += s_reduce[(index + stride) * LUMEN_REDUCE_STRIDE + k];
			}
		}
		barrier();
	}
	float count = max(s_reduce[9], 1.0);
	LumenSH3 brdf;
	brdf.v0 = vec4(s_reduce[0], s_reduce[1], s_reduce[2], s_reduce[3]) / count;
	brdf.v1 = vec4(s_reduce[4], s_reduce[5], s_reduce[6], s_reduce[7]) / count;
	brdf.v2 = s_reduce[8] / count;
	if(index == 0)
	{
		float disoccluded = s_reduce[10] >= s_reduce[9] * LUMEN_IS_DISOCCLUSION_FRACTION && s_reduce[9] > 0.0 ? 1.0 : 0.0;
		imageStore(i_lumen_screen_data, tile, vec4(disoccluded, 0.0, 0.0, 0.0));
	}
	vec3 texel_direction = LumenEquiAreaSphericalMapping((vec2(local) + 0.5) / float(LUMEN_PROBE_TRACE_RES));
	float brdf_pdf = max(LumenDotSH3(brdf, LumenSHBasis3(texel_direction)), 0.0);
	vec2 jitter = LumenProbeRayJitter(LumenProbeScreenTile(tile, record), u_lumen_frame_mod);
	vec3 ray_direction = LumenEquiAreaSphericalMapping((vec2(local) + jitter) / float(LUMEN_PROBE_TRACE_RES));
	float lighting = 0.0;
	BRANCH
	if(valid)
	{
		int cache_clipmap = u_lumen_cache_valid ? LumenRcClipmapOf(probe_position) : LUMEN_RADIANCE_CACHE_CLIPMAPS;
		vec3 prev_probe_position = LumenPrevWorldPosition(LumenPixelUv(probe_pixel), probe_position);
		vec3 prior = LumenLightingPrior(local, probe_position, prev_probe_position, probe_normal, probe_depth,
		                                ray_direction, cache_clipmap);
		lighting = dot(prior, vec3(0.2126, 0.7152, 0.0722));
	}
	s_sum[index] = lighting;
	barrier();
	LumenReduceSum(index);
	float lighting_pdf = lighting / max(s_sum[0], LUMEN_IS_MIN_LIGHTING_SUM);
	float pdf = brdf_pdf * lighting_pdf * float(LUMEN_PROBE_TEXELS);
	if(brdf_pdf >= LUMEN_IS_MIN_PDF_TO_TRACE)
	{
		pdf = max(pdf, LUMEN_IS_MIN_PDF_TO_TRACE);
	}
	s_pdf[index] = pdf;
	s_ray[index] = LumenPackRay(local, 1);
	if(index == 0)
	{
		s_num_subdivide = 0u;
	}
	barrier();
	int rank = 0;
	for(int other = 0; other < LUMEN_PROBE_TEXELS; ++other)
	{
		float other_pdf = s_pdf[other];
		rank += (other_pdf < pdf || (other_pdf == pdf && other < index)) ? 1 : 0;
	}
	s_sorted_pdf[rank] = pdf;
	s_sorted_ray[rank] = s_ray[index];
	barrier();
	int triple = index / 3;
	int member = index - triple * 3;
	int refine_index = max(LUMEN_PROBE_TEXELS - triple - 1, 0);
	int last_of_triple = triple * 3 + 2;
	uint ray = s_sorted_ray[index];
	if(last_of_triple < refine_index && s_sorted_pdf[last_of_triple] < LUMEN_IS_MIN_PDF_TO_TRACE)
	{
		ivec3 parent = LumenUnpackRay(s_sorted_ray[refine_index]);
		ray = LumenPackRay(parent.xy * 2 + ivec2((member + 1) % 2, (member + 1) / 2), parent.z - 1);
		if(member == 0)
		{
			atomicAdd(s_num_subdivide, 1u);
		}
	}
	barrier();
	s_sorted_ray[index] = ray;
	barrier();
	if(uint(index) < s_num_subdivide)
	{
		ivec3 parent = LumenUnpackRay(s_sorted_ray[LUMEN_PROBE_TEXELS - 1 - index]);
		s_sorted_ray[LUMEN_PROBE_TEXELS - 1 - index] = LumenPackRay(parent.xy * 2, parent.z - 1);
	}
	barrier();
	imageStore(i_lumen_ray_info, tile * LUMEN_PROBE_TRACE_RES + local, vec4(float(s_sorted_ray[index]), 0.0, 0.0, 0.0));
}
