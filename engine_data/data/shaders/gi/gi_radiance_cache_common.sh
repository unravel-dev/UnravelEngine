#ifndef __GI_RADIANCE_CACHE_COMMON_SH__
#define __GI_RADIANCE_CACHE_COMMON_SH__

/*
 * The radiance cache: sparse world-space radiance probes on GI_RADIANCE_CACHE_CLIPMAPS camera-centred clipmaps of
 * GI_RADIANCE_CACHE_GRID^3 cells, cell 0 = 2 x GI_RADIANCE_CACHE_EXTENT / GRID, doubling per clipmap.
 * A probe sits at its cell's lattice point (a multiple of the cell size, world-locked) and holds an
 * equal-area octahedral radiance map of u_gi_rc_probe_res^2 texels plus their hit distances.
 *
 * Buffers (uint everywhere, no float bit patterns that a typed store could flush):
 *  - indirection: one entry per cell of every clipmap, x-major with the clipmaps side by side along x:
 *    GI_RC_INVALID, GI_RC_USED (marked this frame, no probe yet) or the probe index;
 *  - probe state: LastUsed[p], LastTraced[p], FreeList[i] (three arrays of MAX_PROBES);
 *  - counters: see GI_RC_COUNTER_*;
 *  - trace list: two words per traced probe: cell + clipmap + force-downsample, probe index;
 *  - tile list: two words per 8x8 trace tile: tile + level, trace index.
 * Stored radiance is cached lighting (GI_CACHED_LIGHTING_PRE_EXPOSURE, gi_pre_exposure.sh).
 */

#include "gi/gi_pass_constants.sh"

#define GI_RC_INVALID 0xFFFFFFFFu
#define GI_RC_USED    0xFFFFFFFEu
#define GI_RC_INDIRECTION_X (GI_RADIANCE_CACHE_GRID * GI_RADIANCE_CACHE_CLIPMAPS)
#define GI_RC_INDIRECTION_SIZE (GI_RC_INDIRECTION_X * GI_RADIANCE_CACHE_GRID * GI_RADIANCE_CACHE_GRID)
/// Directions per trace tile edge (one 8x8 group per tile).
#define GI_RC_TILE_RES 8
/// Trace tiles per probe at the normal level of the largest probe resolution (32): (R / 2) << 1 directions per axis.
#define GI_RC_MAX_TILES_PER_PROBE 16

#define GI_RC_COUNTER_FREE_LIST        0
#define GI_RC_COUNTER_PROBES           1
#define GI_RC_COUNTER_TRACES           2
#define GI_RC_COUNTER_TILES            3
#define GI_RC_COUNTER_NEW_PROBE_COST   4
#define GI_RC_COUNTER_SPENT            5
#define GI_RC_COUNTER_SPENT_LAST       6
#define GI_RC_COUNTER_MAX_BUCKET       7
#define GI_RC_COUNTER_REMAINDER        8
#define GI_RC_COUNTER_TRACE_COUNT      9
#define GI_RC_COUNTER_HISTOGRAM        16
#define GI_RC_COUNTER_COUNT            32

#define GI_RC_STATE_LAST_USED   0
#define GI_RC_STATE_LAST_TRACED GI_RADIANCE_CACHE_MAX_PROBES
#define GI_RC_STATE_FREE_LIST   (2 * GI_RADIANCE_CACHE_MAX_PROBES)

/// Per clipmap: xyz = world position of cell (0, 0, 0)'s corner, w = cell size.
uniform vec4 u_gi_rc_clipmaps[GI_RADIANCE_CACHE_CLIPMAPS];
/// Last frame's clipmaps, for the carry-over.
uniform vec4 u_gi_rc_prev_clipmaps[GI_RADIANCE_CACHE_CLIPMAPS];
/// x = cache frame (>= 1), y = trace budget in cost units, z = 1 when the cache persists from last frame,
/// w = the mode of the single-thread bookkeeping pass.
uniform vec4 u_gi_rc_params;
/// xyz = camera position.
uniform vec4 u_gi_rc_camera;
/// x = the probes' radiance map resolution in texels per axis, the quality tier's (GI_RADIANCE_CACHE_PROBE_RES at
/// the epic tier, 16 at high; a multiple of 2 x GI_RC_TILE_RES).
uniform vec4 u_gi_rc_layout;

#define u_gi_rc_frame       uint(u_gi_rc_params.x)
#define u_gi_rc_budget      uint(u_gi_rc_params.y)
#define u_gi_rc_persistent  (u_gi_rc_params.z > 0.0)
#define u_gi_rc_mode        int(u_gi_rc_params.w)
#define u_gi_rc_probe_res   int(u_gi_rc_layout.x)
/// Texels of a probe tile in the final atlas: the radiance map plus a one-texel octahedral border.
#define u_gi_rc_final_res   (u_gi_rc_probe_res + 2)

/// Probes one frame may trace: GI_RADIANCE_CACHE_MAX_TRACES while the cache continues; every probe of the pool on a
/// frame that rebuilds it, so a full update traces the whole atlas at once.
#define u_gi_rc_trace_cap (u_gi_rc_persistent ? uint(GI_RADIANCE_CACHE_MAX_TRACES) : uint(GI_RADIANCE_CACHE_MAX_PROBES))
/// Groups per row of the 2D trace dispatch: a rebuild traces up to GI_RADIANCE_CACHE_MAX_PROBES x 16 tiles, past
/// one dispatch dimension's limit.
#define GI_RC_TRACE_DISPATCH_WIDTH 128u

