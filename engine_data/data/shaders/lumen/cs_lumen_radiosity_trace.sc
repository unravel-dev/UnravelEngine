/*
 * Lumen surface cache radiosity, trace (UE 5.8 LumenRadiosity.usf:81-170): one 8x8 group per scheduled card
 * tile, 2 x 2 probes x 16 rays. Each ray leaves its probe 5 cm off the surface and along the ray, marches the
 * global SDF from 10 cm to 200 m, and takes the cards' final lighting at the hit through the object grid -
 * faded to zero within two voxel extents of the start (the self-lighting guard) - or the sky on a miss. Rays
 * brighter than MaxRayIntensity are scaled down. That reads the cache as earlier updates left it: each update
 * adds one more bounce.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "../lighting.sh"
#include "../sampling.sh"
#include "lumen/lumen_constants.sh"
/// The global SDF's coverage (gi/sdf_clipmap.sh).
#define SDF_CLIPMAP_COVERAGE_STAGE 10
#include "lumen/lumen_global_sdf.sh"

IMAGE2D_WO(s_lumen_radiosity_trace_out, rgba16f, 0);
SAMPLER2D(s_lumen_card_depth, 1);
SAMPLER2D(s_lumen_card_normal, 2);
SAMPLER2D(s_lumen_env_sh, 3);
BUFFER_RO(b_lumen_light_tiles, vec4, 5);
BUFFER_RO(b_lumen_scene, vec4, 6);
SAMPLER2D(s_lumen_card_final, 7);
SAMPLER3D(s_lumen_object_grid, 8);

#define LUMEN_SURFACE_CACHE_OBJECT_GRID
#include "lumen/lumen_surface_cache.sh"
#include "lumen/lumen_radiosity_common.sh"

/// x = first tile of this dispatch, y = tile count.
uniform vec4 u_lumen_card_lighting;
/// x = the ray clamp in cached units (MaxRayIntensity / view pre-exposure).
uniform vec4 u_lumen_radiosity;

NUM_THREADS(8, 8, 1)
void main()
{
	int tile_index = int(u_lumen_card_lighting.x) + int(gl_WorkGroupID.x);
	if(float(gl_WorkGroupID.x) >= u_lumen_card_lighting.y)
	{
		return;
	}
	vec4 t0 = b_lumen_light_tiles[tile_index * LUMEN_RADIOSITY_TILE_STRIDE + 0];
	vec4 uv_rect = b_lumen_light_tiles[tile_index * LUMEN_RADIOSITY_TILE_STRIDE + 1];
	vec4 page = b_lumen_light_tiles[tile_index * LUMEN_RADIOSITY_TILE_STRIDE + 2];
	ivec2 local = ivec2(gl_LocalInvocationID.xy);
	ivec2 trace_texel = local % LUMEN_RADIOSITY_RAYS_PER_AXIS;
	ivec2 cell_origin = ivec2(t0.xy) + (local / LUMEN_RADIOSITY_PROBE_SPACING) * LUMEN_RADIOSITY_PROBE_SPACING;
	ivec2 probe_texel = cell_origin + LumenRadiosityJitter(t0.w);
	ivec2 out_texel = cell_origin + trace_texel;
	float depth = texelFetch(s_lumen_card_depth, probe_texel, 0).x;
	BRANCH
	if(depth >= 1.0)
	{
		imageStore(s_lumen_radiosity_trace_out, out_texel, vec4_splat(0.0));
		return;
	}
	LumenCard card = LumenLoadCard(int(t0.z));
	vec2 card_uv = mix(uv_rect.xy, uv_rect.zw, (vec2(probe_texel) - page.xy + 0.5) / page.zw);
	vec3 position = LumenCardWorldPosition(card, LumenCardLocalPosition(card, card_uv, depth));
	vec3 normal = decodeNormalOctahedron(texelFetch(s_lumen_card_normal, probe_texel, 0).xy);
	vec3 direction = LumenRadiosityRayDirection(normal, cell_origin / LUMEN_RADIOSITY_PROBE_SPACING, trace_texel, t0.w);
	vec3 origin = position + normal * LUMEN_RADIOSITY_SURFACE_BIAS + direction * LUMEN_RADIOSITY_SURFACE_BIAS;
	LumenSdfHit hit = LumenTraceGlobalSdf(origin, direction, LUMEN_RADIOSITY_MIN_TRACE_DISTANCE,
	                                      LUMEN_RADIOSITY_MAX_TRACE_DISTANCE, true);
	vec3 radiance = vec3_splat(0.0);
	BRANCH
	if(hit.hit)
	{
		vec3 surface = origin + direction * (hit.t + hit.hit_field);
		vec3 surface_normal = dot(hit.normal, direction) > 0.0 ? -hit.normal : hit.normal;
		float voxel_extent = 0.5 * hit.voxel;
		vec4 cards = LumenSampleGlobalSdfHit(surface, surface_normal, voxel_extent, s_lumen_card_final);
		if(cards.w > 0.0)
		{
			radiance = cards.xyz / cards.w * smoothstep(1.5 * voxel_extent, 2.0 * voxel_extent, hit.t);
		}
	}
	else
	{
		radiance = eval_radiance_sh(s_lumen_env_sh, direction);
	}
	float brightest = max(radiance.x, max(radiance.y, radiance.z));
	if(brightest > u_lumen_radiosity.x)
	{
		radiance *= u_lumen_radiosity.x / brightest;
	}
	imageStore(s_lumen_radiosity_trace_out, out_texel, vec4(radiance, 0.0));
}
