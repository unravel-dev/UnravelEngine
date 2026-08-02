#ifndef __GI_RADIANCE_CACHE_SH__
#define __GI_RADIANCE_CACHE_SH__

/*
 * World-anchored spatial hash holding cached radiance.
 *
 * MIRROR OF engine/engine/rendering/gi/radiance_cache.{h,cpp}. The hash, the key derivation,
 * the normal quantisation and the probe policy must match that file exactly -- it is the
 * reference the gi_tests harness pins down, and a key that differs by one bit between the
 * writer and the reader simply never finds anything, which looks like an empty cache rather
 * than like a transcription bug.
 *
 * RESERVED RESOURCE STAGES 6 (keys) and 7 (payload).
 */

#include "../bgfx_compute.sh"

#define GI_CACHE_PROBE_LENGTH 8
#define GI_CACHE_EMPTY_KEY    0u
#define GI_CACHE_INVALID_SLOT 0xFFFFFFFFu
/// vec4 elements of payload per entry. Mirror of radiance_cache_gpu::data_vec4_stride.
#define GI_CACHE_DATA_STRIDE  5

/// x = capacity mask, y = base cell size, z = base distance, w = max level.
uniform vec4 u_gi_cache_params;
#define u_gi_cache_mask          uint(u_gi_cache_params.x)
#define u_gi_cache_base_cell     u_gi_cache_params.y
#define u_gi_cache_base_distance u_gi_cache_params.z
#define u_gi_cache_max_level     u_gi_cache_params.w

/// x = current frame, y = minimum blend weight, z = maximum samples,
/// w = level cross-fade band as a fraction of the handover distance.
///
/// The band belongs with the other KEY parameters, on the cache, for the reason the comment on
/// radiance_cache_gpu::get_settings gives: a writer and a reader that disagree on it insert and
/// look up at different levels near a boundary and never find each other's entries.
uniform vec4 u_gi_cache_params2;
#define u_gi_cache_frame       uint(u_gi_cache_params2.x)
#define u_gi_cache_min_alpha   u_gi_cache_params2.y
#define u_gi_cache_max_samples u_gi_cache_params2.z
#define u_gi_cache_level_blend u_gi_cache_params2.w

