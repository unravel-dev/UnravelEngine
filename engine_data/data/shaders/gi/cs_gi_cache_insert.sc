/*
 * Registers the surfaces currently on screen into the world-space radiance cache.
 *
 * Only registration happens here -- the key is claimed and the cell's world position and normal
 * are recorded. Lighting is deliberately left to cs_gi_cache_update, which runs one thread per
 * ENTRY rather than per pixel: many pixels resolve to the same cell, so lighting here would
 * repeat the same expensive shadow rays dozens of times and race while accumulating them.
 *
 * Driving insertion from the screen is a starting point, not the end state. It populates what
 * the camera can currently see; cells that are only reachable by a bounce, or that were visible
 * a moment ago, are populated once the update pass casts rays of its own. The cache CONTENTS
 * are world-space and outlive visibility either way -- an entry does not disappear when its
 * surface leaves the screen, which is the whole point of anchoring it to the world.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
// DecodeGBufferNormalMetalRoughness lives here, not in common.sh.
#include "../lighting.sh"

#define GI_CACHE_READ_WRITE
#include "gi/radiance_cache.sh"
// SdfResolveSurfacePoint: the cache is addressed in FIELD space, not raster space.
#include "gi/sdf_common.sh"

SAMPLER2D(s_gi_depth, 8);
SAMPLER2D(s_gi_normal, 9);
SAMPLER2D(s_gi_base_color, 10);
SAMPLER2D(s_gi_emissive, 11);

/// xy = full G-buffer size in pixels, z = sampling stride, w = camera distance limit.
uniform vec4 u_gi_insert_params;
#define u_gi_gbuffer_size   u_gi_insert_params.xy
#define u_gi_insert_stride  u_gi_insert_params.z
#define u_gi_insert_max_distance u_gi_insert_params.w

uniform vec4 u_gi_camera_position;

NUM_THREADS(8, 8, 1)
void main()
{
	vec2 pixel = (vec2(gl_GlobalInvocationID.xy) + vec2_splat(0.5)) * u_gi_insert_stride;
	if(any(greaterThanEqual(pixel, u_gi_gbuffer_size)))
	{
		return;
	}
	vec2 uv = pixel / u_gi_gbuffer_size;
	float depth = texture2DLod(s_gi_depth, uv, 0.0).x;
	// Sky: nothing to cache. The sampled value is the cleared far plane, which would otherwise
	// reconstruct to a position on the far clip and register a cell there.
	if(depth >= 1.0)
	{
		return;
	}
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(depth)));
	vec3 world_position = clipToWorld(u_invViewProj, clip);
	if(length(world_position - u_gi_camera_position.xyz) > u_gi_insert_max_distance)
	{
		return;
	}
	// The Lod variant is required: the plain one samples with implicit derivatives, which a
	// compute shader has no way to compute and D3D rejects outright.
	GBufferDataNormalMetalRoughness nd = DecodeGBufferNormalMetalRoughnessLod(uv, s_gi_normal, 0.0);
	vec3 world_normal = nd.world_normal;
	if(dot(world_normal, world_normal) < 0.5)
	{
		return;
	}
	world_normal = normalize(world_normal);
	// The G-buffer gives an accurate STARTING point, but not the address. Every reader reaches
	// this cache along an SDF ray and so lands on the field's isosurface, which is displaced from
	// the rasterised triangles by a fraction of a voxel. Registering the raster position would
	// key entries to cells that no ray query can ever address. Resolve onto the field first, and
	// take the facing from the field too, so both sides derive the key from the same function.
	SdfSurfacePoint surface = SdfResolveSurfacePoint(world_position);
	// A field with no coverage here reports a saturated distance and resolves to nothing usable;
	// registering that would place an entry at an arbitrary point.
	if(dot(surface.normal, world_normal) < 0.0)
	{
		return;
	}
	uint level = GiCacheLevel(surface.position, u_gi_camera_position.xyz);
	uint key = GiCacheKey(surface.position, surface.normal, level);
	uint slot = GiCacheInsert(key, u_gi_cache_frame);
	if(slot == GI_CACHE_INVALID_SLOT)
	{
		return;
	}
	// Record where this cell is, so the update pass can light it without the G-buffer. The
	// position is snapped to the cell centre rather than kept as the exact sampled point: every
	// pixel resolving to this cell would otherwise fight over it, and the resulting position
	// would jitter with the camera -- reintroducing the view dependence the world-space key was
	// chosen to avoid.
	float cell_size = GiCacheCellSize(level);
	vec3 face_direction = GiFaceDirection(GiQuantizeNormal(surface.normal));
	vec3 snapped = surface.position + face_direction * (cell_size * 0.5);
	vec3 cell_center = (floor(snapped / cell_size) + vec3_splat(0.5)) * cell_size;
	// Undo the half-cell lift the key applies. That lift exists only to keep the surface off a
	// cell boundary; leaving it in would record a point floating half a cell above the surface,
	// and the update pass would light empty air there.
	vec3 surface_point = cell_center - face_direction * (cell_size * 0.5);
	b_gi_cache_data[GiCacheDataIndex(slot, GI_CACHE_DATA_POSITION)] =
	    vec4(surface_point, float(u_gi_cache_frame));
	b_gi_cache_data[GiCacheDataIndex(slot, GI_CACHE_DATA_NORMAL)] = vec4(surface.normal, float(level));
	// Surface properties, captured here because this is the ONE place the cache touches a surface
	// whose material is known. The fields carry geometry only, so a cell discovered by a bounce
	// ray has to fall back to a neutral guess; a cell discovered on screen can have the real
	// thing. Storing them is what lets bounced light be tinted and emitters contribute.
	GBufferDataColorAndAO color_data = DecodeGBufferColorAndAOLod(uv, s_gi_base_color, 0.0);
	GBufferDataEmissive emissive_data = DecodeGBufferEmissiveLod(uv, s_gi_emissive, 0.0);
	// Metals have no diffuse albedo, so their diffuse bounce is scaled away by the same factor
	// the shading pass uses.
	vec3 albedo = color_data.base_color * (1.0 - nd.metalness);
	b_gi_cache_data[GiCacheDataIndex(slot, GI_CACHE_DATA_ALBEDO)] = vec4(albedo, 0.0);
	b_gi_cache_data[GiCacheDataIndex(slot, GI_CACHE_DATA_EMISSIVE)] =
	    vec4(emissive_data.emissive_color, 0.0);
}
