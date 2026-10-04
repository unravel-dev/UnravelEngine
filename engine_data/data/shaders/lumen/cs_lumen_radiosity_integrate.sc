/*
 * Lumen surface cache radiosity, integrate + temporal + final combine (UE 5.8 LumenRadiosity.usf:475-573 and
 * LumenSceneLighting.usf:476-507): one 8x8 group per scheduled card tile, one thread per texel.
 *
 * Each texel interpolates the SH of its four nearest probes with expanded bilinear weights (no weight is ever
 * 0 or 1), dropping probes that are invalid or off its tangent plane, and evaluates the irradiance along its
 * normal. The tile's update count n (capped at 4) sets the blend 1 / (1 + n) into the indirect atlas, so a
 * fresh page converges over ~5 updates. The final lighting is recomputed from the new indirect:
 * albedo / pi x (direct + indirect) + emissive, with the card depth in alpha.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "../sampling.sh"

IMAGE2D_RW(s_lumen_indirect, rgba16f, 0);
IMAGE2D_WO(s_lumen_final_out, rgba16f, 1);
IMAGE2D_RW(i_lumen_radiosity_frames, r32f, 2);
SAMPLER2D(s_lumen_card_depth, 3);
SAMPLER2D(s_lumen_card_normal, 4);
SAMPLER2D(s_lumen_card_albedo, 5);
SAMPLER2D(s_lumen_card_emissive, 6);
SAMPLER2D(s_lumen_card_direct, 7);
SAMPLER2D(s_lumen_radiosity_sh_r, 8);
SAMPLER2D(s_lumen_radiosity_sh_g, 9);
SAMPLER2D(s_lumen_radiosity_sh_b, 10);
BUFFER_RO(b_lumen_light_tiles, vec4, 11);
BUFFER_RO(b_lumen_scene, vec4, 12);

#define LUMEN_SURFACE_CACHE_TABLES_ONLY
#include "lumen/lumen_surface_cache.sh"
#include "lumen/lumen_radiosity_common.sh"
#include "lumen/lumen_surface_cache_lighting.sh"

/// x = first tile of this dispatch, y = tile count.
uniform vec4 u_lumen_card_lighting;

SHARED float s_frames;

/// One probe's contribution: rgb = weighted irradiance, a = weight.
vec4 LumenProbeIrradiance(LumenCard card, vec4 uv_rect, vec4 page, ivec2 probe_cell_in_page, ivec2 jitter,
                          float weight, vec3 position, vec3 normal)
{
	ivec2 cell_origin = ivec2(page.xy) + probe_cell_in_page * LUMEN_RADIOSITY_PROBE_SPACING;
	if(any(lessThan(probe_cell_in_page, ivec2(0, 0))) || any(greaterThanEqual(cell_origin, ivec2(page.xy + page.zw))))
	{
		return vec4_splat(0.0);
	}
	ivec2 probe_texel = cell_origin + jitter;
	float probe_depth = texelFetch(s_lumen_card_depth, probe_texel, 0).x;
	if(probe_depth >= 1.0)
	{
		return vec4_splat(0.0);
	}
	vec3 probe_position = LumenTexelPosition(card, uv_rect, page, probe_texel, probe_depth);
	if(!LumenRadiosityPlaneTest(position, normal, probe_position))
	{
		return vec4_splat(0.0);
	}
	ivec2 probe_cell = cell_origin / LUMEN_RADIOSITY_PROBE_SPACING;
	vec4 transfer = LumenSH2DiffuseTransfer(normal);
	vec3 irradiance = vec3(dot(texelFetch(s_lumen_radiosity_sh_r, probe_cell, 0), transfer),
	                       dot(texelFetch(s_lumen_radiosity_sh_g, probe_cell, 0), transfer),
	                       dot(texelFetch(s_lumen_radiosity_sh_b, probe_cell, 0), transfer));
	return vec4(irradiance * weight, weight);
}

NUM_THREADS(8, 8, 1)
void main()
{
	int tile_index = int(u_lumen_card_lighting.x) + int(gl_WorkGroupID.x);
	bool is_tile_active = float(gl_WorkGroupID.x) < u_lumen_card_lighting.y;
	vec4 t0 = b_lumen_light_tiles[tile_index * LUMEN_RADIOSITY_TILE_STRIDE + 0];
	vec4 uv_rect = b_lumen_light_tiles[tile_index * LUMEN_RADIOSITY_TILE_STRIDE + 1];
	vec4 page = b_lumen_light_tiles[tile_index * LUMEN_RADIOSITY_TILE_STRIDE + 2];
	ivec2 frames_texel = ivec2(t0.xy) / LUMEN_CARD_TILE_SIZE;
	if(gl_LocalInvocationIndex == 0u)
	{
		// The tile's update count, advanced once per update and capped.
		float frames = is_tile_active ? min(imageLoad(i_lumen_radiosity_frames, frames_texel).x + 1.0, LUMEN_RADIOSITY_MAX_FRAMES) : 0.0;
		s_frames = frames;
		if(is_tile_active)
		{
			imageStore(i_lumen_radiosity_frames, frames_texel, vec4(frames, 0.0, 0.0, 0.0));
		}
	}
	barrier();
	if(!is_tile_active)
	{
		return;
	}
	ivec2 texel = ivec2(t0.xy) + ivec2(gl_LocalInvocationID.xy);
	float depth = texelFetch(s_lumen_card_depth, texel, 0).x;
	if(depth >= 1.0)
	{
		imageStore(s_lumen_indirect, texel, vec4_splat(0.0));
		imageStore(s_lumen_final_out, texel, vec4(0.0, 0.0, 0.0, 1.0));
		return;
	}
	LumenCard card = LumenLoadCard(int(t0.z));
	vec3 position = LumenTexelPosition(card, uv_rect, page, texel, depth);
	vec3 normal = decodeNormalOctahedron(texelFetch(s_lumen_card_normal, texel, 0).xy);
	ivec2 jitter = LumenRadiosityJitter(t0.w);
	// Expanded bilinear over the four nearest probes (UE BilinearExpand).
	ivec2 texel_in_page = max(texel - ivec2(page.xy) - jitter, ivec2(0, 0));
	ivec2 p00 = texel_in_page / LUMEN_RADIOSITY_PROBE_SPACING;
	vec2 f = (vec2(texel_in_page - p00 * LUMEN_RADIOSITY_PROBE_SPACING) + 1.0) / float(LUMEN_RADIOSITY_PROBE_SPACING + 2);
	vec4 sum = LumenProbeIrradiance(card, uv_rect, page, p00, jitter, (1.0 - f.x) * (1.0 - f.y), position, normal);
	sum += LumenProbeIrradiance(card, uv_rect, page, p00 + ivec2(1, 0), jitter, f.x * (1.0 - f.y), position, normal);
	sum += LumenProbeIrradiance(card, uv_rect, page, p00 + ivec2(0, 1), jitter, (1.0 - f.x) * f.y, position, normal);
	sum += LumenProbeIrradiance(card, uv_rect, page, p00 + ivec2(1, 1), jitter, f.x * f.y, position, normal);
	vec3 irradiance = sum.w > 0.0 ? max(sum.xyz / sum.w, vec3_splat(0.0)) : vec3_splat(0.0);
	float alpha = 1.0 / (1.0 + s_frames);
	vec3 indirect = mix(imageLoad(s_lumen_indirect, texel).xyz, irradiance, alpha);
	imageStore(s_lumen_indirect, texel, vec4(indirect, 0.0));
	vec3 albedo_encoded = texelFetch(s_lumen_card_albedo, texel, 0).xyz;
	vec3 direct = texelFetch(s_lumen_card_direct, texel, 0).xyz;
	vec3 emissive = texelFetch(s_lumen_card_emissive, texel, 0).xyz;
	imageStore(s_lumen_final_out, texel, vec4(LumenCombineFinalLighting(albedo_encoded, emissive, direct, indirect), depth));
}
