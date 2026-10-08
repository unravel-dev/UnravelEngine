#ifndef __LUMEN_SURFACE_CACHE_SH__
#define __LUMEN_SURFACE_CACHE_SH__

/*
 * Lumen's surface cache, sampling side: UE 5.8 ComputeSurfaceCacheSample, SampleLumenCard and
 * SampleLumenMeshCards (SurfaceCache/LumenSurfaceCacheSampling.ush:99-437), over the scene table
 * lumen_scene packs (engine/engine/rendering/gi/lumen_scene.h) into one buffer, b_lumen_scene:
 *  - cards from 0, 6 vec4 each: origin + page-table offset, axis_x + extent x, axis_y + extent y,
 *    axis_z + extent z, (size in pages x, y, res level x, y) - res level 0 = not resident, and the reflections'
 *    page table (offset, size in pages x, y, 1 when it is the card's hi-res mip's): an includer that defines
 *    LUMEN_SURFACE_CACHE_HI_RES samples through it (UE bHiResSurface), whose unmapped pages point at the locked pages
 *    covering them;
 *  - the page table from u_lumen_surface_cache.y, 1 vec4 per virtual page: (atlas bias x, y in texels,
 *    res level x, y) - 0 = unmapped;
 *  - per GI instance from u_lumen_surface_cache.z: (first card, card count, two-sided, how far it moved since last
 *    frame at most, in world units).
 * Cards are world-space boxes (the placement applied on the CPU), so the mesh-space axis masks of UE
 * become a facing test against each card's world axis_z with the same squared-cosine weight.
 *
 * The final lighting atlas carries the card depth in alpha (0 at the card front, 1 = uncovered), so a
 * sampler needs two bindings. The includer declares b_lumen_scene (BUFFER_RO vec4) and
 * s_lumen_card_final (sampler2D) and binds u_lumen_surface_cache; with LUMEN_SURFACE_CACHE_TABLES_ONLY
 * defined it gets the card tables and the atlas footprints alone and needs no sampler.
 */

#include "lumen/lumen_constants.sh"

/// x = physical atlas size in texels, y = page table base, z = instance table base, w = instance count.
uniform vec4 u_lumen_surface_cache;

#define LUMEN_PHYSICAL_PAGE_SIZE 128.0
#define LUMEN_SUB_ALLOCATION_RES_LEVEL 7.0
#define LUMEN_CARD_STRIDE 6
/// SampleLumenMeshCards: two-sided placements sample with 50 cm more bias.
#define LUMEN_TWO_SIDED_SURFACE_CACHE_BIAS 0.5

struct LumenCard
{
	vec3 origin;
	vec3 axis_x;
	vec3 axis_y;
	vec3 axis_z;
	vec3 extent;
	float page_table_offset;
	vec2 size_in_pages;
	vec2 res_level;
	/// The reflections' page table: its offset and size in pages.
	float reflection_page_table_offset;
	vec2 reflection_size_in_pages;
};

LumenCard LumenLoadCard(int index)
{
	int base = index * LUMEN_CARD_STRIDE;
	vec4 r0 = b_lumen_scene[base + 0];
	vec4 r1 = b_lumen_scene[base + 1];
	vec4 r2 = b_lumen_scene[base + 2];
	vec4 r3 = b_lumen_scene[base + 3];
	vec4 r4 = b_lumen_scene[base + 4];
	vec4 r5 = b_lumen_scene[base + 5];
	LumenCard card;
	card.origin = r0.xyz;
	card.page_table_offset = r0.w;
	card.axis_x = r1.xyz;
	card.axis_y = r2.xyz;
	card.axis_z = r3.xyz;
	card.extent = vec3(r1.w, r2.w, r3.w);
	card.size_in_pages = r4.xy;
	card.res_level = r4.zw;
	card.reflection_page_table_offset = r5.x;
	card.reflection_size_in_pages = r5.yz;
	return card;
}

/// The cards of a GI instance: x = first card, y = card count, z > 0.5 when two-sided, w = its motion since last frame.
vec4 LumenLoadInstanceCards(int instance)
{
	if(instance < 0 || float(instance) >= u_lumen_surface_cache.w)
	{
		return vec4_splat(0.0);
	}
	return b_lumen_scene[int(u_lumen_surface_cache.z) + instance];
}

