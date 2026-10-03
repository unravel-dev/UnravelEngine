/*
 * Lumen surface cache: card capture -> physical atlases (UE 5.8 LumenSurfaceCache.usf CopyCardsToSurfaceCache,
 * LumenCardBasePass.ush:98-148, and LumenSceneLighting.usf CopyCardCaptureLightingToAtlasPS:688-734), one 8x8 group
 * per captured tile.
 *
 * The capture atlas holds the G-buffer encoding of each captured page (rt0 = sRGB base colour + AO, rt1 =
 * octahedral world normal + metalness + roughness, rt2 = emissive, depth = the card's linear depth: 0 at
 * its front plane, 1 at its back, cleared to 1). The surface cache stores what Lumen's card base pass
 * outputs:
 *  - albedo: sqrt of the diffuse colour, BaseColor (1 - Metallic) + 0.45 x lerp(0.04, BaseColor, Metallic)
 *    (EnvBRDFApproxFullyRough), a = 1 where the card sees a surface;
 *  - normal: the octahedral world normal, a = covered;
 *  - emissive: linear emissive radiance;
 *  - depth: the card depth, 1 where nothing was captured;
 *  - lighting: a reallocated card's pages take the lighting resampled from its previous allocation
 *    (cs_lumen_card_resample.sc): direct, indirect, the radiosity update count and the final lighting combined
 *    from them with the new material; a card that was not resident starts unlit (direct and indirect 0, update
 *    count 0, final lighting = its emissive). The final lighting carries the depth in alpha for the samplers.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "../lighting.sh"

IMAGE2D_WO(s_lumen_albedo_out, rgba8, 0);
IMAGE2D_WO(s_lumen_normal_out, rgba8, 1);
IMAGE2D_WO(s_lumen_emissive_out, rgba16f, 2);
IMAGE2D_WO(s_lumen_depth_out, r32f, 3);
IMAGE2D_WO(s_lumen_final_out, rgba16f, 4);
IMAGE2D_WO(s_lumen_indirect_out, rgba16f, 5);
IMAGE2D_WO(s_lumen_radiosity_frames_out, r32f, 6);
IMAGE2D_WO(s_lumen_direct_out, rgba16f, 7);
SAMPLER2D(s_lumen_capture_rt0, 8);
SAMPLER2D(s_lumen_capture_rt1, 9);
SAMPLER2D(s_lumen_capture_rt2, 10);
SAMPLER2D(s_lumen_capture_depth, 11);
SAMPLER2D(s_lumen_resample_direct, 12);
SAMPLER2D(s_lumen_resample_indirect, 13);
SAMPLER2D(s_lumen_resample_frames, 14);
BUFFER_RO(b_lumen_copy_tiles, vec4, 15);

#include "lumen/lumen_surface_cache_lighting.sh"

/// x = tile count.
uniform vec4 u_lumen_card_copy;

/// UE's default Specular (0.5) as F0: 0.08 x 0.5.
#define LUMEN_DIELECTRIC_F0 0.04
/// EnvBRDFApproxFullyRough: the share of the specular colour a fully rough surface scatters diffusely.
#define LUMEN_FULLY_ROUGH_SPECULAR 0.45

NUM_THREADS(8, 8, 1)
void main()
{
	int tile_index = int(gl_WorkGroupID.x);
	if(float(tile_index) >= u_lumen_card_copy.x)
	{
		return;
	}
	vec4 tile = b_lumen_copy_tiles[tile_index * LUMEN_CARD_COPY_TILE_STRIDE + 0];
	float previous_card = b_lumen_copy_tiles[tile_index * LUMEN_CARD_COPY_TILE_STRIDE + 3].x;
	ivec2 local = ivec2(gl_LocalInvocationID.xy);
	ivec2 capture_texel = ivec2(tile.xy) + local;
	ivec2 atlas_texel = ivec2(tile.zw) + local;
	float depth = texelFetch(s_lumen_capture_depth, capture_texel, 0).x;
	bool covered = depth < 1.0;
	vec4 rt0 = texelFetch(s_lumen_capture_rt0, capture_texel, 0);
	vec4 rt1 = texelFetch(s_lumen_capture_rt1, capture_texel, 0);
	vec3 emissive = texelFetch(s_lumen_capture_rt2, capture_texel, 0).xyz;
	vec3 base_color = srgb_to_linear(rt0.xyz);
	float metalness = rt1.z;
	vec3 specular_color = mix(vec3_splat(LUMEN_DIELECTRIC_F0), base_color, metalness);
	vec3 diffuse_color = base_color * (1.0 - metalness) + LUMEN_FULLY_ROUGH_SPECULAR * specular_color;
	float coverage = covered ? 1.0 : 0.0;
	float card_depth = covered ? depth : 1.0;
	vec3 albedo_encoded = sqrt(saturate(diffuse_color)) * coverage;
	vec3 direct = vec3_splat(0.0);
	vec3 indirect = vec3_splat(0.0);
	float frames = 0.0;
	if(previous_card >= 0.0)
	{
		direct = texelFetch(s_lumen_resample_direct, capture_texel, 0).xyz;
		indirect = texelFetch(s_lumen_resample_indirect, capture_texel, 0).xyz;
		frames = texelFetch(s_lumen_resample_frames, capture_texel / LUMEN_CARD_TILE_SIZE, 0).x;
	}
	imageStore(s_lumen_albedo_out, atlas_texel, vec4(albedo_encoded, coverage));
	imageStore(s_lumen_normal_out, atlas_texel, vec4(rt1.xy * coverage, 0.0, coverage));
	imageStore(s_lumen_emissive_out, atlas_texel, vec4(emissive * coverage, 0.0));
	imageStore(s_lumen_depth_out, atlas_texel, vec4(card_depth, 0.0, 0.0, 0.0));
	imageStore(s_lumen_direct_out, atlas_texel, vec4(direct, 0.0));
	imageStore(s_lumen_indirect_out, atlas_texel, vec4(indirect, 0.0));
	imageStore(s_lumen_final_out,
	           atlas_texel,
	           vec4(LumenCombineFinalLighting(albedo_encoded, emissive * coverage, direct, indirect), card_depth));
	if(local.x == 0 && local.y == 0)
	{
		imageStore(s_lumen_radiosity_frames_out, ivec2(tile.zw) / LUMEN_CARD_TILE_SIZE, vec4(frames, 0.0, 0.0, 0.0));
	}
}
