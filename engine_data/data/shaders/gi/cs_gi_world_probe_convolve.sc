/*
 * Convolves each world probe's 16x16 radiance atlas into its 8x8 irradiance tile and 8x8 depth
 * moment tile, gutters included - one thread group per probe. Runs every frame over every
 * probe: the radiance atlas is the windowed mean, so this is the one place its integral
 * materialises, and doing it unconditionally is what makes the result exactly as stable as the
 * atlas itself.
 *
 *  - Irradiance: E(n)/pi = sum(L_d * max(0, n . w_d)) / (N / 4) - equal-solid-angle octahedral
 *    texels (the same identity the screen probe filter uses). Sky rides in alpha: the
 *    cosine-weighted fraction of the lobe that escaped, which is what lets a consumer split
 *    "measured scene energy" from "sky the environment term already covers".
 *  - Depth: mean and mean^2 of hitT under a cos^GI_WORLD_PROBE_DEPTH_SHARPNESS lobe [RTXGI],
 *    misses clamped to GI_WORLD_PROBE_DEPTH_CLAMP spacings - the moments Chebyshev asks for.
 *  - Gutter: the 1-texel octahedral mirror border that makes hardware bilinear correct at tile
 *    edges (DDGI's border-copy map, computed rather than table-driven).
 *
 * GROUP SHAPE AND STAGING. 8x8 = 64 threads, every lane an interior texel through the heavy
 * phase - the old 10x10 group carried 36 lanes that idled at the barrier while 64 worked,
 * padding the group to 4 half-busy warps. The 256 radiance texels and their 256 decoded
 * directions are staged into shared memory ONCE by the group (4 per thread) instead of each
 * of the 64 threads fetching and decoding all 256 privately - that was 16,384 fetches and
 * 16,384 normalize()s per group for 256 distinct values of each. The gutter is written after
 * the barrier by the first 36 threads from the shared interior results, exactly as before.
 */

#include "bgfx_compute.sh"
#include "gi/sdf_common.sh"
#include "gi/gi_world_probes.sh"

// Stages chosen clear of sdf_common.sh's fixed set (0-4 samplers/buffers, 12-13 grid).
SAMPLER2D(s_world_probe_radiance, 11);
IMAGE2D_WO(s_world_probe_irradiance_out, rgba16f, 5);
IMAGE2D_WO(s_world_probe_depth_out, rg16f, 6);

#define GUTTER_EDGE (GI_WORLD_PROBE_OCT_IRRADIANCE + 2)
#define RADIANCE_TEXELS (GI_WORLD_PROBE_OCT_RADIANCE * GI_WORLD_PROBE_OCT_RADIANCE)

SHARED vec4 s_irradiance[GI_WORLD_PROBE_OCT_IRRADIANCE * GI_WORLD_PROBE_OCT_IRRADIANCE];
SHARED vec2 s_depth[GI_WORLD_PROBE_OCT_IRRADIANCE * GI_WORLD_PROBE_OCT_IRRADIANCE];
SHARED vec4 s_radiance[RADIANCE_TEXELS];
SHARED vec3 s_sample_dir[RADIANCE_TEXELS];
/// Per-texel solid angle (GiOctTexelSolidAngle): the octahedral map is not equal-area, and
/// both the irradiance sum and the depth lobe weight by it.
SHARED float s_sample_omega[RADIANCE_TEXELS];

