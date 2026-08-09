/*
 * Composes ONE cascade level's ATTRIBUTE voxels - albedo, emissive, and the surface-voxel
 * list - one thread per attribute voxel, after the level's distance voxels are current.
 *
 * TRANSCRIPTION of global_sdf_clipmap::compose_level_attributes, in the same reference
 * relationship as cs_gi_clipmap_compose.sc is to compose_level, pinned by
 * test_clipmap_attribute_transcription_matches_cpu.
 *
 * A voxel is SURFACE when the composed field at its centre lies within GI_SURFACE_VOXEL_BAND
 * attribute voxels of zero (band gate alone - see gi_constants.h). Surface voxels take the
 * albedo/emissive of the instance with the smallest |distance| at the centre - ties broken by
 * the smaller instance INDEX, which is what keeps this walk and the CPU composer's
 * differently-ordered walk in exact agreement - and append their packed coordinate to the
 * level's segment of the surface-voxel list, which drives the light-voxel update's indirect
 * dispatch.
 *
 * Non-surface voxels write zeros: a level that scrolled must not keep serving the previous
 * region's materials.
 */

#include "bgfx_compute.sh"
#include "gi/sdf_common.sh"
#include "gi/gi_constants.sh"
// Toroidal slot/cell math shared with every attribute consumer.
#include "gi/gi_light_voxels.sh"

/// rgb = winning albedo, a = 1 where surface. Levels stacked along Z like the distance volume.
IMAGE3D_WO(s_attr_albedo_out, rgba8, 5);
/// rgb = winning emissive (radiance units), a unused.
IMAGE3D_WO(s_attr_emissive_out, rgba16f, 6);
/// Packed surface-voxel coordinates, one segment of attr_resolution^3 entries per level. At a
/// high stage ON PURPOSE: OpenGL guarantees only eight image units (bindings 0-7), so the
/// light-volume IMAGE below takes the low slot while buffers tolerate the high ones.
BUFFER_RW(b_surface_list, uint, 9);
/// One append cursor per level, reset by cs_gi_surface_count_reset before this dispatch.
BUFFER_RW(b_surface_count, uint, 8);
/// The light volume (gi_light_voxels.sh layout). Addressing is TOROIDAL: this pass zeroes a
/// slot's six face slabs only when the slot's world CELL changed hands (detected through
/// b_attr_cells) - a surviving cell keeps the radiance it accumulated across any number of
/// level re-snaps, which is what stops camera motion from pulsing the bounce light dark.
IMAGE3D_WO(s_light_voxels_out, rgba16f, 7);
/// One packed cell id per attribute slot per level (GiLightVoxelPackCell).
BUFFER_RW(b_attr_cells, uint, 11);
/// Per-texture mean colours (cs_gi_texture_mean.sc); an instance's albedo is its base colour
/// FACTOR times means[mean_slot]. Slot 0 means "no mean": no texture, capture not yet landed,
/// or the CPU reference composer (which has no mean buffer) - the multiply is skipped rather
/// than read, so the buffer never needs a seeded value.
BUFFER_RO(b_gi_texture_means, vec4, 10);

/// x = level index, y = attribute voxels per axis, z = attribute voxel size (world),
/// w = candidate reach in world units (band + one attribute voxel; see the CPU reference).
uniform vec4 u_clipmap_attr_params;
#define u_attr_level      int(u_clipmap_attr_params.x)
#define u_attr_resolution u_clipmap_attr_params.y
#define u_attr_voxel_size u_clipmap_attr_params.z
#define u_attr_reach      u_clipmap_attr_params.w

/// xyz = this level's world-space origin (snapped minimum corner).
uniform vec4 u_clipmap_compose_origin;

