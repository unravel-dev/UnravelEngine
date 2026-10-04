/*
 * Radiance cache spatial filter + final atlas (UE 5.8 FilterProbeRadianceWithGatherCS,
 * LumenRadianceCache.usf:991-1125, and FixupBordersAndGenerateMipsCS, :1392-1519, merged): for every texel of
 * a traced probe's bordered final tile, the source texel it wraps to (the border mirrors across the
 * octahedron's folds) is averaged with the same texel of the six face-neighbour probes. A neighbour counts
 * only when each probe sees the other's ray point at 2 TMin (no wall between them), weighted by
 * 1 - angle / LUMEN_RADIANCE_CACHE_FILTER_MAX_ANGLE between this probe's direction and the direction from
 * this probe to the neighbour's hit (its hit distance clamped to this probe's own, keeping contact
 * occlusion): only the angularly consistent far field is shared. Neighbours are read as last traced.
 * Dispatch: (ceil(FINAL_RES / 8), ceil(FINAL_RES / 8), traces).
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "lumen/lumen_common.sh"
#include "lumen/lumen_radiance_cache_common.sh"

SAMPLER2D(s_lumen_rc_radiance, 0);
SAMPLER2D(s_lumen_rc_depth, 1);
BUFFER_RO(b_lumen_rc_indirection, uint, 2);
BUFFER_RO(b_lumen_rc_traces, uint, 3);
IMAGE2D_WO(i_lumen_rc_final, rgba16f, 5);

/// The probe-map texel (nearest) a direction falls in.
ivec2 LumenRcDirectionTexel(vec3 direction)
{
	vec2 uv = LumenInverseEquiAreaSphericalMapping(direction);
	return min(ivec2(uv * float(LUMEN_RADIANCE_CACHE_PROBE_RES)), ivec2(LUMEN_RADIANCE_CACHE_PROBE_RES - 1, LUMEN_RADIANCE_CACHE_PROBE_RES - 1));
}

/// Whether the probe whose depth tile starts at @p depth_origin sees the point @p offset away from it.
bool LumenRcSees(ivec2 depth_origin, vec3 offset)
{
	float distance_to_point = length(offset);
	float depth = texelFetch(s_lumen_rc_depth, depth_origin + LumenRcDirectionTexel(offset / max(distance_to_point, 1e-6)), 0).x;
	return depth >= distance_to_point;
}

ivec3 LumenRcFaceNeighbour(int index)
{
	int axis = index >> 1;
	int sign_step = (index & 1) == 0 ? -1 : 1;
	return ivec3(axis == 0 ? sign_step : 0, axis == 1 ? sign_step : 0, axis == 2 ? sign_step : 0);
}

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 final_texel = ivec2(gl_GlobalInvocationID.xy);
	if(final_texel.x >= LUMEN_RC_FINAL_RES || final_texel.y >= LUMEN_RC_FINAL_RES)
	{
		return;
	}
	uint trace = gl_WorkGroupID.z;
	ivec4 cell = LumenRcUnpackTrace(b_lumen_rc_traces[2u * trace]);
	uint probe = b_lumen_rc_traces[2u * trace + 1u];
	int clipmap = cell.w;
	ivec2 texel = LumenOctahedralMapWrapBorder(final_texel, LUMEN_RC_FINAL_RES, 1);
	ivec2 origin = LumenRcProbeTileOrigin(probe, LUMEN_RADIANCE_CACHE_PROBE_RES);
	vec3 sum = texelFetch(s_lumen_rc_radiance, origin + texel, 0).xyz;
	float own_depth = texelFetch(s_lumen_rc_depth, origin + texel, 0).x;
	float weight_sum = 1.0;
	vec3 direction = LumenEquiAreaSphericalMapping((vec2(texel) + 0.5) / float(LUMEN_RADIANCE_CACHE_PROBE_RES));
	vec3 position = LumenRcProbePosition(cell.xyz, clipmap);
	float test_offset = 2.0 * LumenRcTMin(clipmap);
	for(int i = 0; i < 6; ++i)
	{
		ivec3 neighbour_cell = cell.xyz + LumenRcFaceNeighbour(i);
		if(any(lessThan(neighbour_cell, ivec3(0, 0, 0))) ||
		   any(greaterThanEqual(neighbour_cell, ivec3(LUMEN_RADIANCE_CACHE_GRID, LUMEN_RADIANCE_CACHE_GRID, LUMEN_RADIANCE_CACHE_GRID))))
		{
			continue;
		}
		uint neighbour = b_lumen_rc_indirection[LumenRcIndirectionIndex(neighbour_cell, clipmap)];
		if(neighbour == LUMEN_RC_INVALID || neighbour == LUMEN_RC_USED)
		{
			continue;
		}
		ivec2 neighbour_origin = LumenRcProbeTileOrigin(neighbour, LUMEN_RADIANCE_CACHE_PROBE_RES);
		vec3 neighbour_position = LumenRcProbePosition(neighbour_cell, clipmap);
		if(!LumenRcSees(origin, neighbour_position + test_offset * direction - position) ||
		   !LumenRcSees(neighbour_origin, position + test_offset * direction - neighbour_position))
		{
			continue;
		}
		float neighbour_depth = texelFetch(s_lumen_rc_depth, neighbour_origin + texel, 0).x;
		if(neighbour_depth < LUMEN_RADIANCE_CACHE_NO_HIT)
		{
			neighbour_depth = min(neighbour_depth, own_depth);
		}
		vec3 to_hit = neighbour_position + direction * neighbour_depth - position;
		float cos_angle = dot(to_hit, direction) / max(length(to_hit), 1e-6);
		float weight = 1.0 - saturate(acos(clamp(cos_angle, -1.0, 1.0)) / LUMEN_RADIANCE_CACHE_FILTER_MAX_ANGLE);
		sum += weight * texelFetch(s_lumen_rc_radiance, neighbour_origin + texel, 0).xyz;
		weight_sum += weight;
	}
	ivec2 final_origin = LumenRcProbeTileOrigin(probe, LUMEN_RC_FINAL_RES);
	imageStore(i_lumen_rc_final, final_origin + final_texel, vec4(sum / weight_sum, 1.0));
}
