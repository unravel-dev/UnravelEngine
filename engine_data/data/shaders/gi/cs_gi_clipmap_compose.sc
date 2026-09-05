/*
 * Composes ONE cascade level of the global SDF clipmap, one thread per voxel.
 *
 * TRANSCRIPTION of global_sdf_clipmap::compose_level. That function stays as the reference
 * implementation and as the fallback when compute is unavailable, and
 * test_clipmap_gpu_composition_matches_cpu asserts the two agree -- the same arrangement that
 * already guards SdfResolveSurfacePoint, which is the one place this system's correctness rests
 * on agreement between a CPU writer and a GPU reader.
 *
 * Why this moved to the GPU: the CPU composer measured 4.20 ms of WALL time on the main thread
 * (87% of it blocked on the pool it dispatches to), which is very nearly the entire GI GPU cost
 * for the frame. It fires whenever the camera moves far enough to re-snap a level, so it lands
 * as a stutter during movement rather than as steady cost.
 *
 * No new GPU data. The instances, their transforms, the atlas and the cull grid are all already
 * resident for tracing, so this reads exactly what a ray reads -- which is also what keeps the
 * composed field and the traced field from drifting apart.
 */

#include "bgfx_compute.sh"
#include "gi/sdf_common.sh"

IMAGE3D_WO(s_clipmap_out, r8, 5);

/// The surface-voxel list (cursor header + entries; see cs_gi_clipmap_attributes.sc). This
/// pass only RESETS its level's append cursor - folded in here, rather than in a separate
/// one-thread dispatch, because the attribute pass that appends SAMPLES the distance volume
/// this pass writes as an image: that read-after-write is a genuine resource transition on
/// every backend, and it is what orders the reset ahead of the appends. The old standalone
/// reset relied on submission order alone, which D3D12 does not turn into a barrier for
/// same-state UAV->UAV access - the appends could begin before the reset landed.
BUFFER_RW(b_surface_list, uint, 9);

/// x = level index, y = voxels per axis, z = this level's voxel size, w = reach in world units
/// (encode_range * voxel_size), the distance beyond a voxel at which an instance cannot change
/// the byte written here.
uniform vec4 u_clipmap_compose_params;
#define u_compose_level      int(u_clipmap_compose_params.x)
#define u_compose_resolution u_clipmap_compose_params.y
#define u_compose_voxel_size u_clipmap_compose_params.z
#define u_compose_reach      u_clipmap_compose_params.w

/// xyz = this level's world-space origin (its minimum corner, already snapped).
uniform vec4 u_clipmap_compose_origin;

/// The voxel box this dispatch composes: xyz = its minimum corner in level voxels, w > 0.5
/// when this is the level's first dispatch of the frame and resets the surface-list cursor.
/// A full recompose is one box over the whole level; a SCROLL-ONLY recompose (the origin
/// moved with the instance content unchanged - global_sdf_clipmap::level::scroll_only) blits
/// the overlap of the old and new windows into place and dispatches only the exposed slabs.
uniform vec4 u_clipmap_compose_range;
/// xyz = the box's size in voxels.
uniform vec4 u_clipmap_compose_range_size;

