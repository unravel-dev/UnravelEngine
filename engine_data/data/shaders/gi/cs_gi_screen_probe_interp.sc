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
 * is what lets the atlas bind once as a read-write image. A REVALIDATING probe (mode 4) is
 * the one exception: the trace wrote its tile this frame, and this pass compares that tile
 * with the parents' blend - within GI_ADAPTIVE_RADIANCE_TOLERANCE of the blend's luminance
 * the blend stands in (mode 2), so the revalidation never shows as a one-frame flash of a
 * differently sampled tile; beyond it the probe holds sub-cell lighting structure and stays
 * traced until its next revalidation (mode 3, sticky in the classify pass).
 *
 * Weights are the uniform 0.25 x 4 parent taps: a non-straddled axis and a lattice edge both
 * DUPLICATE a parent (see GiProbeParents), so the four taps always normalise to the correct
 * 1 / 0.5-0.5 / 0.25-each blend without a special case. The alpha channel (encoded proximity /
 * hitT) is NOT blended - mixing a sky marker with a hit distance means nothing - the first
 * parent's value is taken verbatim; the filter's angle test degrades gracefully under an
 * approximate hitT and everything else reads alpha only through its sign and saturate.
 *
 * Both barriers sit at the top level with every lane arriving: the mode comes from a buffer
 * load, and fxc refuses a sync under flow control it cannot prove uniform.
 */

#include "bgfx_compute.sh"
#include "gi/gi_constants.sh"
#include "gi/gi_probe_common.sh"

IMAGE2D_RW(s_probe_radiance_rw, rgba16f, 5);
BUFFER_RW(b_gi_probes, vec4, 7);

/// Tile luminance sums of the traced tile and of the parents' blend, for a revalidating probe
/// (mode 4): one lane per texel, reduced by lane 0.
SHARED float s_traced_luma[GI_PROBE_DIR_COUNT];
SHARED float s_blend_luma[GI_PROBE_DIR_COUNT];
SHARED uint s_keep_trace;

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
	bool valid = meta.w >= 0.5;
	bool interpolated = meta.w > 1.5 && meta.w < 2.5;
	bool revalidating = meta.w > 3.5;
	int dir_index = local.y * GI_PROBE_DIR_EDGE + local.x;
	ivec2 own_texel = GiProbeAtlasBase(probe.x, probe.y, 0) + local;
	ivec2 parents[4];
	GiProbeParents(probe, parents);
	vec3 radiance = vec3_splat(0.0);
	float alpha = 0.0;
	float traced_luma = 0.0;
	float blend_luma = 0.0;
	vec3 luma_weights = vec3(0.2126, 0.7152, 0.0722);
	if(interpolated || revalidating)
	{
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
	}
	if(revalidating)
	{
		vec4 own = imageLoad(s_probe_radiance_rw, own_texel);
		traced_luma = dot(max(own.xyz, vec3_splat(0.0)), luma_weights);
		blend_luma = dot(max(radiance, vec3_splat(0.0)), luma_weights);
	}
	s_traced_luma[dir_index] = traced_luma;
	s_blend_luma[dir_index] = blend_luma;
	barrier();
	if(dir_index == 0)
	{
		s_keep_trace = 0u;
		if(revalidating)
		{
			float traced_sum = 0.0;
			float blend_sum = 0.0;
			for(int d = 0; d < GI_PROBE_DIR_COUNT; ++d)
			{
				traced_sum += s_traced_luma[d];
				blend_sum += s_blend_luma[d];
			}
			float tolerance = GI_ADAPTIVE_RADIANCE_TOLERANCE * max(blend_sum, 1e-4);
			s_keep_trace = abs(traced_sum - blend_sum) > tolerance ? 1u : 0u;
			b_gi_probes[record + uint(GI_PROBE_META)] = vec4(meta.xyz, s_keep_trace != 0u ? 3.0 : 2.0);
		}
	}
	barrier();
	if(!valid)
	{
		// Dead anchor: the compacted trace never launches a group for it, so the black tile
		// the trace used to write moves here - this pass owns every non-traced tile.
		imageStore(s_probe_radiance_rw, own_texel, vec4(0.0, 0.0, 0.0, -1.0));
		return;
	}
	if(!interpolated && !revalidating)
	{
		// Traced (1) or sticky-traced (3): the trace wrote this tile and it stands.
		return;
	}
	if(revalidating && s_keep_trace != 0u)
	{
		// The revalidation disagreed with the blend: the traced tile stands, mode 3.
		return;
	}
	if(dir_index == 0)
	{
		// The screen share the temporal weights by: an interpolated probe carries its
		// parents' mean, so the record never holds a stale value from an older trace.
		// x = screen share, y = moving share (rays that hit moving geometry), both the
		// parents' mean.
		vec2 shares = vec2_splat(0.0);
		for(int share_p = 0; share_p < 4; ++share_p)
		{
			uint parent_record = (GiProbeRecord(parents[share_p].x, parents[share_p].y, 0) +
			                      u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
			shares += b_gi_probes[parent_record + uint(GI_PROBE_SCREEN_SHARE)].xy * 0.25;
		}
		b_gi_probes[record + uint(GI_PROBE_SCREEN_SHARE)] = vec4(shares.x, shares.y, 0.0, 0.0);
	}
	imageStore(s_probe_radiance_rw, own_texel, vec4(radiance, alpha));
}
