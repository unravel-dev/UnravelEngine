/*
 * GI probe CLASSIFICATION + COMPACTION: one thread per probe decides traced or
 * interpolated, and appends the traced probes' coordinates to a dense list. The trace then
 * launches EXACTLY the traced count through indirect args (cs_gi_screen_probe_args)
 * instead of the full lattice with early-outs: with half the lattice interpolated and the
 * sky dead, the sparse surviving tracers would pay an occupancy tax the skips cannot recover.
 *
 * The gate chain:
 *   dead anchor -> no list entry, no mode change (the interp pass clears the tile);
 *   even-lattice probes -> always traced (the coarse base everything else leans on);
 *   the geometric gate -> an odd probe whose parents do not share its tangent plane (and it
 *     theirs) is traced, full stop; only a probe that passes is a CANDIDATE for the rest;
 *   the young-pixel gate -> a probe whose anchor pixel held fewer than
 *     GI_ADAPTIVE_YOUNG_PIXEL_FRAMES frames of temporal history last frame (or lay off screen) is
 *     traced: its pixels have no history to average the parents' blend with, so a revealed strip
 *     would show the parents' single-frame estimate at twice the probe spacing;
 *   phased revalidation -> a candidate is traced (mode 4), because an interpolated probe's own
 *     history is derived from its parents and no test below can see what the substitution
 *     erased. The traced tile is NOT shown as such: the interp pass compares it with the
 *     parents' blend and either restores the blend (mode 2) or keeps the trace and marks the
 *     probe STICKY-TRACED (mode 3) until its next revalidation. Showing the trace for its one
 *     frame in eight would be a periodic flash on every flat surface;
 *   sticky-traced last frame -> traced again (mode 3) until the next revalidation;
 *   every other candidate -> interpolated from its parents (mode 2).
 *
 * Radiance agreement (the importance states) is not part of the gate; the periodic revalidation
 * is what catches a substitution the geometry test gets wrong.
 *
 * The LIGHTING PRIOR's requests, for every probe with geometry: a probe without history at its
 * surface - the trace's importance lookup finds no record on its plane, or none of the filter's
 * state candidates holds a state - is marked for the prior pass to read the world-probe cache
 * (cs_gi_screen_probe_prior.sc); a tile whose previous record carries a cache floor younger
 * than the cache's window carries it on instead, so an isolated tile reads the cache once per
 * window.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "gi/gi_constants.sh"
#include "gi/gi_probe_common.sh"

/// [0] = the traced COUNT alone (zeroed by the placement pass this frame, consumed by the
/// indirect-args pass); the coordinates themselves go into the probe buffer's list region.
BUFFER_RW(b_gi_probe_traced, uint, 6);
BUFFER_RW(b_gi_probes, vec4, 7);

/// y = the young-pixel threshold in frames (0 = no temporal history to read), z > 0 = adaptive
/// gather enabled; other components unused here.
uniform vec4 u_gi_screen_trace;
/// Last frame's temporal moments (z = the frames each pixel had accumulated), for the young-pixel
/// gate.
SAMPLER2D(s_gi_prev_moments, 0);
/// The previous view projection: the anchor reprojects into last frame's view.
uniform mat4 u_gi_prev_view_proj;

/// The lighting prior's request and the carried floor (see the header): writes [8] (z = the
/// floor's age, w = the request) and, for a carried floor, [12..15]. @p prev_uv is the anchor's
/// unclamped uv in last frame's view, valid when @p prev_in_front.
void GiClassifyPrior(uint record, vec3 world_position, vec4 meta2, vec2 prev_uv, bool prev_in_front)
{
	bool trace_history = false;
	bool filter_history = false;
	uint floor_record = 0u;
	float floor_age = 0.0;
	if(u_gi_probe_trusted && prev_in_front)
	{
		ivec2 centre = clamp(ivec2(floor(GiProbeLatticeOfPrevPixel(clamp(prev_uv, vec2_splat(0.0), vec2_splat(1.0)) *
		                                                            u_gi_probe_screen.xy))),
		                     ivec2(0, 0),
		                     ivec2(u_gi_probe_count_x - 1, u_gi_probe_count_y - 1));
		// The trace's lookup: the record at the clamped reprojection, on this anchor's plane.
		uint centre_base = (GiProbeRecord(centre.x, centre.y, 0) + u_gi_probe_read_offset) * uint(GI_PROBE_STRIDE);
		vec4 centre_meta = b_gi_probes[centre_base + uint(GI_PROBE_META)];
		vec4 centre_held = b_gi_probes[centre_base + uint(GI_PROBE_IMPORTANCE_FRAMES)];
		float plane_tolerance = GI_FILTER_PLANE_TOLERANCE * max(meta2.w, 0.1);
		trace_history = centre_meta.w > 0.5 && centre_held.x >= 1.0 &&
		                abs(dot(centre_meta.xyz - world_position, meta2.xyz)) < plane_tolerance;
		// The filter's candidates: within one tile of last frame's border, the 3x3 around the
		// reprojection on this anchor's plane holding a state. The centre's floor is the tile's,
		// whichever surface the anchor is on.
		vec2 reach = vec2_splat(u_gi_probe_spacing) * u_gi_probe_screen.zw;
		if(all(greaterThanEqual(prev_uv, -reach)) && all(lessThanEqual(prev_uv, vec2_splat(1.0) + reach)))
		{
			if(centre_meta.w > 0.5)
			{
				floor_record = centre_base;
				floor_age = max(centre_held.z, 0.0);
			}
			for(int n = 0; n < 9; ++n)
			{
				ivec2 candidate = centre + ivec2(n % 3 - 1, n / 3 - 1);
				if(candidate.x < 0 || candidate.y < 0 || candidate.x >= u_gi_probe_count_x ||
				   candidate.y >= u_gi_probe_count_y)
				{
					continue;
				}
				uint candidate_base =
				    (GiProbeRecord(candidate.x, candidate.y, 0) + u_gi_probe_read_offset) * uint(GI_PROBE_STRIDE);
				vec4 candidate_meta = b_gi_probes[candidate_base + uint(GI_PROBE_META)];
				if(candidate_meta.w > 0.5 &&
				   abs(dot(candidate_meta.xyz - world_position, meta2.xyz)) < plane_tolerance &&
				   b_gi_probes[candidate_base + uint(GI_PROBE_IMPORTANCE_FRAMES)].x >= 1.0)
				{
					filter_history = true;
				}
			}
		}
	}
	bool carried = floor_age >= 1.0 && floor_age < float(GI_IMPORTANCE_STATE_FRAMES);
	if(carried)
	{
		for(int lane = 0; lane < 4; ++lane)
		{
			b_gi_probes[record + uint(GI_PROBE_FLOOR + lane)] = b_gi_probes[floor_record + uint(GI_PROBE_FLOOR + lane)];
		}
	}
	bool request = !carried && (!trace_history || !filter_history);
	b_gi_probes[record + uint(GI_PROBE_IMPORTANCE_FRAMES)] =
	    vec4(0.0, 0.0, carried ? floor_age + 1.0 : 0.0, request ? 1.0 : 0.0);
}

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
		// writes the black tile.
		return;
	}
	vec3 world_position = meta.xyz;
	vec4 meta2 = b_gi_probes[record + uint(GI_PROBE_META2)];
	// The anchor in last frame's view: the lighting prior's request.
	vec4 prev_clip4 = mul(u_gi_prev_view_proj, vec4(world_position, 1.0));
	bool prev_in_front = prev_clip4.w > 0.0;
	vec2 prev_uv = vec2_splat(-1.0);
	if(prev_in_front)
	{
		prev_uv = clipTransform(prev_clip4.xyz / prev_clip4.w).xy * 0.5 + vec2_splat(0.5);
	}
	GiClassifyPrior(record, world_position, meta2, prev_uv, prev_in_front);
	bool interpolated = false;
	// The revalidation phase is keyed by the lattice coordinate, which stays with the surfaces
	// while the camera turns.
	ivec2 key = GiProbeLatticeKey(probe);
	bool revalidate =
	    ((uint(key.x) * 3u + uint(key.y) * 5u + u_gi_probe_frame) %
	     uint(GI_ADAPTIVE_REVALIDATE_FRAMES)) == 0u;
	// LAST frame's mode of the same surfaces, from the read half: a sticky-traced probe stays
	// traced between revalidations. Its own revalidation frame re-decides.
	ivec2 last_probe = GiProbeLatticePrevIndex(probe);
	float last_mode = 0.0;
	if(last_probe.x >= 0 && last_probe.y >= 0 && last_probe.x < u_gi_probe_count_x &&
	   last_probe.y < u_gi_probe_count_y)
	{
		uint last_record = (GiProbeRecord(last_probe.x, last_probe.y, 0) + u_gi_probe_read_offset) *
		                   uint(GI_PROBE_STRIDE);
		last_mode = b_gi_probes[last_record + uint(GI_PROBE_META)].w;
	}
	bool odd = ((probe.x | probe.y) & 1) != 0;
	bool adaptive = u_gi_screen_trace.z > 0.0 && odd;
	// THE GEOMETRIC GATE, first and for every odd probe: may its even-lattice parents stand in
	// for it at all? Each parent must lie within tolerance of THIS probe's tangent plane and this
	// probe within tolerance of the parent's - the plane test the integrate pass applies per pixel
	// (and Lumen's adaptive placement: plane distance to the scene plane over depth), so a skipped
	// probe is one whose pixels would have blended those parents at near-full weight anyway.
	// Testing the probe against a plane (or a line) fitted THROUGH the parents cannot work: at a
	// depth edge the parents straddle the edge, their connecting line runs along the view ray,
	// and the probe between them lies on it whichever surface it sits on - such a test passes
	// almost every odd probe, and silhouette probes take a blend of near- and far-surface
	// lighting that slides with the lattice under a camera turn.
	// The G-buffer normal carries normal maps; as a plane DISTANCE over one tile its tilt costs
	// sin(tilt) x the tile's footprint, inside the 5 percent tolerance at spacings up to 32 px
	// except at grazing incidence, where the probe is traced - the safe direction.
	// Distance alone cannot tell two surfaces apart where they MEET: near a corner or a ledge
	// every anchor lies close to both planes, and the tolerance grows with view distance. The
	// normals must agree too (GI_ADAPTIVE_NORMAL_MIN_COS) - a parent on the wall beside a floor
	// probe traces only the wall's hemisphere, and its zeroed back half would darken the blend.
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
			   abs(dot(to_parent, parent_normal)) > tolerance ||
			   dot(parent_normal, meta2.xyz) < GI_ADAPTIVE_NORMAL_MIN_COS)
			{
				parents_stand_in = false;
			}
		}
	}
	// THE YOUNG-PIXEL GATE: the parents stand in only for a probe whose anchor pixel already holds
	// the history to average their blend with (last frame's count, reprojected; off screen = none).
	BRANCH
	if(parents_stand_in && u_gi_screen_trace.y > 0.0)
	{
		float pixel_frames = 0.0;
		if(prev_in_front && all(greaterThanEqual(prev_uv, vec2_splat(0.0))) && all(lessThan(prev_uv, vec2_splat(1.0))))
		{
			pixel_frames = texture2DLod(s_gi_prev_moments, prev_uv, 0.0).z;
		}
		parents_stand_in = pixel_frames >= u_gi_screen_trace.y;
	}
	// Revalidation and the sticky mode are decisions about probes the geometry ALLOWS to be
	// skipped. A probe that fails the gates above is simply traced (mode 1): routing it through
	// mode 4 would let the interp pass overwrite its trace with the parents' blend one frame
	// in eight.
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
