/*
 * GI probe CLASSIFICATION + COMPACTION: one thread per probe decides traced or
 * interpolated, and appends the traced probes' coordinates to a dense list. The trace then
 * launches EXACTLY the traced count through indirect args (cs_gi_screen_probe_args)
 * instead of the full lattice with early-outs: with half the lattice interpolated and the
 * sky dead, the sparse surviving tracers were paying an occupancy tax the skips could not
 * recover (measured: pass savings trailing the ray-count reduction).
 *
 * The gate chain:
 *   dead anchor -> no list entry, no mode change (the interp pass clears the tile);
 *   even-lattice probes -> always traced (the coarse base everything else leans on);
 *   the geometric gate -> an odd probe whose parents do not share its tangent plane (and it
 *     theirs) is traced, full stop; only a probe that passes is a CANDIDATE for the rest;
 *   phased revalidation -> a candidate is traced (mode 4), because an interpolated probe's own
 *     history is derived from its parents and no test below can see what the substitution
 *     erased. The traced tile is NOT shown as such: the interp pass compares it with the
 *     parents' blend and either restores the blend (mode 2) or keeps the trace and marks the
 *     probe STICKY-TRACED (mode 3) until its next revalidation. Showing the trace for its one
 *     frame in eight was a periodic flash on every flat surface (measured 2026-09-17, Sponza
 *     cloister: the Indirect view's at-rest median change 0.12 -> 0.31 with adaptive probes);
 *   sticky-traced last frame -> traced again (mode 3) until the next revalidation;
 *   every other candidate -> interpolated from its parents (mode 2).
 *
 * Importance-mip radiance agreement is not a live gate. It was written for a
 * history_cap encoding that never shipped (x was 0/1, the test was > 1.5).
 * Turning it on vetoes every flat-wall substitution: windowed temporal's
 * sticky origins and 16-ray mips disagree across a cell even when the
 * geometry test is right. Revalidation is the miss catch.
 */

#include "bgfx_compute.sh"
#include "gi/gi_constants.sh"
#include "gi/gi_probe_common.sh"

/// [0] = the traced COUNT alone (zeroed by the placement pass this frame, consumed by the
/// indirect-args pass); the coordinates themselves go into the probe buffer's list region.
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
	// LAST frame's mode, from the read half: a sticky-traced probe stays traced between
	// revalidations. Its own revalidation frame re-decides.
	uint last_record =
	    (GiProbeRecord(probe.x, probe.y, 0) + u_gi_probe_read_offset) * uint(GI_PROBE_STRIDE);
	float last_mode = b_gi_probes[last_record + uint(GI_PROBE_META)].w;
	bool odd = ((probe.x | probe.y) & 1) != 0;
	bool adaptive = u_gi_screen_trace.z > 0.0 && odd;
	// THE GEOMETRIC GATE, first and for every odd probe: may its even-lattice parents stand in
	// for it at all? Each parent must lie within tolerance of THIS probe's tangent plane and this
	// probe within tolerance of the parent's - the plane test the integrate pass applies per pixel
	// (and Lumen's adaptive placement: plane distance to the scene plane over depth), so a skipped
	// probe is one whose pixels would have blended those parents at near-full weight anyway.
	// It used to fit a plane (or a line) THROUGH the parents and test the probe against that.
	// At a depth edge the parents straddle the edge, their connecting line runs along the view
	// ray, and the probe between them lies on it whichever surface it sits on: the test passed
	// ~99 percent of odd probes at every pose and spacing (measured 2026-09-18, Sponza: a fixed
	// checkerboard straight across curtain and column silhouettes), and silhouette probes took a
	// blend of near- and far-surface lighting that slid with the lattice under a camera turn.
	// The G-buffer normal carries normal maps; as a plane DISTANCE over one tile its tilt costs
	// sin(tilt) x the tile's footprint, inside the 5 percent tolerance at spacings up to 32 px
	// except at grazing incidence, where the probe is traced - the safe direction.
	bool parents_stand_in = false;
	BRANCH
	if(adaptive)
	{
		ivec2 parents[4];
		GiProbeParents(probe, parents);
		float tolerance = GI_ADAPTIVE_PLANE_TOLERANCE * max(meta2.w, 0.1);
		parents_stand_in = true;
		LOOP for(int p = 0; p < 4; ++p)
		{
			uint parent_record =
			    (GiProbeRecord(parents[p].x, parents[p].y, 0) + u_gi_probe_write_offset) *
			    uint(GI_PROBE_STRIDE);
			vec4 parent_meta = b_gi_probes[parent_record + uint(GI_PROBE_META)];
			vec3 parent_normal = b_gi_probes[parent_record + uint(GI_PROBE_META2)].xyz;
			vec3 to_parent = parent_meta.xyz - world_position;
			if(parent_meta.w < 0.5 || abs(dot(to_parent, meta2.xyz)) > tolerance ||
			   abs(dot(to_parent, parent_normal)) > tolerance)
			{
				parents_stand_in = false;
			}
		}
	}
	// Revalidation and the sticky mode are decisions about probes the geometry ALLOWS to be
	// skipped. A probe that fails the gate above is simply traced (mode 1): routing it through
	// mode 4 let the interp pass overwrite its trace with the parents' blend one frame in eight.
	bool candidate = adaptive && parents_stand_in;
	bool sticky = candidate && !revalidate && last_mode > 2.5 && last_mode < 3.5;
	interpolated = candidate && !revalidate && !sticky;
	if(interpolated)
	{
		b_gi_probes[record + uint(GI_PROBE_META)] = vec4(world_position, 2.0);
		return;
	}
	if(candidate && revalidate)
	{
		b_gi_probes[record + uint(GI_PROBE_META)] = vec4(world_position, 4.0);
	}
	else if(sticky)
	{
		b_gi_probes[record + uint(GI_PROBE_META)] = vec4(world_position, 3.0);
	}
	uint slot;
	atomicFetchAndAdd(b_gi_probe_traced[0], 1u, slot);
	// The coordinate lands in the probe buffer's list region (see GiProbeTracedListBase);
	// this buffer keeps only the counter. One whole vec4 per coordinate - typed UAV stores
	// must write every component, and each slot is atomically unique, so no race.
	float encoded = uintBitsToFloat(uint(probe.x) | (uint(probe.y) << 16u));
	b_gi_probes[GiProbeTracedListBase() + slot] = vec4(encoded, 0.0, 0.0, 0.0);
}
