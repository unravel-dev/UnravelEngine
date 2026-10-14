/*
 * Surface cache radiosity, integrate + temporal + final combine: one 8x8 group per scheduled card tile, one thread
 * per texel.
 *
 * Each texel interpolates the SH of its four nearest probes of the card with expanded bilinear weights (no weight is
 * ever 0 or 1; past a page edge in the card's neighbouring page, GiResolveRadiosityCell), dropping probes that are
 * invalid or off its tangent plane, and evaluates the irradiance along its normal. Texels before a card's first probe
 * read it as the nearest. The tile's update count n (capped at GI_RADIOSITY_ACCUMULATED_UPDATES) blends 1 / n into
 * the indirect atlas: a running mean while a page warms up, so its first update stands whole rather than half over
 * the black it starts from, and a steady 1 / (1 + 4) after (1 / (1 + min(n, 4)) from the first update would reach
 * only 50% / 67% / 75% / 80% of the light after 1-4). The final lighting is recomputed from the new indirect:
 * albedo / pi x (direct + indirect) + emissive, with the card depth in alpha.
 */

#include "bgfx_compute.sh"
#include "gi/gi_image_formats.sh"
#include "../common.sh"
#include "../sampling.sh"

IMAGE2D_RW(s_gi_indirect, rg11b10f, 0);
IMAGE2D_WO(s_gi_final_out, rgba16f, 1);
IMAGE2D_RW(i_gi_radiosity_frames, r32f, 2);
SAMPLER2D(s_gi_card_depth, 3);
SAMPLER2D(s_gi_card_normal, 4);
SAMPLER2D(s_gi_card_albedo, 5);
SAMPLER2D(s_gi_card_emissive, 6);
SAMPLER2D(s_gi_card_direct, 7);
SAMPLER2D(s_gi_radiosity_sh_r, 8);
SAMPLER2D(s_gi_radiosity_sh_g, 9);
SAMPLER2D(s_gi_radiosity_sh_b, 10);
BUFFER_RO(b_gi_light_tiles, vec4, 11);
#define GI_TILE_RECORDS_LIGHT_TILES
#include "gi/gi_tile_records.sh"
BUFFER_RO(b_gi_scene, vec4, 12);

#define GI_SURFACE_CACHE_TABLES_ONLY
#include "gi/gi_surface_cache.sh"
#include "gi/gi_radiosity_common.sh"
#include "gi/gi_surface_cache_lighting.sh"

/// x = first tile of this dispatch, y = tile count, z = the float4 the tile words start at, w = the frame index.
uniform vec4 u_gi_card_lighting;

/// The updates the running mean counts at most: its steady blend is 1 / (1 + GI_RADIOSITY_MAX_FRAMES).
#define GI_RADIOSITY_ACCUMULATED_UPDATES (GI_RADIOSITY_MAX_FRAMES + 1.0)

SHARED float s_frames;

/// One probe's contribution: rgb = weighted irradiance, a = weight. The probe is cell @p probe_cell_in_page of the page
/// (atlas origin xy, size xy) @p page, updated last with @p update_index.
vec4 GiProbeIrradiance(GiCard card, vec4 uv_rect, vec4 page, float update_index, ivec2 probe_cell_in_page,
                          float weight, vec3 position, vec3 normal)
{
	GiRadiosityCell cell = GiResolveRadiosityCell(card, uv_rect, page, update_index, probe_cell_in_page);
	if(!cell.valid)
	{
		return vec4_splat(0.0);
	}
	ivec2 probe_texel = cell.atlas_origin + GiRadiosityJitter(cell.update_index);
	float probe_depth = texelFetch(s_gi_card_depth, probe_texel, 0).x;
	if(probe_depth >= 1.0)
	{
		return vec4_splat(0.0);
	}
	vec3 probe_position = GiTexelPosition(card, cell.uv_rect, cell.page, probe_texel, probe_depth);
	if(!GiRadiosityPlaneTest(position, normal, probe_position))
	{
		return vec4_splat(0.0);
	}
	ivec2 probe_cell = cell.atlas_origin / u_gi_radiosity_spacing;
	vec4 transfer = GiSH2DiffuseTransfer(normal);
	vec3 irradiance = vec3(dot(texelFetch(s_gi_radiosity_sh_r, probe_cell, 0), transfer),
	                       dot(texelFetch(s_gi_radiosity_sh_g, probe_cell, 0), transfer),
	                       dot(texelFetch(s_gi_radiosity_sh_b, probe_cell, 0), transfer));
	return vec4(irradiance * weight, weight);
}

