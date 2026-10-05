/*
 * Lumen surface cache direct lighting and final combine (UE 5.8 LumenSceneDirectLighting.usf:99-164 and
 * LumenSceneDirectLightingSoftwareRayTracing.usf:72-150, LumenSceneLighting.usf:476-507), one 8x8 group per
 * card tile the lighting scheduler picked for direct lighting.
 *
 * Per covered texel: its world position from the card (page UV rectangle + card depth) and every light's
 * Lambert irradiance with a binary global-SDF shadow ray (start bias (1 + 2 (1 - N.L)) voxel extents of each
 * level the ray samples, the expansion ramping in from there; local lights stop one extent short of the
 * light). Directional lights take the cloud layer's shadow as the deferred light does (UE
 * r.Lumen.DirectLighting.CloudTransmittance, cloud_shadow.sh). With more than LUMEN_CARD_CULL_MIN_LIGHTS lights a
 * tile culls them first (UE LumenSceneDirectLightingCulling.usf): its texels' world bounds, then a bit per light
 * whose range reaches them; the texels add the marked lights in index order - a culled light adds exactly 0, so the
 * sum is the full loop's. Direct lighting keeps no history: every texel of a
 * scheduled tile is rewritten, and its final lighting is combined with the indirect lighting it holds (the radiosity
 * integrate combines the tiles it updates); the sky reaches the cache only through radiosity.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "lumen/lumen_constants.sh"
/// The global SDF's coverage (gi/sdf_clipmap.sh).
#define SDF_CLIPMAP_COVERAGE_STAGE 10
#define SDF_CLIPMAP_MIP_STAGE 14
#include "lumen/lumen_global_sdf.sh"
#include "gi/gpu_lights.sh"

IMAGE2D_WO(s_lumen_direct_out, rgba16f, 0);
IMAGE2D_WO(s_lumen_final_out, rgba16f, 1);
SAMPLER2D(s_lumen_card_normal, 3);
SAMPLER2D(s_lumen_card_depth, 7);
/// Per tile, 3 vec4: (top-left atlas texel xy, card index, the page's update count), the page's card UV rectangle,
/// (the page's atlas origin xy, the page size in texels xy).
BUFFER_RO(b_lumen_light_tiles, vec4, 8);
BUFFER_RO(b_lumen_scene, vec4, 9);
SAMPLER2D(s_lumen_card_albedo, 11);
SAMPLER2D(s_lumen_card_emissive, 12);
SAMPLER2D(s_lumen_card_indirect, 13);
/// The sun's cloud transmittance map (cloud_shadow.sh).
SAMPLER2D(s_cloudShadow, 2);
#include "../cloud_shadow.sh"

#define LUMEN_SURFACE_CACHE_TABLES_ONLY
#include "lumen/lumen_surface_cache.sh"
#include "lumen/lumen_surface_cache_lighting.sh"

/// x = first tile of this dispatch, y = tile count.
uniform vec4 u_lumen_card_lighting;

/// Lights at or below which a tile skips the culling (its cost is not repaid).
#define LUMEN_CARD_CULL_MIN_LIGHTS 4
/// The lights the culling marks per tile; more fall back to every light.
#define LUMEN_CARD_CULL_MAX_LIGHTS 256
#define LUMEN_CARD_CULL_WORDS (LUMEN_CARD_CULL_MAX_LIGHTS / 32)
#define LUMEN_CARD_TILE_TEXELS 64

SHARED vec3 s_tile_min[LUMEN_CARD_TILE_TEXELS];
SHARED vec3 s_tile_max[LUMEN_CARD_TILE_TEXELS];
SHARED uint s_tile_lights[LUMEN_CARD_CULL_WORDS];

/// r.LumenScene.DirectLighting.GlobalSDF.ShadowRayBias, in voxel extents.
#define LUMEN_GLOBAL_SDF_SHADOW_RAY_BIAS 1.0
/// The shadow ray length of directional lights (Lumen's MaxTraceDistance, 200 m).
#define LUMEN_DIRECTIONAL_SHADOW_DISTANCE 200.0
#define LUMEN_LIGHT_TILE_STRIDE 3

/// GetCardBiasForShadowing, in voxel extents.
float LumenShadowRayBias(vec3 normal, vec3 to_light)
{
	return LUMEN_GLOBAL_SDF_SHADOW_RAY_BIAS * (1.0 + 2.0 * saturate(1.0 - dot(normal, to_light)));
}

float LumenShadowVisibility(vec3 position, vec3 normal, vec3 to_light, float light_distance, bool is_local)
{
	float t_max = is_local ? light_distance : LUMEN_DIRECTIONAL_SHADOW_DISTANCE;
	float end_bias = is_local ? LUMEN_GLOBAL_SDF_SHADOW_RAY_BIAS : 0.0;
	LumenSdfHit hit = LumenTraceGlobalSdfBiased(position, to_light, 0.0, t_max, true, LumenShadowRayBias(normal, to_light),
	                                            end_bias);
	return hit.hit ? 0.0 : 1.0;
}

/// One light's shadowed Lambert irradiance at a card texel.
vec3 LumenCardLightIrradiance(GpuLight light, vec3 position, vec3 normal)
{
	vec3 to_light = -light.direction;
	float attenuation = 1.0;
	float light_distance = LUMEN_DIRECTIONAL_SHADOW_DISTANCE;
	bool is_local = light.type != GPU_LIGHT_TYPE_DIRECTIONAL;
	if(is_local)
	{
		vec3 delta = light.position - position;
		vec3 over_range = delta / max(light.range, 1e-4);
		attenuation = light.type == GPU_LIGHT_TYPE_POINT
		                  ? GpuRadialAttenuation(over_range, light.falloff_exponent)
		                  : GpuRadialAttenuation(over_range, 1.0) *
		                        GpuSpotAttenuation(delta, light.direction, light.cos_inner, light.cos_outer);
		light_distance = length(delta);
		to_light = light_distance > 1e-6 ? delta / light_distance : vec3(0.0, 1.0, 0.0);
	}
	float n_dot_l = saturate(dot(normal, to_light));
	if(n_dot_l * attenuation <= 0.0)
	{
		return vec3_splat(0.0);
	}
	float visibility = is_local ? 1.0 : CloudShadow(position, to_light);
	BRANCH
	if(light.casts_shadows && visibility > 0.0)
	{
		visibility *= LumenShadowVisibility(position, normal, to_light, light_distance, is_local);
	}
	return light.color * (light.intensity * attenuation * n_dot_l * visibility);
}

/// Whether light @p index can light a point of the box (@p tile_min, @p tile_max): a directional light always, a
/// local one when the box lies within its range (its attenuation is 0 beyond).
bool LumenLightReachesTile(int index, vec3 tile_min, vec3 tile_max)
{
	uint base = uint(index) * uint(GPU_LIGHT_STRIDE);
	vec4 l0 = b_gpu_lights[base + 0u];
	if(int(l0.w) == GPU_LIGHT_TYPE_DIRECTIONAL)
	{
		return true;
	}
	vec3 nearest = clamp(l0.xyz, tile_min, tile_max);
	float range = b_gpu_lights[base + 1u].w;
	vec3 to_light = l0.xyz - nearest;
	return dot(to_light, to_light) <= range * range;
}

NUM_THREADS(8, 8, 1)
void main()
{
	int tile_index = int(u_lumen_card_lighting.x) + int(gl_WorkGroupID.x);
	if(float(gl_WorkGroupID.x) >= u_lumen_card_lighting.y)
	{
		return;
	}
	vec4 t0 = b_lumen_light_tiles[tile_index * LUMEN_LIGHT_TILE_STRIDE + 0];
	vec4 uv_rect = b_lumen_light_tiles[tile_index * LUMEN_LIGHT_TILE_STRIDE + 1];
	vec4 page = b_lumen_light_tiles[tile_index * LUMEN_LIGHT_TILE_STRIDE + 2];
	ivec2 texel = ivec2(t0.xy) + ivec2(gl_LocalInvocationID.xy);
	float depth = texelFetch(s_lumen_card_depth, texel, 0).x;
	bool covered = depth < 1.0;
	LumenCard card = LumenLoadCard(int(t0.z));
	vec2 card_uv = mix(uv_rect.xy, uv_rect.zw, (vec2(texel) - page.xy + 0.5) / page.zw);
	vec3 position = LumenCardWorldPosition(card, LumenCardLocalPosition(card, card_uv, min(depth, 1.0)));
	// The culling: barriers in uniform control flow (the light count is the dispatch's).
	bool culls = u_gpu_light_count > LUMEN_CARD_CULL_MIN_LIGHTS && u_gpu_light_count <= LUMEN_CARD_CULL_MAX_LIGHTS;
	BRANCH
	if(culls)
	{
		int local_index = int(gl_LocalInvocationID.y) * 8 + int(gl_LocalInvocationID.x);
		s_tile_min[local_index] = covered ? position : vec3_splat(1e30);
		s_tile_max[local_index] = covered ? position : vec3_splat(-1e30);
		if(local_index < LUMEN_CARD_CULL_WORDS)
		{
			s_tile_lights[local_index] = 0u;
		}
		barrier();
		for(int stride = LUMEN_CARD_TILE_TEXELS / 2; stride > 0; stride = stride / 2)
		{
			if(local_index < stride)
			{
				s_tile_min[local_index] = min(s_tile_min[local_index], s_tile_min[local_index + stride]);
				s_tile_max[local_index] = max(s_tile_max[local_index], s_tile_max[local_index + stride]);
			}
			barrier();
		}
		vec3 tile_min = s_tile_min[0];
		vec3 tile_max = s_tile_max[0];
		for(int i = local_index; i < u_gpu_light_count; i += LUMEN_CARD_TILE_TEXELS)
		{
			if(LumenLightReachesTile(i, tile_min, tile_max))
			{
				atomicOr(s_tile_lights[i / 32], 1u << uint(i - (i / 32) * 32));
			}
		}
		barrier();
	}
	BRANCH
	if(!covered)
	{
		imageStore(s_lumen_direct_out, texel, vec4_splat(0.0));
		imageStore(s_lumen_final_out, texel, vec4(0.0, 0.0, 0.0, 1.0));
		return;
	}
	vec3 normal = LumenDecodeCardNormal(texelFetch(s_lumen_card_normal, texel, 0).xy, card.axis_x, card.axis_y, card.axis_z);
	vec3 direct = vec3_splat(0.0);
	LOOP
	for(int i = 0; i < u_gpu_light_count; ++i)
	{
		BRANCH
		if(!culls || ((s_tile_lights[i / 32] >> uint(i - (i / 32) * 32)) & 1u) != 0u)
		{
			direct += LumenCardLightIrradiance(GpuLoadLight(i), position, normal);
		}
	}
	imageStore(s_lumen_direct_out, texel, vec4(direct, 0.0));
	vec3 final_lighting = LumenCombineFinalLighting(texelFetch(s_lumen_card_albedo, texel, 0).xyz,
	                                                texelFetch(s_lumen_card_emissive, texel, 0).xyz,
	                                                direct,
	                                                texelFetch(s_lumen_card_indirect, texel, 0).xyz);
	imageStore(s_lumen_final_out, texel, vec4(final_lighting, depth));
}
