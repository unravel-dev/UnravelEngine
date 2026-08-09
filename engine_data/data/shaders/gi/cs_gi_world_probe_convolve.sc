/*
 * Convolves each world probe's 16x16 radiance atlas into its 8x8 irradiance tile and 8x8 depth
 * moment tile, gutters included - one thread group per probe, one thread per GUTTERED texel
 * (10x10). Runs every frame over every probe: the radiance atlas is the windowed mean, so this
 * is the one place its integral materialises, and doing it unconditionally is what makes the
 * result exactly as stable as the atlas itself.
 *
 *  - Irradiance: E(n)/pi = sum(L_d * max(0, n . w_d)) / (N / 4) - equal-solid-angle octahedral
 *    texels (the same identity the screen probe filter uses). Sky rides in alpha: the
 *    cosine-weighted fraction of the lobe that escaped, which is what lets a consumer split
 *    "measured scene energy" from "sky the environment term already covers".
 *  - Depth: mean and mean^2 of hitT under a cos^GI_WORLD_PROBE_DEPTH_SHARPNESS lobe [RTXGI],
 *    misses clamped to GI_WORLD_PROBE_DEPTH_CLAMP spacings - the moments Chebyshev asks for.
 *  - Gutter: the 1-texel octahedral mirror border that makes hardware bilinear correct at tile
 *    edges (DDGI's border-copy map, computed rather than table-driven).
 */

#include "bgfx_compute.sh"
#include "gi/sdf_common.sh"
#include "gi/gi_world_probes.sh"

// Stages chosen clear of sdf_common.sh's fixed set (0-4 samplers/buffers, 12-13 grid).
SAMPLER2D(s_world_probe_radiance, 11);
IMAGE2D_WO(s_world_probe_irradiance_out, rgba16f, 5);
IMAGE2D_WO(s_world_probe_depth_out, rg16f, 6);

#define GUTTER_EDGE (GI_WORLD_PROBE_OCT_IRRADIANCE + 2)
#if (GI_WORLD_PROBE_OCT_IRRADIANCE + 2) != 10
#error NUM_THREADS below hardcodes GUTTER_EDGE = 10; keep them in step.
#endif

SHARED vec4 s_irradiance[GI_WORLD_PROBE_OCT_IRRADIANCE * GI_WORLD_PROBE_OCT_IRRADIANCE];
SHARED vec2 s_depth[GI_WORLD_PROBE_OCT_IRRADIANCE * GI_WORLD_PROBE_OCT_IRRADIANCE];

// Literal 10 = GUTTER_EDGE (GI_WORLD_PROBE_OCT_IRRADIANCE + 2): the OpenGL backend's layout
// parser rejects expressions in local_size values, so the macro cannot appear here.
NUM_THREADS(10, 10, 1)
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
	ivec2 radiance_tile = GiWorldProbeTileBase(slot, level, GI_WORLD_PROBE_OCT_RADIANCE);
	ivec2 out_tile = GiWorldProbeTileBase(slot, level, GUTTER_EDGE);
	bool interior = local.x >= 1 && local.x <= GI_WORLD_PROBE_OCT_IRRADIANCE && local.y >= 1 &&
	                local.y <= GI_WORLD_PROBE_OCT_IRRADIANCE;
	float depth_clamp = GI_WORLD_PROBE_DEPTH_CLAMP * GiWorldProbeSpacing(level);
	if(interior)
	{
		ivec2 texel = local - ivec2(1, 1);
		vec3 normal = GiOctDecode((vec2(texel) + vec2_splat(0.5)) / float(GI_WORLD_PROBE_OCT_IRRADIANCE));
		vec3 irradiance = vec3_splat(0.0);
		float sky = 0.0;
		float depth_mean = 0.0;
		float depth_mean_sq = 0.0;
		float depth_weight = 0.0;
		for(int d = 0; d < GI_WORLD_PROBE_OCT_RADIANCE * GI_WORLD_PROBE_OCT_RADIANCE; ++d)
		{
			ivec2 sample_texel = radiance_tile + ivec2(d % GI_WORLD_PROBE_OCT_RADIANCE,
			                                           d / GI_WORLD_PROBE_OCT_RADIANCE);
			vec4 sample_value = texelFetch(s_world_probe_radiance, sample_texel, 0);
			vec3 sample_dir = GiOctDecode((vec2(sample_texel - radiance_tile) + vec2_splat(0.5)) /
			                              float(GI_WORLD_PROBE_OCT_RADIANCE));
			float cosine = max(dot(normal, sample_dir), 0.0);
			if(cosine <= 0.0)
			{
				continue;
			}
			irradiance += sample_value.xyz * cosine;
			sky += (sample_value.w < 0.0 ? 1.0 : 0.0) * cosine;
			float lobe = pow(cosine, GI_WORLD_PROBE_DEPTH_SHARPNESS);
			float depth = sample_value.w < 0.0 ? depth_clamp : min(sample_value.w, depth_clamp);
			depth_mean += depth * lobe;
			depth_mean_sq += depth * depth * lobe;
			depth_weight += lobe;
		}
		float norm = float(GI_WORLD_PROBE_OCT_RADIANCE * GI_WORLD_PROBE_OCT_RADIANCE) * 0.25;
		vec4 result = vec4(irradiance / norm, sky / norm);
		s_irradiance[texel.y * GI_WORLD_PROBE_OCT_IRRADIANCE + texel.x] = result;
		vec2 moments = depth_weight > 1e-6 ? vec2(depth_mean, depth_mean_sq) / depth_weight
		                                   : vec2(depth_clamp, depth_clamp * depth_clamp);
		s_depth[texel.y * GI_WORLD_PROBE_OCT_IRRADIANCE + texel.x] = moments;
		imageStore(s_world_probe_irradiance_out, out_tile + local, result);
		imageStore(s_world_probe_depth_out, out_tile + local, vec4(moments, 0.0, 0.0));
	}
	barrier();
	if(!interior)
	{
		// Octahedral gutter: crossing a tile edge lands on the mirrored interior texel
		// (transverse axis flipped); corners map to the diagonally opposite corner. Computed
		// with the same wrap the screen probes use, applied to the out-of-range coordinate.
		ivec2 wrapped = local - ivec2(1, 1);
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
		imageStore(s_world_probe_irradiance_out, out_tile + local, s_irradiance[index]);
		imageStore(s_world_probe_depth_out, out_tile + local, vec4(s_depth[index], 0.0, 0.0));
	}
}
