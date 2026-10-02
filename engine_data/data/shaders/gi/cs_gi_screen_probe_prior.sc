/*
 * GI probe LIGHTING PRIOR - one thread group per probe, between the classify and the trace.
 *
 * A probe with no history at its surface (a region the camera just revealed, a disocclusion, an
 * isolated tile whose anchor changes surface) would trace with a uniform allocation: each 2x2
 * direction block one coarse ray, so the small bright directions that carry most of the energy -
 * sunlit openings seen from a corridor - are a coin flip per probe, and the region reads as
 * blotches until its pixels have averaged enough frames. The radiance cache answers this as the
 * lighting PDF of a probe without history (Lumen's radiance-cache importance): the WORLD-PROBE
 * RADIANCE CACHE is read in each of the probe's 64 texel directions at its ray origin (the cage
 * read the trace completes its rays with) and the block means go into the probe's record
 * [12..15], their age into [8].z. The trace allocates the probe's rays by them. The classify
 * decides which probes read (its
 * request in [8].w) and carries a younger floor on for the cache's window, so an isolated tile
 * reads the cache once per window and every other group leaves after one load.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "gi/gi_constants.sh"
#include "gi/gi_probe_common.sh"
// The cache is stored in its own space; the prior is kept in the gather's pre-exposed one.
#include "gi/gi_pre_exposure.sh"
// The world-probe radiance cache (stages 6, 13, 15) and the clipmap its cage-visibility march
// samples (stage 4).
#include "gi/sdf_clipmap.sh"
#define GI_WORLD_PROBE_READ
#define GI_WORLD_PROBE_READ_RADIANCE
#define GI_WORLD_PROBE_SKIP_IRRADIANCE
#include "gi/gi_world_probes.sh"

/// RW: the request is read from [8].w, the prior written to [12..15] and [8].z.
BUFFER_RW(b_gi_probes, vec4, 7);

/// xyz = the camera position, the world-probe windows' centre.
uniform vec4 u_gi_camera;

SHARED uint s_reads;
SHARED float s_cache_luma[GI_PROBE_DIR_COUNT];

/// Rec. 709 luminance, negative lobes dropped - the filter's measure of a block.
float GiPriorLuminance(vec3 radiance)
{
	return dot(max(radiance, vec3_splat(0.0)), vec3(0.2126, 0.7152, 0.0722));
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
	uint base = (GiProbeRecord(probe.x, probe.y, 0) + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
	int dir_index = local.y * GI_PROBE_DIR_EDGE + local.x;
	if(dir_index == 0)
	{
		s_reads = b_gi_probes[base + uint(GI_PROBE_META)].w > 0.5 &&
		                  b_gi_probes[base + uint(GI_PROBE_IMPORTANCE_FRAMES)].w > 0.5
		              ? 1u
		              : 0u;
	}
	barrier();
	if(s_reads == 0u)
	{
		return;
	}
	vec3 origin = b_gi_probes[base + uint(GI_PROBE_ORIGIN)].xyz;
	vec3 direction = GiOctDecode((vec2(local) + vec2_splat(0.5)) / float(GI_PROBE_DIR_EDGE));
	vec3 radiance = vec3_splat(0.0);
	float luma = 0.0;
	if(GiWorldProbeRadiance(origin, direction, u_gi_camera.xyz, radiance))
	{
		luma = GiPriorLuminance(GiCachedToView(radiance));
	}
	s_cache_luma[dir_index] = luma;
	barrier();
	if(dir_index < 4)
	{
		float block_luma[4];
		for(int b = 0; b < 4; ++b)
		{
			int block = dir_index * 4 + b;
			ivec2 block_base = ivec2((block % 4) * 2, (block / 4) * 2);
			float total = 0.0;
			for(int t = 0; t < 4; ++t)
			{
				ivec2 texel = block_base + ivec2(t % 2, t / 2);
				total += s_cache_luma[texel.y * GI_PROBE_DIR_EDGE + texel.x];
			}
			block_luma[b] = total * 0.25;
		}
		b_gi_probes[base + uint(GI_PROBE_FLOOR + dir_index)] =
		    vec4(block_luma[0], block_luma[1], block_luma[2], block_luma[3]);
	}
	if(dir_index == 4)
	{
		// The floor's age; the filter's first pass writes the rest of [8] after the trace.
		b_gi_probes[base + uint(GI_PROBE_IMPORTANCE_FRAMES)] = vec4(0.0, 0.0, 1.0, 0.0);
	}
}
