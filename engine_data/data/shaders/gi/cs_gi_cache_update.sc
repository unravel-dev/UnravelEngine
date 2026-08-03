/*
 * Lights every resident cache entry and blends the result into its running mean.
 *
 * One thread per ENTRY, not per pixel. That is what makes the payload race-free: exactly one
 * thread owns each entry, so the accumulation is a plain read-modify-write with no atomics and
 * no fixed-point encoding. It also means a cell is lit once per frame however many pixels
 * happen to look at it -- and, more importantly, whether any pixel looks at it at all. An entry
 * whose surface has left the screen keeps being lit and stays valid, which is what lets it be
 * read back instantly when the surface returns instead of converging from nothing.
 *
 * Each entry also casts a bounce ray of its own, which does two things the screen cannot.
 *
 * It makes the result MULTI-BOUNCE for the price of one ray: the cell the ray lands on already
 * holds its own accumulated radiance from previous frames, so reading it collects light that has
 * bounced once, twice, and so on. The bounce count grows with time rather than with cost, which
 * is the whole reason for caching radiance in the world rather than recomputing it per frame.
 *
 * And when the ray lands somewhere with no entry yet, it CREATES one. Entries therefore spread
 * outward from whatever the camera has seen, along the paths light actually travels, into
 * geometry that has never been on screen. That is what makes offscreen surfaces contribute at
 * all -- a screen-driven cache can only ever hold what the camera has already looked at.
 */

#include "bgfx_compute.sh"
#include "../common.sh"

#define GI_CACHE_READ_WRITE
#include "gi/radiance_cache.sh"
#include "gi/sdf_common.sh"
#include "gi/gpu_lights.sh"
#include "gi/gi_lighting.sh"

/// x = entry capacity, y = position offset along the normal before lighting,
/// z = bounce rays per entry per frame, w = bounce albedo.
uniform vec4 u_gi_update_params;
#define u_gi_update_capacity   uint(u_gi_update_params.x)
#define u_gi_update_surface_offset u_gi_update_params.y
#define u_gi_update_bounce_rays int(u_gi_update_params.z)
/// Albedo used for cells no on-screen pixel ever registered, whose material is unknown.
#define u_gi_update_default_albedo u_gi_update_params.w

/// x = ceiling on any cell's albedo. yzw reserved.
///
/// Separate from the default above because the two answer different questions: the default supplies
/// a value where there is none, while this BOUNDS a value that already exists. Only the second can
/// constrain a material an artist authored, and the material path is the one that matters -- an
/// entry takes the default only until an on-screen pixel registers the real albedo, which is
/// immediately for anything visible.
uniform vec4 u_gi_update_material;
#define u_gi_update_max_albedo u_gi_update_material.x

/// x = bounce ray length, y = near field distance, z = max steps, w = surface bias in voxels.
uniform vec4 u_gi_update_bounce;
#define u_gi_bounce_distance   u_gi_update_bounce.x
#define u_gi_bounce_near_field u_gi_update_bounce.y
#define u_gi_bounce_max_steps  int(u_gi_update_bounce.z)
#define u_gi_bounce_bias       u_gi_update_bounce.w

/// Camera position, needed to choose a cache level for a bounce hit.
uniform vec4 u_gi_update_camera;

/// Defined locally rather than taken from lighting.sh, which this shader does not include. The
/// D3D backend happens to supply one anyway, so relying on it compiles there and fails on GLSL.
#define GI_PI 3.1415926535897932

/// Orthonormal basis around a normal, branch-free at the degenerate axis.
void GiBounceBasis(vec3 n, out vec3 t, out vec3 b)
{
	float s = n.z >= 0.0 ? 1.0 : -1.0;
	float a = -1.0 / (s + n.z);
	float c = n.x * n.y * a;
	t = vec3(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
	b = vec3(c, s + n.y * n.y * a, -n.y);
}

/// Cosine-weighted direction, so the plain mean of the samples estimates irradiance/PI directly.
vec3 GiBounceDirection(vec3 n, float u1, float u2)
{
	vec3 t, b;
	GiBounceBasis(n, t, b);
	float r = sqrt(u1);
	float phi = 6.2831853 * u2;
	return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(0.0, 1.0 - u1)));
}