uint GiHashUint(uint value)
{
	uint state = value * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

uint GiHashCombine(uint seed, uint value)
{
	return GiHashUint(seed ^ (value + 0x9e3779b9u + (seed << 6u) + (seed >> 2u)));
}

/// Cube face of a given axis, sign included.
uint GiFaceFromAxis(vec3 normal, uint axis)
{
	float axis_value = normal.z;
	if(axis == 0u)
	{
		axis_value = normal.x;
	}
	else if(axis == 1u)
	{
		axis_value = normal.y;
	}
	return axis * 2u + (axis_value < 0.0 ? 1u : 0u);
}

/// Index of the largest component. Ties resolve deterministically, x before y before z.
uint GiDominantAxis(vec3 magnitude)
{
	if(magnitude.y > magnitude.x && magnitude.y >= magnitude.z)
	{
		return 1u;
	}
	if(magnitude.z > magnitude.x && magnitude.z >= magnitude.y)
	{
		return 2u;
	}
	return 0u;
}

/**
 * Quantises a normal to one of the 6 cube faces.
 *
 * Six, not a finer subdivision. A 2x2 split per face puts a bin boundary exactly through the
 * face CENTRE -- that is, through the axis-aligned directions, which are by far the most common
 * normals in a built scene. A floor at (0, 1, 0) then has its bin decided by the sign of two
 * components that are both zero, so the tiniest disagreement between two sources of that normal
 * yields a different bin, and every large flat surface becomes a permanent cache miss. With six
 * faces the axis directions sit at bin CENTRES instead, where they are maximally robust.
 *
 * Coarse is also sufficient: the facing only has to separate surfaces that face meaningfully
 * differently -- the two sides of a wall, the floor from the ceiling -- which six faces do.
 */
uint GiQuantizeNormal(vec3 normal)
{
	return GiFaceFromAxis(normal, GiDominantAxis(abs(normal)));
}

/// The runner up to GiQuantizeNormal, used to tolerate a facing near a bin boundary.
uint GiQuantizeNormalSecond(vec3 normal)
{
	vec3 magnitude = abs(normal);
	uint axis = GiDominantAxis(magnitude);
	// Mask the winner out so the same comparison yields the runner up.
	if(axis == 0u)
	{
		magnitude.x = -1.0;
	}
	else if(axis == 1u)
	{
		magnitude.y = -1.0;
	}
	else
	{
		magnitude.z = -1.0;
	}
	return GiFaceFromAxis(normal, GiDominantAxis(magnitude));
}

/// Unit direction of a cube face produced by GiQuantizeNormal.
vec3 GiFaceDirection(uint face)
{
	uint axis = face >> 1u;
	float face_sign = (face & 1u) != 0u ? -1.0 : 1.0;
	vec3 direction = vec3_splat(0.0);
	if(axis == 0u)
	{
		direction.x = face_sign;
	}
	else if(axis == 1u)
	{
		direction.y = face_sign;
	}
	else
	{
		direction.z = face_sign;
	}
	return direction;
}

float GiCacheCellSize(uint level)
{
	return u_gi_cache_base_cell * float(1u << level);
}

/**
 * Level of detail from distance to the camera. Cells grow with distance so the cache stays
 * bounded regardless of world size. A step function on purpose: within a level the cell size is
 * constant, so a moving camera does not continuously reshape the grid under the cached values.
 */
uint GiCacheLevel(vec3 position, vec3 camera_position)
{
	float distance = length(position - camera_position);
	if(distance <= u_gi_cache_base_distance)
	{
		return 0u;
	}
	float ratio = distance / u_gi_cache_base_distance;
	float level = floor(log2(ratio)) + 1.0;
	return uint(clamp(level, 0.0, u_gi_cache_max_level));
}

/**
 * As GiCacheLevel, also reporting how far into the cross-fade band the point lies: 0 where the
 * level answers alone, rising to 1 at the handover distance.
 *
 * Levels are a step function of distance to the camera, so crossing a boundary changes the cell
 * size and therefore every KEY for that surface. The entries built at the previous level are not
 * wrong, they are UNREACHABLE, and a miss lowers the resolve weight so the consumer falls back to
 * the environment probe. That reads as GI getting darker as the camera approaches a surface.
 *
 * A caller inside the band must address BOTH levels -- a writer inserts into each, a reader blends
 * them -- so the surface stays reachable across the handover. Same fix, and the same reason, as the
 * cascade cross-fade: a step in a function that two consumers must agree on makes them resolve
 * different answers either side of it.
 *
 * Transcription of radiance_cache::compute_level_ex.
 */
uint GiCacheLevelEx(vec3 position, vec3 camera_position, out float out_blend)
{
	out_blend = 0.0;
	uint level = GiCacheLevel(position, camera_position);
	// The outermost level has nothing to fade into.
	if(float(level) >= u_gi_cache_max_level || u_gi_cache_level_blend <= 0.0)
	{
		return level;
	}
	// Level 0 covers up to base_distance and each level after it doubles, so the handover for level
	// k sits at base_distance * 2^k.
	float handover = u_gi_cache_base_distance * exp2(float(level));
	float band = handover * clamp(u_gi_cache_level_blend, 0.0, 1.0);
	if(band <= 0.0)
	{
		return level;
	}
	float distance = length(position - camera_position);
	float band_start = handover - band;
	if(distance > band_start)
	{
		out_blend = clamp((distance - band_start) / band, 0.0, 1.0);
	}
	return level;
}

/**
 * The key for a surface point. Depends only on quantised position, level and facing, never on
 * anything camera-derived, which is what makes a point resolve to the same entry from any
 * viewpoint. The normal is part of the key so the lit and unlit sides of a wall never share an
 * entry -- the first line of defence against light leaking through it.
 */
uint GiCacheKeyForFace(vec3 position, uint face, uint level)
{
	float cell_size = GiCacheCellSize(level);
	// Lift half a cell along the face direction before snapping. A surface lying exactly on a
	// cell plane -- a ground plane at y = 0 with 0.25 m cells, say -- otherwise has the grid
	// boundary running through it, so the sign of a rounding error decides the cell and a writer
	// and reader that disagree by an epsilon address different entries. Half a cell is far
	// larger than any such epsilon, and the offset is derived from the QUANTISED face rather
	// than the raw normal so both sides shift identically.
	vec3 snapped = position + GiFaceDirection(face) * (cell_size * 0.5);
	// floor, not truncation: truncation folds the cells either side of zero together, putting
	// two surfaces a whole cell apart into one entry across every axis plane.
	vec3 cell = floor(snapped / cell_size);
	uint key = GiHashUint(uint(int(cell.x)));
	key = GiHashCombine(key, uint(int(cell.y)));
	key = GiHashCombine(key, uint(int(cell.z)));
	key = GiHashCombine(key, level);
	key = GiHashCombine(key, face);
	// Never produce the empty sentinel, or an occupied slot would read as free.
	return key == GI_CACHE_EMPTY_KEY ? 1u : key;
}

uint GiCacheKey(vec3 position, vec3 normal, uint level)
{
	return GiCacheKeyForFace(position, GiQuantizeNormal(normal), level);
}

#endif // __GI_RADIANCE_CACHE_SH__

#if defined(GI_CACHE_READ_ONLY) || defined(GI_CACHE_READ_WRITE)

#ifndef __GI_RADIANCE_CACHE_ACCESS_SH__
#define __GI_RADIANCE_CACHE_ACCESS_SH__

/*
 * Cache storage access.
 *
 * Split from the pure key maths above so a read-only consumer -- the indirect lighting pass --
 * can bind the buffers as read-only. Declaring them read/write everywhere would force every
 * consumer into a UAV binding, which serialises against anything else touching the resource.
 *
 * Define exactly one of GI_CACHE_READ_ONLY or GI_CACHE_READ_WRITE before including.
 */

#ifdef GI_CACHE_READ_WRITE
BUFFER_RW(b_gi_cache_keys, uint, 6);
BUFFER_RW(b_gi_cache_data, vec4, 7);
#else
BUFFER_RO(b_gi_cache_keys, uint, 6);
BUFFER_RO(b_gi_cache_data, vec4, 7);
#endif

/// Payload slot indices, per radiance_cache_gpu::data_vec4_stride.
#define GI_CACHE_DATA_RADIANCE 0u
#define GI_CACHE_DATA_POSITION 1u
#define GI_CACHE_DATA_NORMAL   2u
/// Surface properties of the cell, captured where it was discovered.
///
/// Without these the cache holds lighting but nothing about what the surface DOES with it, so
/// bounced light comes back uncoloured -- a red floor cannot tint a wall -- and emitters cannot
/// contribute at all, because emission is a material property rather than a light in the buffer.
#define GI_CACHE_DATA_ALBEDO   3u
#define GI_CACHE_DATA_EMISSIVE 4u

uint GiCacheDataIndex(uint slot, uint field)
{
	return slot * uint(GI_CACHE_DATA_STRIDE) + field;
}

/**
 * Finds a key's slot without inserting. Returns GI_CACHE_INVALID_SLOT when not resident.
 */
uint GiCacheFind(uint key)
{
	uint base = key & u_gi_cache_mask;
	for(uint i = 0u; i < uint(GI_CACHE_PROBE_LENGTH); ++i)
	{
		uint slot = (base + i) & u_gi_cache_mask;
		if(b_gi_cache_keys[slot] == key)
		{
			return slot;
		}
	}
	return GI_CACHE_INVALID_SLOT;
}

/**
 * Finds the slot for a surface, tolerating a facing that sits on a quantisation boundary.
 *
 * A writer and a reader derive the normal from different sources -- the G-buffer on one side,
 * the field gradient on the other -- so a surface whose normal falls between two cube faces can
 * be binned differently by each, and would then never be found. When the dominant face misses,
 * the runner up is tried: near a tie both sides agree on the SET of the top two faces even when
 * they disagree on the order, so this always covers the writer's choice.
 */
uint GiCacheFindSurface(vec3 position, vec3 normal, uint level)
{
	uint slot = GiCacheFind(GiCacheKeyForFace(position, GiQuantizeNormal(normal), level));
	if(slot == GI_CACHE_INVALID_SLOT)
	{
		slot = GiCacheFind(GiCacheKeyForFace(position, GiQuantizeNormalSecond(normal), level));
	}
	return slot;
}

/**
 * Bilinearly interpolates cached radiance over the four cells bracketing a point in its TANGENT
 * plane. Returns false when none of them holds an entry.
 *
 * A cell is metres across where a pixel is millimetres, so a point lookup makes any gather
 * piecewise constant at cell scale -- blocks that shift whenever the cascade re-snaps or the level
 * steps. That is BIAS, not noise: temporal accumulation converges to it rather than averaging it
 * away, and a luminance edge stop cannot tell a cell boundary from a real lighting edge, so no
 * filter setting removes the blocks without removing genuine detail along with them.
 *
 * Interpolating removes the steps at their source, and does it DETERMINISTICALLY. The tempting
 * alternative -- jitter the lookup within the cell and let the temporal filter integrate the
 * result -- costs no extra lookups but converts the blocks into shimmer, which is strictly worse
 * while anything is moving, because motion is precisely when there are no frames to integrate over
 * and when the accumulation count has been reset by disocclusion.
 *
 * The cell grid is world-axis aligned, so the tangent axes are simply the two world axes other than
 * the one the quantised face points along. No arbitrary basis is needed and the interpolation lines
 * up with the cells exactly.
 *
 * Interpolating ONLY in the tangent plane is load bearing. Blending along the normal would mix the
 * two sides of a thin wall, which is the leak that putting the normal in the key exists to prevent.
 */
bool GiCacheGatherForFace(vec3 position, uint face, uint level, out vec3 out_radiance)
{
	out_radiance = vec3_splat(0.0);
	uint axis = face >> 1u;
	vec3 tu;
	vec3 tv;
	if(axis == 0u)
	{
		tu = vec3(0.0, 1.0, 0.0);
		tv = vec3(0.0, 0.0, 1.0);
	}
	else if(axis == 1u)
	{
		tu = vec3(1.0, 0.0, 0.0);
		tv = vec3(0.0, 0.0, 1.0);
	}
	else
	{
		tu = vec3(1.0, 0.0, 0.0);
		tv = vec3(0.0, 1.0, 0.0);
	}
	float cell_size = GiCacheCellSize(level);
	// Continuous cell coordinates along the tangent axes. The key's half-cell lift is along the face
	// NORMAL, so it does not move these and does not need applying here.
	float gu = dot(position, tu) / cell_size;
	float gv = dot(position, tv) / cell_size;
	// Bracket by cell CENTRES, which sit at index + 0.5. Bracketing by cell boundaries instead would
	// step the weights as floor() ticks over, which is the discontinuity this exists to remove.
	float base_u = floor(gu - 0.5);
	float base_v = floor(gv - 0.5);
	float fu = (gu - 0.5) - base_u;
	float fv = (gv - 0.5) - base_v;
	// Displacement from the sample point to the centre of the bracket's lower cell on each axis.
	vec3 corner = position + tu * ((base_u + 0.5 - gu) * cell_size) +
	              tv * ((base_v + 0.5 - gv) * cell_size);
	float weight_sum = 0.0;
	for(int j = 0; j < 2; ++j)
	{
		for(int i = 0; i < 2; ++i)
		{
			vec3 tap = corner + (tu * float(i) + tv * float(j)) * cell_size;
			uint slot = GiCacheFind(GiCacheKeyForFace(tap, face, level));
			if(slot == GI_CACHE_INVALID_SLOT)
			{
				continue;
			}
			float weight_u = i == 0 ? 1.0 - fu : fu;
			float weight_v = j == 0 ? 1.0 - fv : fv;
			float weight = weight_u * weight_v;
			out_radiance += b_gi_cache_data[GiCacheDataIndex(slot, GI_CACHE_DATA_RADIANCE)].xyz * weight;
			weight_sum += weight;
		}
	}
	if(weight_sum <= 1e-6)
	{
		return false;
	}
	// Renormalised over the taps that resolved, so a neighbour the cache has not reached yet -- the
	// edge of a surface, or a cell nothing has registered -- hands its weight to the ones that did
	// rather than dragging the result toward black.
	out_radiance /= weight_sum;
	return true;
}

/// As @ref GiCacheGatherForFace, tolerating a facing on a quantisation boundary exactly as
/// GiCacheFindSurface does: near a tie both sides agree on the SET of the top two faces.
bool GiCacheGatherSurface(vec3 position, vec3 normal, uint level, out vec3 out_radiance)
{
	if(GiCacheGatherForFace(position, GiQuantizeNormal(normal), level, out_radiance))
	{
		return true;
	}
	return GiCacheGatherForFace(position, GiQuantizeNormalSecond(normal), level, out_radiance);
}

/**
 * Gathers a surface at its level, cross-fading into the next level inside the transition band.
 *
 * This is what a reader should call. Without the fade, crossing a level boundary re-keys the
 * surface, every entry built at the old level becomes unreachable, and the weight collapses to the
 * environment probe -- which looks like GI dimming as the camera approaches.
 *
 * Falls back cleanly when only one side is populated: the band is entered from one direction, so
 * for a few frames after a boundary crossing the far level may be the only one with entries. Taking
 * whichever resolved, rather than blending a miss in as black, keeps that transient invisible.
 */
bool GiCacheGatherLevels(vec3 position, vec3 normal, vec3 camera_position, out vec3 out_radiance)
{
	float blend;
	uint level = GiCacheLevelEx(position, camera_position, blend);
	vec3 near_radiance;
	bool near_found = GiCacheGatherSurface(position, normal, level, near_radiance);
	if(blend <= 0.0)
	{
		out_radiance = near_radiance;
		return near_found;
	}
	vec3 far_radiance;
	bool far_found = GiCacheGatherSurface(position, normal, level + 1u, far_radiance);
	if(near_found && far_found)
	{
		out_radiance = mix(near_radiance, far_radiance, blend);
		return true;
	}
	// Only one side has entries yet. Blending toward a miss would darken the band, which is the
	// artefact this exists to remove, so the populated side answers alone.
	out_radiance = near_found ? near_radiance : far_radiance;
	return near_found || far_found;
}

/// Single-cell lookup, the un-interpolated counterpart of @ref GiCacheGatherSurface. Kept so the
/// interpolation can be switched off and compared against, rather than being the kind of change
/// that can only be evaluated by rebuilding without it.
bool GiCacheGatherPoint(vec3 position, vec3 normal, uint level, out vec3 out_radiance)
{
	out_radiance = vec3_splat(0.0);
	uint slot = GiCacheFindSurface(position, normal, level);
	if(slot == GI_CACHE_INVALID_SLOT)
	{
		return false;
	}
	out_radiance = b_gi_cache_data[GiCacheDataIndex(slot, GI_CACHE_DATA_RADIANCE)].xyz;
	return true;
}

#ifdef GI_CACHE_READ_WRITE

/**
 * Finds a key's slot, claiming a free one or evicting the oldest when the chain is full.
 *
 * Only the KEYS contend: many threads may resolve to the same cell, so claiming a slot is done
 * with compare-exchange. The payload is written by exactly one thread per entry in the update
 * pass, so it needs no atomics and no fixed-point accumulation.
 */
uint GiCacheInsert(uint key, uint frame)
{
	uint base = key & u_gi_cache_mask;
	uint oldest_slot = GI_CACHE_INVALID_SLOT;
	uint oldest_frame = 0xFFFFFFFFu;
	uint oldest_key = GI_CACHE_EMPTY_KEY;
	for(uint i = 0u; i < uint(GI_CACHE_PROBE_LENGTH); ++i)
	{
		uint slot = (base + i) & u_gi_cache_mask;
		uint previous;
		atomicFetchCompareExchange(b_gi_cache_keys[slot], GI_CACHE_EMPTY_KEY, key, previous);
		if(previous == GI_CACHE_EMPTY_KEY)
		{
			// Freshly claimed. Only the KEYS are ever cleared -- the payload is deliberately left
			// alone at startup, on the reasoning that an entry is unreachable until its key
			// matches. Claiming a key is exactly the moment that stops being true, so the payload
			// has to be initialised here or the first read sees whatever the allocation held.
			//
			// This is the one place that can tell a fresh claim from a repeat, so callers cannot
			// do it: they receive a slot either way and zeroing on every call would reset the
			// accumulation every frame.
			b_gi_cache_data[GiCacheDataIndex(slot, GI_CACHE_DATA_RADIANCE)] = vec4_splat(0.0);
			b_gi_cache_data[GiCacheDataIndex(slot, GI_CACHE_DATA_ALBEDO)] = vec4_splat(0.0);
			b_gi_cache_data[GiCacheDataIndex(slot, GI_CACHE_DATA_EMISSIVE)] = vec4_splat(0.0);
			return slot;
		}
		if(previous == key)
		{
			return slot;
		}
		uint slot_frame = uint(b_gi_cache_data[GiCacheDataIndex(slot, GI_CACHE_DATA_POSITION)].w);
		if(slot_frame < oldest_frame)
		{
			oldest_frame = slot_frame;
			oldest_slot = slot;
			oldest_key = previous;
		}
	}
	// The chain is full. Take the least recently touched entry: a cell being asked for now
	// matters more than one nothing has looked at in a while. This is what replaces a free
	// list -- nothing to compact, no fragmentation to accumulate.
	//
	// Never take one touched THIS frame, though. Two cells contending for one chain would
	// otherwise replace each other every frame and neither would ever accumulate.
	if(oldest_slot == GI_CACHE_INVALID_SLOT || oldest_frame >= frame)
	{
		return GI_CACHE_INVALID_SLOT;
	}
	// Compare against the key we actually observed: if another thread claimed the victim in
	// the meantime, its claim stands and this one gives up rather than stamping over it.
	uint previous;
	atomicFetchCompareExchange(b_gi_cache_keys[oldest_slot], oldest_key, key, previous);
	if(previous != oldest_key)
	{
		return GI_CACHE_INVALID_SLOT;
	}
	// Reset the payload: this slot now represents a different cell entirely, and inheriting the
	// previous occupant's radiance would bleed one part of the world into another.
	//
	// EVERY field, not just the radiance. The surface properties are read back as albedo and
	// emissive, so a stale emissive left behind by the previous occupant is injected as light
	// that nothing in the scene emits -- arbitrary coloured blobs wherever a slot was recycled.
	// The buffer is never cleared wholesale either, so an uninitialised field is not zero, it is
	// whatever the allocation happened to contain.
	b_gi_cache_data[GiCacheDataIndex(oldest_slot, GI_CACHE_DATA_RADIANCE)] = vec4_splat(0.0);
	b_gi_cache_data[GiCacheDataIndex(oldest_slot, GI_CACHE_DATA_ALBEDO)] = vec4_splat(0.0);
	b_gi_cache_data[GiCacheDataIndex(oldest_slot, GI_CACHE_DATA_EMISSIVE)] = vec4_splat(0.0);
	return oldest_slot;
}

#endif // GI_CACHE_READ_WRITE

#endif // __GI_RADIANCE_CACHE_ACCESS_SH__
#endif // GI_CACHE_READ_ONLY || GI_CACHE_READ_WRITE
