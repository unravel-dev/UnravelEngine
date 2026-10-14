/*
 * Adaptive screen probes, marking. One thread per candidate pixel, the u_gi_adaptive_samples candidates of a
 * uniform tile adjacent in the group: a candidate on geometry inside the view whose uniform-probe interpolation
 * weights (this frame's records, no full-resolution jitter) sum below GI_INTERP_MIN_WEIGHT sets its bit in the
 * tile's placement mask (gi_adaptive_probes.sh).
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "gi/gi_common.sh"
#include "gi/gi_adaptive_probes.sh"

SAMPLER2D(s_gi_depth, 0);
/// G-buffer target 1: octahedral normal, metalness, roughness.
SAMPLER2D(s_gi_normal, 1);
SAMPLER2D(s_gi_probe_records, 2);
BUFFER_RW(b_gi_adaptive, uint, 3);

SHARED uint s_mask[GI_ADAPTIVE_MAX_GROUP_TILES];

/// Whether the uniform probes cannot interpolate @p pixel: their corner weights sum below GI_INTERP_MIN_WEIGHT.
bool GiNeedsAdaptiveProbe(ivec2 pixel)
{
	float depth01 = texelFetch(s_gi_depth, pixel, 0).x;
	if(depth01 >= 1.0)
	{
		return false;
	}
	vec3 position = GiWorldFromDepth(GiPixelUv(pixel), depth01);
	vec3 normal = decodeNormalOctahedron(texelFetch(s_gi_normal, pixel, 0).xy);
	float depth = GiLinearDepth(depth01);
	vec4 interpolation = GiUniformInterpolation(vec2(pixel) - u_gi_placement_jitter);
	ivec2 base = ivec2(interpolation.xy);
	vec2 f = interpolation.zw;
	float weight_sum = GiProbeCornerWeights(texelFetch(s_gi_probe_records, base, 0), (1.0 - f.x) * (1.0 - f.y),
	                                           position, normal, depth).x +
	                   GiProbeCornerWeights(texelFetch(s_gi_probe_records, base + ivec2(1, 0), 0), f.x * (1.0 - f.y),
	                                           position, normal, depth).x +
	                   GiProbeCornerWeights(texelFetch(s_gi_probe_records, base + ivec2(0, 1), 0), (1.0 - f.x) * f.y,
	                                           position, normal, depth).x +
	                   GiProbeCornerWeights(texelFetch(s_gi_probe_records, base + ivec2(1, 1), 0), f.x * f.y,
	                                           position, normal, depth).x;
	return weight_sum < GI_INTERP_MIN_WEIGHT;
}

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 local = ivec2(gl_LocalInvocationID.xy);
	ivec2 samples = ivec2(u_gi_adaptive_samples_x, u_gi_adaptive_samples_y);
	ivec2 group_tiles = ivec2(8, 8) / samples;
	ivec2 local_tile = local / samples;
	ivec2 sample2d = local - local_tile * samples;
	int sample_index = sample2d.x + samples.x * sample2d.y;
	int mask_slot = local_tile.y * group_tiles.x + local_tile.x;
	ivec2 tile = ivec2(gl_WorkGroupID.xy) * group_tiles + local_tile;
	bool inside = all(lessThan(tile, u_gi_probe_count));
	if(sample_index == 0)
	{
		s_mask[mask_slot] = 0u;
	}
	barrier();
	ivec2 pixel = GiAdaptiveSamplePixel(tile, sample_index);
	bool allocate = false;
	BRANCH
	if(inside && all(lessThan(pixel, ivec2(u_gi_view_size))))
	{
		allocate = GiNeedsAdaptiveProbe(pixel);
	}
	if(allocate)
	{
		atomicOr(s_mask[mask_slot], 1u << uint(sample_index));
	}
	barrier();
	if(sample_index == 0 && inside)
	{
		b_gi_adaptive[GiAdaptiveMaskEntry(GiAdaptiveTileIndex(tile))] = s_mask[mask_slot];
	}
}
