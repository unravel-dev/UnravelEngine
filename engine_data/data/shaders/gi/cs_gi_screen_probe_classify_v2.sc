/*
 * GI v2 probe CLASSIFICATION + COMPACTION: one thread per probe decides traced or
 * interpolated, and appends the traced probes' coordinates to a dense list. The trace then
 * launches EXACTLY the traced count through indirect args (cs_gi_screen_probe_args_v2)
 * instead of the full lattice with early-outs: with half the lattice interpolated and the
 * sky dead, the sparse surviving tracers were paying an occupancy tax the skips could not
 * recover (measured: pass savings trailing the ray-count reduction).
 *
 * The gate chain, moved verbatim from the trace's first thread:
 *   dead anchor -> no list entry, no mode change (the interp pass clears the tile);
 *   even-lattice probes -> always traced (the coarse base everything else leans on);
 *   phased revalidation -> traced regardless, because an interpolated probe's own history
 *     is derived from its parents and no test below can see what the substitution erased;
 *   coplanarity of the parent anchors (cell plane / collinearity at parent scale);
 *   parents' radiance agreement + own-history deviation (the importance mips, last frame's
 *     half) - geometry sameness is necessary, radiance sameness is what makes the
 *     substitution invisible.
 */

#include "bgfx_compute.sh"
#include "gi/gi_constants.sh"
#include "gi/gi_probe_common.sh"

/// [0] = traced count (zeroed by the placement pass this frame), [1..] = packed traced
/// probe coordinates (x | y << 16), appended atomically, consumed by the trace at stage 11.
BUFFER_RW(b_gi_probe_traced, uint, 6);
BUFFER_RW(b_gi_probes, vec4, 7);

/// z > 0 = adaptive gather enabled; other components unused here.
uniform vec4 u_gi_screen_trace;

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 probe = ivec2(gl_GlobalInvocationID.xy);
	if(probe.x >= u_gi_probe_count_x || probe.y >= u_gi_probe_count_y)
	{
		return;
	}
	uint record = (GiProbeRecord(probe.x, probe.y, 0) + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
	vec4 meta = b_gi_probes[record + uint(GI_PROBE_META)];
	if(meta.w < 0.5)
	{
		// No geometry under the anchor: no trace group, no mode change - the interp pass
		// writes the black tile the trace used to.
		return;
	}
	vec3 world_position = meta.xyz;
	vec4 meta2 = b_gi_probes[record + uint(GI_PROBE_META2)];
	bool interpolated = false;
	bool revalidate =
	    ((uint(probe.x) * 3u + uint(probe.y) * 5u + u_gi_probe_frame) %
	     uint(GI_ADAPTIVE_REVALIDATE_FRAMES)) == 0u;
	BRANCH
	if(u_gi_screen_trace.z > 0.0 && !revalidate && ((probe.x | probe.y) & 1) != 0)
	{
		ivec2 parents[4];
		GiProbeParents(probe, parents);
		vec3 positions[4];
		bool parents_valid = true;
		LOOP for(int p = 0; p < 4; ++p)
		{
			uint parent_record =
			    (GiProbeRecord(parents[p].x, parents[p].y, 0) + u_gi_probe_write_offset) *
			    uint(GI_PROBE_STRIDE);
			vec4 parent_meta = b_gi_probes[parent_record + uint(GI_PROBE_META)];
			positions[p] = parent_meta.xyz;
			if(parent_meta.w < 0.5)
			{
				parents_valid = false;
			}
		}
		if(parents_valid)
		{
			float tolerance = GI_ADAPTIVE_PLANE_TOLERANCE * max(meta2.w, 0.1);
			// Duplicated parents (non-straddled axis, lattice edge) zero their edge.
			vec3 edge_x = positions[1] - positions[0];
			vec3 edge_y = positions[2] - positions[0];
			vec3 cell_normal = cross(edge_x, edge_y);
			float cell_len = length(cell_normal);
			if(cell_len > 1e-6)
			{
				vec3 plane_normal = cell_normal / cell_len;
				interpolated =
				    abs(dot(world_position - positions[0], plane_normal)) <= tolerance &&
				    abs(dot(positions[3] - positions[0], plane_normal)) <= tolerance;
			}
			else
			{
				// One distinct edge (or none): the collinearity test. The duplicate edge is
				// zero, so the sum IS the live axis; both zero fails closed.
				vec3 axis = edge_x + edge_y;
				float axis_len2 = dot(axis, axis);
				if(axis_len2 > 1e-8)
				{
					vec3 delta = world_position - positions[0];
					vec3 off_axis = delta - axis * (dot(delta, axis) / axis_len2);
					interpolated = dot(off_axis, off_axis) <= tolerance * tolerance;
				}
			}
		}
		BRANCH
		if(interpolated && u_gi_probe_history_cap > 1.5)
		{
			// Two disagreements end the substitution: the parents among THEMSELVES
			// (structure crossing the cell), and this probe's OWN last-frame mip against
			// the parents' predicted blend (sub-cell structure only the probe itself ever
			// measured). The own test goes blind one revalidation period after a
			// substitution starts; the revalidation cadence refreshes its evidence.
			uint own_history =
			    (GiProbeRecord(probe.x, probe.y, 0) + u_gi_probe_read_offset) *
			    uint(GI_PROBE_STRIDE);
			float luminance_sum = 0.0;
			vec4 spread = vec4_splat(0.0);
			vec4 deviation = vec4_splat(0.0);
			LOOP for(int m = 0; m < 4; ++m)
			{
				vec4 lo = vec4_splat(1e9);
				vec4 hi = vec4_splat(-1e9);
				vec4 parent_sum = vec4_splat(0.0);
				LOOP for(int p = 0; p < 4; ++p)
				{
					uint history_record =
					    (GiProbeRecord(parents[p].x, parents[p].y, 0) + u_gi_probe_read_offset) *
					    uint(GI_PROBE_STRIDE);
					vec4 mip = b_gi_probes[history_record + uint(m)];
					lo = min(lo, mip);
					hi = max(hi, mip);
					parent_sum += mip;
					luminance_sum += mip.x + mip.y + mip.z + mip.w;
				}
				spread = max(spread, hi - lo);
				vec4 own_mip = b_gi_probes[own_history + uint(m)];
				deviation = max(deviation, abs(own_mip - parent_sum * 0.25));
			}
			float mean = luminance_sum / 64.0;
			float limit = GI_ADAPTIVE_RADIANCE_TOLERANCE * max(mean, 1e-3);
			float worst = max(max(max(spread.x, spread.y), max(spread.z, spread.w)),
			                  max(max(deviation.x, deviation.y), max(deviation.z, deviation.w)));
			if(worst > limit)
			{
				interpolated = false;
			}
		}
	}
	if(interpolated)
	{
		b_gi_probes[record + uint(GI_PROBE_META)] = vec4(world_position, 2.0);
		return;
	}
	uint slot;
	atomicFetchAndAdd(b_gi_probe_traced[0], 1u, slot);
	b_gi_probe_traced[1u + slot] = uint(probe.x) | (uint(probe.y) << 16u);
}
