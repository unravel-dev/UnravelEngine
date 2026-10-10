/*
 * Surface cache: the lighting history of a reallocated card resampled into the capture atlas, one 8x8 group per
 * captured tile.
 *
 * Runs before the copy writes the physical atlases, which still hold the card's previous allocation (the resample
 * table, lumen_scene::get_resample_table, bound as the scene table). Each texel maps its page's card UV onto the
 * previous mip, the card's extent being unchanged, and samples its direct and indirect lighting bilinearly; the tile
 * takes the average radiosity update count of its 64 texels (0 where the previous mip held nothing), rounded to the
 * nearest whole count, stored in every texel's direct alpha. The copy (cs_lumen_card_copy.sc)
 * moves them into the new pages. Tiles of cards without a previous allocation, and pages refreshed in place (the tile
 * record's flag: the copy keeps their lighting where it is), are left alone.
 */

#include "bgfx_compute.sh"
#include "../common.sh"

/// rgb = direct lighting, a = the tile's radiosity update count.
IMAGE2D_WO(s_lumen_resample_direct_out, rgba16f, 0);
IMAGE2D_WO(s_lumen_resample_indirect_out, rgba16f, 1);
SAMPLER2D(s_lumen_card_direct, 3);
SAMPLER2D(s_lumen_card_indirect, 4);
SAMPLER2D(s_lumen_radiosity_frames, 5);
BUFFER_RO(b_lumen_copy_tiles, vec4, 6);
BUFFER_RO(b_lumen_scene, vec4, 7);

#define LUMEN_SURFACE_CACHE_TABLES_ONLY
#include "lumen/lumen_surface_cache.sh"
#include "lumen/lumen_surface_cache_lighting.sh"
#define LUMEN_TILE_RECORDS_COPY_TILES
#include "lumen/lumen_tile_records.sh"

/// x = tile count, y = the float4 the tile words start at (lumen_tile_records.sh).
uniform vec4 u_lumen_card_copy;

#define LUMEN_CARD_TILE_TEXELS 64

SHARED float s_frames_sum[LUMEN_CARD_TILE_TEXELS];

NUM_THREADS(8, 8, 1)
void main()
{
	int tile_index = LumenCopyTileIndex(ivec2(gl_WorkGroupID.xy));
	// The last row's groups past the list run to the barrier on the list's last tile.
	int tile_count = int(u_lumen_card_copy.x);
	LumenCopyTile copy_tile = LumenLoadCopyTile(min(tile_index, max(tile_count - 1, 0)), int(u_lumen_card_copy.y));
	vec4 texels = copy_tile.texels;
	vec4 uv_rect = copy_tile.uv_rect;
	vec4 page = copy_tile.page;
	float previous_card = copy_tile.card.x;
	bool keeps_lighting = copy_tile.axis_y.w > 0.5;
	bool resamples = tile_index < tile_count && previous_card >= 0.0 && !keeps_lighting;
	ivec2 local = ivec2(gl_LocalInvocationID.xy);
	float frames = 0.0;
	vec3 direct = vec3_splat(0.0);
	vec3 indirect = vec3_splat(0.0);
	BRANCH
	if(resamples)
	{
		vec2 card_uv = mix(uv_rect.xy, uv_rect.zw, (page.xy + vec2(local) + 0.5) / page.zw);
		LumenCard card = LumenLoadCard(int(previous_card));
		LumenCardSample s = LumenComputeCardSample(card, LumenCardLocalPosition(card, card_uv, 0.0).xy);
		if(s.valid)
		{
			direct = LumenFetchCardAtlas(s_lumen_card_direct, s, s.weights);
			indirect = LumenFetchCardAtlas(s_lumen_card_indirect, s, s.weights);
			// The tile of the footprint's nearest texel.
			ivec2 nearest = s.texel + ivec2(s.weights.y + s.weights.w > 0.5 ? 1 : 0, s.weights.z + s.weights.w > 0.5 ? 1 : 0);
			frames = texelFetch(s_lumen_radiosity_frames, nearest / LUMEN_CARD_TILE_SIZE, 0).x;
		}
	}
	s_frames_sum[gl_LocalInvocationIndex] = frames;
	barrier();
	for(uint stride = uint(LUMEN_CARD_TILE_TEXELS / 2); stride > 0u; stride = stride / 2u)
	{
		if(gl_LocalInvocationIndex < stride)
		{
			s_frames_sum[gl_LocalInvocationIndex] += s_frames_sum[gl_LocalInvocationIndex + stride];
		}
		barrier();
	}
	if(resamples)
	{
		float average = floor(s_frames_sum[0] / float(LUMEN_CARD_TILE_TEXELS) + 0.5);
		ivec2 capture_texel = ivec2(texels.xy) + local;
		imageStore(s_lumen_resample_direct_out, capture_texel, vec4(direct, average));
		imageStore(s_lumen_resample_indirect_out, capture_texel, vec4(indirect, 0.0));
	}
}
