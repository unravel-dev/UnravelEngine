/*
 * Filters each probe's radiance across its 3x3 probe neighbourhood and projects the result to
 * SH2, one probe per thread group.
 *
 * Filtering happens IN PROBE SPACE -- the same octahedral texel across neighbouring probes is
 * the same world-space direction, so averaging them denoises the radiance BEFORE any pixel
 * integrates it, with none of the edge-stopping machinery a screen-space filter needs. The
 * plane and facing weights keep a probe from borrowing radiance across a depth or orientation
 * break, which is the probe-space form of the silhouette-leak guard the integration also has.
 *
 * The projection target is SH2 rather than the raw 64 texels because the consumer is DIFFUSE:
 * per-pixel integration needs the cosine-weighted hemisphere integral around the PIXEL's normal,
 * which for SH is nine multiply-adds instead of 64 texture fetches per probe. The resolved flag
 * is projected alongside, so the per-pixel weight -- how much of the hemisphere the gather
 * actually measured -- stays directional too.
 */

#include "bgfx_compute.sh"

#define GI_CACHE_READ_ONLY
// GiHashUint / GiHashCombine live here; the probe header uses them for jitter.
#include "gi/radiance_cache.sh"
#include "gi/gi_probe_common.sh"

SAMPLER2D(s_probe_radiance, 0);
BUFFER_RW(b_gi_probes, vec4, 1);

/// How far off the centre probe's plane a neighbour may anchor before it stops contributing,
/// as a fraction of the centre's view distance. The probe-space analogue of the integration's
/// plane weight, and the reason a probe on a wall does not borrow the floor's radiance.
#define GI_PROBE_FILTER_PLANE_TOLERANCE 0.05

SHARED vec4 s_filtered[GI_PROBE_DIR_COUNT];

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 probe = ivec2(gl_WorkGroupID.xy);
	ivec2 local = ivec2(gl_LocalInvocationID.xy);
	if(probe.x >= u_gi_probe_count_x || probe.y >= u_gi_probe_count_y)
	{
		return;
	}
	uint probe_index = GiProbeIndex(probe.x, probe.y);
	// This frame's half of the double-buffered probe state, always: the filter consumes what the
	// trace just wrote and never touches the history half.
	uint base = (probe_index + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
	vec4 center_meta = b_gi_probes[base + uint(GI_PROBE_META)];
	vec4 center_meta2 = b_gi_probes[base + uint(GI_PROBE_META2)];
	bool center_valid = center_meta.w > 0.5;
	int dir_index = local.y * GI_PROBE_DIR_EDGE + local.x;
	// Each thread filters ITS direction across the neighbourhood.
	vec4 filtered = vec4_splat(0.0);
	if(center_valid)
	{
		float weight_sum = 0.0;
		float plane_tolerance = GI_PROBE_FILTER_PLANE_TOLERANCE * max(center_meta2.w, 0.1);
		for(int oy = -1; oy <= 1; ++oy)
		{
			for(int ox = -1; ox <= 1; ++ox)
			{
				int nx = probe.x + ox;
				int ny = probe.y + oy;
				if(nx < 0 || ny < 0 || nx >= u_gi_probe_count_x || ny >= u_gi_probe_count_y)
				{
					continue;
				}
				uint neighbour_base =
				    (GiProbeIndex(nx, ny) + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
				vec4 neighbour_meta = b_gi_probes[neighbour_base + uint(GI_PROBE_META)];
				if(neighbour_meta.w < 0.5)
				{
					continue;
				}
				vec4 neighbour_meta2 = b_gi_probes[neighbour_base + uint(GI_PROBE_META2)];
				// Same-surface tests, both scaled to the centre probe: off-plane distance and
				// facing agreement. A neighbour across a silhouette fails one of them.
				float plane = abs(dot(neighbour_meta.xyz - center_meta.xyz, center_meta2.xyz));
				float plane_weight = saturate(1.0 - plane / plane_tolerance);
				float facing = saturate(dot(neighbour_meta2.xyz, center_meta2.xyz));
				float weight = plane_weight * facing * facing;
				if(weight <= 1e-4)
				{
					continue;
				}
				ivec2 texel = ivec2(nx, ny) * GI_PROBE_DIR_EDGE + local;
				filtered += texelFetch(s_probe_radiance, texel, 0) * weight;
				weight_sum += weight;
			}
		}
		filtered = weight_sum > 1e-4 ? filtered / weight_sum : vec4_splat(0.0);
	}
	s_filtered[dir_index] = filtered;
	barrier();
	// One thread projects the filtered sphere to SH. 64 x 9 multiply-adds on a single lane is
	// noise beside the texture traffic above, and a parallel reduction here would spend more in
	// synchronisation than it saves.
	if(dir_index == 0)
	{
		vec3 sh_radiance[9];
		float sh_resolved[9];
		for(int k = 0; k < 9; ++k)
		{
			sh_radiance[k] = vec3_splat(0.0);
			sh_resolved[k] = 0.0;
		}
		if(center_valid)
		{
			// Every octahedral texel subtends approximately the same solid angle, so the
			// projection is a plain sum scaled by 4pi / N.
			float d_omega = 4.0 * 3.14159265 / float(GI_PROBE_DIR_COUNT);
			for(int d = 0; d < GI_PROBE_DIR_COUNT; ++d)
			{
				vec2 tile_uv = (vec2(float(d % GI_PROBE_DIR_EDGE), float(d / GI_PROBE_DIR_EDGE)) +
				                vec2_splat(0.5)) /
				               float(GI_PROBE_DIR_EDGE);
				vec3 direction = GiOctDecode(tile_uv);
				float basis[9];
				GiShBasis(direction, basis);
				vec4 value = s_filtered[d];
				for(int k = 0; k < 9; ++k)
				{
					sh_radiance[k] += value.xyz * (basis[k] * d_omega);
					sh_resolved[k] += value.w * (basis[k] * d_omega);
				}
			}
		}
		for(int k = 0; k < 9; ++k)
		{
			b_gi_probes[base + uint(k)] = vec4(sh_radiance[k], sh_resolved[k]);
		}
	}
}
