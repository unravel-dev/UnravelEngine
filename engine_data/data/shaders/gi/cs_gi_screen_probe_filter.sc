/*
 * GI probe-space filter + irradiance convolution - one thread group per probe.
 *
 * Filters each direction across the 3x3 probe neighbourhood - a 3-probe kernel in probe space
 * is a ~48-pixel kernel in screen space [S21 s57] - with the two guards that matter:
 *  - plane agreement between probe anchors (no borrowing across a depth break), and
 *  - the CONTACT-SHADOW-PRESERVING angle test [S21 s61]: the neighbour's hit, with its distance
 *    CLAMPED to our own, reprojected toward this probe; if the direction disagrees by more than
 *    GI_FILTER_ANGLE_LIMIT_COS the neighbour saw meaningfully different visibility. The clamp
 *    is the fix for the naive test's failure - distant hits have no parallax, always pass, and
 *    leak over local shadowing.
 *
 * Every pass taps the direct neighbours, interpolated probes (the adaptive gather) included:
 * a wider stride over skipped regions doubles the kernel's reach and the parallax the angle
 * test tolerates, and blurs contact shading off the corners it belongs to.
 *
 * A YOUNG probe - its importance state continued at its surface for fewer than
 * GI_FILTER_YOUNG_FRAMES frames: the probes a moving camera brings on screen - also takes the
 * outer ring of the 5x5 probes in the first pass, under the same plane and hit-angle tests. Its
 * pixels start from this frame's gather alone; the wider kernel is what they have in place of a
 * history, and it is gone from the probe before it is a large share of their window. A young
 * probe is never capped (below), so the ring stays out of the governor's capped re-average.
 *
 * THE FIREFLY GOVERNOR is the first pass. It keeps a sporadic bright tap - one ray that found a
 * small bright source, the bright phase of the 16-frame direction cycle on a partly lit cone -
 * from carrying its probe's footprint for a frame, and it keeps no light: what it clips comes
 * back as the direction's running mean. Every tap is capped at GI_GATHER_FIREFLY_CLAMP x the
 * probe's IMPORTANCE STATE - the luminance of each 2x2 direction block of the probe-filtered
 * radiance, taken BEFORE the cap and averaged over GI_IMPORTANCE_STATE_FRAMES frames at the
 * surface (it continues the state of the probe that covered this anchor's world position last
 * frame, within one tile of last frame's view, or starts from its neighbours' as a one-frame
 * prior), floored by the state's tile mean. The light the cap removes from a texel is averaged
 * over the frames the probe has capped in a row (the EXCESS ATLAS, continued with the state)
 * and added back, so the cap shapes a frame's noise while the output's expectation stays the
 * uncapped one at every age: a revealed probe and a settled one on the same surface converge
 * to the same light, and the strips a moving camera reveals come in at the level of the image
 * around them. Only a SETTLED reference caps - the state continued at its surface for the whole
 * window and its mean holding all of it (a lighting change's relight shortens the mean):
 * a younger or shorter mean clips more, and the running mean of an excess that shrinks as its
 * reference settles would return too much. An unsettled probe passes its taps uncapped and its
 * excess mean starts over when it caps again. The state is also what next frame's trace reads
 * to allocate its rays.
 *
 * Then projects the filtered sphere onto third-order spherical harmonics (nine solid-angle weighted
 * coefficients per probe) and evaluates the clamped-cosine convolution from them into the probe's 8x8
 * octahedral IRRADIANCE tile (E(n)/pi), which is what integration samples at each pixel's own normal - Lumen's
 * irradiance path (ScreenProbeConvertToIrradiance).
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "gi/gi_constants.sh"
#include "gi/gi_probe_common.sh"
// The importance state is kept in pre-exposed luminance; last frame's is converted on read.
#include "gi/gi_pre_exposure.sh"

SAMPLER2D(s_probe_radiance, 0);
/// RW: the first pass writes the probe's importance state into record slots 0-3 and the frames it
/// holds into slot GI_PROBE_IMPORTANCE_FRAMES, which next frame's trace reads - through the buffer
/// it already binds - to supersample bright cones.
BUFFER_RW(b_gi_probes, vec4, 7);
IMAGE2D_WO(s_probe_irradiance_out, rgba16f, 2);
/// The filtered RADIANCE (rgb, w = the probe's own hitT lane verbatim, so the next pass's
/// hit-angle test still has it), written by every pass - each pass reads the previous one's
/// output through s_probe_radiance (gi_resolve_pass ping-pongs two atlases), and the final
/// pass's is what the rough specular (fs_gi_rough_specular.sc) integrates its lobes over.
IMAGE2D_WO(s_probe_filtered_out, rgba16f, 3);
/// The EXCESS ATLAS (rgb): per texel, the running mean of the light the cap removed over the
/// frames the probe has capped in a row - last frame's, point-read through the state's
/// candidate, and this frame's, written by the first pass (zero where the probe did not cap).
SAMPLER2D(s_gi_probe_excess, 1);
IMAGE2D_WO(s_probe_excess_out, rgba16f, 4);
/// x = 1 for a radiance-only pass (write s_probe_filtered_out and stop), 0 for the final
/// pass that also convolves to irradiance. y = 1 for the FIRST pass, which governs the taps
/// and advances the importance state. z = the frames the state's mean may hold: one during
/// the frames after a lighting change in which the light voxels relight, so the cap follows
/// the switched light frame by frame, then growing to GI_IMPORTANCE_STATE_FRAMES. w unused.
uniform vec4 u_gi_probe_filter;
#define u_gi_probe_filter_radiance_only (u_gi_probe_filter.x > 0.5)
#define u_gi_probe_filter_governs       (u_gi_probe_filter.y > 0.5)
#define u_gi_probe_filter_state_frames  u_gi_probe_filter.z
/// Previous view projection: the anchor reprojects into LAST frame's lattice for the state it
/// continues (the trace reprojects the same way for its ray allocation).
uniform mat4 u_gi_prev_view_proj;

/// Where the state's age stops counting: exact in half precision should the record ever be
/// stored that way.
#define GI_IMPORTANCE_STATE_AGE_CAP 1024.0

SHARED vec4 s_filtered[GI_PROBE_DIR_COUNT];
/// The 3x3 neighbourhood's metas and plane weights are per-GROUP quantities: staged once by
/// nine threads instead of being re-derived by all 64 (704 buffer loads per probe where 11
/// carry information).
SHARED vec4 s_nb_meta[9];
SHARED float s_nb_weight[9];
/// Anchor-to-anchor distance, the parallax baseline of the adaptive angle test below.
SHARED float s_nb_baseline[9];
/// The young ring's taps (the outer ring of the 5x5, GiFilterRingOffset), staged the same way by the
/// next sixteen threads in the first pass.
#define GI_FILTER_RING_TAPS 16
SHARED vec4 s_ring_meta[GI_FILTER_RING_TAPS];
SHARED float s_ring_weight[GI_FILTER_RING_TAPS];
SHARED float s_ring_baseline[GI_FILTER_RING_TAPS];
/// Every thread's decoded direction, for the SH3 projection below: 64 threads re-decoding all
/// 64 directions would run GiOctDecode (a normalize among other things) 4096 times per probe
/// for 64 distinct values each thread already computes once.
SHARED vec3 s_dir[GI_PROBE_DIR_COUNT];
/// Every texel's solid angle (GiOctTexelSolidAngle): the octahedral map is not equal-area.
SHARED float s_omega[GI_PROBE_DIR_COUNT];
/// The probe's filtered radiance in nine real SH coefficients (rgb, w = the measured lane), projected once per
/// probe - one coefficient per thread on threads 0-8 - and evaluated by every thread.
SHARED vec4 s_sh[9];
/// The importance state (first pass): every texel's filtered luminance before the cap, the state
/// of each direction block continued from last frame, its tile mean, the frames its mean holds
/// and its AGE - the frames it has been continued at the surface, which a lighting change's
/// window collapse does not reset.
SHARED float s_tap_luma[GI_PROBE_DIR_COUNT];
SHARED float s_state[GI_PROBE_BLOCK_COUNT];
SHARED float s_state_mean;
SHARED float s_state_frames;
SHARED float s_state_age;
/// Last frame's candidates for the state to continue: the probe that covered this anchor's world
/// position ([4]) and the eight around it in last frame's lattice - the frames each one's state
/// holds (0 = none usable), its age and its record.
SHARED float s_last_frames[9];
SHARED float s_last_age[9];
SHARED uint s_last_record[9];
/// The state candidate [4]'s lattice coordinates and the frames it had capped in a row, and this
/// probe's count this frame (0 = not capped).
SHARED ivec2 s_last_coord;
SHARED float s_last_capped;
SHARED float s_capped_frames;

/// Real spherical-harmonic basis normalisations for bands 0-2, and the clamped-cosine convolution's band scales
/// in the E/pi convention (A_l / pi = 1, 2/3, 1/4 [Ramamoorthi and Hanrahan 2001]).
#define GI_SH_BASIS_0        0.282095
#define GI_SH_BASIS_1        0.488603
#define GI_SH_BASIS_2_CROSS  1.092548
#define GI_SH_BASIS_2_ZZ     0.315392
#define GI_SH_BASIS_2_XX_YY  0.546274
#define GI_SH_COSINE_BAND_1  (2.0 / 3.0)
#define GI_SH_COSINE_BAND_2  0.25

/// Rec. 709 luminance of a radiance value, negative lobes dropped.
float GiFilterLuminance(vec3 radiance)
{
	return dot(max(radiance, vec3_splat(0.0)), vec3(0.2126, 0.7152, 0.0722));
}

/// One lane of a record vec4 by index. Explicit components rather than a dynamically indexed
/// vec4, which does not survive every backend translation.
float GiFilterLane(vec4 value, int lane)
{
	return lane == 0 ? value.x : (lane == 1 ? value.y : (lane == 2 ? value.z : value.w));
}

/// The radiance capped at @p ceiling by luminance, its hue kept.
vec3 GiFilterCap(vec3 radiance, float ceiling)
{
	float luminance = GiFilterLuminance(radiance);
	return luminance > ceiling ? radiance * (ceiling / luminance) : radiance;
}

/// The offset of tap @p ring_n of the young ring: the 5x5's outer ring, row by row.
ivec2 GiFilterRingOffset(int ring_n)
{
	if(ring_n < 5)
	{
		return ivec2(ring_n - 2, -2);
	}
	if(ring_n < 11)
	{
		int side = ring_n - 5;
		return ivec2((side % 2) * 4 - 2, side / 2 - 1);
	}
	return ivec2(ring_n - 13, 2);
}

/**
 * The hitT-clamped reprojection test of one tap: true when the neighbour's hit, its distance CLAMPED
 * to this probe's own, reprojects toward this probe within the accepted angle, or when either probe
 * has no hit (a completed/sky texel has no parallax to test and shares freely). The limit is
 * PARALLAX-ADAPTIVE: a co-planar neighbour's hit reprojects with an error of about baseline/hitT
 * purely from geometry, so a fixed pi/50 would reject ALL sharing for hits within ~16 baselines -
 * exactly the voxel-read band, whose per-probe sampling bias would then stand unfiltered as wall
 * blotches. The accepted angle is GI_FILTER_PARALLAX_SCALE x that intrinsic error, capped
 * (GI_FILTER_ANGLE_RELAX_MAX - contact scale stays the screen trace's), floored by the published
 * pi/50 for the far field (min of cosines = wider angle wins).
 */