/// Card-local position of a card UV and a card depth (0 at the front plane, 1 at the back).
vec3 LumenCardLocalPosition(LumenCard card, vec2 card_uv, float depth)
{
	return vec3((card_uv.x - 0.5) * 2.0 * card.extent.x,
	            (0.5 - card_uv.y) * 2.0 * card.extent.y,
	            (0.5 - depth) * 2.0 * card.extent.z);
}

vec3 LumenCardWorldPosition(LumenCard card, vec3 local)
{
	return card.origin + card.axis_x * local.x + card.axis_y * local.y + card.axis_z * local.z;
}

/// The world position of atlas texel @p texel of a page at @p page (atlas origin xy, size xy) covering the card UV
/// rectangle @p uv_rect, at the card depth @p depth.
vec3 LumenTexelPosition(LumenCard card, vec4 uv_rect, vec4 page, ivec2 texel, float depth)
{
	vec2 card_uv = mix(uv_rect.xy, uv_rect.zw, (vec2(texel) - page.xy + 0.5) / page.zw);
	return LumenCardWorldPosition(card, LumenCardLocalPosition(card, card_uv, depth));
}

/// One bilinear footprint in the physical atlas: the top-left texel and the four weights
/// (x = top-left, y = top-right, z = bottom-left, w = bottom-right), the sample's atlas position in texels and
/// its page (index into b_lumen_scene).
struct LumenCardSample
{
	ivec2 texel;
	vec4 weights;
	vec2 atlas_coord;
	int page_index;
	bool valid;
	/// The sample's card UV in [0, 1).
	vec2 card_uv;
};

/// ComputeSurfaceCacheSample: card-local xy -> page -> physical atlas footprint.
LumenCardSample LumenComputeCardSample(LumenCard card, vec2 local_xy)
{
	LumenCardSample result;
	result.texel = ivec2(0, 0);
	result.weights = vec4_splat(0.0);
	result.atlas_coord = vec2_splat(0.0);
	result.page_index = 0;
	result.valid = false;
	vec2 card_uv = min(saturate(vec2(0.5, -0.5) * (local_xy / card.extent.xy) + 0.5), vec2_splat(0.999999));
	result.card_uv = card_uv;
	vec2 page_coord = floor(card_uv * card.size_in_pages);
	int page_index = int(u_lumen_surface_cache.y + card.page_table_offset + page_coord.x + page_coord.y * card.size_in_pages.x);
	vec4 page = b_lumen_scene[page_index];
	vec2 res_level = page.zw;
	if(res_level.x <= 0.0)
	{
		return result;
	}
	vec2 atlas_bias = page.xy;
	vec2 size_in_pages = vec2(res_level.x > LUMEN_SUB_ALLOCATION_RES_LEVEL ? exp2(res_level.x - LUMEN_SUB_ALLOCATION_RES_LEVEL) : 1.0,
	                          res_level.y > LUMEN_SUB_ALLOCATION_RES_LEVEL ? exp2(res_level.y - LUMEN_SUB_ALLOCATION_RES_LEVEL) : 1.0);
	page_coord = floor(card_uv * size_in_pages);
	vec2 atlas_scale = vec2(res_level.x > LUMEN_SUB_ALLOCATION_RES_LEVEL ? LUMEN_PHYSICAL_PAGE_SIZE : exp2(res_level.x),
	                        res_level.y > LUMEN_SUB_ALLOCATION_RES_LEVEL ? LUMEN_PHYSICAL_PAGE_SIZE : exp2(res_level.y));
	vec2 page_uv = fract(card_uv * size_in_pages);
	// Interior page edges hold half a texel of the neighbouring page; card edges hold none.
	vec2 min_border = vec2(page_coord.x == 0.0 ? 0.0 : 0.5, page_coord.y == 0.0 ? 0.0 : 0.5);
	vec2 max_border = vec2(page_coord.x + 1.0 == size_in_pages.x ? 0.0 : 0.5, page_coord.y + 1.0 == size_in_pages.y ? 0.0 : 0.5);
	vec2 coord_in_page = page_uv * (atlas_scale - min_border - max_border) + min_border;
	float sub_texel_bias = 1.0 / 512.0;
	coord_in_page = clamp(coord_in_page, vec2_splat(0.5), atlas_scale - vec2_splat(0.5 + sub_texel_bias));
	vec2 atlas_coord = coord_in_page + atlas_bias;
	vec2 f = fract(atlas_coord + 0.5 + sub_texel_bias);
	result.texel = ivec2(floor(atlas_coord - 0.5 + sub_texel_bias));
	result.weights = vec4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
	result.atlas_coord = atlas_coord;
	result.page_index = page_index;
	result.valid = true;
	return result;
}