float GiRcCellSize(int clipmap)
{
	return u_gi_rc_clipmaps[clipmap].w;
}

/// The probe rays' start distance and the screen-probe hand-off's blind ball: the cell diagonal.
float GiRcTMin(int clipmap)
{
	return 1.7320508 * GiRcCellSize(clipmap);
}

/// The screen ray's hand-off distance for a probe in @p clipmap: TMin plus the cell diagonal, so every probe
/// a lookup interpolates has its unsampled ball inside the part of the ray the screen probe traced itself.
float GiRcHandOffDistance(int clipmap)
{
	return GiRcTMin(clipmap) + 1.7320508 * GiRcCellSize(clipmap);
}

vec3 GiRcProbePosition(ivec3 cell, int clipmap)
{
	vec4 clip = u_gi_rc_clipmaps[clipmap];
	return clip.xyz + (vec3(cell) + 0.5) * clip.w;
}

uint GiRcIndirectionIndex(ivec3 cell, int clipmap)
{
	return uint(cell.x + clipmap * GI_RADIANCE_CACHE_GRID) +
	       uint(GI_RC_INDIRECTION_X) * uint(cell.y + GI_RADIANCE_CACHE_GRID * cell.z);
}

/// Inverse of GiRcIndirectionIndex: xyz = cell, w = clipmap.
ivec4 GiRcIndirectionCell(uint index)
{
	int x = int(index % uint(GI_RC_INDIRECTION_X));
	int rest = int(index / uint(GI_RC_INDIRECTION_X));
	int clipmap = x / GI_RADIANCE_CACHE_GRID;
	return ivec4(x - clipmap * GI_RADIANCE_CACHE_GRID,
	             rest % GI_RADIANCE_CACHE_GRID,
	             rest / GI_RADIANCE_CACHE_GRID,
	             clipmap);
}

/// The finest clipmap whose interpolation lattice covers @p position: every axis strictly inside half a
/// cell of the grid's face (no dither across clipmap borders), or GI_RADIANCE_CACHE_CLIPMAPS.
int GiRcClipmapOf(vec3 position)
{
	for(int clipmap = 0; clipmap < GI_RADIANCE_CACHE_CLIPMAPS; ++clipmap)
	{
		vec4 clip = u_gi_rc_clipmaps[clipmap];
		vec3 f = (position - clip.xyz) / clip.w;
		if(all(greaterThan(f, vec3_splat(0.5))) && all(lessThan(f, vec3_splat(float(GI_RADIANCE_CACHE_GRID) - 0.5))))
		{
			return clipmap;
		}
	}
	return GI_RADIANCE_CACHE_CLIPMAPS;
}

/// The cell whose lattice point is the (0, 0, 0) corner of @p position's interpolation cube.
ivec3 GiRcInterpolationBase(vec3 position, int clipmap)
{
	vec4 clip = u_gi_rc_clipmaps[clipmap];
	return ivec3(floor((position - clip.xyz) / clip.w - 0.5));
}

/// Texel origin of probe @p probe's tile in an atlas of @p tile_res texels per probe.
ivec2 GiRcProbeTileOrigin(uint probe, int tile_res)
{
	return ivec2(int(probe % uint(GI_RADIANCE_CACHE_ATLAS_PROBES_X)),
	             int(probe / uint(GI_RADIANCE_CACHE_ATLAS_PROBES_X))) * tile_res;
}

/// Trace cost in units (1 unit = (R / 2)^2 rays) of the probe at @p position.
uint GiRcTraceCost(vec3 position)
{
	vec3 to_camera = position - u_gi_rc_camera.xyz;
	float limit = GI_RADIANCE_CACHE_DOWNSAMPLE_DISTANCE;
	return dot(to_camera, to_camera) >= limit * limit ? uint(GI_RADIANCE_CACHE_COST_DOWNSAMPLED)
	                                                   : uint(GI_RADIANCE_CACHE_COST_NORMAL);
}

/// Update priority: 0 = never traced, then 15 - log2(frames since the last trace / (clipmap + 1)), so
/// 15 = traced since its last use and far clipmaps age (clipmap + 1) times slower.
uint GiRcPriorityBucket(uint last_traced, uint last_used, int clipmap)
{
	if(last_traced == 0u)
	{
		return 0u;
	}
	uint age = last_used > last_traced ? last_used - last_traced : 1u;
	return uint(15.0 - clamp(log2(float(age) / float(clipmap + 1)), 0.0, 14.0));
}

uint GiRcPackTrace(ivec3 cell, int clipmap, bool force_downsample)
{
	return uint(cell.x) | (uint(cell.y) << 6u) | (uint(cell.z) << 12u) | (uint(clipmap) << 18u) |
	       (force_downsample ? (1u << 20u) : 0u);
}

/// xyz = cell, w = clipmap.
ivec4 GiRcUnpackTrace(uint packed)
{
	return ivec4(int(packed & 63u), int((packed >> 6u) & 63u), int((packed >> 12u) & 63u), int((packed >> 18u) & 3u));
}

bool GiRcTraceForceDownsample(uint packed)
{
	return (packed & (1u << 20u)) != 0u;
}

#endif // __GI_RADIANCE_CACHE_COMMON_SH__
