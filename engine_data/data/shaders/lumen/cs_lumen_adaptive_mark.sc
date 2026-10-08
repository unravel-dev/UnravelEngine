/*
 * Lumen adaptive screen probes, marking (UE 5.8 ScreenProbeAdaptivePlacementMarkCS, LumenScreenProbeGather.usf
 * :1731-1806). One thread per candidate pixel, the u_lumen_adaptive_samples candidates of a uniform tile adjacent in the
 * group: a candidate on geometry inside the view whose uniform-probe interpolation weights (this frame's records, no
 * full-resolution jitter) sum below LUMEN_INTERP_MIN_WEIGHT sets its bit in the tile's placement mask
 * (lumen_adaptive_probes.sh).
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "lumen/lumen_common.sh"
#include "lumen/lumen_adaptive_probes.sh"

SAMPLER2D(s_lumen_depth, 0);
/// G-buffer target 1: octahedral normal, metalness, roughness.
SAMPLER2D(s_lumen_normal, 1);
SAMPLER2D(s_lumen_probe_records, 2);
BUFFER_RW(b_lumen_adaptive, uint, 3);

SHARED uint s_mask[LUMEN_ADAPTIVE_MAX_GROUP_TILES];

/// Whether the uniform probes cannot interpolate @p pixel (UE: dot(Weights, 1) < MIN_PROBE_INTERPOLATION_WEIGHT).
bool LumenNeedsAdaptiveProbe(ivec2 pixel)
{
	float depth01 = texelFetch(s_lumen_depth, pixel, 0).x;
	if(depth01 >= 1.0)
	{
		return false;
	}
	vec3 position = LumenWorldFromDepth(LumenPixelUv(pixel), depth01);
	vec3 normal = decodeNormalOctahedron(texelFetch(s_lumen_normal, pixel, 0).xy);
	float depth = LumenLinearDepth(depth01);
	vec4 interpolation = LumenUniformInterpolation(vec2(pixel) - u_lumen_placement_jitter);
	ivec2 base = ivec2(interpolation.xy);
	vec2 f = interpolation.zw;
	float weight_sum = LumenProbeCornerWeights(texelFetch(s_lumen_probe_records, base, 0), (1.0 - f.x) * (1.0 - f.y),
	                                           position, normal, depth).x +
	                   LumenProbeCornerWeights(texelFetch(s_lumen_probe_records, base + ivec2(1, 0), 0), f.x * (1.0 - f.y),
	                                           position, normal, depth).x +
	                   LumenProbeCornerWeights(texelFetch(s_lumen_probe_records, base + ivec2(0, 1), 0), (1.0 - f.x) * f.y,
	                                           position, normal, depth).x +
	                   LumenProbeCornerWeights(texelFetch(s_lumen_probe_records, base + ivec2(1, 1), 0), f.x * f.y,
	                                           position, normal, depth).x;
	return weight_sum < LUMEN_INTERP_MIN_WEIGHT;
}

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 local = ivec2(gl_LocalInvocationID.xy);
	ivec2 samples = ivec2(u_lumen_adaptive_samples_x, u_lumen_adaptive_samples_y);
	ivec2 group_tiles = ivec2(8, 8) / samples;
	ivec2 local_tile = local / samples;
	ivec2 sample2d = local - local_tile * samples;
	int sample_index = sample2d.x + samples.x * sample2d.y;
	int mask_slot = local_tile.y * group_tiles.x + local_tile.x;
	ivec2 tile = ivec2(gl_WorkGroupID.xy) * group_tiles + local_tile;
	bool inside = all(lessThan(tile, u_lumen_probe_count));
	if(sample_index == 0)
	{
		s_mask[mask_slot] = 0u;
	}
	barrier();
	ivec2 pixel = LumenAdaptiveSamplePixel(tile, sample_index);
	bool allocate = false;
	BRANCH
	if(inside && all(lessThan(pixel, ivec2(u_lumen_view_size))))
	{
		allocate = LumenNeedsAdaptiveProbe(pixel);
	}
	if(allocate)
	{
		atomicOr(s_mask[mask_slot], 1u << uint(sample_index));
	}
	barrier();
	if(sample_index == 0 && inside)
	{
		b_lumen_adaptive[LumenAdaptiveMaskEntry(LumenAdaptiveTileIndex(tile))] = s_mask[mask_slot];
	}
}
