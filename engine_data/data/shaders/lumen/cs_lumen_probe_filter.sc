/*
 * Lumen screen probe gather, spatial filter (UE 5.8 ScreenProbeFilterGatherTracesCS,
 * LumenScreenProbeFiltering.usf:292-506), dispatched LUMEN_FILTER_PASSES times with ping-pong atlases.
 * One group per probe, one thread per octahedral texel (LUMEN_PROBE_TRACE_RES^2).
 *
 * Each texel averages itself with the same texel of the four uniform probes beside the probe's uniform tile (an
 * adaptive probe's: the tile it was spawned in; adaptive probes are never neighbours), weighted by
 *  - position: exp2(-LUMEN_FILTER_POSITION_WEIGHT_SCALE (dz / z)^2) on the probes' view depths;
 *  - angle: 1 - angle / LUMEN_FILTER_MAX_HIT_ANGLE_DEGREES between this probe's ray and the direction
 *    from this probe to the neighbour's hit point, the neighbour's hit distance clamped to this
 *    probe's own so contact shadows survive: a neighbour whose ray saw a different point is dropped.
 * A texel no ray reached (hit distance < 0) is filled from its neighbours alone and is nobody's
 * neighbour. A probe whose rays saw moving surfaces (moving fraction above LUMEN_FILTER_MOVING_THRESHOLD) filters
 * strongly, as the temporal will shorten its history: eight more neighbours (the axes at 2, the diagonals) and no
 * angle weight (UE bStrongFilter, LumenScreenProbeFiltering.usf:423-483).
 *
 * The neighbours' records and positions are the same for every texel of a probe: the group loads them once.
 *
 * Reads and writes rgb = radiance, a = the composite's hit distance, passed through unchanged.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "lumen/lumen_common.sh"

SAMPLER2D(s_lumen_probe_radiance, 0);
SAMPLER2D(s_lumen_probe_records, 1);
IMAGE2D_WO(i_lumen_probe_filtered, rgba16f, 2);
/// x > 0.5 when the probe is disoccluded (cs_lumen_probe_generate_rays.sc): its neighbours count without
/// the angle test, blurring revealed content more while it has no history.
SAMPLER2D(s_lumen_screen_data, 3);
/// One texel per probe: the fraction of its rays that hit a moving surface (cs_lumen_probe_composite.sc).
SAMPLER2D(s_lumen_probe_moving, 4);

/// The uniform neighbours: 0-3 the 5-tap cross, 4-11 the strong filter's (the axes at 2, the diagonals).
ivec2 LumenFilterNeighbourOffset(int index)
{
	ivec2 cross_offset = index == 0 ? ivec2(-1, 0) : (index == 1 ? ivec2(1, 0) : (index == 2 ? ivec2(0, -1) : ivec2(0, 1)));
	int strong_index = index - 4;
	ivec2 axis_offset = 2 * (strong_index == 0 ? ivec2(-1, 0) :
	                         (strong_index == 1 ? ivec2(1, 0) : (strong_index == 2 ? ivec2(0, -1) : ivec2(0, 1))));
	int diagonal_index = index - 8;
	ivec2 diagonal_offset = diagonal_index == 0 ? ivec2(-1, 1) :
	                        (diagonal_index == 1 ? ivec2(1, 1) : (diagonal_index == 2 ? ivec2(-1, -1) : ivec2(1, -1)));
	return index < 4 ? cross_offset : (index < 8 ? axis_offset : diagonal_offset);
}

#define LUMEN_FILTER_NEIGHBOURS 12

/// The group's probe's neighbours (LumenFilterNeighbourOffset order): xyz = position, w = view depth (<= 0: none).
SHARED vec4 s_lumen_filter_neighbours[LUMEN_FILTER_NEIGHBOURS];

/// The texel being filtered: its octahedral texel, its probe's position and view depth, its ray direction, its own
/// hit distance (< 0: no sample) and whether neighbours skip the angle weight.
struct LumenFilterTexel
{
	ivec2 texel;
	vec3 position;
	float depth;
	vec3 direction;
	float own_hit;
	bool relaxed;
};

/// Neighbour @p index of the probe at uniform tile @p screen_tile: xyz = its position, w = its view depth; w = 0 off
/// the uniform probes or without a probe.
vec4 LumenLoadFilterNeighbour(ivec2 screen_tile, int index)
{
	ivec2 neighbour = screen_tile + LumenFilterNeighbourOffset(index);
	if(any(lessThan(neighbour, ivec2(0, 0))) || any(greaterThanEqual(neighbour, u_lumen_probe_count)))
	{
		return vec4_splat(0.0);
	}
	vec4 record = texelFetch(s_lumen_probe_records, neighbour, 0);
	return record.x > 0.0 ? vec4(LumenProbePosition(record), record.x) : vec4_splat(0.0);
}

/// Neighbour @p index's contribution to @p center at uniform tile @p screen_tile: rgb = radiance x weight, a = weight
/// (0 off the uniform probes, without a probe or without a sample).
vec4 LumenFilterNeighbour(LumenFilterTexel center, ivec2 screen_tile, int index)
{
	vec4 neighbour_probe = s_lumen_filter_neighbours[index];
	if(neighbour_probe.w <= 0.0)
	{
		return vec4_splat(0.0);
	}
	ivec2 neighbour = screen_tile + LumenFilterNeighbourOffset(index);
	vec4 neighbour_texel = texelFetch(s_lumen_probe_radiance, neighbour * LUMEN_PROBE_TRACE_RES + center.texel, 0);
	float neighbour_hit = neighbour_texel.w;
	if(neighbour_hit < 0.0)
	{
		return vec4_splat(0.0);
	}
	float relative_depth = (neighbour_probe.w - center.depth) / center.depth;
	float position_weight = exp2(-LUMEN_FILTER_POSITION_WEIGHT_SCALE * relative_depth * relative_depth);
	float angle_weight = 1.0;
	if(!center.relaxed)
	{
		if(center.own_hit >= 0.0)
		{
			neighbour_hit = min(neighbour_hit, center.own_hit);
		}
		vec3 to_hit = neighbour_probe.xyz + center.direction * neighbour_hit - center.position;
		float cos_angle = dot(to_hit, center.direction) / max(length(to_hit), 1e-6);
		angle_weight = 1.0 - saturate(acos(clamp(cos_angle, -1.0, 1.0)) / radians(LUMEN_FILTER_MAX_HIT_ANGLE_DEGREES));
	}
	float weight = position_weight * angle_weight;
	return vec4(neighbour_texel.xyz * weight, weight);
}

NUM_THREADS(LUMEN_PROBE_TRACE_RES, LUMEN_PROBE_TRACE_RES, 1)
void main()
{
	ivec2 tile = ivec2(gl_WorkGroupID.xy);
	ivec2 texel = ivec2(gl_LocalInvocationID.xy);
	ivec2 atlas_texel = tile * LUMEN_PROBE_TRACE_RES + texel;
	int index = texel.y * LUMEN_PROBE_TRACE_RES + texel.x;
	vec4 record = texelFetch(s_lumen_probe_records, tile, 0);
	vec4 own = texelFetch(s_lumen_probe_radiance, atlas_texel, 0);
	// The barrier stays in uniform flow control: a probe-less group loads nothing and returns after it.
	bool valid = record.x > 0.0;
	ivec2 screen_tile = valid ? LumenProbeScreenTile(tile, record) : ivec2(0, 0);
	if(index < LUMEN_FILTER_NEIGHBOURS)
	{
		s_lumen_filter_neighbours[index] = valid ? LumenLoadFilterNeighbour(screen_tile, index) : vec4_splat(0.0);
	}
	barrier();
	if(!valid)
	{
		imageStore(i_lumen_probe_filtered, atlas_texel, vec4(0.0, 0.0, 0.0, -1.0));
		return;
	}
	vec2 jitter = LumenProbeRayJitter(screen_tile, u_lumen_frame_mod);
	bool is_strong = texelFetch(s_lumen_probe_moving, tile, 0).x > LUMEN_FILTER_MOVING_THRESHOLD;
	LumenFilterTexel center;
	center.texel = texel;
	center.position = LumenProbePosition(record);
	center.depth = record.x;
	center.direction = LumenEquiAreaSphericalMapping((vec2(texel) + jitter) / float(LUMEN_PROBE_TRACE_RES));
	center.own_hit = own.w;
	center.relaxed = is_strong || (u_lumen_importance_sampling && texelFetch(s_lumen_screen_data, tile, 0).x > 0.5);
	vec4 total = own.w >= 0.0 ? vec4(own.xyz, 1.0) : vec4_splat(0.0);
	for(int i = 0; i < 4; ++i)
	{
		total += LumenFilterNeighbour(center, screen_tile, i);
	}
	BRANCH
	if(is_strong)
	{
		for(int i = 4; i < LUMEN_FILTER_NEIGHBOURS; ++i)
		{
			total += LumenFilterNeighbour(center, screen_tile, i);
		}
	}
	vec3 filtered = total.w > 0.0 ? total.xyz / total.w : vec3_splat(0.0);
	imageStore(i_lumen_probe_filtered, atlas_texel, vec4(filtered, own.w));
}
