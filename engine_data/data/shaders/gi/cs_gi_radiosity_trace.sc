/*
 * Surface cache radiosity, trace: per scheduled card tile, groups of GI_RADIOSITY_GROUP_THREADS over its probes'
 * rays (gi_radiosity_common.sh), one thread per ray, the group's y the block of the tile's rays. Each ray leaves
 * its probe 5 cm off the surface and along the ray, marches the global SDF from 10 cm to 200 m, and takes the cards'
 * final lighting at the hit through the object grid - faded to zero within two voxel extents of the start (the
 * self-lighting guard) - or the sky on a miss. Rays brighter than the max ray intensity are scaled down. That reads
 * the cache as earlier updates left it: each update adds one more bounce.
 */

#include "bgfx_compute.sh"
#include "gi/gi_image_formats.sh"
#include "../common.sh"
#include "../lighting.sh"
#include "../sampling.sh"
#include "gi/gi_pass_constants.sh"
/// The global SDF's coverage (gi/sdf_clipmap.sh).
#define SDF_CLIPMAP_COVERAGE_STAGE 10
#define SDF_CLIPMAP_MIP_STAGE 9
#include "gi/gi_global_sdf.sh"

IMAGE2D_WO(s_gi_radiosity_trace_out, rg11b10f, 0);
SAMPLER2D(s_gi_card_depth, 1);
SAMPLER2D(s_gi_card_normal, 2);
SAMPLER2D(s_gi_env_sh, 3);
BUFFER_RO(b_gi_light_tiles, vec4, 5);
#define GI_TILE_RECORDS_LIGHT_TILES
#include "gi/gi_tile_records.sh"
BUFFER_RO(b_gi_scene, vec4, 6);
SAMPLER2D(s_gi_card_final, 7);
SAMPLER3D(s_gi_object_grid, 8);

#define GI_SURFACE_CACHE_OBJECT_GRID
#include "gi/gi_surface_cache.sh"
#include "gi/gi_surface_cache_lighting.sh"
#include "gi/gi_radiosity_common.sh"

/// x = first tile of this dispatch, y = tile count, z = the float4 the tile words start at, w = the frame index.
uniform vec4 u_gi_card_lighting;

NUM_THREADS(GI_RADIOSITY_GROUP_THREADS, 1, 1)
void main()
{
	int tile_index = int(u_gi_card_lighting.x) + int(gl_WorkGroupID.x);
	if(float(gl_WorkGroupID.x) >= u_gi_card_lighting.y)
	{
		return;
	}
	int spacing = u_gi_radiosity_spacing;
	int resolution = u_gi_radiosity_resolution;
	int probes_per_axis = GiRadiosityProbesPerTileAxis();
	int rays_per_probe = resolution * resolution;
	int ray = int(gl_WorkGroupID.y) * GI_RADIOSITY_GROUP_THREADS + int(gl_LocalInvocationIndex);
	if(ray >= probes_per_axis * probes_per_axis * rays_per_probe)
	{
		return;
	}
	int probe = ray / rays_per_probe;
	ivec2 trace_texel = GiRadiosityGridCoord(ray - probe * rays_per_probe, resolution);
	GiLightTile light_tile = GiLoadLightTile(tile_index, int(u_gi_card_lighting.z));
	vec4 t0 = light_tile.t0;
	vec4 uv_rect = light_tile.uv_rect;
	vec4 page = light_tile.page;
	ivec2 cell_origin = ivec2(t0.xy) + GiRadiosityGridCoord(probe, probes_per_axis) * spacing;
	ivec2 probe_texel = cell_origin + GiRadiosityJitter(t0.w);
	ivec2 out_texel = GiRadiosityTraceTexel(cell_origin / spacing, trace_texel);
	float depth = texelFetch(s_gi_card_depth, probe_texel, 0).x;
	BRANCH
	if(depth >= 1.0)
	{
		imageStore(s_gi_radiosity_trace_out, out_texel, vec4_splat(0.0));
		return;
	}
	GiCard card = GiLoadCard(int(t0.z));
	vec2 card_uv = mix(uv_rect.xy, uv_rect.zw, (vec2(probe_texel) - page.xy + 0.5) / page.zw);
	vec3 position = GiCardWorldPosition(card, GiCardLocalPosition(card, card_uv, depth));
	vec3 normal =
	    GiDecodeCardNormal(texelFetch(s_gi_card_normal, probe_texel, 0).xy, card.axis_x, card.axis_y, card.axis_z);
	vec3 direction = GiRadiosityRayDirection(normal, cell_origin / spacing, trace_texel, t0.w);
	vec3 origin = position + normal * GI_RADIOSITY_SURFACE_BIAS + direction * GI_RADIOSITY_SURFACE_BIAS;
	GiSdfHit hit = GiTraceGlobalSdf(origin, direction, GI_RADIOSITY_MIN_TRACE_DISTANCE,
	                                      GI_RADIOSITY_MAX_TRACE_DISTANCE, true);
	vec3 radiance = vec3_splat(0.0);
	BRANCH
	if(hit.hit)
	{
		vec3 surface = origin + direction * (hit.t + hit.hit_field);
		vec3 surface_normal = dot(hit.normal, direction) > 0.0 ? -hit.normal : hit.normal;
		float voxel_extent = 0.5 * hit.voxel;
		vec4 cards = GiSampleGlobalSdfHit(surface, surface_normal, voxel_extent, s_gi_card_final);
		if(cards.w > 0.0)
		{
			radiance = cards.xyz / cards.w * smoothstep(1.5 * voxel_extent, 2.0 * voxel_extent, hit.t);
		}
	}
	else
	{
		radiance = eval_radiance_sh(s_gi_env_sh, direction);
	}
	float brightest = max(radiance.x, max(radiance.y, radiance.z));
	if(brightest > u_gi_radiosity_max_ray_intensity)
	{
		radiance *= u_gi_radiosity_max_ray_intensity / brightest;
	}
	imageStore(s_gi_radiosity_trace_out, out_texel, vec4(GiQuantizeCardLighting(radiance, out_texel, u_gi_card_lighting.w), 0.0));
}
