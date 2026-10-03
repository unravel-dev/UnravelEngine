#ifndef __LUMEN_SURFACE_CACHE_LIGHTING_SH__
#define __LUMEN_SURFACE_CACHE_LIGHTING_SH__

/*
 * Lumen surface cache lighting, shared by the capture copy (cs_lumen_card_copy.sc), the lighting resample
 * (cs_lumen_card_resample.sc) and the radiosity integrate (cs_lumen_radiosity_integrate.sc).
 *
 * A copy tile record is LUMEN_CARD_COPY_TILE_STRIDE vec4 (lumen_surface_cache_pass::build_copy_tiles): (the tile's
 * top-left texel in the capture atlas xy, in the physical atlas zw), the page's card UV rectangle, (the tile's texel
 * offset in its page xy, the page size in texels zw), (the card's previous allocation in the resample table -
 * lumen_scene::get_resample_table - or -1 when there is none, 0, 0, 0).
 */

#define LUMEN_CARD_COPY_TILE_STRIDE 4
/// Card tiles are 8 x 8 texels; the radiosity update count is stored per tile.
#define LUMEN_CARD_TILE_SIZE 8
#define LUMEN_INV_PI 0.31830989

/// UE CombineFinalLighting (SurfaceCache/LumenSurfaceCache.ush:44-61): what a ray sees at a card texel, from its
/// encoded albedo (the square root of the diffuse colour), emissive, direct and indirect lighting.
vec3 LumenCombineFinalLighting(vec3 albedo_encoded, vec3 emissive, vec3 direct, vec3 indirect)
{
	vec3 albedo = albedo_encoded * albedo_encoded;
	return max(albedo * LUMEN_INV_PI * (direct + indirect) + emissive, vec3_splat(0.0));
}

#endif // __LUMEN_SURFACE_CACHE_LIGHTING_SH__
