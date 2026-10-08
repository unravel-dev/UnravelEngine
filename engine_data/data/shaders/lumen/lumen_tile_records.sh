#ifndef __LUMEN_TILE_RECORDS_SH__
#define __LUMEN_TILE_RECORDS_SH__

/*
 * Tile lists of the card passes (lumen_surface_cache_pass build_copy_tiles / build_light_tiles), UE's page records
 * plus one word per tile (LumenSceneRendering.cpp:1908-1921): each page's data once (a record of the pass's stride),
 * then the tiles, four words per vec4 from the dispatch's words base. A word is page x LUMEN_TILE_WORD_PAGE_SCALE +
 * the tile's column x LUMEN_TILE_WORD_COLUMN_SCALE + its row in the page, both in 8-texel tiles: an integer below
 * 2^24, exact as a float.
 */

#define LUMEN_TILE_WORD_PAGE_SCALE 256u
#define LUMEN_TILE_WORD_COLUMN_SCALE 16u
#define LUMEN_TILE_WORDS_PER_VEC4 4
#define LUMEN_TILE_RECORD_TILE_TEXELS 8

/// The word of tile @p tile from the vec4 @p words that holds it (vec4 tile / 4 of the list).
float LumenTileWord(vec4 words, int tile)
{
	int lane = tile - (tile / LUMEN_TILE_WORDS_PER_VEC4) * LUMEN_TILE_WORDS_PER_VEC4;
	return lane == 0 ? words.x : lane == 1 ? words.y : lane == 2 ? words.z : words.w;
}

/// A tile word decoded: xy = the tile's texel offset in its page, z = its page record.
ivec3 LumenDecodeTileWord(float word)
{
	uint packed = uint(word + 0.5);
	uint in_page = packed - (packed / LUMEN_TILE_WORD_PAGE_SCALE) * LUMEN_TILE_WORD_PAGE_SCALE;
	uint column = in_page / LUMEN_TILE_WORD_COLUMN_SCALE;
	uint row = in_page - column * LUMEN_TILE_WORD_COLUMN_SCALE;
	return ivec3(int(column) * LUMEN_TILE_RECORD_TILE_TEXELS,
	             int(row) * LUMEN_TILE_RECORD_TILE_TEXELS,
	             int(packed / LUMEN_TILE_WORD_PAGE_SCALE));
}

#ifdef LUMEN_TILE_RECORDS_COPY_TILES
/// float4s per copy page record (lumen_surface_cache_pass.cpp copy_page_stride).
#define LUMEN_CARD_COPY_PAGE_STRIDE 6

/// A copy tile (the card copy and the lighting resample) of b_lumen_copy_tiles, which the includer declares.
struct LumenCopyTile
{
	/// The tile's first texel in the capture atlas (xy) and in the physical atlas (zw).
	vec4 texels;
	/// The page's card UV rectangle.
	vec4 uv_rect;
	/// The tile's texel offset in its page (xy), the page's size in texels (zw).
	vec4 page;
	/// x = the card's previous allocation in the resample table (lumen_scene::get_resample_table) or -1, yzw = the
	/// card's world axis x.
	vec4 card;
	/// xyz = the card's world axis y, w = 1 when the page is refreshed in place (it keeps its lighting).
	vec4 axis_y;
	/// xyz = the card's world axis z.
	vec4 axis_z;
};

/// Tile @p tile_index of a copy tile list whose words start at float4 @p words_base.
LumenCopyTile LumenLoadCopyTile(int tile_index, int words_base)
{
	vec4 words = b_lumen_copy_tiles[words_base + tile_index / LUMEN_TILE_WORDS_PER_VEC4];
	ivec3 tile = LumenDecodeTileWord(LumenTileWord(words, tile_index));
	int record = tile.z * LUMEN_CARD_COPY_PAGE_STRIDE;
	vec4 origins = b_lumen_copy_tiles[record + 0];
	LumenCopyTile result;
	result.texels = origins + vec4(vec2(tile.xy), vec2(tile.xy));
	result.uv_rect = b_lumen_copy_tiles[record + 1];
	result.page = vec4(vec2(tile.xy), b_lumen_copy_tiles[record + 2].zw);
	result.card = b_lumen_copy_tiles[record + 3];
	result.axis_y = b_lumen_copy_tiles[record + 4];
	result.axis_z = b_lumen_copy_tiles[record + 5];
	return result;
}
#endif

#ifdef LUMEN_TILE_RECORDS_LIGHT_TILES
/// float4s per lighting page record (lumen_surface_cache_pass.cpp light_page_stride).
#define LUMEN_LIGHT_PAGE_STRIDE 4

/// A lighting tile (the card direct lighting, the radiosity passes) of b_lumen_light_tiles, which the includer
/// declares: t0 = (the tile's first atlas texel, the card, the page's update index), the page's card UV rectangle,
/// page = (the page's atlas offset, its size in texels), and mip = the page's mip in the page table (its span's first
/// entry from the table's base, its size in pages xy; the card's locked mip or its hi-res mip).
struct LumenLightTile
{
	vec4 t0;
	vec4 uv_rect;
	vec4 page;
	vec4 mip;
};

/// Tile @p tile_index of a lighting tile list whose words start at float4 @p words_base.
LumenLightTile LumenLoadLightTile(int tile_index, int words_base)
{
	vec4 words = b_lumen_light_tiles[words_base + tile_index / LUMEN_TILE_WORDS_PER_VEC4];
	ivec3 tile = LumenDecodeTileWord(LumenTileWord(words, tile_index));
	int record = tile.z * LUMEN_LIGHT_PAGE_STRIDE;
	vec4 page_origin = b_lumen_light_tiles[record + 0];
	LumenLightTile result;
	result.t0 = vec4(page_origin.xy + vec2(tile.xy), page_origin.zw);
	result.uv_rect = b_lumen_light_tiles[record + 1];
	result.page = b_lumen_light_tiles[record + 2];
	result.mip = b_lumen_light_tiles[record + 3];
	return result;
}
#endif

#endif // __LUMEN_TILE_RECORDS_SH__
