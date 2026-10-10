#ifndef __LUMEN_SURFACE_CACHE_LIGHTING_SH__
#define __LUMEN_SURFACE_CACHE_LIGHTING_SH__

/*
 * Surface cache lighting, shared by the capture copy (cs_lumen_card_copy.sc), the lighting resample
 * (cs_lumen_card_resample.sc) and the radiosity integrate (cs_lumen_radiosity_integrate.sc). The copy and the resample
 * read their tiles through lumen_tile_records.sh (LumenLoadCopyTile).
 *
 * The normal atlas holds card-space normals: a placement that rotates keeps its captured normals, which every reader
 * turns into world space through the card's current axes.
 *
 * The atlas formats: albedo RGBA8, normal RG8, depth R16, emissive, direct and indirect lighting R11G11B10 float,
 * stored through LumenQuantizeCardLighting. The final lighting stays RGBA16F with the card depth in alpha: a hit
 * reads its lighting and its depth test from one texel, and the hit tracers have no stage left for a depth atlas of
 * their own.
 */

#include "lumen/lumen_finite.sh"
#include "lumen/lumen_image_formats.sh"
#include "sampling.sh"

/// @p lighting ready for a store to an R11G11B10 atlas at @p texel: LumenQuantizeForRg11b10f with interleaved gradient
/// noise over the atlas, offset by @p frame_index, so neither a single store nor the radiosity's running blend drifts.
vec3 LumenQuantizeCardLighting(vec3 lighting, ivec2 texel, float frame_index)
{
	return LumenQuantizeForRg11b10f(lighting, InterleavedGradientNoise(vec2(texel), mod(frame_index, 8.0)));
}

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

/// What a ray sees at a card texel, from its encoded albedo (the square root of the diffuse colour), emissive, direct
/// and indirect lighting. Made finite: the final atlas feeds the radiosity back into itself, so a NaN or an infinity
/// would spread through the cache.
vec3 LumenCombineFinalLighting(vec3 albedo_encoded, vec3 emissive, vec3 direct, vec3 indirect)
{
	vec3 albedo = albedo_encoded * albedo_encoded;
	return max(LumenMakeFinite3(albedo * LUMEN_INV_PI * (direct + indirect) + emissive), vec3_splat(0.0));
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
