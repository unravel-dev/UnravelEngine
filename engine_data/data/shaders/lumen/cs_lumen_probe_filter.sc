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
 * neighbour.
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

/// The four uniform neighbours of the 5-tap cross.
ivec2 LumenFilterNeighbourOffset(int index)
{
	return index == 0 ? ivec2(-1, 0) : (index == 1 ? ivec2(1, 0) : (index == 2 ? ivec2(0, -1) : ivec2(0, 1)));
}

NUM_THREADS(LUMEN_PROBE_TRACE_RES, LUMEN_PROBE_TRACE_RES, 1)
void main()
{
	ivec2 tile = ivec2(gl_WorkGroupID.xy);
	ivec2 texel = ivec2(gl_LocalInvocationID.xy);
	ivec2 atlas_texel = tile * LUMEN_PROBE_TRACE_RES + texel;
	vec4 record = texelFetch(s_lumen_probe_records, tile, 0);
	vec4 own = texelFetch(s_lumen_probe_radiance, atlas_texel, 0);
	if(record.x <= 0.0)
	{
		imageStore(i_lumen_probe_filtered, atlas_texel, vec4(0.0, 0.0, 0.0, -1.0));
		return;
	}
	float depth = record.x;
	vec3 position = LumenProbePosition(record);
	ivec2 screen_tile = LumenProbeScreenTile(tile, record);
	vec2 jitter = LumenProbeRayJitter(screen_tile, u_lumen_frame_mod);
	vec3 direction = LumenEquiAreaSphericalMapping((vec2(texel) + jitter) / float(LUMEN_PROBE_TRACE_RES));
	float own_hit = own.w;
	bool has_own = own_hit >= 0.0;
	bool relaxed = u_lumen_importance_sampling && texelFetch(s_lumen_screen_data, tile, 0).x > 0.5;
	vec3 sum = has_own ? own.xyz : vec3_splat(0.0);
	float weight_sum = has_own ? 1.0 : 0.0;
	float max_angle = radians(LUMEN_FILTER_MAX_HIT_ANGLE_DEGREES);
	for(int i = 0; i < 4; ++i)
	{
		ivec2 neighbour = screen_tile + LumenFilterNeighbourOffset(i);
		if(any(lessThan(neighbour, ivec2(0, 0))) || any(greaterThanEqual(neighbour, u_lumen_probe_count)))
		{
			continue;
		}
		vec4 neighbour_record = texelFetch(s_lumen_probe_records, neighbour, 0);
		if(neighbour_record.x <= 0.0)
		{
			continue;
		}
		vec4 neighbour_texel = texelFetch(s_lumen_probe_radiance, neighbour * LUMEN_PROBE_TRACE_RES + texel, 0);
		float neighbour_hit = neighbour_texel.w;
		if(neighbour_hit < 0.0)
		{
			continue;
		}
		float relative_depth = (neighbour_record.x - depth) / depth;
		float position_weight = exp2(-LUMEN_FILTER_POSITION_WEIGHT_SCALE * relative_depth * relative_depth);
		float angle_weight = 1.0;
		if(!relaxed)
		{
			if(has_own)
			{
				neighbour_hit = min(neighbour_hit, own_hit);
			}
			vec3 to_hit = LumenProbePosition(neighbour_record) + direction * neighbour_hit - position;
			float cos_angle = dot(to_hit, direction) / max(length(to_hit), 1e-6);
			angle_weight = 1.0 - saturate(acos(clamp(cos_angle, -1.0, 1.0)) / max_angle);
		}
		float weight = position_weight * angle_weight;
		sum += neighbour_texel.xyz * weight;
		weight_sum += weight;
	}
	vec3 filtered = weight_sum > 0.0 ? sum / weight_sum : vec3_splat(0.0);
	imageStore(i_lumen_probe_filtered, atlas_texel, vec4(filtered, own_hit));
}
