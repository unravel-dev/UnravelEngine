/*
 * Traces one screen-space radiance probe per thread group.
 *
 * The group's 8x8 threads are the probe's 64 octahedral directions. Thread (0,0) anchors the
 * probe -- reads the G-buffer at a DETERMINISTIC pixel of the probe's tile and runs the SAME
 * launch preparation the per-pixel gather runs (gi_gather_common.sh) -- and shares the result;
 * every thread then traces its own direction through the SAME per-ray pipeline. Nothing about
 * how a ray is traced, rejected or read differs between the two gathers; only how many rays
 * exist and who integrates them.
 *
 * STABILITY is this pass's second product, and two choices carry it.
 *
 * The anchor is deterministic -- the tile centre, falling back through a FIXED candidate ring
 * when the centre is sky -- never a per-frame random pixel. A re-rolled anchor moves the probe's
 * position, normal and therefore its entire hemisphere every frame, and because one probe feeds
 * a whole tile of pixels that error is spatially CORRELATED: the screen temporal's neighbourhood
 * clamp compares a pixel against neighbours that are all moving in unison and waves the change
 * through. Per-pixel gather noise was independent per pixel, which is exactly why the same
 * temporal filter kept it still.
 *
 * The per-texel direction jitter stays, and probe-space HISTORY is what turns it from shimmer
 * into convergence: each texel blends into its own previous value (validated against the
 * previous anchor -- same plane, same facing -- and cut on disocclusion), so over a few frames a
 * texel integrates its whole cone instead of point-sampling a different direction of it each
 * frame. Accumulating per DIRECTION, before any pixel integrates, is the piece a screen-space
 * history cannot replicate: it happens before the correlation is created.
 *
 * Directions pointing into the anchor surface are not traced: no pixel whose normal roughly
 * agrees with the probe's can weight them in, and the integration's plane and facing weights
 * reject the pixels that would. Their texels stay zero with a zero resolved flag, which the SH
 * projection treats as "unmeasured" exactly like an escaped ray.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
// DecodeGBufferNormalMetalRoughnessLod lives here, not in common.sh.
#include "../lighting.sh"

#define GI_CACHE_READ_ONLY
#include "gi/radiance_cache.sh"
#include "gi/sdf_common.sh"
#include "gi/gi_gather_common.sh"
#include "gi/gi_probe_common.sh"

/// rgb = radiance, a = resolved flag. One 8x8 tile per probe.
IMAGE2D_WO(s_probe_radiance_out, rgba16f, 5);
/// Probe buffer; this pass writes the meta slots, the filter pass writes the SH.
BUFFER_RW(b_gi_probes, vec4, 10);

SAMPLER2D(s_gi_depth, 8);
SAMPLER2D(s_gi_normal, 9);
/// PREVIOUS frame's radiance atlas, for the per-texel history blend.
SAMPLER2D(s_probe_history, 11);

/// The PREVIOUS frame's view projection, for history REPROJECTION. The probe grid is fixed to
/// the screen while the world slides across it, so under any camera motion probe (i, j)'s
/// history lives at whichever probe covered its anchor's world position LAST frame. Comparing
/// and blending against the probe's own slot instead is only correct for a perfectly still
/// camera; the moment it rotates, every tile's history is "somewhere else", a same-slot validity
/// test correctly refuses it, and the whole screen re-noises -- the red flood in the history
/// debug view. Reprojection is how Lumen's screen probes survive motion, and it is the entire
/// point of the double-buffered probe state.
uniform mat4 u_gi_prev_view_proj;

SHARED vec3 s_world_position;
SHARED vec3 s_world_normal;
SHARED vec3 s_origin;
SHARED float s_origin_voxel;
SHARED float s_near_field;
SHARED uint s_own_key;
SHARED uint s_own_key_far;
SHARED float s_valid;
SHARED float s_history_alpha;
/// The FOUR previous probes bracketing the reprojected anchor, with their blend weights.
/// Bilinear rather than nearest: a nearest fetch snaps the whole tile's history to a new probe
/// each time the reprojection crosses a lattice boundary, which reads as tiles STEPPING during
/// ordinary camera rotation. Weighted across the bracket, history slides continuously with the
/// reprojection instead.
SHARED vec2 s_history_probes[4];
SHARED float s_history_weights[4];
SHARED float s_history_weight_sum;

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
	uint write_base = (probe_index + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
	if(local.x == 0 && local.y == 0)
	{
		s_valid = 0.0;
		s_history_alpha = 1.0;
		s_history_weight_sum = 0.0;
		for(int slot = 0; slot < 4; ++slot)
		{
			s_history_probes[slot] = vec2(probe.xy);
			s_history_weights[slot] = 0.0;
		}
		// Deterministic, MEDIAN-DEPTH anchor. Five fixed candidate pixels are read every frame in
		// a never-varying order, and the one whose depth is the median of the valid set anchors
		// the probe. The median is what survives high-frequency geometry under the raster's
		// sub-pixel TAA jitter: on ivy or a railing, any single fixed pixel alternates between
		// the leaf and the wall behind it as the jitter moves, which re-anchors the probe across
		// metres of depth every frame -- the history validity test then correctly refuses to
		// blend, and the probe re-noises forever (solid red patches on exactly that geometry in
		// the history debug view). The median ignores one or two such flips and keeps anchoring
		// on the tile's majority surface.
		vec2 candidates[5];
		candidates[0] = vec2(0.5, 0.5);
		candidates[1] = vec2(0.25, 0.25);
		candidates[2] = vec2(0.75, 0.25);
		candidates[3] = vec2(0.25, 0.75);
		candidates[4] = vec2(0.75, 0.75);
		float candidate_depths[5];
		int valid_count = 0;
		for(int gather = 0; gather < 5; ++gather)
		{
			vec2 pixel = (vec2(probe.xy) + candidates[gather]) * u_gi_probe_spacing;
			pixel = min(pixel, u_gi_probe_screen.xy - vec2_splat(1.0));
			vec2 uv = (pixel + vec2_splat(0.5)) * u_gi_probe_screen.zw;
			candidate_depths[gather] = texture2DLod(s_gi_depth, uv, 0.0).x;
			if(candidate_depths[gather] < 1.0)
			{
				++valid_count;
			}
		}
		int anchor_index = -1;
		if(valid_count > 0)
		{
			// The valid candidate whose depth-rank is the middle of the valid set. O(n^2) over
			// five elements: cheaper than any cleverness.
			int target_rank = (valid_count - 1) / 2;
			for(int pick = 0; pick < 5 && anchor_index < 0; ++pick)
			{
				if(candidate_depths[pick] >= 1.0)
				{
					continue;
				}
				int rank = 0;
				for(int other = 0; other < 5; ++other)
				{
					if(other != pick && candidate_depths[other] < 1.0 &&
					   (candidate_depths[other] < candidate_depths[pick] ||
					    (candidate_depths[other] == candidate_depths[pick] && other < pick)))
					{
						++rank;
					}
				}
				if(rank == target_rank)
				{
					anchor_index = pick;
				}
			}
		}
		if(anchor_index >= 0)
		{
			vec2 pixel = (vec2(probe.xy) + candidates[anchor_index]) * u_gi_probe_spacing;
			pixel = min(pixel, u_gi_probe_screen.xy - vec2_splat(1.0));
			vec2 uv = (pixel + vec2_splat(0.5)) * u_gi_probe_screen.zw;
			float depth = candidate_depths[anchor_index];
			vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(depth)));
			vec3 world_position = clipToWorld(u_invViewProj, clip);
			GBufferDataNormalMetalRoughness nd = DecodeGBufferNormalMetalRoughnessLod(uv, s_gi_normal, 0.0);
			vec3 world_normal = nd.world_normal;
			if(dot(world_normal, world_normal) >= 0.5)
			{
				world_normal = normalize(world_normal);
				GiGatherSetup setup = GiPrepareGather(world_position, world_normal);
				s_world_position = setup.world_position;
				s_world_normal = setup.world_normal;
				s_origin = setup.origin;
				s_origin_voxel = setup.origin_voxel;
				s_near_field = setup.near_field;
				s_own_key = setup.own_key;
				s_own_key_far = setup.own_key_far;
				s_valid = 1.0;
				float view_distance = length(world_position - u_gi_resolve_camera.xyz);
				b_gi_probes[write_base + uint(GI_PROBE_META)] = vec4(world_position, 1.0);
				b_gi_probes[write_base + uint(GI_PROBE_META2)] = vec4(world_normal, view_distance);
				// REPROJECTED history: project this anchor through the PREVIOUS view projection
				// and fetch state from whichever probe covered that spot last frame. The probe
				// grid is screen-fixed while the world slides across it, so under motion "this
				// probe's history" lives at a different index -- comparing same-slot is only
				// right for a motionless camera, and cutting on the mismatch is what re-noised
				// the whole screen on rotation.
				//
				// The validity thresholds stay deliberately LOOSE: a probe integrates its whole
				// sphere, so its radiance changes far more slowly with anchor position than any
				// pixel quantity. Only a genuine surface change -- disocclusion, an off-screen
				// entry, differently-facing geometry -- should reset.
				//
				// The blend weight is 1/count with the count growing to the cap: a true mean,
				// which settles completely on a static view, rather than a fixed-weight EMA,
				// which holds a permanent variance floor and dances forever.
				float count = 1.0;
				vec4 prev_clip4 = mul(u_gi_prev_view_proj, vec4(world_position, 1.0));
				if(prev_clip4.w > 0.0)
				{
					vec3 prev_clip = clipTransform(prev_clip4.xyz / prev_clip4.w);
					vec2 prev_uv = prev_clip.xy * 0.5 + 0.5;
					if(all(greaterThanEqual(prev_uv, vec2_splat(0.0))) &&
					   all(lessThanEqual(prev_uv, vec2_splat(1.0))))
					{
						// The four previous probes bracketing the reprojected anchor, in the same
						// tile-centre lattice convention the integration uses. Each tap is
						// validity-gated INDIVIDUALLY -- a bracket straddling a silhouette keeps
						// the taps on this surface and drops the ones across the break -- and the
						// weighted count carries convergence over so a slide onto an equally
						// converged neighbour does not restart accumulation.
						vec2 prev_pixel = prev_uv * u_gi_probe_screen.xy;
						vec2 prev_grid = prev_pixel / u_gi_probe_spacing - vec2_splat(0.5);
						vec2 prev_base = floor(prev_grid);
						vec2 prev_frac = prev_grid - prev_base;
						float plane_tolerance = max(0.05 * view_distance, 0.1);
						float weighted_count = 0.0;
						float weight_sum = 0.0;
						for(int tap = 0; tap < 4; ++tap)
						{
							vec2 offset = vec2(float(tap % 2), float(tap / 2));
							vec2 tap_probe = prev_base + offset;
							tap_probe.x = clamp(tap_probe.x, 0.0, float(u_gi_probe_count_x - 1));
							tap_probe.y = clamp(tap_probe.y, 0.0, float(u_gi_probe_count_y - 1));
							uint read_base = (GiProbeIndex(int(tap_probe.x), int(tap_probe.y)) +
							                  u_gi_probe_read_offset) *
							                 uint(GI_PROBE_STRIDE);
							vec4 previous_meta = b_gi_probes[read_base + uint(GI_PROBE_META)];
							if(previous_meta.w < 0.5)
							{
								continue;
							}
							vec4 previous_meta2 = b_gi_probes[read_base + uint(GI_PROBE_META2)];
							float plane = abs(dot(previous_meta.xyz - world_position, world_normal));
							float facing = dot(previous_meta2.xyz, world_normal);
							if(plane >= plane_tolerance || facing <= 0.7)
							{
								continue;
							}
							float bilinear = (offset.x < 0.5 ? 1.0 - prev_frac.x : prev_frac.x) *
							                 (offset.y < 0.5 ? 1.0 - prev_frac.y : prev_frac.y);
							if(bilinear <= 1e-3)
							{
								continue;
							}
							s_history_probes[tap] = tap_probe;
							s_history_weights[tap] = bilinear;
							weight_sum += bilinear;
							weighted_count += bilinear * b_gi_probes[read_base + 11u].x;
						}
						if(weight_sum > 1e-3)
						{
							s_history_weight_sum = weight_sum;
							count = min(weighted_count / weight_sum + 1.0,
							            max(u_gi_probe_history_cap, 1.0));
							s_history_alpha = 1.0 / count;
						}
					}
				}
				b_gi_probes[write_base + 11u] = vec4(count, s_history_alpha, 0.0, 0.0);
			}
		}
		if(s_valid < 0.5)
		{
			b_gi_probes[write_base + uint(GI_PROBE_META)] = vec4_splat(0.0);
			b_gi_probes[write_base + uint(GI_PROBE_META2)] = vec4_splat(0.0);
			b_gi_probes[write_base + 11u] = vec4(1.0, 1.0, 0.0, 0.0);
		}
	}
	barrier();
	ivec2 texel = probe * GI_PROBE_DIR_EDGE + local;
	if(s_valid < 0.5)
	{
		imageStore(s_probe_radiance_out, texel, vec4_splat(0.0));
		return;
	}
	// FIXED direction: the texel centre, every frame. Per-frame jitter within the texel was the
	// probe path's residual shimmer: a cone straddling a bright emitter swings between hit and
	// miss per frame, the swing is shared by a whole tile of pixels at once, and no history with
	// a responsiveness floor can hold it perfectly still. With deterministic directions a static
	// scene produces bit-identical probe input every frame -- stable BY CONSTRUCTION -- and the
	// 3x3 probe-space filter recovers most of the sub-texel coverage the jitter was sampling,
	// because neighbouring probes see the same feature from slightly different angles.
	//
	// The honest cost: an emitter smaller than a texel cone can be missed outright (slightly
	// darker, stable) where jitter sometimes found it (brighter, flickering). Importance
	// sampling is the eventual fix for that -- aim texel BUDGET at bright cones -- not jitter.
	vec2 tile_uv = (vec2(local.xy) + vec2_splat(0.5)) / float(GI_PROBE_DIR_EDGE);
	vec3 direction = GiOctDecode(tile_uv);
	// Below the anchor's tangent plane: not traced (see the header note).
	if(dot(direction, s_world_normal) < -0.2)
	{
		imageStore(s_probe_radiance_out, texel, vec4_splat(0.0));
		return;
	}
	GiGatherSetup setup;
	setup.world_position = s_world_position;
	setup.world_normal = s_world_normal;
	setup.origin = s_origin;
	setup.origin_voxel = s_origin_voxel;
	setup.origin_distance = 0.0;
	setup.lift = 0.0;
	setup.near_field = s_near_field;
	setup.own_key = s_own_key;
	setup.own_key_far = s_own_key_far;
	GiRayOutcome outcome = GiGatherRay(setup, direction);
	// Per-texel history, blended across the four REPROJECTED previous probes. Directions are
	// world-space octahedral, so the same local texel of a different probe is the same world
	// direction and transfers one to one; the bilinear weights make the history slide
	// continuously with the reprojection instead of stepping a whole tile at a time. The
	// resolved flag rides in alpha and accumulates through the same blend, so partial-sky cones
	// settle to their true covered fraction.
	vec4 current = vec4(outcome.radiance, outcome.resolved);
	vec4 previous = current;
	if(s_history_weight_sum > 1e-3)
	{
		previous = vec4_splat(0.0);
		for(int tap = 0; tap < 4; ++tap)
		{
			if(s_history_weights[tap] <= 0.0)
			{
				continue;
			}
			ivec2 history_texel = ivec2(s_history_probes[tap]) * GI_PROBE_DIR_EDGE + local;
			previous += texelFetch(s_probe_history, history_texel, 0) * s_history_weights[tap];
		}
		previous /= s_history_weight_sum;
	}
	vec4 blended = mix(previous, current, s_history_alpha);
	imageStore(s_probe_radiance_out, texel, blended);
}