NUM_THREADS(8, 8, 1)
void main()
{
	int tile_index = int(u_gi_card_lighting.x) + int(gl_WorkGroupID.x);
	bool is_tile_active = float(gl_WorkGroupID.x) < u_gi_card_lighting.y;
	GiLightTile light_tile = GiLoadLightTile(is_tile_active ? tile_index : int(u_gi_card_lighting.x), int(u_gi_card_lighting.z));
	vec4 t0 = light_tile.t0;
	vec4 uv_rect = light_tile.uv_rect;
	vec4 page = light_tile.page;
	ivec2 frames_texel = ivec2(t0.xy) / GI_CARD_TILE_SIZE;
	if(gl_LocalInvocationIndex == 0u)
	{
		// The tile's update count, advanced once per update and capped.
		float frames = is_tile_active ? min(imageLoad(i_gi_radiosity_frames, frames_texel).x + 1.0,
		                                    GI_RADIOSITY_ACCUMULATED_UPDATES)
		                              : 0.0;
		s_frames = frames;
		if(is_tile_active)
		{
			imageStore(i_gi_radiosity_frames, frames_texel, vec4(frames, 0.0, 0.0, 0.0));
		}
	}
	barrier();
	if(!is_tile_active)
	{
		return;
	}
	ivec2 texel = ivec2(t0.xy) + ivec2(gl_LocalInvocationID.xy);
	float depth = texelFetch(s_gi_card_depth, texel, 0).x;
	if(depth >= 1.0)
	{
		imageStore(s_gi_indirect, texel, vec4_splat(0.0));
		imageStore(s_gi_final_out, texel, vec4(0.0, 0.0, 0.0, 1.0));
		return;
	}
	GiCard card = GiLoadCard(int(t0.z));
	// The page's own mip (locked or hi-res) is the one whose neighbouring pages the probes are read from.
	card.page_table_offset = light_tile.mip.x;
	card.size_in_pages = light_tile.mip.yz;
	vec3 position = GiTexelPosition(card, uv_rect, page, texel, depth);
	vec3 normal = GiDecodeCardNormal(texelFetch(s_gi_card_normal, texel, 0).xy, card.axis_x, card.axis_y, card.axis_z);
	ivec2 jitter = GiRadiosityJitter(t0.w);
	// Expanded bilinear over the four nearest probes, in this page's probe grid. Before the page's
	// first probe the previous page's last one is the nearest; before the card's first (or past every page edge when
	// the probes stop there), the first probe is.
	int spacing = u_gi_radiosity_spacing;
	ivec2 page_coord = GiRadiosityPageCoord(uv_rect, card.size_in_pages);
	bvec2 has_previous = bvec2(page_coord.x > 0 && u_gi_radiosity_page_words >= 0.0,
	                           page_coord.y > 0 && u_gi_radiosity_page_words >= 0.0);
	ivec2 texel_in_page = texel - ivec2(page.xy) - jitter;
	texel_in_page = ivec2(has_previous.x ? texel_in_page.x : max(texel_in_page.x, 0),
	                      has_previous.y ? texel_in_page.y : max(texel_in_page.y, 0));
	ivec2 p00 = ivec2(floor(vec2(texel_in_page) / float(spacing)));
	vec2 f = (vec2(texel_in_page - p00 * spacing) + 1.0) / float(spacing + 2);
	vec4 sum = GiProbeIrradiance(card, uv_rect, page, t0.w, p00, (1.0 - f.x) * (1.0 - f.y), position, normal);
	sum += GiProbeIrradiance(card, uv_rect, page, t0.w, p00 + ivec2(1, 0), f.x * (1.0 - f.y), position, normal);
	sum += GiProbeIrradiance(card, uv_rect, page, t0.w, p00 + ivec2(0, 1), (1.0 - f.x) * f.y, position, normal);
	sum += GiProbeIrradiance(card, uv_rect, page, t0.w, p00 + ivec2(1, 1), f.x * f.y, position, normal);
	vec3 irradiance = sum.w > 0.0 ? max(sum.xyz / sum.w, vec3_splat(0.0)) : vec3_splat(0.0);
	float alpha = 1.0 / max(s_frames, 1.0);
	vec3 indirect = mix(imageLoad(s_gi_indirect, texel).xyz, irradiance, alpha);
	imageStore(s_gi_indirect, texel, vec4(GiQuantizeCardLighting(indirect, texel, u_gi_card_lighting.w), 0.0));
	vec3 albedo_encoded = texelFetch(s_gi_card_albedo, texel, 0).xyz;
	vec3 direct = texelFetch(s_gi_card_direct, texel, 0).xyz;
	vec3 emissive = texelFetch(s_gi_card_emissive, texel, 0).xyz;
	imageStore(s_gi_final_out, texel, vec4(GiCombineFinalLighting(albedo_encoded, emissive, direct, indirect), depth));
}
