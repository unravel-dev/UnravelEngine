#ifndef __GI_SURFACE_CACHE_LIGHTING_SH__
#define __GI_SURFACE_CACHE_LIGHTING_SH__

/*
 * Surface cache lighting, shared by the capture copy (cs_gi_card_copy.eshader), the lighting resample
 * (cs_gi_card_resample.eshader) and the radiosity integrate (cs_gi_radiosity_integrate.eshader). The copy and the resample
 * read their tiles through gi_tile_records.sh (GiLoadCopyTile).
 *
 * The normal atlas holds card-space normals: a placement that rotates keeps its captured normals, which every reader
 * turns into world space through the card's current axes.
 *
 * The atlas formats: albedo RGBA8, normal RG8, depth R16, emissive, direct and indirect lighting R11G11B10 float,
 * stored through GiQuantizeCardLighting (direct and indirect: RGBA16F under WebGPU, GiQuantizeBounceLighting). The
 * final lighting stays RGBA16F with the card depth in alpha: a hit
 * reads its lighting and its depth test from one texel, and the hit tracers have no stage left for a depth atlas of
 * their own.
 */

#include "gi/gi_finite.sh"
#include "gi/gi_image_formats.sh"
#include "sampling.sh"

/// @p lighting ready for a store to an R11G11B10 atlas at @p texel: GiQuantizeForRg11b10f with interleaved gradient
/// noise over the atlas, offset by @p frame_index, so neither a single store nor the radiosity's running blend drifts.
vec3 GiQuantizeCardLighting(vec3 lighting, ivec2 texel, float frame_index)
{
	return GiQuantizeForRg11b10f(lighting, InterleavedGradientNoise(vec2(texel), mod(frame_index, 8.0)));
}

/// @p lighting ready for a store to the direct or indirect atlas (GI_BOUNCE_ATLAS_FORMAT). An rgba16f atlas takes it
/// as it is: the dither's one rg11b10f step would brighten every store, and the radiosity's running blend would
/// compound that, while rgba16f's own step is 16 times finer and drifts the blend by under half a percent.
vec3 GiQuantizeBounceLighting(vec3 lighting, ivec2 texel, float frame_index)
{
#if BGFX_SHADER_LANGUAGE_WGSL
	return lighting;
#else
	return GiQuantizeCardLighting(lighting, texel, frame_index);
#endif
}

/// Copy tiles per dispatch row (the copy and the resample run rows of tiles: a full capture atlas has more tiles than
/// one dispatch dimension allows). Mirror of gi_surface_cache_pass.cpp copy_dispatch_width.
#define GI_CARD_COPY_DISPATCH_WIDTH 256

/// The copy tile of group @p group of a copy / resample dispatch.
int GiCopyTileIndex(ivec2 group)
{
	return group.y * GI_CARD_COPY_DISPATCH_WIDTH + group.x;
}
/// Card tiles are 8 x 8 texels; the radiosity update count is stored per tile.
#define GI_CARD_TILE_SIZE 8
#define GI_INV_PI 0.31830989

/// What a ray sees at a card texel, from its encoded albedo (the square root of the diffuse colour), emissive, direct
/// and indirect lighting. Made finite: the final atlas feeds the radiosity back into itself, so a NaN or an infinity
/// would spread through the cache.
vec3 GiCombineFinalLighting(vec3 albedo_encoded, vec3 emissive, vec3 direct, vec3 indirect)
{
	vec3 albedo = albedo_encoded * albedo_encoded;
	return max(GiMakeFinite3(albedo * GI_INV_PI * (direct + indirect) + emissive), vec3_splat(0.0));
}

/// A world normal in a card's axes, octahedral-encoded for the normal atlas.
vec2 GiEncodeCardNormal(vec3 world_normal, vec3 axis_x, vec3 axis_y, vec3 axis_z)
{
	return encodeNormalOctahedron(vec3(dot(world_normal, axis_x), dot(world_normal, axis_y), dot(world_normal, axis_z)));
}

/// The world normal of a normal-atlas texel @p encoded of the card with axes @p axis_x, @p axis_y, @p axis_z.
vec3 GiDecodeCardNormal(vec2 encoded, vec3 axis_x, vec3 axis_y, vec3 axis_z)
{
	vec3 card_normal = decodeNormalOctahedron(encoded);
	return normalize(axis_x * card_normal.x + axis_y * card_normal.y + axis_z * card_normal.z);
}

#endif // __GI_SURFACE_CACHE_LIGHTING_SH__