vec3 LumenFetchCardAtlas(sampler2D atlas, LumenCardSample s, vec4 weights)
{
	return texelFetch(atlas, s.texel, 0).xyz * weights.x +
	       texelFetch(atlas, s.texel + ivec2(1, 0), 0).xyz * weights.y +
	       texelFetch(atlas, s.texel + ivec2(0, 1), 0).xyz * weights.z +
	       texelFetch(atlas, s.texel + ivec2(1, 1), 0).xyz * weights.w;
}

float LumenCardTexelVisibility(float texel_depth, float hit_depth, float threshold, float falloff)
{
	return texel_depth < 1.0 ? 1.0 - saturate((abs(hit_depth - texel_depth) - threshold) / falloff) : 0.0;
}

#ifndef LUMEN_SURFACE_CACHE_TABLES_ONLY

/// One card at a hit before its value is read (UE SampleLumenCard up to the fetch): the atlas footprint, its weights
/// times the depth test, their sum, the hit's weight on the card (squared facing x the sum; 0 when the card does not
/// see the hit) and the card's largest half extent across its face.
struct LumenCardHit
{
	LumenCardSample s;
	vec4 weights;
	float weight_sum;
	float sample_weight;
	float face_extent;
};

LumenCardHit LumenEvaluateCardHit(int card_index, vec3 position, vec3 normal, float bias)
{
	LumenCardHit hit;
	hit.weights = vec4_splat(0.0);
	hit.weight_sum = 0.0;
	hit.sample_weight = 0.0;
	hit.face_extent = 0.0;
	hit.s.valid = false;
	// The card's projection angle first: only cards facing the normal's side, by its squared cosine. Its axis z and
	// residency rows decide that, so a card facing away or not resident costs two loads, not the whole record.
	int base = card_index * LUMEN_CARD_STRIDE;
	float facing = dot(normal, b_lumen_scene[base + 3].xyz);
	BRANCH
	if(facing <= 0.0 || b_lumen_scene[base + 4].z <= 0.0)
	{
		return hit;
	}
	LumenCard card = LumenLoadCard(card_index);
#ifdef LUMEN_SURFACE_CACHE_HI_RES
	card.page_table_offset = card.reflection_page_table_offset;
	card.size_in_pages = card.reflection_size_in_pages;
#endif
	vec3 d = position - card.origin;
	vec3 local = vec3(dot(d, card.axis_x), dot(d, card.axis_y), dot(d, card.axis_z));
	if(any(greaterThan(abs(local), card.extent + vec3_splat(0.5 * bias))))
	{
		return hit;
	}
	local.xy = clamp(local.xy, -card.extent.xy, card.extent.xy);
	hit.s = LumenComputeCardSample(card, local.xy);
	if(!hit.s.valid)
	{
		return hit;
	}
	// Depth test against the captured surface, full within the bias and gone 25% beyond it.
	float hit_depth = -(local.z / card.extent.z) * 0.5 + 0.5;
	float threshold = bias / card.extent.z;
	float falloff = 0.25 * threshold;
	vec4 visibility = vec4(LumenCardTexelVisibility(texelFetch(s_lumen_card_final, hit.s.texel, 0).w, hit_depth, threshold, falloff),
	                       LumenCardTexelVisibility(texelFetch(s_lumen_card_final, hit.s.texel + ivec2(1, 0), 0).w, hit_depth, threshold, falloff),
	                       LumenCardTexelVisibility(texelFetch(s_lumen_card_final, hit.s.texel + ivec2(0, 1), 0).w, hit_depth, threshold, falloff),
	                       LumenCardTexelVisibility(texelFetch(s_lumen_card_final, hit.s.texel + ivec2(1, 1), 0).w, hit_depth, threshold, falloff));
	hit.weights = hit.s.weights * visibility;
	hit.weight_sum = dot(hit.weights, vec4_splat(1.0));
	hit.sample_weight = facing * facing * hit.weight_sum;
	hit.face_extent = max(card.extent.x, card.extent.y);
	return hit;
}

