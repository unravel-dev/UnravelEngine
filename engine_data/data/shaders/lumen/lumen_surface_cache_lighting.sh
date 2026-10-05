#ifndef __LUMEN_SURFACE_CACHE_LIGHTING_SH__
#define __LUMEN_SURFACE_CACHE_LIGHTING_SH__

/*
 * Lumen surface cache lighting, shared by the capture copy (cs_lumen_card_copy.sc), the lighting resample
 * (cs_lumen_card_resample.sc) and the radiosity integrate (cs_lumen_radiosity_integrate.sc).
 *
 * A copy tile record is LUMEN_CARD_COPY_TILE_STRIDE vec4 (lumen_surface_cache_pass::build_copy_tiles): (the tile's
 * top-left texel in the capture atlas xy, in the physical atlas zw), the page's card UV rectangle, (the tile's texel
 * offset in its page xy, the page size in texels zw), (the card's previous allocation in the resample table -
 * lumen_scene::get_resample_table - or -1 when there is none, the card's world axis x), (axis y, 0), (axis z, 0).
 *
 * The normal atlas holds card-space normals (UE LumenCardBasePass.ush:136-146, SurfaceCache/LumenSurfaceCache.ush:
 * 26-40): a placement that rotates keeps its captured normals, which every reader turns into world space through the
 * card's current axes.
 */

#define LUMEN_CARD_COPY_TILE_STRIDE 6
/// Copy tiles per dispatch row (the copy and the resample run rows of tiles: a full capture atlas has more tiles than
/// one dispatch dimension allows). Mirror of lumen_surface_cache_pass.cpp copy_dispatch_width.
#define LUMEN_CARD_COPY_DISPATCH_WIDTH 256

/// The copy tile of group @p group of a copy / resample dispatch.
int LumenCopyTileIndex(ivec2 group)
{
	return group.y * LUMEN_CARD_COPY_DISPATCH_WIDTH + group.x;
}
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

/// A world normal in a card's axes, octahedral-encoded for the normal atlas.
vec2 LumenEncodeCardNormal(vec3 world_normal, vec3 axis_x, vec3 axis_y, vec3 axis_z)
{
	return encodeNormalOctahedron(vec3(dot(world_normal, axis_x), dot(world_normal, axis_y), dot(world_normal, axis_z)));
}

/// The world normal of a normal-atlas texel @p encoded of the card with axes @p axis_x, @p axis_y, @p axis_z.
vec3 LumenDecodeCardNormal(vec2 encoded, vec3 axis_x, vec3 axis_y, vec3 axis_z)
{
	vec3 card_normal = decodeNormalOctahedron(encoded);
	return normalize(axis_x * card_normal.x + axis_y * card_normal.y + axis_z * card_normal.z);
}

#endif // __LUMEN_SURFACE_CACHE_LIGHTING_SH__