bool GiFilterHitAgrees(vec3 neighbour_position, float baseline, float neighbour_hit_t, float own_hit_t,
                       vec3 center_position, vec3 dir)
{
	if(own_hit_t <= 0.0 || neighbour_hit_t <= 0.0)
	{
		return true;
	}
	float clamped_t = min(neighbour_hit_t, own_hit_t);
	vec3 reprojected = neighbour_position + dir * clamped_t - center_position;
	float reprojected_length = max(length(reprojected), 1e-4);
	float parallax = GI_FILTER_PARALLAX_SCALE * baseline / max(clamped_t, 1e-3);
	float limit_cos = min(GI_FILTER_ANGLE_LIMIT_COS, cos(min(parallax, GI_FILTER_ANGLE_RELAX_MAX)));
	return dot(reprojected / reprojected_length, dir) >= limit_cos;
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
	vec4 center_meta = b_gi_probes[base + uint(GI_PROBE_META)];
	vec4 center_meta2 = b_gi_probes[base + uint(GI_PROBE_META2)];
	bool center_valid = center_meta.w > 0.5;
	bool governs = u_gi_probe_filter_governs && center_valid;
	int dir_index = local.y * GI_PROBE_DIR_EDGE + local.x;
	vec2 dir_uv = (vec2(local.xy) + vec2_splat(0.5)) / float(GI_PROBE_DIR_EDGE);
	vec3 dir = GiOctDecode(dir_uv);
	s_dir[dir_index] = dir;
	s_omega[dir_index] = GiOctTexelSolidAngle(local, GI_PROBE_DIR_EDGE);
	float plane_tolerance = GI_FILTER_PLANE_TOLERANCE * max(center_meta2.w, 0.1);
	if(dir_index < 9)
	{
		int ox = dir_index % 3 - 1;
		int oy = dir_index / 3 - 1;
		int nx = probe.x + ox;
		int ny = probe.y + oy;
		float plane_weight = 0.0;
		vec4 neighbour_meta = vec4_splat(0.0);
		if(center_valid && nx >= 0 && ny >= 0 && nx < u_gi_probe_count_x && ny < u_gi_probe_count_y)
		{
			uint neighbour_base =
			    (GiProbeRecord(nx, ny, 0) + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
			neighbour_meta = b_gi_probes[neighbour_base + uint(GI_PROBE_META)];
			if(neighbour_meta.w > 0.5)
			{
				float plane = abs(dot(neighbour_meta.xyz - center_meta.xyz, center_meta2.xyz));
				plane_weight = saturate(1.0 - plane / plane_tolerance);
			}
		}
		s_nb_meta[dir_index] = neighbour_meta;
		s_nb_weight[dir_index] = plane_weight;
		s_nb_baseline[dir_index] = length(neighbour_meta.xyz - center_meta.xyz);
		// The state to continue: the anchor reprojects into last frame's lattice, clamped to its
		// border when it lands within one tile of it (content the camera just brought on screen
		// shares a surface with the nearest border probe), and this thread's candidate is the
		// probe at that index plus its own offset. A candidate counts when it held geometry on
		// this anchor's plane.
		float last_frames = 0.0;
		float last_age = 0.0;
		uint last_record = 0u;
		ivec2 last_coord = ivec2(0, 0);
		float last_capped = 0.0;
		if(governs && u_gi_probe_trusted)
		{
			vec4 prev_clip4 = mul(u_gi_prev_view_proj, vec4(center_meta.xyz, 1.0));
			vec3 prev_clip = clipTransform(prev_clip4.xyz / max(prev_clip4.w, 1e-6));
			vec2 prev_uv = prev_clip.xy * 0.5 + 0.5;
			vec2 reach = vec2_splat(u_gi_probe_spacing) * u_gi_probe_screen.zw;
			bool in_reach = all(greaterThanEqual(prev_uv, -reach)) && all(lessThanEqual(prev_uv, vec2_splat(1.0) + reach));
			prev_uv = clamp(prev_uv, vec2_splat(0.0), vec2_splat(1.0));
			ivec2 last_probe = clamp(ivec2(floor(GiProbeLatticeOfPrevPixel(prev_uv * u_gi_probe_screen.xy))),
			                         ivec2(0, 0),
			                         ivec2(u_gi_probe_count_x - 1, u_gi_probe_count_y - 1)) +
			                   ivec2(ox, oy);
			if(prev_clip4.w > 0.0 && in_reach && last_probe.x >= 0 && last_probe.y >= 0 &&
			   last_probe.x < u_gi_probe_count_x && last_probe.y < u_gi_probe_count_y)
			{
				uint last_base = (GiProbeRecord(last_probe.x, last_probe.y, 0) + u_gi_probe_read_offset) *
				                 uint(GI_PROBE_STRIDE);
				vec4 last_meta = b_gi_probes[last_base + uint(GI_PROBE_META)];
				float last_plane = abs(dot(last_meta.xyz - center_meta.xyz, center_meta2.xyz));
				if(last_meta.w > 0.5 && last_plane < plane_tolerance)
				{
					vec4 last_held = b_gi_probes[last_base + uint(GI_PROBE_IMPORTANCE_FRAMES)];
					last_frames = max(last_held.x, 0.0);
					last_age = max(last_held.y, 0.0);
					last_record = last_base;
					last_coord = last_probe;
					last_capped = max(last_held.w, 0.0);
				}
			}
		}
		s_last_frames[dir_index] = last_frames;
		s_last_age[dir_index] = last_age;
		s_last_record[dir_index] = last_record;
		if(dir_index == 4)
		{
			s_last_coord = last_coord;
			s_last_capped = last_capped;
		}
	}
	else if(dir_index < 9 + GI_FILTER_RING_TAPS)
	{
		int ring_n = dir_index - 9;
		ivec2 ring_probe = probe + GiFilterRingOffset(ring_n);
		vec4 ring_meta = vec4_splat(0.0);
		float ring_weight = 0.0;
		if(governs && ring_probe.x >= 0 && ring_probe.y >= 0 && ring_probe.x < u_gi_probe_count_x &&
		   ring_probe.y < u_gi_probe_count_y)
		{
			uint ring_base = (GiProbeRecord(ring_probe.x, ring_probe.y, 0) + u_gi_probe_write_offset) *
			                 uint(GI_PROBE_STRIDE);
			ring_meta = b_gi_probes[ring_base + uint(GI_PROBE_META)];
			if(ring_meta.w > 0.5)
			{
				float plane = abs(dot(ring_meta.xyz - center_meta.xyz, center_meta2.xyz));
				ring_weight = saturate(1.0 - plane / plane_tolerance);
			}
		}
		s_ring_meta[ring_n] = ring_meta;
		s_ring_weight[ring_n] = ring_weight;
		s_ring_baseline[ring_n] = length(ring_meta.xyz - center_meta.xyz);
	}
	barrier();
	// A YOUNG probe (see the header): the age its state reaches this frame - one when it starts fresh.
	float young_age = s_last_frames[4] >= 1.0 ? s_last_age[4] + 1.0 : 1.0;
	bool young = governs && young_age < float(GI_FILTER_YOUNG_FRAMES) - 0.5;
	vec4 filtered = vec4_splat(0.0);
	float own_hit_t = 0.0;
	vec4 center_texel = vec4_splat(0.0);
	// The taps and the weights they passed with are kept: the governor re-averages them capped.
	vec4 neighbour_values[9];
	float neighbour_weights[9];
	float weight_sum = 0.0;
	UNROLL
	for(int clear_n = 0; clear_n < 9; ++clear_n)
	{
		neighbour_values[clear_n] = vec4_splat(0.0);
		neighbour_weights[clear_n] = 0.0;
	}
	if(center_valid)
	{
		center_texel =
		    texelFetch(s_probe_radiance, GiProbeAtlasBase(probe.x, probe.y, 0) + local, 0);
		own_hit_t = center_texel.w;
		// The nine taps are fetched before any is tested - a tap's test reads only its own value,
		// so the fetches need not wait on each other. A tap without plane weight fetches its
		// clamped neighbour and discards it (a weighted tap is always in range).
		UNROLL
		for(int fetch_n = 0; fetch_n < 9; ++fetch_n)
		{
			int fetch_x = clamp(probe.x + fetch_n % 3 - 1, 0, u_gi_probe_count_x - 1);
			int fetch_y = clamp(probe.y + fetch_n / 3 - 1, 0, u_gi_probe_count_y - 1);
			neighbour_values[fetch_n] =
			    texelFetch(s_probe_radiance, GiProbeAtlasBase(fetch_x, fetch_y, 0) + local, 0);
		}
		UNROLL
		for(int n = 0; n < 9; ++n)
		{
			float weight = s_nb_weight[n];
			if(weight <= 1e-3)
			{
				continue;
			}
			vec4 neighbour_value = neighbour_values[n];
			if(!GiFilterHitAgrees(s_nb_meta[n].xyz, s_nb_baseline[n], neighbour_value.w, own_hit_t,
			                      center_meta.xyz, dir))
			{
				continue;
			}
			filtered.xyz += neighbour_value.xyz * weight;
			neighbour_weights[n] = weight;
			weight_sum += weight;
		}
		// A young probe's ring (see the header). A weighted tap is always in range.
		BRANCH
		if(young)
		{
			LOOP
			for(int ring_n = 0; ring_n < GI_FILTER_RING_TAPS; ++ring_n)
			{
				float ring_weight = s_ring_weight[ring_n];
				if(ring_weight <= 1e-3)
				{
					continue;
				}
				ivec2 ring_probe = probe + GiFilterRingOffset(ring_n);
				vec4 ring_value =
				    texelFetch(s_probe_radiance, GiProbeAtlasBase(ring_probe.x, ring_probe.y, 0) + local, 0);
				if(!GiFilterHitAgrees(s_ring_meta[ring_n].xyz, s_ring_baseline[ring_n], ring_value.w, own_hit_t,
				                      center_meta.xyz, dir))
				{
					continue;
				}
				filtered.xyz += ring_value.xyz * ring_weight;
				weight_sum += ring_weight;
			}
		}
		filtered.xyz = weight_sum > 1e-4 ? filtered.xyz / weight_sum : center_texel.xyz;
		filtered.w = 1.0;
	}
	// THE IMPORTANCE STATE (first pass, see the header). Each block's value this frame is the
	// mean uncapped filtered luminance of its four texels; the state continues the probe that
	// covered this anchor last frame, or - where that probe held nothing - starts from the
	// frames-weighted mean of the eight around it as a one-frame prior. Every barrier stays in
	// uniform flow: the work behind it is what the governs flag guards.
	s_tap_luma[dir_index] = GiFilterLuminance(filtered.xyz);
	barrier();
	if(dir_index < GI_PROBE_BLOCK_COUNT)
	{
		ivec2 block_base = ivec2((dir_index % 4) * 2, (dir_index / 4) * 2);
		float now = 0.0;
		for(int block_t = 0; block_t < 4; ++block_t)
		{
			ivec2 block_texel = block_base + ivec2(block_t % 2, block_t / 2);
			now += s_tap_luma[block_texel.y * GI_PROBE_DIR_EDGE + block_texel.x];
		}
		now *= 0.25;
		float state = now;
		float frames = 1.0;
		float age = 1.0;
		BRANCH
		if(governs)
		{
			int lane = dir_index % 4;
			uint slot = uint(dir_index / 4);
			float previous = 0.0;
			float previous_frames = 0.0;
			if(s_last_frames[4] >= 1.0)
			{
				previous = GiFilterLane(b_gi_probes[s_last_record[4] + slot], lane);
				previous_frames = s_last_frames[4];
				age = min(s_last_age[4] + 1.0, GI_IMPORTANCE_STATE_AGE_CAP);
			}
			else
			{
				float prior_weight = 0.0;
				LOOP
				for(int prior_n = 0; prior_n < 9; ++prior_n)
				{
					if(s_last_frames[prior_n] < 1.0)
					{
						continue;
					}
					previous += GiFilterLane(b_gi_probes[s_last_record[prior_n] + slot], lane) * s_last_frames[prior_n];
					prior_weight += s_last_frames[prior_n];
				}
				if(prior_weight > 0.0)
				{
					previous /= prior_weight;
					previous_frames = 1.0;
				}
			}
			if(previous_frames >= 1.0)
			{
				frames = min(previous_frames + 1.0, max(u_gi_probe_filter_state_frames, 1.0));
				state = mix(previous * u_history_pre_exposure_correction, now, 1.0 / frames);
			}
		}
		s_state[dir_index] = state;
		if(dir_index == 0)
		{
			s_state_frames = frames;
			s_state_age = age;
		}
	}
	barrier();
	if(dir_index == 0)
	{
		float total = 0.0;
		for(int state_b = 0; state_b < GI_PROBE_BLOCK_COUNT; ++state_b)
		{
			total += s_state[state_b];
		}
		s_state_mean = total / float(GI_PROBE_BLOCK_COUNT);
		// A settled reference caps (see the header); the count restarts whenever the probe stops.
		float window = float(GI_IMPORTANCE_STATE_FRAMES);
		bool settled = governs && s_state_age > window - 0.5 && s_state_frames > window - 0.5;
		s_capped_frames = settled ? min((s_last_frames[4] >= 1.0 ? s_last_capped : 0.0) + 1.0, window) : 0.0;
	}
	barrier();
	BRANCH
	if(governs)
	{
		// The cap on a settled reference: no tap counts for more than GI_GATHER_FIREFLY_CLAMP times
		// what this direction's block carries on average, floored by the tile mean, and what it
		// removes returns as this texel's running mean over the frames the probe has capped in a
		// row. An unsettled probe keeps its uncapped taps (see the header).
		vec3 excess_mean = vec3_splat(0.0);
		BRANCH
		if(s_capped_frames > 0.5)
		{
			int block = (local.y / 2) * 4 + (local.x / 2);
			float ceiling =
			    GI_GATHER_FIREFLY_CLAMP * max(max(s_state[block], s_state_mean), GI_GATHER_FIREFLY_REFERENCE_FLOOR);
			vec3 capped_sum = vec3_splat(0.0);
			UNROLL
			for(int cap_n = 0; cap_n < 9; ++cap_n)
			{
				capped_sum += GiFilterCap(neighbour_values[cap_n].xyz, ceiling) * neighbour_weights[cap_n];
			}
			vec3 capped = weight_sum > 1e-4 ? capped_sum / weight_sum : GiFilterCap(center_texel.xyz, ceiling);
			excess_mean = max(filtered.xyz - capped, vec3_splat(0.0));
			if(s_capped_frames > 1.5)
			{
				// Last frame's mean, from the probe the state continued (it capped too), in last
				// frame's exposure.
				vec3 excess_previous =
				    texelFetch(s_gi_probe_excess, GiProbeAtlasBase(s_last_coord.x, s_last_coord.y, 0) + local, 0).xyz;
				excess_mean = mix(excess_previous * u_history_pre_exposure_correction, excess_mean, 1.0 / s_capped_frames);
			}
			filtered.xyz = capped + excess_mean;
		}
		imageStore(s_probe_excess_out, GiProbeAtlasBase(probe.x, probe.y, 0) + local, vec4(excess_mean, 0.0));
		// The state, four blocks per record vec4, then the frames its mean holds and its age.
		if(dir_index < 4)
		{
			b_gi_probes[base + uint(dir_index)] = vec4(s_state[dir_index * 4],
			                                           s_state[dir_index * 4 + 1],
			                                           s_state[dir_index * 4 + 2],
			                                           s_state[dir_index * 4 + 3]);
		}
		if(dir_index == 4)
		{
			// The floor's age stays as the classify / prior pass set it; the spent prior request
			// gives way to the capping count, which next frame's first pass continues.
			float floor_age = b_gi_probes[base + uint(GI_PROBE_IMPORTANCE_FRAMES)].z;
			b_gi_probes[base + uint(GI_PROBE_IMPORTANCE_FRAMES)] =
			    vec4(s_state_frames, s_state_age, floor_age, s_capped_frames);
		}
	}
	// Every pass hands its filtered sphere on: to the next pass, or from the final one to the
	// rough specular. An invalid probe writes zeros so the derived atlas is fully rewritten
	// like the trace atlas.
	imageStore(s_probe_filtered_out, GiProbeAtlasBase(probe.x, probe.y, 0) + local,
	           vec4(filtered.xyz, own_hit_t));
	// RADIANCE-ONLY PASS: stop here. Group-uniform, so no thread reaches the barriers below
	// alone.
	if(u_gi_probe_filter_radiance_only)
	{
		return;
	}
	s_filtered[dir_index] = filtered;
	barrier();
	// SH3 PROJECTION of the filtered sphere, once per probe: each direction weighted by its texel's SOLID ANGLE
	// (the octahedral map is not equal-area - GiOctTexelSolidAngle), so a uniform field L projects to exactly
	// E/pi = L below. The directions and solid angles come from shared memory. Nine threads, one coefficient
	// each, every coefficient summed over the directions in index order.
	if(center_valid && dir_index < 9)
	{
		vec4 coefficient = vec4_splat(0.0);
		for(int d = 0; d < GI_PROBE_DIR_COUNT; ++d)
		{
			vec3 w = s_dir[d];
			vec4 weighted = s_filtered[d] * s_omega[d];
			float basis = dir_index == 0   ? GI_SH_BASIS_0
			              : dir_index == 1 ? GI_SH_BASIS_1 * w.y
			              : dir_index == 2 ? GI_SH_BASIS_1 * w.z
			              : dir_index == 3 ? GI_SH_BASIS_1 * w.x
			              : dir_index == 4 ? GI_SH_BASIS_2_CROSS * w.x * w.y
			              : dir_index == 5 ? GI_SH_BASIS_2_CROSS * w.y * w.z
			              : dir_index == 6 ? GI_SH_BASIS_2_ZZ * (3.0 * w.z * w.z - 1.0)
			              : dir_index == 7 ? GI_SH_BASIS_2_CROSS * w.x * w.z
			                               : GI_SH_BASIS_2_XX_YY * (w.x * w.x - w.y * w.y);
			coefficient += weighted * basis;
		}
		s_sh[dir_index] = coefficient;
	}
	barrier();
	// Irradiance at THIS texel's normal direction from the SH3 coefficients: the clamped-cosine convolution's band
	// scales (GI_SH_COSINE_BAND_*), clamped at zero - band-limited SH rings slightly negative behind a bright lobe.
	// w is the measured lane evaluated the same way: texels the trace refused hand their share to integration's
	// weighting rather than reading as darkness.
	vec3 normal = dir;
	vec4 irradiance = vec4_splat(0.0);
	if(center_valid)
	{
		vec4 value = s_sh[0] * GI_SH_BASIS_0;
		value += (s_sh[1] * normal.y + s_sh[2] * normal.z + s_sh[3] * normal.x) * (GI_SH_BASIS_1 * GI_SH_COSINE_BAND_1);
		value += (s_sh[4] * (GI_SH_BASIS_2_CROSS * normal.x * normal.y) +
		          s_sh[5] * (GI_SH_BASIS_2_CROSS * normal.y * normal.z) +
		          s_sh[6] * (GI_SH_BASIS_2_ZZ * (3.0 * normal.z * normal.z - 1.0)) +
		          s_sh[7] * (GI_SH_BASIS_2_CROSS * normal.x * normal.z) +
		          s_sh[8] * (GI_SH_BASIS_2_XX_YY * (normal.x * normal.x - normal.y * normal.y))) *
		         GI_SH_COSINE_BAND_2;
		irradiance = vec4(max(value.xyz, vec3_splat(0.0)), saturate(value.w));
	}
	imageStore(s_probe_irradiance_out, GiProbeAtlasBase(probe.x, probe.y, 0) + local, irradiance);
}
