/*
 * Radiance cache probe trace (UE 5.8 TraceFromProbesCS + TraceForProbeTexel, LumenRadianceCache.usf:779-985):
 * one 8x8 group per trace tile. Each probe direction is an equal-area texel centre (fixed, no jitter); the
 * ray marches the global SDF from TMin (the cell diagonal) with the surface expansion ramping in over the
 * distance the ray keeps from surfaces, with dithered transparency where only two-sided meshes are near
 * (lumen_global_sdf.sh; UE LumenRadianceCache.usf:874, the screen probes' cache computes no irradiance). A hit reads
 * the surface cache through the object grid, black when no card covers it or the ray starts inside geometry; a miss
 * reads the sky. A probe traced at a quarter of the rays fills
 * 2x2 texels per ray. Writes the persistent source atlas (cached lighting) and the hit-distance atlas.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
// eval_radiance_sh.
#include "../lighting.sh"
#include "lumen/lumen_common.sh"
/// The global SDF's coverage (gi/sdf_clipmap.sh).
#define SDF_CLIPMAP_COVERAGE_STAGE 10
#include "lumen/lumen_global_sdf.sh"
#include "lumen/lumen_radiance_cache_common.sh"
#include "gi/gi_constants.sh"

BUFFER_RO(b_lumen_rc_traces, uint, 0);
BUFFER_RO(b_lumen_rc_tiles, uint, 1);
/// The environment SH (9 texels), absolute radiance.
SAMPLER2D(s_lumen_env_sh, 3);
IMAGE2D_WO(s_lumen_rc_radiance, rgba16f, 5);
IMAGE2D_WO(s_lumen_rc_depth, r16f, 6);

/// The surface cache (lumen_surface_cache.sh): global-SDF hits read the cards' final lighting through the
/// object grid when u_lumen_hit_lighting.x > 0.5 (the cache is lit), black otherwise.
BUFFER_RO(b_lumen_scene, vec4, 13);
SAMPLER2D(s_lumen_card_final, 14);
SAMPLER3D(s_lumen_object_grid, 15);
#define LUMEN_SURFACE_CACHE_OBJECT_GRID
#include "lumen/lumen_surface_cache.sh"
uniform vec4 u_lumen_hit_lighting;
#define u_lumen_hits_read_surface_cache (u_lumen_hit_lighting.x > 0.5)

/// One probe ray's cached-lighting radiance and hit distance (LUMEN_RADIANCE_CACHE_NO_HIT on a miss).
vec4 LumenRcTraceRay(vec3 origin, vec3 direction, float t_min, LumenSdfDither dither)
{
	LumenSdfHit hit =
	    LumenTraceGlobalSdfDithered(origin, direction, t_min, u_lumen_max_trace_distance, false, 0.0, 0.0, 1.0, dither);
	if(!hit.hit)
	{
		vec3 sky = eval_radiance_sh(s_lumen_env_sh, direction) * GI_CACHED_LIGHTING_PRE_EXPOSURE;
		return vec4(sky, LUMEN_RADIANCE_CACHE_NO_HIT);
	}
	vec3 radiance = vec3_splat(0.0);
	if(hit.t > t_min)
	{
		// The march stops short of the surface by its expansion; the surface cache is a surface store.
		vec3 surface = origin + direction * (hit.t + hit.hit_field);
		vec3 surface_normal = dot(hit.normal, direction) > 0.0 ? -hit.normal : hit.normal;
		BRANCH
		if(u_lumen_hits_read_surface_cache)
		{
			vec4 cards = LumenSampleGlobalSdfHit(surface, surface_normal, 0.5 * hit.voxel, s_lumen_card_final);
			if(cards.w > 0.0)
			{
				radiance = cards.xyz / cards.w;
			}
		}
	}
	return vec4(radiance, hit.t);
}

NUM_THREADS(8, 8, 1)
void main()
{
	uint tile_index = gl_WorkGroupID.x;
	uint tile_word = b_lumen_rc_tiles[2u * tile_index];
	uint trace = b_lumen_rc_tiles[2u * tile_index + 1u];
	ivec2 tile = ivec2(int(tile_word & 255u), int((tile_word >> 8u) & 255u));
	uint level = (tile_word >> 16u) & 255u;
	ivec4 cell = LumenRcUnpackTrace(b_lumen_rc_traces[2u * trace]);
	uint probe = b_lumen_rc_traces[2u * trace + 1u];
	int directions = (LUMEN_RADIANCE_CACHE_PROBE_RES / 2) << int(level);
	ivec2 texel = tile * LUMEN_RC_TILE_RES + ivec2(gl_LocalInvocationID.xy);
	vec3 direction = LumenEquiAreaSphericalMapping((vec2(texel) + 0.5) / float(directions));
	ivec2 origin = LumenRcProbeTileOrigin(probe, LUMEN_RADIANCE_CACHE_PROBE_RES);
	int scale = LUMEN_RADIANCE_CACHE_PROBE_RES / directions;
	// UE DitherScreenCoord: the probe's atlas tile x its resolution + the ray's texel.
	LumenSdfDither dither = LumenSdfMakeDither(vec2(origin + texel * scale), u_lumen_frame_mod);
	vec4 ray = LumenRcTraceRay(LumenRcProbePosition(cell.xyz, cell.w), direction, LumenRcTMin(cell.w), dither);
	for(int y = 0; y < scale; ++y)
	{
		for(int x = 0; x < scale; ++x)
		{
			ivec2 target = origin + texel * scale + ivec2(x, y);
			imageStore(s_lumen_rc_radiance, target, vec4(ray.xyz, 1.0));
			imageStore(s_lumen_rc_depth, target, vec4_splat(ray.w));
		}
	}
}
