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
 * Dispatch z is the probe LAYER (see GI_PROBE_LAYERS): layer 0 anchors the tile's majority
 * surface, layer 1 the minority surface layer 0's plane rejects, and only where that rejection
 * is real -- a planar tile leaves layer 1 invalid and traces nothing.
 *
 * STABILITY is this pass's second product, carried by four choices that all exist because probe
 * error is spatially CORRELATED -- one probe feeds a whole tile of pixels, so anything that
 * moves a probe moves a tile, and the screen temporal's neighbourhood clamp waves correlated
 * motion straight through:
 *
 *  - Deterministic anchors: fixed candidate pixels, the median-by-depth chosen so raster jitter
 *    on high-frequency geometry cannot re-roll the anchor.
 *  - ANCHOR HYSTERESIS: a probe keeps tracking the surface it tracked last frame (verified at
 *    its REPROJECTED location) while any candidate still sees it, and re-anchors exactly once
 *    when that surface truly leaves the tile. Without this, camera motion flips the median
 *    between the two surfaces of a crevice, every flip re-planes the probe, history correctly
 *    cuts, and edges visibly re-settle while moving.
 *  - Fixed trace directions: texel centres, so a static scene produces bit-identical probe
 *    input every frame.
 *  - Per-texel history, blended from the four REPROJECTED previous probes at a true-mean 1/count
 *    weight: converges completely when still, slides continuously when moving.
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

/// rgb = radiance, a = resolved flag. One 8x8 tile per probe, layers stacked vertically.
IMAGE2D_WO(s_probe_radiance_out, rgba16f, 5);
/// Probe buffer; this pass writes the meta slots, the filter pass writes the SH.
BUFFER_RW(b_gi_probes, vec4, 10);

SAMPLER2D(s_gi_depth, 8);
SAMPLER2D(s_gi_normal, 9);
/// PREVIOUS frame's radiance atlas, for the per-texel history blend.
SAMPLER2D(s_probe_history, 11);

/// The PREVIOUS frame's view projection, for history REPROJECTION and anchor hysteresis. The
/// probe grid is fixed to the screen while the world slides across it, so under any camera
/// motion probe (i, j)'s history lives at whichever probe covered its anchor's world position
/// LAST frame. Reprojection is how Lumen's screen probes survive motion, and it is the entire
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
/// Per-texel history luminance and its group total, driving the importance allocation of the
/// group's ray budget across its 64 cones.
SHARED float s_texel_importance[GI_PROBE_DIR_COUNT];
SHARED float s_importance_total;

/// Encodes what one ray saw into the atlas alpha convention (see GI_PROBE_PROXIMITY_SKY):
/// a failure to measure is 0, sky is the floor, a hit is 1 / (1 + t) floored at sky.
float GiProbeEncodeProximity(GiRayOutcome outcome)
{
	if(outcome.resolved <= 0.0)
	{
		return 0.0;
	}
	if(outcome.hit <= 0.0)
	{
		return GI_PROBE_PROXIMITY_SKY;
	}
	return max(1.0 / (1.0 + outcome.t), GI_PROBE_PROXIMITY_SKY);
}

/**
 * Whether a candidate surface CONTINUES a surface a probe of @p layer tracked last frame at the
 * candidate's reprojected location: the previous probe there anchored on (near) the candidate's
 * plane, facing the same way.
 *
 * This is the discriminator anchor hysteresis runs on, and it separates the two surfaces of a
 * crevice cleanly: the wall candidate reprojects onto a previous probe that tracked the wall and
 * passes; the recess candidate reprojects onto that same wall probe and fails its plane test.
 */