/// One card's contribution to a hit: rgb = weighted value sum, a = weight sum. LumenEvaluateCardHit's steps, kept
/// inline: the hit shaders' hot loop, which a struct return would cost registers.
vec4 LumenSampleCard(int card_index, vec3 position, vec3 normal, float bias, sampler2D values)
{
	// The card's projection angle first: only cards facing the normal's side, by its squared cosine. Its axis z and
	// residency rows decide that, so a card facing away or not resident costs two loads, not the whole record.
	int base = card_index * LUMEN_CARD_STRIDE;
	float facing = dot(normal, b_lumen_scene[base + 3].xyz);
	BRANCH
	if(facing <= 0.0 || b_lumen_scene[base + 4].z <= 0.0)
	{
		return vec4_splat(0.0);
	}
	LumenCard card = LumenLoadCard(card_index);
#ifdef LUMEN_SURFACE_CACHE_HI_RES
	card.page_table_offset = card.reflection_page_table_offset;
	card.size_in_pages = card.reflection_size_in_pages;
#endif
	vec3 d = position - card.origin;
	vec3 local = vec3(dot(d, card.axis_x), dot(d, card.axis_y), dot(d, card.axis_z));
	if(any(greaterThan(abs(local), card.extent + vec3_splat(0.5 * bias))))
	{
		return vec4_splat(0.0);
	}
	local.xy = clamp(local.xy, -card.extent.xy, card.extent.xy);
	LumenCardSample s = LumenComputeCardSample(card, local.xy);
	if(!s.valid)
	{
		return vec4_splat(0.0);
	}
	// Depth test against the captured surface, full within the bias and gone 25% beyond it.
	float hit_depth = -(local.z / card.extent.z) * 0.5 + 0.5;
	float threshold = bias / card.extent.z;
	float falloff = 0.25 * threshold;
	vec4 visibility = vec4(LumenCardTexelVisibility(texelFetch(s_lumen_card_final, s.texel, 0).w, hit_depth, threshold, falloff),
	                       LumenCardTexelVisibility(texelFetch(s_lumen_card_final, s.texel + ivec2(1, 0), 0).w, hit_depth, threshold, falloff),
	                       LumenCardTexelVisibility(texelFetch(s_lumen_card_final, s.texel + ivec2(0, 1), 0).w, hit_depth, threshold, falloff),
	                       LumenCardTexelVisibility(texelFetch(s_lumen_card_final, s.texel + ivec2(1, 1), 0).w, hit_depth, threshold, falloff));
	vec4 weights = s.weights * visibility;
	float weight_sum = dot(weights, vec4_splat(1.0));
	float sample_weight = facing * facing * weight_sum;
	if(sample_weight <= 0.0)
	{
		return vec4_splat(0.0);
	}
	vec3 value = LumenFetchCardAtlas(values, s, weights / weight_sum);
	return vec4(value * sample_weight, sample_weight);
}

/// SampleLumenMeshCards over a GI instance's cards: rgb = weighted sum, a = weight sum (0 = no card
/// covers the hit, which Lumen shades black).
vec4 LumenSampleInstanceCards(int instance, vec3 position, vec3 normal, float bias, sampler2D values)
{
	vec4 accumulated = vec4_splat(0.0);
	vec4 cards = LumenLoadInstanceCards(instance);
	int first = int(cards.x);
	int count = int(cards.y);
	float sample_bias = bias + (cards.z > 0.5 ? LUMEN_TWO_SIDED_SURFACE_CACHE_BIAS : 0.0);
	LOOP
	for(int i = 0; i < count; ++i)
	{
		accumulated += LumenSampleCard(first + i, position, normal, sample_bias, values);
	}
	return accumulated;
}

#ifdef LUMEN_SURFACE_CACHE_OBJECT_GRID

