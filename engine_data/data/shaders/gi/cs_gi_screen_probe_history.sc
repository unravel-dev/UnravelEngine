/*
 * GI probe-space temporal: copy last frame's reprojected radiance tile into this
 * frame's atlas BEFORE the stratum trace overwrites 16 of the 64 texels.
 *
 * Screen probes are a SCREEN lattice - probe (x, y) this frame is a different world
 * point than (x, y) last frame - so the copy reprojects the anchor into last frame's
 * lattice (the same plane-gated test the trace uses for the importance mip) rather
 * than copying the same index. A failed reprojection writes the sky marker: the
 * 3x3 probe-space filter and the pixel temporal cover the hole, which is the
 * Lumen TemporalFilterProbes miss path.
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
BUFFER_RO(b_gi_probes, vec4, 7);

uniform vec4 u_gi_camera;
uniform mat4 u_gi_prev_view_proj;

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
		s_history_x = 0;
		s_history_y = 0;
		uint record = (GiProbeRecord(probe.x, probe.y, 0) + u_gi_probe_write_offset) *
		              uint(GI_PROBE_STRIDE);
		vec4 meta = b_gi_probes[record + uint(GI_PROBE_META)];
		vec4 meta2 = b_gi_probes[record + uint(GI_PROBE_META2)];
		vec3 world_position = meta.xyz;
		vec3 world_normal = meta2.xyz;
		vec4 prev_clip4 = mul(u_gi_prev_view_proj, vec4(world_position, 1.0));
		BRANCH
		if(u_gi_probe_history_cap > 0.5 && prev_clip4.w > 0.0)
		{
			vec3 prev_clip = clipTransform(prev_clip4.xyz / prev_clip4.w);
			vec2 prev_uv = prev_clip.xy * 0.5 + 0.5;
			if(all(greaterThanEqual(prev_uv, vec2_splat(0.0))) &&
			   all(lessThanEqual(prev_uv, vec2_splat(1.0))))
			{
				vec2 prev_probe = floor(prev_uv * u_gi_probe_screen.xy / u_gi_probe_spacing);
				int hx = int(clamp(prev_probe.x, 0.0, float(u_gi_probe_count_x - 1)));
				int hy = int(clamp(prev_probe.y, 0.0, float(u_gi_probe_count_y - 1)));
				uint history_base =
				    (GiProbeRecord(hx, hy, 0) + u_gi_probe_read_offset) * uint(GI_PROBE_STRIDE);
				vec4 history_meta = b_gi_probes[history_base + uint(GI_PROBE_META)];
				float plane = abs(dot(history_meta.xyz - world_position, world_normal));
				if(history_meta.w > 0.5 &&
				   plane < 0.05 * max(length(world_position - u_gi_camera.xyz), 0.1))
				{
					s_history_x = hx;
					s_history_y = hy;
					s_history_valid = 1;
				}
			}
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