bool GiCandidateContinues(vec3 position, vec3 normal, int layer)
{
	vec4 prev_clip4 = mul(u_gi_prev_view_proj, vec4(position, 1.0));
	if(prev_clip4.w <= 0.0)
	{
		return false;
	}
	vec3 prev_clip = clipTransform(prev_clip4.xyz / prev_clip4.w);
	vec2 prev_uv = prev_clip.xy * 0.5 + 0.5;
	if(any(lessThan(prev_uv, vec2_splat(0.0))) || any(greaterThan(prev_uv, vec2_splat(1.0))))
	{
		return false;
	}
	vec2 prev_probe = floor(prev_uv * u_gi_probe_screen.xy / u_gi_probe_spacing);
	int px = int(clamp(prev_probe.x, 0.0, float(u_gi_probe_count_x - 1)));
	int py = int(clamp(prev_probe.y, 0.0, float(u_gi_probe_count_y - 1)));
	uint read_base = (GiProbeRecord(px, py, layer) + u_gi_probe_read_offset) * uint(GI_PROBE_STRIDE);
	vec4 previous_meta = b_gi_probes[read_base + uint(GI_PROBE_META)];
	if(previous_meta.w < 0.5)
	{
		return false;
	}
	vec4 previous_meta2 = b_gi_probes[read_base + uint(GI_PROBE_META2)];
	float plane = abs(dot(previous_meta.xyz - position, normal));
	float plane_tolerance = max(0.05 * length(position - u_gi_resolve_camera.xyz), 0.1);
	return plane < plane_tolerance && dot(previous_meta2.xyz, normal) > 0.7;
}

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 probe = ivec2(gl_WorkGroupID.xy);
	int layer = int(gl_WorkGroupID.z);
	ivec2 local = ivec2(gl_LocalInvocationID.xy);
	if(probe.x >= u_gi_probe_count_x || probe.y >= u_gi_probe_count_y)
	{
		return;
	}
	uint write_base =
	    (GiProbeRecord(probe.x, probe.y, layer) + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
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
		// All candidates reconstructed up front: position, normal, usability. A handful of
		// unprojects and normal decodes are noise next to one traced ray, and having every
		// candidate resident is what lets the selection reason about SURFACES rather than
		// raw depths.
		//
		// Slots 0-4 are the FIXED lattice; slot 5 is the CONTINUITY candidate -- the pixel the
		// surface this lattice site tracked LAST frame projects to THIS frame. Five fixed taps
		// sample a tile sparsely, so thin geometry -- a sill, a rail, a reveal edge -- is seen
		// by a different subset of them each frame, and the frame every tap misses breaks the
		// anchor hysteresis, re-anchors the probe, cuts its history and pops the whole tile.
		// Projecting the previous anchor forward keeps the tracked surface eligible for as long
		// as it is still anywhere in the tile. It joins the HYSTERESIS searches only, never the
		// median: continuity must not bias a genuinely fresh choice.
		//
		// Always LAYER 0's previous anchor, in both layer workgroups: layer 1 re-derives layer
		// 0's choice from the same candidate pool, and reading each workgroup's own layer here
		// would hand the two groups different pools and let their layer-0 choices diverge.
		vec2 candidates[5];
		candidates[0] = vec2(0.5, 0.5);
		candidates[1] = vec2(0.25, 0.25);
		candidates[2] = vec2(0.75, 0.25);
		candidates[3] = vec2(0.25, 0.75);
		candidates[4] = vec2(0.75, 0.75);
		vec2 candidate_uvs[6];
		bool candidate_present[6];
		for(int fixed_c = 0; fixed_c < 5; ++fixed_c)
		{
			vec2 pixel = (vec2(probe.xy) + candidates[fixed_c]) * u_gi_probe_spacing;
			pixel = min(pixel, u_gi_probe_screen.xy - vec2_splat(1.0));
			candidate_uvs[fixed_c] = (pixel + vec2_splat(0.5)) * u_gi_probe_screen.zw;
			candidate_present[fixed_c] = true;
		}
		candidate_present[5] = false;
		candidate_uvs[5] = candidate_uvs[0];
		uint continuity_base =
		    (GiProbeRecord(probe.x, probe.y, 0) + u_gi_probe_read_offset) * uint(GI_PROBE_STRIDE);
		vec4 continuity_meta = b_gi_probes[continuity_base + uint(GI_PROBE_META)];
		if(continuity_meta.w > 0.5)
		{
			vec4 cur_clip4 = mul(u_viewProj, vec4(continuity_meta.xyz, 1.0));
			if(cur_clip4.w > 0.0)
			{
				vec3 cur_clip = clipTransform(cur_clip4.xyz / cur_clip4.w);
				vec2 cur_pixel = (cur_clip.xy * 0.5 + 0.5) * u_gi_probe_screen.xy;
				vec2 cur_tile = floor(cur_pixel / u_gi_probe_spacing);
				// Only while the tracked surface still lands in THIS tile. Once it moves on, the
				// tile it moved to sees it through its own candidates; resurrecting it here would
				// track a surface the tile no longer covers.
				if(int(cur_tile.x) == probe.x && int(cur_tile.y) == probe.y)
				{
					candidate_uvs[5] = (floor(cur_pixel) + vec2_splat(0.5)) * u_gi_probe_screen.zw;
					candidate_present[5] = true;
				}
			}
		}
		float candidate_depths[6];
		vec3 candidate_positions[6];
		vec3 candidate_normals[6];
		bool candidate_ok[6];
		bool candidate_buried[6];
		int valid_count = 0;
		int clear_count = 0;
		for(int gather = 0; gather < 6; ++gather)
		{
			candidate_buried[gather] = false;
			candidate_ok[gather] = false;
			candidate_depths[gather] = 1.0;
			candidate_positions[gather] = vec3_splat(0.0);
			candidate_normals[gather] = vec3(0.0, 1.0, 0.0);
			if(!candidate_present[gather])
			{
				continue;
			}
			vec2 uv = candidate_uvs[gather];
			candidate_depths[gather] = texture2DLod(s_gi_depth, uv, 0.0).x;
			if(candidate_depths[gather] < 1.0)
			{
				vec3 candidate_clip =
				    clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(candidate_depths[gather])));
				vec3 candidate_position = clipToWorld(u_invViewProj, candidate_clip);
				GBufferDataNormalMetalRoughness candidate_nd =
				    DecodeGBufferNormalMetalRoughnessLod(uv, s_gi_normal, 0.0);
				vec3 candidate_normal = candidate_nd.world_normal;
				if(dot(candidate_normal, candidate_normal) >= 0.5)
				{
					// A candidate BURIED in the cascade -- a groove, a slit, a reveal narrower
					// than the field can represent -- anchors a probe whose rays mostly die at
					// contact range: a dark, near-unmeasured probe that drags its whole tile dark
					// whenever it wins the anchor, and back-and-forth as the camera moves. That
					// probe-goes-dark flicker is what the health view shows as a dark red band
					// sliding along grooves. Buried candidates are refused while any CLEAR
					// candidate exists; the crevice pixels then read the stable wall probes.
					float clearance_voxel;
					float clearance = SdfSampleClipmapEx(candidate_position, clearance_voxel);
					candidate_buried[gather] = clearance < -0.35 * max(clearance_voxel, 0.01) &&
					                           clearance > -0.9 * SDF_CLIPMAP_OUTSIDE;
					candidate_ok[gather] = true;
					candidate_positions[gather] = candidate_position;
					candidate_normals[gather] = normalize(candidate_normal);
					// The continuity candidate never joins the median pool, so it does not count.
					if(gather < 5)
					{
						++valid_count;
						if(!candidate_buried[gather])
						{
							++clear_count;
						}
					}
				}
			}
		}
		// Buried candidates fall out of eligibility unless the whole tile is buried (a view
		// entirely inside an alcove must still anchor on SOMETHING).
		for(int demote = 0; demote < 6; ++demote)
		{
			if(candidate_ok[demote] && candidate_buried[demote] && clear_count > 0)
			{
				candidate_ok[demote] = false;
				if(demote < 5)
				{
					--valid_count;
				}
			}
		}
		// Majority anchor: the usable candidate whose depth-rank is the middle of the usable set.
		// The median survives raster jitter on high-frequency geometry, where any single fixed
		// pixel alternates between a leaf and the wall behind it as the sub-pixel jitter moves.
		int majority = -1;
		if(valid_count > 0)
		{
			int target_rank = (valid_count - 1) / 2;
			for(int pick = 0; pick < 5 && majority < 0; ++pick)
			{
				if(!candidate_ok[pick])
				{
					continue;
				}
				int rank = 0;
				for(int other = 0; other < 5; ++other)
				{
					if(other != pick && candidate_ok[other] &&
					   (candidate_depths[other] < candidate_depths[pick] ||
					    (candidate_depths[other] == candidate_depths[pick] && other < pick)))
					{
						++rank;
					}
				}
				if(rank == target_rank)
				{
					majority = pick;
				}
			}
		}
		// Anchor hysteresis, layer 0: keep tracking the previously tracked surface while any
		// candidate still sees it -- including the continuity candidate, which exists precisely
		// so this search can find the tracked surface when the fixed lattice misses it. The
		// median decides only when continuity cannot -- a fresh tile, or the tracked surface
		// genuinely gone.
		if(majority >= 0 &&
		   !GiCandidateContinues(candidate_positions[majority], candidate_normals[majority], 0))
		{
			for(int c = 0; c < 6; ++c)
			{
				if(c == majority || !candidate_ok[c])
				{
					continue;
				}
				if(GiCandidateContinues(candidate_positions[c], candidate_normals[c], 0))
				{
					majority = c;
					break;
				}
			}
		}
		// A tile whose five fixed taps all missed usable geometry can still be TRACKING a thin
		// surface through the continuity candidate -- a railing narrower than the lattice pitch
		// sees every fixed tap land on sky behind it. Continuity is required, not just a usable
		// sample: a fresh tile must come up through the median, never through this.
		if(majority < 0 && candidate_ok[5] &&
		   GiCandidateContinues(candidate_positions[5], candidate_normals[5], 0))
		{
			majority = 5;
		}
		int anchor_index = majority;
		// LAYER 1 re-anchors on the candidate most rejected by layer 0's CHOSEN plane -- the
		// minority surface -- and only where that rejection is real. Layer 0's choice, hysteresis
		// included, is recomputed here deterministically rather than communicated: the two
		// layers run in different thread groups.
		if(anchor_index >= 0 && layer == 1)
		{
			vec3 majority_position = candidate_positions[majority];
			vec3 majority_normal = candidate_normals[majority];
			float split_threshold =
			    max(0.05 * length(majority_position - u_gi_resolve_camera.xyz), 0.1);
			float candidate_offsets[6];
			int split_index = -1;
			float split_offset = 0.0;
			for(int split = 0; split < 6; ++split)
			{
				candidate_offsets[split] = 0.0;
				if(split == majority || !candidate_ok[split])
				{
					continue;
				}
				candidate_offsets[split] =
				    abs(dot(candidate_positions[split] - majority_position, majority_normal));
				if(candidate_offsets[split] > split_offset)
				{
					split_offset = candidate_offsets[split];
					split_index = split;
				}
			}
			anchor_index = (split_index >= 0 && split_offset > split_threshold) ? split_index : -1;
			// Hysteresis again, against layer 1's OWN previous lattice, restricted to candidates
			// that are genuinely off the majority plane -- the minority anchor must not drift
			// back onto the surface layer 0 already covers.
			if(anchor_index >= 0 &&
			   !GiCandidateContinues(candidate_positions[anchor_index], candidate_normals[anchor_index], 1))
			{
				for(int c = 0; c < 6; ++c)
				{
					if(c == anchor_index || !candidate_ok[c] ||
					   candidate_offsets[c] <= split_threshold)
					{
						continue;
					}
					if(GiCandidateContinues(candidate_positions[c], candidate_normals[c], 1))
					{
						anchor_index = c;
						break;
					}
				}
			}
		}
		if(anchor_index >= 0)
		{
			vec3 world_position = candidate_positions[anchor_index];
			vec3 world_normal = candidate_normals[anchor_index];
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
			// REPROJECTED history: project this anchor through the PREVIOUS view projection and
			// blend state from the four probes bracketing that spot last frame, each validated
			// individually (same plane, same facing) and weighted bilinearly, with the count
			// carried as the weighted average so convergence travels with the camera.
			//
			// The blend weight is 1/count with the count growing to the cap: a true mean, which
			// settles completely on a static view, rather than a fixed-weight EMA, which holds a
			// permanent variance floor and dances forever.
			float count = 1.0;
			vec4 prev_clip4 = mul(u_gi_prev_view_proj, vec4(world_position, 1.0));
			if(prev_clip4.w > 0.0)
			{
				vec3 prev_clip = clipTransform(prev_clip4.xyz / prev_clip4.w);
				vec2 prev_uv = prev_clip.xy * 0.5 + 0.5;
				if(all(greaterThanEqual(prev_uv, vec2_splat(0.0))) &&
				   all(lessThanEqual(prev_uv, vec2_splat(1.0))))
				{
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
						uint read_base =
						    (GiProbeRecord(int(tap_probe.x), int(tap_probe.y), layer) +
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
		if(s_valid < 0.5)
		{
			b_gi_probes[write_base + uint(GI_PROBE_META)] = vec4_splat(0.0);
			b_gi_probes[write_base + uint(GI_PROBE_META2)] = vec4_splat(0.0);
			b_gi_probes[write_base + 11u] = vec4(1.0, 1.0, 0.0, 0.0);
		}
	}
	barrier();
	ivec2 texel = GiProbeAtlasBase(probe.x, probe.y, layer) + local;
	if(s_valid < 0.5)
	{
		imageStore(s_probe_radiance_out, texel, vec4_splat(0.0));
		return;
	}
	// The texel's REPROJECTED history, fetched before tracing: the importance allocation below
	// derives from its luminance, and the final blend consumes it either way.
	vec4 previous = vec4_splat(0.0);
	bool has_history = s_history_weight_sum > 1e-3;
	if(has_history)
	{
		for(int tap = 0; tap < 4; ++tap)
		{
			if(s_history_weights[tap] <= 0.0)
			{
				continue;
			}
			ivec2 history_texel =
			    GiProbeAtlasBase(int(s_history_probes[tap].x), int(s_history_probes[tap].y), layer) +
			    local;
			previous += texelFetch(s_probe_history, history_texel, 0) * s_history_weights[tap];
		}
		previous /= s_history_weight_sum;
	}
	// FIXED directions within the texel, every frame -- determinism is this pass's foundation --
	// but the RAY BUDGET is importance-allocated. Each texel's share of the group's 64 rays is
	// proportional to its own history luminance: a cone holding an emitter earns up to four
	// sub-cone samples (which is what resolves a bulb smaller than the cone), a dark cone coasts
	// on its converged history and spends nothing. The allocation derives from history, which
	// changes slowly, so it is as stable as the directions themselves; a round-robin forced
	// refresh guarantees every cone is re-measured within eight frames, bounding the discovery
	// latency of a light turning on in a previously dark direction.
	int dir_index = local.y * GI_PROBE_DIR_EDGE + local.x;
	float texel_importance =
	    dot(previous.xyz, vec3(0.299, 0.587, 0.114)) + 0.02;
	s_texel_importance[dir_index] = texel_importance;
	barrier();
	if(dir_index == 0)
	{
		float total = 0.0;
		for(int d = 0; d < GI_PROBE_DIR_COUNT; ++d)
		{
			total += s_texel_importance[d];
		}
		s_importance_total = max(total, 1e-4);
	}
	barrier();
	int sample_count =
	    min(4, int(floor(float(GI_PROBE_DIR_COUNT) * texel_importance / s_importance_total + 0.5)));
	bool forced_refresh = ((uint(dir_index) + u_gi_probe_frame) & 7u) == 0u;
	if(sample_count == 0 && (forced_refresh || !has_history))
	{
		sample_count = 1;
	}
	vec2 tile_uv = (vec2(local.xy) + vec2_splat(0.5)) / float(GI_PROBE_DIR_EDGE);
	vec3 direction = GiOctDecode(tile_uv);
	// Below the anchor's tangent plane: not traced (see the header note).
	if(dot(direction, s_world_normal) < -0.2)
	{
		imageStore(s_probe_radiance_out, texel, vec4_splat(0.0));
		return;
	}
	if(sample_count == 0)
	{
		// Unsampled this frame: the converged history stands as-is.
		imageStore(s_probe_radiance_out, texel, previous);
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
	// One to four rays through FIXED sub-cone positions -- deterministic for any given count, so
	// a static scene still produces bit-identical probe input every frame. Multiple samples are
	// what resolve an emitter smaller than the cone: the single centre ray either skewered a bulb
	// or missed it entirely, and either answer was wrong by most of the bulb's energy.
	vec2 sub_positions[4];
	sub_positions[0] = vec2(0.5, 0.5);
	sub_positions[1] = vec2(0.25, 0.25);
	sub_positions[2] = vec2(0.75, 0.25);
	sub_positions[3] = vec2(0.25, 0.75);
	vec3 sample_sum = vec3_splat(0.0);
	float sample_proximity = 0.0;
	for(int s = 0; s < sample_count; ++s)
	{
		vec2 sample_uv = (vec2(local.xy) + sub_positions[s]) / float(GI_PROBE_DIR_EDGE);
		GiRayOutcome outcome = GiGatherRay(setup, GiOctDecode(sample_uv));
		sample_sum += outcome.radiance;
		sample_proximity += GiProbeEncodeProximity(outcome);
	}
	vec4 current = vec4(sample_sum, sample_proximity) / float(sample_count);
	// Blended into the reprojected history fetched above. The encoded proximity rides in alpha
	// and accumulates through the same blend, so it is as converged and as reprojected as the
	// radiance it gates.
	if(!has_history)
	{
		previous = current;
	}
	vec4 blended = mix(previous, current, s_history_alpha);
	imageStore(s_probe_radiance_out, texel, blended);
}
