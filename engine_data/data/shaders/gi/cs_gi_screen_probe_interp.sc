/*
 * GI adaptive-gather reconstruction: probes the trace marked INTERPOLATED (meta mode 2)
 * get their radiance tile rebuilt from their even-lattice parents, texel by texel - the same
 * bilinear blend the integrate pass performs per pixel, materialised once per tile instead of
 * traced 64 times. Runs between the trace and the probe-space filter, so the filter and
 * everything downstream see a complete atlas and need no knowledge of adaptivity.
 *
 * Parents are ALWAYS traced (the classification never marks an even-lattice probe, and
 * GiProbeParents never clamps onto an odd coordinate), so this dispatch only reads tiles the
 * trace wrote and only writes tiles the trace skipped: no intra-pass dependency exists, which
 * is what lets the atlas bind once as a read-write image.
 *
 * Weights are the uniform 0.25 x 4 parent taps: a non-straddled axis and a lattice edge both
 * DUPLICATE a parent (see GiProbeParents), so the four taps always normalise to the correct
 * 1 / 0.5-0.5 / 0.25-each blend without a special case. The alpha channel (encoded proximity /
 * hitT) is NOT blended - mixing a sky marker with a hit distance means nothing - the first
 * parent's value is taken verbatim; the filter's angle test degrades gracefully under an
 * approximate hitT and everything else reads alpha only through its sign and saturate.
 */

#include "bgfx_compute.sh"
#include "gi/gi_constants.sh"
#include "gi/gi_probe_common.sh"

IMAGE2D_RW(s_probe_radiance_rw, rgba16f, 5);
BUFFER_RW(b_gi_probes, vec4, 7);

/// y > 0 = tier debug view: interpolated tiles paint magenta so the adaptive coverage is
/// visible in the same view that shows the trace's tiers. Other components unused here.
uniform vec4 u_gi_screen_trace;

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
	vec4 meta = b_gi_probes[record + uint(GI_PROBE_META)];
	if(meta.w < 0.5)
	{
		// Dead anchor: the compacted trace never launches a group for it, so the black tile
		// the trace used to write moves here - this pass owns every non-traced tile.
		imageStore(s_probe_radiance_rw, GiProbeAtlasBase(probe.x, probe.y, 0) + local,
		           vec4(0.0, 0.0, 0.0, -1.0));
		return;
	}
	if(meta.w < 1.5)
	{
		// Traced: the trace wrote this tile.
		return;
	}
	ivec2 parents[4];
	GiProbeParents(probe, parents);
	if(local.x == 0 && local.y == 0)
	{
		// The screen share the temporal weights by: an interpolated probe carries its
		// parents' mean, so the record never holds a stale value from an older trace.
		float screen_share = 0.0;
		for(int share_p = 0; share_p < 4; ++share_p)
		{
			uint parent_record = (GiProbeRecord(parents[share_p].x, parents[share_p].y, 0) +
			                      u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
			screen_share += b_gi_probes[parent_record + uint(GI_PROBE_SCREEN_SHARE)].x * 0.25;
		}
		b_gi_probes[record + uint(GI_PROBE_SCREEN_SHARE)] = vec4(screen_share, 0.0, 0.0, 0.0);
	}
	vec3 radiance = vec3_splat(0.0);
	float alpha = 0.0;
	LOOP for(int p = 0; p < 4; ++p)
	{
		vec4 value = imageLoad(s_probe_radiance_rw,
		                       GiProbeAtlasBase(parents[p].x, parents[p].y, 0) + local);
		radiance += value.xyz * 0.25;
		if(p == 0)
		{
			alpha = value.w;
		}
	}
	if(u_gi_screen_trace.y > 0.5)
	{
		radiance = vec3(1.0, 0.0, 1.0);
	}
	imageStore(s_probe_radiance_rw, GiProbeAtlasBase(probe.x, probe.y, 0) + local,
	           vec4(radiance, alpha));
}