NUM_THREADS(64, 1, 1)
void main()
{
	uint slot = gl_GlobalInvocationID.x;
	if(slot >= u_gi_update_capacity)
	{
		return;
	}
	if(b_gi_cache_keys[slot] == GI_CACHE_EMPTY_KEY)
	{
		return;
	}
	vec4 position_data = b_gi_cache_data[GiCacheDataIndex(slot, GI_CACHE_DATA_POSITION)];
	vec4 normal_data = b_gi_cache_data[GiCacheDataIndex(slot, GI_CACHE_DATA_NORMAL)];
	vec3 normal = normal_data.xyz;
	if(dot(normal, normal) < 0.5)
	{
		return;
	}
	normal = normalize(normal);

	// --- Retire entries whose surface is no longer there ---
	//
	// Entries are world-anchored and deliberately outlive visibility, which is the whole point of
	// the cache. The flip side is that nothing reclaims one when its surface MOVES or is deleted:
	// it keeps its stored emissive and albedo and goes on radiating at a position where there is
	// no longer anything at all. Eviction does not save us -- that only triggers on probe-chain
	// contention, which a lightly loaded table never produces -- so a moved emitter leaves a
	// permanent imprint of itself behind.
	//
	// The field already knows. An entry sits ON a surface, so the distance there should be
	// approximately zero; if the geometry has moved away the field now reports open space.
	float validate_voxel_size;
	float surface_distance = SdfSampleClipmapEx(position_data.xyz, validate_voxel_size);
	// A saturated reading means no cascade covers this point, so nothing can be concluded and the
	// entry must be left alone. Treating "unknown" as "gone" would delete the entire far field.
	//
	// Compared against the sampler's OWN give-up value rather than a local threshold of the same
	// intent. A second constant here would be one edit away from disagreeing with the sampler, and
	// the direction that disagreement fails in is deleting the whole far field.
	uint entry_level = uint(max(normal_data.w, 0.0));
	float entry_cell_size = GiCacheCellSize(entry_level);
	if(surface_distance < SDF_CLIPMAP_OUTSIDE)
	{
		// Tolerant on purpose: the cascade is rebuilt a level per frame, so it lags a moving
		// object by a few frames, and a false positive here costs only a re-created entry.
		float tolerance = max(entry_cell_size, validate_voxel_size * 2.0);
		if(abs(surface_distance) > tolerance)
		{
			b_gi_cache_keys[slot] = GI_CACHE_EMPTY_KEY;
			return;
		}
	}

	// Lift off the recorded surface before lighting, in fractions of THIS ENTRY'S CELL.
	//
	// The stored point is not the sampled surface: insertion snaps it to the cell grid, so it can
	// sit up to a cell inside the geometry it represents. That error is therefore a function of the
	// CELL, which is 0.25 m at level 0 and 2 m at the level cap -- an eightfold range that a fixed
	// world distance cannot cover. At 0.05 world it cleared a fifth of a near cell and a fortieth of
	// a far one, so distant entries shaded from inside their own surface, every shadow ray started
	// occluded, and they converged to black.
	//
	// Getting this wrong is expensive in both directions and neither announces itself: too small
	// and the far field goes black, too large and the entry floats off its surface so shadow rays
	// sail over nearby occluders and everything reads over-lit. Scaling by the cell is what makes
	// one value work at every level instead of trading one end against the other.
	// Cell-relative so it scales with the entry's own quantisation error, then clamped so it
	// cannot reach the metre it would at the outer cascade, where it lifts the entry clear of
	// its own surface and every shadow ray sails over nearby occluders.
	// Scales with THIS entry's cell and is deliberately NOT clamped to the finest one, unlike
	// every other bias here.
	//
	// The others clear the FIELD's error -- how far the isosurface sits from the real surface --
	// which is a property of the level that answered and can be held to the finest level at the
	// cost of some acne. This clears the entry's own QUANTISATION error: insertion snaps the
	// position to the cell grid, so the entry can genuinely BE up to a cell inside its geometry,
	// and at the coarsest level that is metres. Clamping it does not move the entry back out --
	// it just leaves it buried, so every shadow ray starts occluded and the far field converges
	// to black. That was measured: clamping this produced pure black patches at distance while
	// clamping the others only produced acne.
	float lift_target = u_gi_update_surface_offset * entry_cell_size;
	// MEASURED, not guessed -- and the measurement is free: surface_distance was already sampled
	// above for the retirement test.
	//
	// A fixed fraction of the cell has to assume the worst case for every entry, and near the camera
	// the cell is SMALL, so that fraction is a small world distance. It cannot clear a surface with
	// relief of its own: on rusticated stone the entry snaps to a cell centre inside the groove,
	// every shadow ray starts occluded, and the entry converges to black. Neighbouring cells that
	// happen to snap outside stay lit, which is the checkerboard the cache view shows near a wall.
	//
	// Using the distance the cascade reports at the entry pushes out by exactly what is needed:
	// nothing where the entry already sits clear, and target + depth where it is buried. Guarded on
	// coverage, since outside every level the reading carries no information.
	float surface_offset = lift_target;
	if(surface_distance < SDF_CLIPMAP_OUTSIDE)
	{
		surface_offset = max(lift_target, lift_target - surface_distance);
	}
	vec3 position = position_data.xyz + normal * surface_offset;

	// Direct IRRADIANCE arriving at the cell.
	// The voxel size is already in hand from the retirement check above, so the shadow rays' own
	// normal offset costs nothing extra to make resolution-relative.
	vec3 irradiance = GiEvalDirectLighting(position, normal, max(validate_voxel_size, 0.01));

	// --- Bounce: gather from the cache itself, and populate wherever it is still empty ---
	//
	// One ray per entry per frame by default. It does not need to be more: the result is
	// accumulated into a running mean, so successive frames explore different directions and the
	// estimate converges over time rather than within a single frame. The cost stays fixed while
	// the effective sample count grows.
	int bounce_rays = u_gi_update_bounce_rays;
	if(bounce_rays > 0)
	{
		// Seeded by SLOT as well as frame, so neighbouring entries explore different directions
		// in the same frame instead of all sampling the same one and correlating their error.
		uint seed = GiHashCombine(GiHashUint(slot), u_gi_cache_frame);
		vec3 bounce = vec3_splat(0.0);
		// Rays that actually MEASURED something, which is not the same as rays cast.
		//
		// A ray that escapes measured "no light from that direction" and must count: with no sky
		// term that is a real zero. A ray discarded for landing back on its OWN cell measured
		// nothing at all -- it is a failure to sample, not a sample of darkness -- and counting it
		// scales the surviving rays down by the self-hit fraction. In a shadowed recess, where
		// direct is legitimately zero and most rays graze their own surface, that drives the cell
		// to black and holds it there: converged, stable, and wrong. Adding bounce rays cannot
		// help, because every extra ray is discarded and divided by too.
		float bounce_samples = 0.0;
		for(int i = 0; i < bounce_rays; ++i)
		{
			seed = GiHashUint(seed);
			float u1 = float(seed & 0xFFFFu) / 65535.0;
			seed = GiHashUint(seed);
			float u2 = float(seed & 0xFFFFu) / 65535.0;
			vec3 direction = GiBounceDirection(normal, u1, u2);
			SdfRayHit hit = SdfTraceRay(position, direction, u_gi_bounce_distance,
			                            u_gi_bounce_near_field, u_gi_bounce_max_steps,
			                            u_gi_bounce_bias, 0.0, false);
			if(!hit.hit)
			{
				// Escaped: a genuine measurement of zero, so it counts.
				bounce_samples += 1.0;
				continue;
			}
			// Resolved onto the field, exactly as the writer and every other reader does. A raw
			// hit addresses a neighbouring cell and would both miss the lookup and insert a
			// duplicate entry a cell away from the one that already exists.
			SdfSurfacePoint surface = SdfResolveSurfacePoint(position + direction * hit.t);
			// Outside every cascade level, so this hit has no address. Creating an entry for it
			// anyway is what filled the table with cells keyed to a fabricated facing that no
			// reader could ever find again.
			if(!surface.valid)
			{
				continue;
			}
			uint hit_level = GiCacheLevel(surface.position, u_gi_update_camera.xyz);
			uint hit_slot = GiCacheFindSurface(surface.position, surface.normal, hit_level);
			if(hit_slot == GI_CACHE_INVALID_SLOT)
			{
				// Nothing here yet. Create it so the next update lights it, and let this frame
				// contribute nothing rather than guess -- an unlit entry reads as black, and
				// feeding that back would darken the surface that discovered it.
				uint new_key = GiCacheKey(surface.position, surface.normal, hit_level);
				uint new_slot = GiCacheInsert(new_key, u_gi_cache_frame);
				if(new_slot != GI_CACHE_INVALID_SLOT)
				{
					float cell_size = GiCacheCellSize(hit_level);
					vec3 face_direction = GiFaceDirection(GiQuantizeNormal(surface.normal));
					vec3 snapped = surface.position + face_direction * (cell_size * 0.5);
					vec3 cell_center = (floor(snapped / cell_size) + vec3_splat(0.5)) * cell_size;
					// Undo the key's half-cell lift so the recorded point sits on the surface
					// plane rather than floating above it.
					vec3 surface_point = cell_center - face_direction * (cell_size * 0.5);
					b_gi_cache_data[GiCacheDataIndex(new_slot, GI_CACHE_DATA_POSITION)] =
					    vec4(surface_point, float(u_gi_cache_frame));
					b_gi_cache_data[GiCacheDataIndex(new_slot, GI_CACHE_DATA_NORMAL)] =
					    vec4(surface.normal, float(hit_level));
					// Material of the instance the ray actually hit. A distance field carries
					// geometry only, so this is the one place a bounce-discovered cell can learn
					// what colour it is -- without it the entry keeps a neutral grey until an
					// on-screen pixel registers one, which for the offscreen geometry this cache
					// exists to serve is exactly what never happens.
					//
					// A cascade hit reports no instance: it is composed from many fields at once
					// and cannot attribute a sample to one of them. Those keep the zero that
					// GiCacheInsert wrote, which still means "material unknown", and fall back to
					// the neutral albedo below.
					if(hit.instance_index != SDF_NO_INSTANCE)
					{
						SdfInstance hit_instance = SdfLoadInstance(hit.instance_index);
						b_gi_cache_data[GiCacheDataIndex(new_slot, GI_CACHE_DATA_ALBEDO)] =
						    vec4(hit_instance.albedo, 0.0);
						b_gi_cache_data[GiCacheDataIndex(new_slot, GI_CACHE_DATA_EMISSIVE)] =
						    vec4(hit_instance.emissive, 0.0);
					}
				}
				continue;
			}
			// A ray that resolves back to its OWN cell would feed this entry into itself. That
			// is not a bounce -- a surface does not illuminate itself -- and it turns the series
			// into L = albedo * L + rest, which converges far too bright and, with a near-white
			// albedo, keeps visibly climbing long after the real bounces have settled. Grazing
			// rays hit this case readily, because the trace starts only a small bias off the
			// surface and accepts a hit within a fraction of a voxel.
			if(hit_slot == slot)
			{
				continue;
			}
			// An entry first discovered through the CASCADE has no material, because the cascade
			// cannot say which field a sample came from. If a later ray reaches the same surface
			// through an instance, that is the first and possibly only chance to learn its colour,
			// so fill it in. Guarded on "still unknown" so this can never overwrite the exact
			// value an on-screen pixel registered with a per-submesh average.
			if(hit.instance_index != SDF_NO_INSTANCE)
			{
				vec4 hit_albedo = b_gi_cache_data[GiCacheDataIndex(hit_slot, GI_CACHE_DATA_ALBEDO)];
				if(dot(hit_albedo.xyz, hit_albedo.xyz) <= 0.0)
				{
					SdfInstance hit_instance = SdfLoadInstance(hit.instance_index);
					b_gi_cache_data[GiCacheDataIndex(hit_slot, GI_CACHE_DATA_ALBEDO)] =
					    vec4(hit_instance.albedo, hit_albedo.w);
					b_gi_cache_data[GiCacheDataIndex(hit_slot, GI_CACHE_DATA_EMISSIVE)] =
					    vec4(hit_instance.emissive, 0.0);
				}
			}
			bounce += b_gi_cache_data[GiCacheDataIndex(hit_slot, GI_CACHE_DATA_RADIANCE)].xyz;
			bounce_samples += 1.0;
		}
		// Cosine-weighted directions make the plain mean an estimate of radiance, so multiplying
		// by PI converts it to the irradiance the direct term is already in. Keeping both in one
		// unit is what lets a single albedo multiply apply to the whole sum below.
		irradiance += bounce * (GI_PI / max(bounce_samples, 1.0));
	}

	// Convert arriving irradiance into the radiance this surface EMITS back into the scene. This
	// is the quantity every reader wants: a gather ray asks "how bright is what I am looking at",
	// not "how much light falls on it". Storing irradiance instead is what left bounced light
	// uncoloured -- a red floor reflects red because its albedo tints what it sends onward, and
	// with no albedo in the cache there was nothing to tint it with.
	//
	// Emission is added rather than scaled, because an emitter radiates regardless of what falls
	// on it. It is also the only way an emissive surface enters this system at all: emission is a
	// material property, not an entry in the light buffer, so no amount of direct lighting finds
	// it.
	vec4 albedo_data = b_gi_cache_data[GiCacheDataIndex(slot, GI_CACHE_DATA_ALBEDO)];
	vec4 emissive_data = b_gi_cache_data[GiCacheDataIndex(slot, GI_CACHE_DATA_EMISSIVE)];
	vec3 albedo = albedo_data.xyz;
	// A cell discovered by a bounce ray has no material, because the fields carry geometry only.
	// A neutral mid grey keeps its energy plausible until an on-screen pixel registers the real
	// value, which happens the moment the camera looks at it.
	if(dot(albedo, albedo) <= 0.0)
	{
		albedo = vec3_splat(u_gi_update_default_albedo);
	}
	// Bound the loop gain. Bounce reads other entries' radiance and this line writes this entry's,
	// so the recursion is L = albedo * mean(L_in) and the gain PER CHANNEL is exactly the albedo.
	// At 1.0 a sealed room conserves light forever -- it neither converges nor diverges, it simply
	// holds whatever it had, which reads as a cache that never invalidates. Above 1.0 it climbs.
	//
	// sRGB 255 is linear 1.0, so a pure authored colour lands exactly on the unstable point rather
	// than near it: a 255,0,0 room stays red indefinitely with the green and blue channels gone in
	// a frame. Clamping here rather than at insertion keeps ONE place responsible for the gain, and
	// it is the place the recursion is actually closed.
	albedo = min(albedo, vec3_splat(u_gi_update_max_albedo));
	vec3 radiance = albedo * irradiance / GI_PI + emissive_data.xyz;

	vec4 stored = b_gi_cache_data[GiCacheDataIndex(slot, GI_CACHE_DATA_RADIANCE)];
	float sample_count = min(stored.w + 1.0, max(u_gi_cache_max_samples, 1.0));
	// 1/n while the entry is young so it converges immediately, floored so a mature one keeps
	// responding. Without the floor the mean freezes and a light that switches off stays
	// visible forever.
	float alpha = max(1.0 / sample_count, u_gi_cache_min_alpha);
	vec3 blended = stored.xyz + (radiance - stored.xyz) * alpha;
	b_gi_cache_data[GiCacheDataIndex(slot, GI_CACHE_DATA_RADIANCE)] = vec4(blended, sample_count);
}