NUM_THREADS(4, 4, 4)
void main()
{
	ivec3 slot = ivec3(gl_GlobalInvocationID.xyz);
	int resolution = int(u_attr_resolution);
	if(slot.x >= resolution || slot.y >= resolution || slot.z >= resolution)
	{
		return;
	}
	ivec3 texel = ivec3(slot.x, slot.y, slot.z + u_attr_level * resolution);
	// Which world cell this slot represents under the current window: the level origin is an
	// exact multiple of the attribute voxel (the snap guarantees it), so the window base is
	// integer cells and the slot's cell is the unique one inside the window that wraps to it.
	ivec3 window_base = ivec3(floor(u_clipmap_compose_origin.xyz / u_attr_voxel_size + vec3_splat(0.5)));
	ivec3 base_slot = GiLightVoxelSlot(window_base);
	ivec3 offset = ivec3((slot.x - base_slot.x + resolution) % resolution,
	                     (slot.y - base_slot.y + resolution) % resolution,
	                     (slot.z - base_slot.z + resolution) % resolution);
	ivec3 cell = window_base + offset;
	// Radiance survives while the cell identity does; a slot claimed by a NEW cell resets its
	// light so the departed region's radiance can never be read at the new position.
	uint packed_cell_id = GiLightVoxelPackCell(cell, u_attr_level);
	int cell_index =
	    ((u_attr_level * resolution + slot.z) * resolution + slot.y) * resolution + slot.x;
	if(b_attr_cells[cell_index] != packed_cell_id)
	{
		b_attr_cells[cell_index] = packed_cell_id;
		for(int face = 0; face < 6; ++face)
		{
			imageStore(s_light_voxels_out, GiLightVoxelTexel(slot, u_attr_level, face), vec4_splat(0.0));
		}
	}
	vec3 center = (vec3(cell) + vec3_splat(0.5)) * u_attr_voxel_size;
	float band = GI_SURFACE_VOXEL_BAND * u_attr_voxel_size;
	// Band gate on the composed field, ALONE (transcribes the CPU reference, including the
	// removal of the gradient gate - see compose_level_attributes for the thin-wall-valley
	// reasoning). Deep interiors read the bake's conservative empty-inside distances and fail
	// the band on their own.
	float field_distance = SdfSampleClipmapLevel(u_attr_level, center);
	bool is_surface = field_distance < 0.5 * SDF_CLIPMAP_OUTSIDE && abs(field_distance) <= band;
	if(!is_surface)
	{
		imageStore(s_attr_albedo_out, texel, vec4_splat(0.0));
		imageStore(s_attr_emissive_out, texel, vec4_splat(0.0));
		// A voxel that STOPPED being surface leaves the list and is never re-lit, so its
		// radiance must die here or it survives as a ghost: geometry that moved away kept
		// glowing at its old cells (a closed door's old radiance held the room lit through
		// the trilinear neighbourhood). Surface voxels keep their radiance - zeroing THOSE
		// is the recompose flicker this pass's claim logic exists to prevent.
		for(int face = 0; face < 6; ++face)
		{
			imageStore(s_light_voxels_out, GiLightVoxelTexel(slot, u_attr_level, face), vec4_splat(0.0));
		}
		return;
	}
	// Winner: smallest |distance| at the centre over the tracer grid's candidates within
	// reach, ties to the smaller GLOBAL instance index. The same cell-range walk as
	// cs_gi_clipmap_compose, for the same reason: an instance within reach of this voxel
	// without containing it must still be found.
	// TOP-2 attribution, blended by proximity (see the CPU reference for the full argument):
	// a coarse voxel CONTAINS a mixture of the surfaces inside it, and winner-take-all painted
	// whole voxels one instance's colour (red halos around distant awnings). Both slots update
	// min-style with index tie-breaks and skip already-tracked indices, so repeated candidate
	// visits from overlapping grid cells stay no-ops - the idempotence that keeps this walk
	// and the CPU composer's full loop in exact agreement.
	float best_magnitude = u_attr_reach;
	float second_magnitude = u_attr_reach;
	int best_index = -1;
	int second_index = -1;
	if(u_sdf_grid_enabled)
	{
		vec3 grid_min = u_sdf_grid_origin;
		vec3 inv_cell = vec3_splat(1.0) / vec3_splat(u_sdf_grid_cell_size);
		vec3 last_cell = u_sdf_grid_dim - vec3_splat(1.0);
		vec3 lo_f = clamp(floor((center - vec3_splat(u_attr_reach) - grid_min) * inv_cell),
		                  vec3_splat(0.0),
		                  last_cell);
		vec3 hi_f = clamp(floor((center + vec3_splat(u_attr_reach) - grid_min) * inv_cell),
		                  vec3_splat(0.0),
		                  last_cell);
		ivec3 lo = ivec3(lo_f);
		ivec3 hi = ivec3(hi_f);
		int dim_x = int(u_sdf_grid_dim.x);
		int dim_xy = int(u_sdf_grid_dim.x * u_sdf_grid_dim.y);
		for(int cz = lo.z; cz <= hi.z; ++cz)
		{
			for(int cy = lo.y; cy <= hi.y; ++cy)
			{
				for(int cx = lo.x; cx <= hi.x; ++cx)
				{
					uint cell = uint(cx + cy * dim_x + cz * dim_xy);
					uint candidate_begin = b_sdf_grid_offsets[cell];
					uint candidate_end = b_sdf_grid_offsets[cell + 1u];
					for(uint candidate = candidate_begin; candidate < candidate_end; ++candidate)
					{
						int index = int(b_sdf_grid_instances[candidate]);
						if(index == best_index || index == second_index)
						{
							continue;
						}
						SdfInstance inst = SdfLoadInstance(index);
						vec3 clamped_to_bounds =
						    clamp(center, inst.world_bounds_min, inst.world_bounds_max);
						if(length(center - clamped_to_bounds) >= second_magnitude)
						{
							continue;
						}
						SdfHeader header = SdfLoadHeader(inst.header_index);
						vec3 local_position = SdfTransformPoint(inst.world_to_local_rows, center);
						float magnitude =
						    abs(SdfSampleLocal(header, local_position) * inst.local_to_world_scale);
						if(magnitude < best_magnitude ||
						   (magnitude == best_magnitude && (best_index < 0 || index < best_index)))
						{
							second_magnitude = best_magnitude;
							second_index = best_index;
							best_magnitude = magnitude;
							best_index = index;
						}
						else if(magnitude < second_magnitude ||
						        (magnitude == second_magnitude &&
						         (second_index < 0 || index < second_index)))
						{
							second_magnitude = magnitude;
							second_index = index;
						}
					}
				}
			}
		}
	}
	if(best_index < 0)
	{
		// The field says surface but nothing is attributable within reach: stay dark - energy
		// loss, never a fabricated material. Unattributed voxels also leave the list, so any
		// radiance they held dies with the attribution (same ghost rule as the branch above).
		imageStore(s_attr_albedo_out, texel, vec4_splat(0.0));
		imageStore(s_attr_emissive_out, texel, vec4_splat(0.0));
		for(int face = 0; face < 6; ++face)
		{
			imageStore(s_light_voxels_out, GiLightVoxelTexel(slot, u_attr_level, face), vec4_splat(0.0));
		}
		return;
	}
	SdfInstance first = SdfLoadInstance(best_index);
	vec3 first_albedo = first.albedo;
	if(first.mean_slot != 0u)
	{
		first_albedo *= b_gi_texture_means[first.mean_slot].xyz;
	}
	// Single-source voxels copy EXACTLY: (a * w) / w is not an identity in float, and a
	// one-ULP wobble flips quantisation on boundary values (see the CPU reference).
	vec3 blended_albedo = first_albedo;
	vec3 blended_emissive = first.emissive;
	if(second_index >= 0)
	{
		SdfInstance second = SdfLoadInstance(second_index);
		vec3 second_albedo = second.albedo;
		if(second.mean_slot != 0u)
		{
			second_albedo *= b_gi_texture_means[second.mean_slot].xyz;
		}
		float w1 = u_attr_reach - best_magnitude;
		float w2 = u_attr_reach - second_magnitude;
		float w_sum = max(w1 + w2, 1e-6);
		blended_albedo = (first_albedo * w1 + second_albedo * w2) / w_sum;
		blended_emissive = (first.emissive * w1 + second.emissive * w2) / w_sum;
	}
	imageStore(s_attr_albedo_out, texel, vec4(blended_albedo, 1.0));
	imageStore(s_attr_emissive_out, texel, vec4(blended_emissive, 0.0));
	uint cursor;
	atomicFetchAndAdd(b_surface_count[uint(u_attr_level)], 1u, cursor);
	uint capacity = uint(resolution * resolution * resolution);
	// Cannot overflow by construction - the segment holds one entry per voxel and each thread
	// appends at most once - so this clamp is a guard against a mis-sized buffer, not policy.
	if(cursor < capacity)
	{
		// packed_slot, not `packed`: a GLSL layout-qualifier keyword, illegal as a variable
		// name on the OpenGL backend.
		uint packed_slot = uint(slot.x) | (uint(slot.y) << 8u) | (uint(slot.z) << 16u) |
		                   (uint(u_attr_level) << 24u);
		b_surface_list[uint(u_attr_level) * capacity + cursor] = packed_slot;
	}
}
