/*
 * Radiance cache spatial filter + final atlas, merged in one pass: for every texel of
 * a traced probe's bordered final tile, the source texel it wraps to (the border mirrors across the
 * octahedron's folds) is averaged with the same texel of the six face-neighbour probes. A neighbour counts
 * only when each probe sees the other's ray point at 2 TMin (no wall between them), weighted by
 * 1 - angle / GI_RADIANCE_CACHE_FILTER_MAX_ANGLE between this probe's direction and the direction from
 * this probe to the neighbour's hit (its hit distance clamped to this probe's own, keeping contact
 * occlusion): only the angularly consistent far field is shared. Neighbours are read as last traced. The
 * alpha carries the source texel's own hit distance, which the hand-off reads from the tile it samples.
 * Dispatch: (ceil(FINAL_RES / 8), ceil(FINAL_RES / 8), traces).
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "gi/gi_common.sh"
#include "gi/gi_radiance_cache_common.sh"

/// The traced probes: rgb = radiance, a = hit distance.
SAMPLER2D(s_gi_rc_radiance, 0);
BUFFER_RO(b_gi_rc_indirection, uint, 2);
BUFFER_RO(b_gi_rc_traces, uint, 3);
IMAGE2D_WO(i_gi_rc_final, rgba16f, 5);

/// The probe-map texel (nearest) a direction falls in.
ivec2 GiRcDirectionTexel(vec3 direction)
{
	vec2 uv = GiInverseEquiAreaSphericalMapping(direction);
	int last = u_gi_rc_probe_res - 1;
	return min(ivec2(uv * float(u_gi_rc_probe_res)), ivec2(last, last));
}

/// Whether the probe whose depth tile starts at @p depth_origin sees the point @p offset away from it.
bool GiRcSees(ivec2 depth_origin, vec3 offset)
{
	float distance_to_point = length(offset);
	float depth = texelFetch(s_gi_rc_radiance, depth_origin + GiRcDirectionTexel(offset / max(distance_to_point, 1e-6)), 0).w;
	return depth >= distance_to_point;
}

ivec3 GiRcFaceNeighbour(int index)
{
	int axis = index >> 1;
	int sign_step = (index & 1) == 0 ? -1 : 1;
	return ivec3(axis == 0 ? sign_step : 0, axis == 1 ? sign_step : 0, axis == 2 ? sign_step : 0);
}

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 final_texel = ivec2(gl_GlobalInvocationID.xy);
	if(final_texel.x >= u_gi_rc_final_res || final_texel.y >= u_gi_rc_final_res)
	{
		return;
	}
	uint trace = gl_WorkGroupID.z;
	ivec4 cell = GiRcUnpackTrace(b_gi_rc_traces[2u * trace]);
	uint probe = b_gi_rc_traces[2u * trace + 1u];
	int clipmap = cell.w;
	ivec2 texel = GiOctahedralMapWrapBorder(final_texel, u_gi_rc_final_res, 1);
	ivec2 origin = GiRcProbeTileOrigin(probe, u_gi_rc_probe_res);
	vec4 own = texelFetch(s_gi_rc_radiance, origin + texel, 0);
	vec3 sum = own.xyz;
	float own_depth = own.w;
	float weight_sum = 1.0;
	vec3 direction = GiEquiAreaSphericalMapping((vec2(texel) + 0.5) / float(u_gi_rc_probe_res));
	vec3 position = GiRcProbePosition(cell.xyz, clipmap);
	float test_offset = 2.0 * GiRcTMin(clipmap);
	for(int i = 0; i < 6; ++i)
	{
		ivec3 neighbour_cell = cell.xyz + GiRcFaceNeighbour(i);
		if(any(lessThan(neighbour_cell, ivec3(0, 0, 0))) ||
		   any(greaterThanEqual(neighbour_cell, ivec3(GI_RADIANCE_CACHE_GRID, GI_RADIANCE_CACHE_GRID, GI_RADIANCE_CACHE_GRID))))
		{
			continue;
		}
		uint neighbour = b_gi_rc_indirection[GiRcIndirectionIndex(neighbour_cell, clipmap)];
		if(neighbour == GI_RC_INVALID || neighbour == GI_RC_USED)
		{
			continue;
		}
		ivec2 neighbour_origin = GiRcProbeTileOrigin(neighbour, u_gi_rc_probe_res);
		vec3 neighbour_position = GiRcProbePosition(neighbour_cell, clipmap);
		if(!GiRcSees(origin, neighbour_position + test_offset * direction - position) ||
		   !GiRcSees(neighbour_origin, position + test_offset * direction - neighbour_position))
		{
			continue;
		}
		vec4 neighbour_ray = texelFetch(s_gi_rc_radiance, neighbour_origin + texel, 0);
		float neighbour_depth = neighbour_ray.w;
		if(neighbour_depth < GI_RADIANCE_CACHE_NO_HIT)
		{
			neighbour_depth = min(neighbour_depth, own_depth);
		}
		vec3 to_hit = neighbour_position + direction * neighbour_depth - position;
		float cos_angle = dot(to_hit, direction) / max(length(to_hit), 1e-6);
		float weight = 1.0 - saturate(acos(clamp(cos_angle, -1.0, 1.0)) / GI_RADIANCE_CACHE_FILTER_MAX_ANGLE);
		BRANCH
		if(weight > 0.0)
		{
			sum += weight * neighbour_ray.xyz;
			weight_sum += weight;
		}
	}
	ivec2 final_origin = GiRcProbeTileOrigin(probe, u_gi_rc_final_res);
	imageStore(i_gi_rc_final, final_origin + final_texel, vec4(sum / weight_sum, own_depth));
}
