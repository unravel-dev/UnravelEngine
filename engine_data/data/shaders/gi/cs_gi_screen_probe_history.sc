/*
 * GI probe-space temporal: copy THIS probe's previous radiance tile into this
 * frame's atlas BEFORE the stratum trace blends 16 of the 64 texels.
 *
 * A valid previous tile is always copied. Writing the sky marker on a miss is
 * what made camera motion flash the whole image dark (48 of 64 cones went
 * black at once). The running-mean count is kept for the sticky reconstruct
 * and for a scheduled in-tile walk (ANCHOR.w). An unscheduled Halton resets it.
 *
 * Compacted dispatch: one 8x8 group per traced probe, same indirect args as the
 * trace. Interpolated and dead tiles are the interp pass's job.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "gi/gi_constants.sh"
#include "gi/gi_probe_common.sh"

SAMPLER2D(s_probe_radiance_history, 0);
IMAGE2D_WO(s_probe_radiance_out, rgba16f, 5);
BUFFER_RW(b_gi_probes, vec4, 7);

SHARED int s_history_x;
SHARED int s_history_y;
SHARED int s_history_valid;

NUM_THREADS(8, 8, 1)
void main()
{
	uint packed_probe = floatBitsToUint(b_gi_probes[GiProbeTracedListBase() + gl_WorkGroupID.x].x);
	ivec2 probe = ivec2(int(packed_probe & 0xFFFFu), int(packed_probe >> 16u));
	ivec2 local = ivec2(gl_LocalInvocationID.xy);
	if(local.x == 0 && local.y == 0)
	{
		s_history_valid = 0;
		s_history_x = probe.x;
		s_history_y = probe.y;
		uint write_record = (GiProbeRecord(probe.x, probe.y, 0) + u_gi_probe_write_offset) *
		                    uint(GI_PROBE_STRIDE);
		uint read_record = (GiProbeRecord(probe.x, probe.y, 0) + u_gi_probe_read_offset) *
		                   uint(GI_PROBE_STRIDE);
		vec4 current_meta = b_gi_probes[write_record + uint(GI_PROBE_META)];
		vec4 current_meta2 = b_gi_probes[write_record + uint(GI_PROBE_META2)];
		vec4 current_anchor = b_gi_probes[write_record + uint(GI_PROBE_ANCHOR)];
		vec4 history_meta = b_gi_probes[read_record + uint(GI_PROBE_META)];
		BRANCH
		if(u_gi_probe_history_cap > 0.5 && history_meta.w > 0.5)
		{
			s_history_valid = 1;
			vec4 prev_count = b_gi_probes[read_record + uint(GI_PROBE_HISTORY)];
			float keep_count =
			    (GiScreenProbeSameOrigin(current_meta.xyz, history_meta.xyz, current_meta2.w) ||
			     current_anchor.w > 0.5)
			        ? 1.0
			        : 0.0;
			b_gi_probes[write_record + uint(GI_PROBE_HISTORY)] =
			    vec4(keep_count > 0.5 ? prev_count.x : 0.0, 0.0, 0.0, keep_count);
		}
		else
		{
			b_gi_probes[write_record + uint(GI_PROBE_HISTORY)] = vec4_splat(0.0);
		}
	}
	barrier();
	vec4 value = vec4(0.0, 0.0, 0.0, -1.0);
	BRANCH
	if(s_history_valid != 0)
	{
		value = texelFetch(s_probe_radiance_history,
		                   GiProbeAtlasBase(s_history_x, s_history_y, 0) + local, 0);
	}
	imageStore(s_probe_radiance_out, GiProbeAtlasBase(probe.x, probe.y, 0) + local, value);
}