NUM_THREADS(8, 8, 1)
void main()
{
	int slot_linear = int(gl_WorkGroupID.x);
	int per_level = GI_WORLD_PROBE_AXIS * GI_WORLD_PROBE_AXIS * GI_WORLD_PROBE_AXIS;
	int level = slot_linear / per_level;
	if(level >= SDF_CLIPMAP_LEVEL_COUNT)
	{
		return;
	}
	int in_level = slot_linear % per_level;
	ivec3 slot = ivec3(in_level % GI_WORLD_PROBE_AXIS,
	                   (in_level / GI_WORLD_PROBE_AXIS) % GI_WORLD_PROBE_AXIS,
	                   in_level / (GI_WORLD_PROBE_AXIS * GI_WORLD_PROBE_AXIS));
	ivec2 local = ivec2(gl_LocalInvocationID.xy);
	int lane = local.y * GI_WORLD_PROBE_OCT_IRRADIANCE + local.x;
	ivec2 radiance_tile = GiWorldProbeTileBase(slot, level, GI_WORLD_PROBE_OCT_RADIANCE);
	ivec2 out_tile = GiWorldProbeTileBase(slot, level, GUTTER_EDGE);
	float depth_clamp = GI_WORLD_PROBE_DEPTH_CLAMP * GiWorldProbeSpacing(level);
	// Cooperative stage: 4 radiance texels + 4 direction decodes per thread.
	UNROLL
	for(int chunk = 0; chunk < RADIANCE_TEXELS / 64; ++chunk)
	{
		int d = lane + chunk * 64;
		ivec2 offset = ivec2(d % GI_WORLD_PROBE_OCT_RADIANCE, d / GI_WORLD_PROBE_OCT_RADIANCE);
		s_radiance[d] = texelFetch(s_world_probe_radiance, radiance_tile + offset, 0);
		s_sample_dir[d] =
		    GiOctDecode((vec2(offset) + vec2_splat(0.5)) / float(GI_WORLD_PROBE_OCT_RADIANCE));
		s_sample_omega[d] = GiOctTexelSolidAngle(offset, GI_WORLD_PROBE_OCT_RADIANCE);
	}
	barrier();
	ivec2 texel = local;
	vec3 normal = GiOctDecode((vec2(texel) + vec2_splat(0.5)) / float(GI_WORLD_PROBE_OCT_IRRADIANCE));
	vec3 irradiance = vec3_splat(0.0);
	float sky = 0.0;
	float cos_omega_sum = 0.0;
	float depth_mean = 0.0;
	float depth_mean_sq = 0.0;
	float depth_weight = 0.0;
	// The depth lobe is numerically dead outside its ~34-degree cone: cos^50 < 1e-4 below
	// cos = 0.829, under RG16F's own precision, so those terms are skipped. The irradiance
	// cosine sum is untouched. Every term carries the texel's solid angle (the octahedral map
	// is not equal-area) and the irradiance normalises by sum(cos x omega): a uniform field
	// integrates exactly (the equal-weight sum over N/4 under-counted by ~5% at 16x16).
	LOOP
	for(int d = 0; d < RADIANCE_TEXELS; ++d)
	{
		vec4 sample_value = s_radiance[d];
		float cosine = max(dot(normal, s_sample_dir[d]), 0.0);
		if(cosine <= 0.0)
		{
			continue;
		}
		float omega = s_sample_omega[d];
		float cos_omega = cosine * omega;
		irradiance += sample_value.xyz * cos_omega;
		// The trace stores the CLAMPED depth (a miss stores the clamp itself), so sky is
		// "at the clamp"; a never-measured texel holds 0.
		sky += (sample_value.w >= 0.999 * depth_clamp ? 1.0 : 0.0) * cos_omega;
		cos_omega_sum += cos_omega;
		if(cosine > 0.829)
		{
			// Exact repeat-squaring for the shipped exponent (50 = 32 + 16 + 2); any other
			// value folds to the general pow at compile time.
			float lobe;
			if(GI_WORLD_PROBE_DEPTH_SHARPNESS == 50.0)
			{
				float c2 = cosine * cosine;
				float c4 = c2 * c2;
				float c8 = c4 * c4;
				float c16 = c8 * c8;
				float c32 = c16 * c16;
				lobe = c32 * c16 * c2;
			}
			else
			{
				lobe = pow(cosine, GI_WORLD_PROBE_DEPTH_SHARPNESS);
			}
			float depth = min(sample_value.w, depth_clamp);
			lobe *= omega;
			depth_mean += depth * lobe;
			depth_mean_sq += depth * depth * lobe;
			depth_weight += lobe;
		}
	}
	float norm = max(cos_omega_sum, 1e-6);
	vec4 result = vec4(irradiance / norm, sky / norm);
	s_irradiance[lane] = result;
	vec2 moments = depth_weight > 1e-6 ? vec2(depth_mean, depth_mean_sq) / depth_weight
	                                   : vec2(depth_clamp, depth_clamp * depth_clamp);
	s_depth[lane] = moments;
	// Interior texels sit at +1 inside the guttered tile.
	imageStore(s_world_probe_irradiance_out, out_tile + texel + ivec2(1, 1), result);
	imageStore(s_world_probe_depth_out, out_tile + texel + ivec2(1, 1), vec4(moments, 0.0, 0.0));
	barrier();
	// Octahedral gutter: the first 36 threads write the 10x10 border from the shared interior
	// results. Crossing a tile edge lands on the mirrored interior texel (transverse axis
	// flipped); corners map to the diagonally opposite corner - the same wrap as before.
	if(lane < 36)
	{
		ivec2 border;
		if(lane < 10)
		{
			border = ivec2(lane, 0);
		}
		else if(lane < 20)
		{
			border = ivec2(lane - 10, GUTTER_EDGE - 1);
		}
		else if(lane < 28)
		{
			border = ivec2(0, lane - 20 + 1);
		}
		else
		{
			border = ivec2(GUTTER_EDGE - 1, lane - 28 + 1);
		}
		ivec2 wrapped = border - ivec2(1, 1);
		if(wrapped.x < 0)
		{
			wrapped.x = 0;
			wrapped.y = GI_WORLD_PROBE_OCT_IRRADIANCE - 1 - wrapped.y;
		}
		else if(wrapped.x >= GI_WORLD_PROBE_OCT_IRRADIANCE)
		{
			wrapped.x = GI_WORLD_PROBE_OCT_IRRADIANCE - 1;
			wrapped.y = GI_WORLD_PROBE_OCT_IRRADIANCE - 1 - wrapped.y;
		}
		if(wrapped.y < 0)
		{
			wrapped.y = 0;
			wrapped.x = GI_WORLD_PROBE_OCT_IRRADIANCE - 1 - wrapped.x;
		}
		else if(wrapped.y >= GI_WORLD_PROBE_OCT_IRRADIANCE)
		{
			wrapped.y = GI_WORLD_PROBE_OCT_IRRADIANCE - 1;
			wrapped.x = GI_WORLD_PROBE_OCT_IRRADIANCE - 1 - wrapped.x;
		}
		wrapped.x = clamp(wrapped.x, 0, GI_WORLD_PROBE_OCT_IRRADIANCE - 1);
		wrapped.y = clamp(wrapped.y, 0, GI_WORLD_PROBE_OCT_IRRADIANCE - 1);
		int index = wrapped.y * GI_WORLD_PROBE_OCT_IRRADIANCE + wrapped.x;
		imageStore(s_world_probe_irradiance_out, out_tile + border, s_irradiance[index]);
		imageStore(s_world_probe_depth_out, out_tile + border, vec4(s_depth[index], 0.0, 0.0));
	}
}
