/*
 * Adaptive screen probes, spawning.
 * One thread per candidate, laid out as the marking pass. A marked candidate becomes a probe unless the
 * marked candidates with a lower sample index in the 2x2 uniform tiles around it, taken as adaptive probes (each
 * corner keeping its best), already interpolate it with weights summing to GI_INTERP_MIN_WEIGHT. The group
 * reserves its probes with one atomic on the counter; probes past u_gi_adaptive_capacity are dropped. A spawned
 * probe is listed in its uniform tile and writes its record into its atlas tile below the uniform probes.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "gi/gi_common.sh"
#include "gi/gi_adaptive_probes.sh"

SAMPLER2D(s_gi_depth, 0);
/// G-buffer target 1: octahedral normal, metalness, roughness.
SAMPLER2D(s_gi_normal, 1);
BUFFER_RW(b_gi_adaptive, uint, 2);
IMAGE2D_WO(i_gi_probe_records, rgba32f, 3);

SHARED uint s_probes_to_allocate;
SHARED uint s_probe_base;

/// Whether the lower-numbered marked candidates around @p pixel (device depth @p depth01, in uniform tile @p tile's
/// candidate @p sample_index) already cover it.
bool GiIsCoveredByEarlierCandidates(ivec2 pixel, float depth01, int sample_index)
{
	vec3 position = GiWorldFromDepth(GiPixelUv(pixel), depth01);
	vec3 normal = decodeNormalOctahedron(texelFetch(s_gi_normal, pixel, 0).xy);
	float depth = GiLinearDepth(depth01);
	ivec2 coord = clamp(pixel - ivec2(u_gi_placement_jitter), ivec2(0, 0), ivec2(u_gi_view_size) - ivec2(1, 1));
	ivec2 base = min(coord / int(u_gi_downsample), u_gi_probe_count - ivec2(2, 2));
	float finished_corners = 0.0;
	for(int corner = 0; corner < 4; ++corner)
	{
		ivec2 corner_tile = base + ivec2(corner & 1, corner >> 1);
		uint corner_mask = b_gi_adaptive[GiAdaptiveMaskEntry(GiAdaptiveTileIndex(corner_tile))];
		float corner_weight = 0.0;
		for(int other = 0; other < sample_index; ++other)
		{
			if((corner_mask & (1u << uint(other))) == 0u)
			{
				continue;
			}
			ivec2 other_pixel = GiAdaptiveSamplePixel(corner_tile, other);
			float other_depth01 = texelFetch(s_gi_depth, other_pixel, 0).x;
			corner_weight = max(corner_weight,
			                    GiAdaptiveProbeWeights(pixel, position, normal, depth, other_pixel, other_depth01).x);
			if(finished_corners + corner_weight >= GI_INTERP_MIN_WEIGHT)
			{
				return true;
			}
		}
		finished_corners += corner_weight;
	}
	return false;
}

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 local = ivec2(gl_LocalInvocationID.xy);
	if(local.x == 0 && local.y == 0)
	{
		s_probes_to_allocate = 0u;
	}
	barrier();
	ivec2 samples = ivec2(u_gi_adaptive_samples_x, u_gi_adaptive_samples_y);
	ivec2 local_tile = local / samples;
	ivec2 sample2d = local - local_tile * samples;
	int sample_index = sample2d.x + samples.x * sample2d.y;
	ivec2 tile = ivec2(gl_WorkGroupID.xy) * (ivec2(8, 8) / samples) + local_tile;
	ivec2 pixel = ivec2(0, 0);
	float depth01 = 1.0;
	bool place = false;
	BRANCH
	if(all(lessThan(tile, u_gi_probe_count)))
	{
		uint mask = b_gi_adaptive[GiAdaptiveMaskEntry(GiAdaptiveTileIndex(tile))];
		if((mask & (1u << uint(sample_index))) != 0u)
		{
			pixel = GiAdaptiveSamplePixel(tile, sample_index);
			depth01 = texelFetch(s_gi_depth, pixel, 0).x;
			place = !GiIsCoveredByEarlierCandidates(pixel, depth01, sample_index);
		}
	}
	uint list_index = 0u;
	if(place)
	{
		atomicFetchAndAdd(s_probes_to_allocate, 1u, list_index);
	}
	barrier();
	if(local.x == 0 && local.y == 0)
	{
		atomicFetchAndAdd(b_gi_adaptive[GI_ADAPTIVE_COUNTER], s_probes_to_allocate, s_probe_base);
	}
	barrier();
	uint probe = s_probe_base + list_index;
	if(place && probe < u_gi_adaptive_capacity)
	{
		int tile_index = GiAdaptiveTileIndex(tile);
		uint tile_slot = 0u;
		atomicFetchAndAdd(b_gi_adaptive[GiAdaptiveCountEntry(tile_index)], 1u, tile_slot);
		b_gi_adaptive[GiAdaptiveProbeEntry(tile_index, int(tile_slot))] = probe;
		vec3 normal = decodeNormalOctahedron(texelFetch(s_gi_normal, pixel, 0).xy);
		imageStore(i_gi_probe_records,
		           GiAdaptiveAtlasTile(int(probe)),
		           GiPackProbe(GiLinearDepth(depth01), normal, pixel, depth01));
	}
}