NUM_THREADS(4, 4, 4)
void main()
{
	ivec3 local = ivec3(gl_GlobalInvocationID.xyz);
	if(all(equal(local, ivec3(0, 0, 0))) && u_clipmap_compose_range.w > 0.5)
	{
		// This level's surface-list append cursor, reset for the attribute pass that follows
		// (see the buffer's note above for why the reset lives here).
		b_surface_list[uint(u_compose_level)] = 0u;
	}
	if(any(greaterThanEqual(local, ivec3(u_clipmap_compose_range_size.xyz))))
	{
		return;
	}
	ivec3 voxel = ivec3(u_clipmap_compose_range.xyz) + local;
	int resolution = int(u_compose_resolution);
	if(voxel.x >= resolution || voxel.y >= resolution || voxel.z >= resolution)
	{
		return;
	}
	vec3 world_position =
	    u_clipmap_compose_origin.xyz + (vec3(voxel) + vec3_splat(0.5)) * u_compose_voxel_size;

	// Seeded at the reach rather than at infinity. A voxel stores distances in [-reach, reach]
	// and saturates beyond, so an instance further away cannot change the byte written here --
	// and starting at infinity would force the FIRST candidate to be sampled in full before the
	// cheap reject below could do anything. Output-identical by construction: with nothing
	// sampled this encodes to exactly the saturated value infinity would have produced.
	float nearest = u_compose_reach;

	// Candidates from the TRACER's grid, not a grid of its own. The CPU composer builds a private
	// per-level grid because it has no other one to hand; here the world grid is already resident.
	//
	// It is binned from RAW instance bounds, though, while the CPU inflates them by the reach
	// before binning. That difference is not an optimisation detail: an instance within reach of
	// this voxel but not CONTAINING it would be missed, the voxel would keep the saturated seed,
	// and the composed distance would be an OVER-estimate -- the one direction a conservative
	// field may never err in, and one a sphere trace turns into stepping straight through a wall.
	//
	// So the cell RANGE covering [position - reach, position + reach] is walked rather than the
	// single containing cell. That is exactly the set the CPU's inflated binning produces, because
	// the cheap reject below discards anything further than the reach anyway. Instances shared
	// between cells are tested more than once, which costs time and cannot change a minimum.
	if(u_sdf_grid_enabled)
	{
		vec3 grid_min = u_sdf_grid_origin;
		vec3 inv_cell = vec3_splat(1.0) / vec3_splat(u_sdf_grid_cell_size);
		vec3 last_cell = u_sdf_grid_dim - vec3_splat(1.0);
		// Clamped rather than skipped when outside: the edge cell holds every instance near that
		// face, so clamping over-reports candidates and can never drop one.
		vec3 lo_f = clamp(floor((world_position - vec3_splat(u_compose_reach) - grid_min) * inv_cell),
		                  vec3_splat(0.0),
		                  last_cell);
		vec3 hi_f = clamp(floor((world_position + vec3_splat(u_compose_reach) - grid_min) * inv_cell),
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
						SdfInstance inst = SdfLoadInstance(index);
						// Cheap reject before the field lookup: outside the instance's bounds the
						// distance to those bounds is already a valid conservative answer, and
						// usually a worse one than what another instance contributes.
						vec3 clamped_to_bounds =
						    clamp(world_position, inst.world_bounds_min, inst.world_bounds_max);
						float to_bounds = length(world_position - clamped_to_bounds);
						// Guarded on nearest being non-negative, exactly as the CPU composer is:
						// to_bounds is zero inside any bounds and never negative, so once the
						// voxel is inside some instance this would skip every remaining candidate
						// and the interior would depend on visit order.
						if(nearest >= 0.0 && to_bounds >= nearest)
						{
							continue;
						}
						SdfHeader header = SdfLoadHeader(inst.header_index);
						vec3 local_position =
						    SdfTransformPoint(inst.world_to_local_rows, world_position);
						float local_distance = SdfSampleLocal(header, local_position);
						nearest = min(nearest, local_distance * inst.local_to_world_scale);
					}
				}
			}
		}
	}

	// Same encoding as encode_clipmap_distance, in VOXELS of this level. The two must agree
	// exactly: the CPU path still composes levels this dispatch does not, and a sampler cannot
	// tell which path wrote the voxel it reads.
	float distance_in_voxels = nearest / u_compose_voxel_size;
	float encoded = clamp(distance_in_voxels / (2.0 * u_sdf_clipmap_encode_range) + 0.5, 0.0, 1.0);
	// The levels are stacked along Z in one volume, so this level's slab starts at level * res.
	ivec3 texel = ivec3(voxel.x, voxel.y, voxel.z + u_compose_level * resolution);
	imageStore(s_clipmap_out, texel, vec4_splat(encoded));
}