/*
 * Global-SDF hits through the object grid (UE 5.8 EvaluateGlobalDistanceFieldHit, LumenSoftwareRayTracing.ush:
 * 637-763): the grid cell one voxel extent off the surface lists up to four nearby instances, nearest first;
 * their cards are sampled with a 3 voxel extent depth tolerance until the accumulated weight reaches 0.9. The
 * includer declares s_lumen_object_grid (SAMPLER3D, rgba16 unorm ids + 1 over LUMEN_OBJECT_GRID_MAX_ID) and binds the
 * two uniforms below.
 */

/// Per clipmap level: xyz = the grid's origin, w = its cell size (0 = no grid for the level).
uniform vec4 u_lumen_object_grid_levels[4];
/// x = cells per axis, y = scale of LUMEN_GLOBAL_SDF_SURFACE_CACHE_BIAS (1 = UE's rule on this layout).
uniform vec4 u_lumen_object_grid_params;

#define LUMEN_OBJECT_GRID_LEVELS 4
/// SampleLumenMeshCards stops once the accumulated weight reaches this.
#define LUMEN_OBJECT_GRID_WEIGHT_DONE 0.9
/// The card sampling bias of global-SDF hits, in voxel extents of the hit level (UE
/// DISTANCE_FIELD_OBJECT_GRID_CARD_INTERPOLATION_RANGE_IN_VOXELS).
#define LUMEN_GLOBAL_SDF_SURFACE_CACHE_BIAS 3.0

/// The instance ids (+1, 0 = none) of the finest grid cell containing @p p.
vec4 LumenObjectGridInstances(vec3 p)
{
	float resolution = u_lumen_object_grid_params.x;
	vec4 ids = vec4_splat(0.0);
	bool found = false;
	for(int level = 0; level < LUMEN_OBJECT_GRID_LEVELS; ++level)
	{
		vec4 info = u_lumen_object_grid_levels[level];
		vec3 cell = floor((p - info.xyz) / max(info.w, 1e-6));
		if(!found && info.w > 0.0 && all(greaterThanEqual(cell, vec3_splat(0.0))) &&
		   all(lessThan(cell, vec3_splat(resolution))))
		{
			ids = floor(texelFetch(s_lumen_object_grid, ivec3(cell.x, cell.y, cell.z + float(level) * resolution), 0) *
			            float(LUMEN_OBJECT_GRID_MAX_ID) +
			            0.5);
			found = true;
		}
	}
	return ids;
}

vec4 LumenAccumulateObject(vec4 accumulated, float id, vec3 position, vec3 normal, float bias, sampler2D values)
{
	BRANCH
	if(id > 0.5 && accumulated.w < LUMEN_OBJECT_GRID_WEIGHT_DONE)
	{
		accumulated += LumenSampleInstanceCards(int(id) - 1, position, normal, bias, values);
	}
	return accumulated;
}

/// Surface cache value at a global-SDF hit: rgb = weighted sum, a = weight sum (0 = no card covers it).
vec4 LumenSampleGlobalSdfHit(vec3 position, vec3 normal, float voxel_extent, sampler2D values)
{
	vec4 ids = LumenObjectGridInstances(position + normal * voxel_extent);
	float bias = LUMEN_GLOBAL_SDF_SURFACE_CACHE_BIAS * u_lumen_object_grid_params.y * voxel_extent;
	vec4 accumulated = vec4_splat(0.0);
	accumulated = LumenAccumulateObject(accumulated, ids.x, position, normal, bias, values);
	accumulated = LumenAccumulateObject(accumulated, ids.y, position, normal, bias, values);
	accumulated = LumenAccumulateObject(accumulated, ids.z, position, normal, bias, values);
	accumulated = LumenAccumulateObject(accumulated, ids.w, position, normal, bias, values);
	return accumulated;
}

/// How far the instance hit at @p outside (a point just off a global-SDF hit, on the ray's side) moved since last frame
/// at most: the nearest instance of the object grid cell there (UE's hit velocity of mesh distance-field hits).
float LumenGlobalSdfHitMotion(vec3 outside)
{
	float id = LumenObjectGridInstances(outside).x;
	return id > 0.5 ? LumenLoadInstanceCards(int(id) - 1).w : 0.0;
}

#endif // LUMEN_SURFACE_CACHE_OBJECT_GRID

#endif // LUMEN_SURFACE_CACHE_TABLES_ONLY

#endif // __LUMEN_SURFACE_CACHE_SH__
