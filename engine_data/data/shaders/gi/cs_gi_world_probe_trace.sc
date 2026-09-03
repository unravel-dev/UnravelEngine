/*
 * Traces every world probe's per-frame direction stratum (gi_rewrite_plan.md 3.3, revised design - see
 * gi_world_probes.sh). One thread group per probe SLOT, one thread per ray:
 * thread t traces octahedral texel t * WINDOW + (frame mod WINDOW), so over one window every
 * texel of the 16x16 radiance atlas refreshes exactly once - the atlas IS the windowed mean,
 * with zero steady-state variance on a static scene (R1) and one-window reaction latency (R4).
 *
 * Rays sphere-trace the global cascade only (near_field 0): a probe is a coarse world-scale
 * structure and mesh-exact contact detail is the screen gather's job. Hits read the light
 * voxels; misses read the sky SH. A slot whose world cell changed (the window scrolled) is
 * claimed by zeroing every stratum but this frame's, so stale radiance from the departed cell
 * can never be read at the new position; the probe then refills over one window.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
// eval_radiance_sh lives here.
#include "../lighting.sh"

#include "gi/sdf_common.sh"
#define GI_LIGHT_VOXEL_READ
#include "gi/gi_light_voxels.sh"
#include "gi/gi_world_probes.sh"
#include "gi/gi_noise.sh"

/// rgb = radiance, a = hitT (negative = sky/miss). READ-write: the radiance is a converging
/// running mean over windows (GI_WORLD_PROBE_EMA_WINDOWS) - the read is this texel's own
/// previous value; hitT stays the latest sample.
IMAGE2D_RW(s_world_probe_radiance_out, rgba16f, 5);
/// One packed cell id per probe slot across all cascades (GiWorldProbePackCell).
BUFFER_RW(b_world_probe_cells, uint, 6);
/// Complete windows accumulated per probe slot since its claim or the last fast window - the
/// running mean's count (saturating at GI_WORLD_PROBE_EMA_WINDOWS).
BUFFER_RW(b_world_probe_counts, uint, 7);
/// xy = this window's R2 offset for the sub-texel direction jitter (double on the CPU, from
/// the window index). zw unused.
uniform vec4 u_gi_world_probe_jitter;
/// The lighting pass's environment SH probe, for the sky at ray miss.
SAMPLER2D(s_gi_env_sh, 14);
/// The PARENT cascade's convolved irradiance, for scroll-in seeding. Read-only and a
/// DIFFERENT texture from the radiance atlas being written, so sampling it here is legal.
SAMPLER2D(s_world_probe_irradiance_seed, 11);
/// xy = 1 / irradiance-depth atlas size (the seeding read shares the irradiance tile layout).
/// z = strata per frame (the fast-refresh window). Carried HERE, in a trace-only uniform,
/// rather than in u_gi_world_probe_params.w: that lane is the cage-visibility variance gate
/// for every reading consumer, and aliasing the two meant one added #define away from the
/// gate silently becoming 1.0 or 2.0 and the sealed-box leak defence never marching.
uniform vec4 u_gi_world_probe_seed_atlas;

/// x = window centre CELL of level 0 (int as float) per axis... levels each get a vec4:
/// xyz = centre cell, w = ray max distance for that level's probes.
uniform vec4 u_gi_world_probe_window[SDF_CLIPMAP_LEVEL_COUNT];

NUM_THREADS(GI_WORLD_PROBE_RAYS_PER_FRAME, 1, 1)
void main()
{
	int slot_linear = int(gl_WorkGroupID.x);
	int per_level = GI_WORLD_PROBE_AXIS * GI_WORLD_PROBE_AXIS * GI_WORLD_PROBE_AXIS;
	int level = slot_linear / per_level;
	if(level >= SDF_CLIPMAP_LEVEL_COUNT)
	{
		return;
	}
	int in_level = slot_linear % per_level;
	ivec3 slot = ivec3(in_level % GI_WORLD_PROBE_AXIS,
	                   (in_level / GI_WORLD_PROBE_AXIS) % GI_WORLD_PROBE_AXIS,
	                   in_level / (GI_WORLD_PROBE_AXIS * GI_WORLD_PROBE_AXIS));
	// The world cell this slot represents under the current window: the unique cell in
	// [centre - half, centre + half] whose mod-AXIS equals the slot.
	ivec3 center_cell = ivec3(u_gi_world_probe_window[level].xyz);
	int half_axis = (GI_WORLD_PROBE_AXIS - 1) / 2;
	ivec3 window_base = center_cell - ivec3(half_axis, half_axis, half_axis);
	ivec3 base_slot = GiWorldProbeSlot(window_base);
	ivec3 offset = ivec3((slot.x - base_slot.x + GI_WORLD_PROBE_AXIS) % GI_WORLD_PROBE_AXIS,
	                     (slot.y - base_slot.y + GI_WORLD_PROBE_AXIS) % GI_WORLD_PROBE_AXIS,
	                     (slot.z - base_slot.z + GI_WORLD_PROBE_AXIS) % GI_WORLD_PROBE_AXIS);
	ivec3 cell = window_base + offset;
	vec3 origin = GiWorldProbeCellPosition(cell, level);
	int thread = int(gl_LocalInvocationID.x);
	// FAST-REFRESH WINDOW (gi_rewrite_plan.md section 8, the DDGI event pattern adapted): while the
	// light set is changing, each frame covers TWO strata instead of one, halving the window to
	// 8 frames so a moved or toggled light propagates through the probes at double speed. The
	// stratum formula stays exhaustive either way: count consecutive strata per frame cover
	// every direction once per (WINDOW / count) frames.
	int stratum_count = int(max(u_gi_world_probe_seed_atlas.z, 1.0));
	uint stratum_base = (u_world_probe_frame * uint(stratum_count)) % uint(GI_WORLD_PROBE_WINDOW);
	ivec2 tile = GiWorldProbeTileBase(slot, level, GI_WORLD_PROBE_OCT_RADIANCE);
	// Scroll claim: the slot's stored cell is compared by every thread (uniform read), thread 0
	// rewrites it, and every thread zeroes the OTHER strata of its own texel column so no stale
	// direction survives into the new cell's window. The claim and the zeroing are idempotent,
	// so the race between thread 0's write and other groups' reads next frame is harmless.
	uint packed_cell = GiWorldProbePackCell(cell, level);
	int slot_index = GiWorldProbeSlotIndex(slot, level);
	// DEAD PROBE gate (the problem RTXGI answers with relocation/classification, answered here
	// by the field itself): a lattice point inside geometry is poison. The trace's launch-slab
	// walk lets its rays exit on EITHER side of the wall it is buried in, so its atlas mixes
	// both sides' light - including sky through the miss fallback - while its depth moments in
	// room-facing directions measure the open ROOM; Chebyshev then trusts it at FULL weight.
	// Visibility-true, content-false: one embedded junction probe floods a sealed interior
	// with the sunlit exterior (measured - no weight floor or bias tuning can reject it,
	// because the visibility math is being told the truth about the wrong point). Writing
	// zero radiance with ZERO-DISTANCE hits collapses its convolved depth, and the visibility
	// test itself then kills it at every read. Cost: one field sample per probe slice.
	bool buried = SdfSampleClipmap(origin) < 0.0;
	bool fresh = b_world_probe_cells[slot_index] != packed_cell;
	// CONVERGING MEAN (GI_WORLD_PROBE_EMA_WINDOWS). The atlas used to be a windowed mean over
	// FIXED texel-centre directions: zero variance, but BIASED per probe - a small emitter is
	// skewered or missed per direction and neighbouring probes disagree, which entered the
	// voxel bounce as the blotch field on emissive-lit walls. Directions now jitter inside
	// their texel per window and each texel is a running mean over the last windows; a fresh
	// claim or a fast (light/content change) window resets the count so changes still land
	// in one window at write-through. Every thread reads the count before thread 0 advances it.
	uint windows_seen = fresh ? 0u : b_world_probe_counts[slot_index];
	bool fast_window = stratum_count > 1;
	// The jitter/mean is a SETTING (u_gi_world_probe_jitter.z, gi_resolve_pass::settings::
	// world_probe_jitter): off, every window is a fast one in this sense - texel centres at
	// write-through, the deterministic atlas that settles the instant the scene does.
	if(fast_window || u_gi_world_probe_jitter.z < 0.5)
	{
		windows_seen = 0u;
	}
	float mean_blend = 1.0 / float(min(windows_seen + 1u, uint(GI_WORLD_PROBE_EMA_WINDOWS)));
	barrier();
	if(thread == 0)
	{
		// A window completes on the frame that traces its last strata.
		bool window_end = stratum_base + uint(stratum_count) >= uint(GI_WORLD_PROBE_WINDOW);
		b_world_probe_counts[slot_index] =
		    (fast_window || u_gi_world_probe_jitter.z < 0.5)
		        ? 0u
		        : (window_end ? min(windows_seen + 1u, 255u) : windows_seen);
	}
	if(fresh)
	{
		if(thread == 0)
		{
			b_world_probe_cells[slot_index] = packed_cell;
		}
		// SCROLL-IN SEEDING (the SDFGI cascade trick): a freshly claimed slot starts from the
		// PARENT cascade's view of the same position instead of black. The parent scrolls at
		// half the rate, so its data is valid here; the seed is its convolved IRRADIANCE at
		// each texel's direction - the cosine-weighted mean of what the parent sees that way,
		// in exactly the E/pi = mean-radiance units a radiance texel holds on average - soft,
		// energy-consistent, refined stratum by stratum over the window. Without it every
		// window edge dragged a dark frontier that took a full window (a quarter second) to
		// converge. The outermost level has no parent and keeps the dark clear (energy loss,
		// never invention); buried probes stay dead.
		//
		// The seed is RADIANCE ONLY. hitT is left at 0 - the "never measured" value the atlas
		// clear writes - never at the -1 sky marker this claim used to stamp. That marker was
		// a lie about GEOMETRY with no relation to the seed: the convolve turns every sky
		// texel into depth = GI_WORLD_PROBE_DEPTH_CLAMP x spacing at near-zero variance, so a
		// just-claimed slot advertised "confidently open in every direction" for a whole
		// window. A cage corner is at most sqrt(3) x spacing away against a 1.5 x spacing
		// clamp, so most corners then took the readers' no-test path (distance <= mean, low
		// variance = neither Chebyshev nor the field march runs) at FULL trilinear weight -
		// and what they read was the COARSER cascade's irradiance, which straddles walls worse
		// by construction. That is a cascade-down import into sealed interiors, re-arming
		// every time the camera crosses a probe cell, and it bypasses every defence in the
		// reader. Leaving hitT at 0 drags the convolved mean toward zero instead, which
		// over-occludes: the fresh cage is rejected and the query falls through to the coarser
		// level - the same energy, by the legitimate path that still gets a visibility test.
		// Untraced strata also stop counting as sky, so a fresh interior probe no longer
		// reports sky_fraction 1.
		int parent_level = level + 1;
		bool parent_valid = false;
		ivec2 parent_tile = ivec2(0, 0);
		if(!buried && parent_level < SDF_CLIPMAP_LEVEL_COUNT)
		{
			float parent_spacing = GiWorldProbeSpacing(parent_level);
			ivec3 parent_cell = ivec3(floor(origin / parent_spacing + vec3_splat(0.5)));
			ivec3 parent_slot = GiWorldProbeSlot(parent_cell);
			// The parent slot must still HOLD that cell - it is toroidal too, and a slot
			// serving a different region would seed someone else's lighting.
			parent_valid =
			    b_world_probe_cells[GiWorldProbeSlotIndex(parent_slot, parent_level)] ==
			    GiWorldProbePackCell(parent_cell, parent_level);
			parent_tile =
			    GiWorldProbeTileBase(parent_slot, parent_level, GI_WORLD_PROBE_OCT_IRRADIANCE + 2);
		}
		for(int s = 0; s < GI_WORLD_PROBE_WINDOW; ++s)
		{
			if(uint(s) >= stratum_base && uint(s) < stratum_base + uint(stratum_count))
			{
				continue;
			}
			int clear_index = thread * GI_WORLD_PROBE_WINDOW + s;
			ivec2 clear_texel = tile + ivec2(clear_index % GI_WORLD_PROBE_OCT_RADIANCE,
			                                 clear_index / GI_WORLD_PROBE_OCT_RADIANCE);
			vec4 clear_value = vec4(0.0, 0.0, 0.0, 0.0);
			if(parent_valid)
			{
				vec2 clear_uv = (vec2(clear_texel - tile) + vec2_splat(0.5)) /
				                float(GI_WORLD_PROBE_OCT_RADIANCE);
				vec3 clear_direction = GiOctDecode(clear_uv);
				vec2 seed_uv = (vec2(parent_tile) + vec2_splat(1.0) +
				                GiOctEncode(clear_direction) * float(GI_WORLD_PROBE_OCT_IRRADIANCE)) *
				               u_gi_world_probe_seed_atlas.xy;
				clear_value.xyz =
				    max(texture2DLod(s_world_probe_irradiance_seed, seed_uv, 0.0).xyz,
				        vec3_splat(0.0));
			}
			imageStore(s_world_probe_radiance_out, clear_texel, clear_value);
		}
	}
	float t_max = u_gi_world_probe_window[level].w;
	for(int si = 0; si < stratum_count; ++si)
	{
		int texel_index = thread * GI_WORLD_PROBE_WINDOW + int(stratum_base) + si;
		ivec2 texel = tile + ivec2(texel_index % GI_WORLD_PROBE_OCT_RADIANCE,
		                           texel_index / GI_WORLD_PROBE_OCT_RADIANCE);
		if(buried)
		{
			imageStore(s_world_probe_radiance_out, texel, vec4_splat(0.0));
			continue;
		}
		// Sub-texel direction jitter, per window (R2) and per atlas texel (IGN, so neighbouring
		// probes and texels decorrelate): the running mean integrates the texel's whole solid
		// angle instead of its centre ray. FAST windows (a light or content change) sample the
		// texel CENTRE at write-through instead - deterministic and reactive, exactly the old
		// atlas - because a jittered sample written through every 4 frames is a flickering
		// probe, and content churn (movers) keeps the fast window open for as long as it lasts
		// (measured: static surfaces noisier under movers than before the jitter). The
		// converging mean resumes from that deterministic base when the scene settles.
		// A probe's FIRST window (fresh claim, or the window after a fast one) samples the
		// centres too: the mean has no base yet and a lone jittered sample written through is
		// a noisy probe for a whole window (measured as a pop at the level-0 scroll).
		bool deterministic_window = fast_window || windows_seen == 0u || u_gi_world_probe_jitter.z < 0.5;
		vec2 texel_jitter = deterministic_window ? vec2_splat(0.5)
		                                         : fract(GiIgnNoise(texel) + u_gi_world_probe_jitter.xy);
		vec2 tile_uv = (vec2(texel - tile) + texel_jitter) / float(GI_WORLD_PROBE_OCT_RADIANCE);
		vec3 direction = GiOctDecode(tile_uv);
		// Coarse world structure: cascade tier only, called DIRECTLY (near_field 0 makes the
		// tiered entry point equivalent), with two probe-specific hardenings against the
		// sealed-box leak. Acceptance at the FULL voxel cap, and - the part acceptance alone
		// cannot do, because the porous field overestimates distance and the march can hop a
		// sub-voxel wall's dip without sampling it - the surface expand at FULL strength from
		// launch. These rays have no mesh tier backing their first metres and are born in open
		// space, so the ramp's contact-zone grace protects nothing here and its blind zone was
		// exactly where rays threaded the level cross-fade shell out of sealed rooms (the
		// camera-locked porosity fans; measured chain at GI_WORLD_PROBE_TRACE_BIAS in
		// gi_constants.h).
		SdfRayHit hit = SdfTraceClipmap(origin, direction, 0.0, t_max, GI_TRACE_MAX_STEPS,
		                                GI_WORLD_PROBE_TRACE_BIAS, GI_PROBE_TRACE_RELAXATION,
		                                true, 0.0, true);
		vec3 radiance;
		float hit_t;
		// The stored distance is the CLAMPED depth the convolve consumes (misses store the clamp
		// itself: a miss and a hit beyond GI_WORLD_PROBE_DEPTH_CLAMP are the same moments and
		// the same sky share), always positive, so it can ride the running mean like the
		// radiance - under the direction jitter a "latest sample" depth made the Chebyshev
		// moments flicker per window and tripped the cage-visibility marches (measured: Light
		// Voxels 2x). Zero stays the never-measured mark (fresh clear, buried probes).
		float depth_clamp = GI_WORLD_PROBE_DEPTH_CLAMP * GiWorldProbeSpacing(level);
		if(!hit.hit)
		{
			radiance = eval_radiance_sh(s_gi_env_sh, direction);
			hit_t = depth_clamp;
		}
		else
		{
			hit_t = min(hit.t, depth_clamp);
			vec3 hit_position = origin + direction * hit.t;
			vec3 hit_normal = hit.normal;
			if(dot(hit_normal, direction) > 0.0)
			{
				hit_normal = -hit_normal;
			}
			vec3 voxel_radiance;
			// Cross-faded across cascade levels like the gather's read (GI_LIGHT_VOXEL_FADE_VOXELS).
			if(!GiLightVoxelReadBlend(hit_position, hit_normal, GI_LIGHT_VOXEL_FADE_VOXELS, voxel_radiance))
			{
				// Occluded but unmeasured: honest darkness, never fabricated energy - the
				// sealed room converges black through exactly this branch.
				voxel_radiance = vec3_splat(0.0);
			}
			radiance = voxel_radiance;
		}
		// The gather's per-ray firefly clamp (GI_MAX_RAY_RADIANCE), applied at the one other
		// stochastic-ray tier: emissive is stored unbounded in the light voxels, and a single
		// stratum ray skewering a small bright emitter otherwise holds its full radiance in
		// the mean for a whole window - the probe-side shimmer near emissives.
		vec3 stored = min(radiance, vec3_splat(GI_MAX_RAY_RADIANCE));
		float stored_t = hit_t;
		BRANCH
		if(mean_blend < 1.0)
		{
			// Running mean over windows: the previous value is this texel's own, written by
			// this thread's slot one window ago (the seed or the clear on a fresh claim -
			// both replaced at write-through while the count is zero). A never-measured depth
			// (0) is not averaged into.
			vec4 previous = imageLoad(s_world_probe_radiance_out, texel);
			stored = mix(GiFiniteOrZero(previous.xyz), stored, mean_blend);
			if(previous.w > 0.0)
			{
				stored_t = mix(previous.w, hit_t, mean_blend);
			}
		}
		imageStore(s_world_probe_radiance_out, texel, vec4(stored, stored_t));
	}
}
