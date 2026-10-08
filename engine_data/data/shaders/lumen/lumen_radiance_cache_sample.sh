#ifndef __LUMEN_RADIANCE_CACHE_SAMPLE_SH__
#define __LUMEN_RADIANCE_CACHE_SAMPLE_SH__

/*
 * The radiance cache read of the screen-probe hand-off (UE 5.8 SampleRadianceCacheInterpolated,
 * LumenRadianceCacheInterpolation.ush:332-497): radiance along a ray from @p origin, interpolated from the
 * eight lattice probes around the ORIGIN with plain trilinear weights. Each probe is read in the direction
 * from itself to where the ray leaves a sphere of LUMEN_RADIANCE_CACHE_REPROJECTION_RADIUS x TMin around it
 * (parallax), scaled by T^2 / (R^2 cos) so the blend stays energy-preserving to second order. An
 * unallocated probe contributes black at its weight; there is no visibility test and no renormalisation.
 *
 * The includer declares b_lumen_rc_indirection (uint) and s_lumen_rc_final (bordered radiance atlas, bilinear; alpha =
 * the source texel's hit distance) and includes lumen_common.sh and lumen_radiance_cache_common.sh.
 */

struct LumenRcSample
{
	/// Cached lighting (GI_CACHED_LIGHTING_PRE_EXPOSURE, gi_pre_exposure.sh).
	vec3 radiance;
	/// The probes' interpolated hit distance along the lookup directions.
	float hit_distance;
};

/// One of the eight lattice probes a lookup from @p origin interpolates (the half of the lookup that does not depend on
/// the direction): xyz = origin - the probe's position, w = its trilinear weight (0: skipped - no weight, or no
/// allocated probe); the second vec4's xy = its bordered tile's first interior texel in s_lumen_rc_final.
struct LumenRcCorner
{
	vec4 offset_weight;
	vec4 tile;
};

LumenRcCorner LumenRcInterpolationCorner(vec3 origin, int clipmap, int corner)
{
	LumenRcCorner result;
	result.offset_weight = vec4_splat(0.0);
	result.tile = vec4_splat(0.0);
	vec4 clip = u_lumen_rc_clipmaps[clipmap];
	vec3 lattice = (origin - clip.xyz) / clip.w - 0.5;
	vec3 base_float = floor(lattice);
	vec3 fraction = lattice - base_float;
	ivec3 step_xyz = ivec3(corner & 1, (corner >> 1) & 1, corner >> 2);
	vec3 axis_weight = mix(vec3_splat(1.0) - fraction, fraction, vec3(step_xyz));
	float weight = axis_weight.x * axis_weight.y * axis_weight.z;
	ivec3 cell = ivec3(base_float) + step_xyz;
	uint probe = b_lumen_rc_indirection[LumenRcIndirectionIndex(cell, clipmap)];
	if(weight <= 0.0 || probe == LUMEN_RC_INVALID || probe == LUMEN_RC_USED)
	{
		return result;
	}
	result.offset_weight = vec4(origin - LumenRcProbePosition(cell, clipmap), weight);
	result.tile = vec4(vec2(LumenRcProbeTileOrigin(probe, u_lumen_rc_final_res) + ivec2(1, 1)), 0.0, 0.0);
	return result;
}

/// One corner's contribution along @p direction: its probe read where the ray leaves the parallax sphere of
/// @p radius around it, at its weight.
LumenRcSample LumenRcShadeCorner(LumenRcCorner corner, vec3 direction, float radius, vec2 final_size)
{
	LumenRcSample result;
	result.radiance = vec3_splat(0.0);
	result.hit_distance = 0.0;
	float weight = corner.offset_weight.w;
	if(weight <= 0.0)
	{
		return result;
	}
	vec3 to_origin = corner.offset_weight.xyz;
	float b = dot(to_origin, direction);
	float c = dot(to_origin, to_origin) - radius * radius;
	float exit_t = -b + sqrt(max(b * b - c, 0.0));
	vec3 lookup = to_origin + direction * exit_t;
	float parallax = exit_t * exit_t / (radius * max(dot(lookup, direction), 1e-4));
	vec2 uv = LumenInverseEquiAreaSphericalMapping(lookup);
	ivec2 final_origin = ivec2(corner.tile.xy);
	vec2 final_texel = vec2(final_origin) + uv * float(u_lumen_rc_probe_res);
	result.radiance = weight * parallax * texture2DLod(s_lumen_rc_final, final_texel / final_size, 0.0).xyz;
	ivec2 depth_texel = final_origin + min(ivec2(uv * float(u_lumen_rc_probe_res)),
	                                       ivec2(u_lumen_rc_probe_res - 1, u_lumen_rc_probe_res - 1));
	result.hit_distance = weight * texelFetch(s_lumen_rc_final, depth_texel, 0).w;
	return result;
}

/// The parallax sphere radius of a lookup in @p clipmap.
float LumenRcReprojectionRadius(int clipmap)
{
	return LUMEN_RADIANCE_CACHE_REPROJECTION_RADIUS * LumenRcTMin(clipmap);
}

LumenRcSample LumenRcSampleInterpolated(vec3 origin, vec3 direction, int clipmap)
{
	LumenRcSample result;
	result.radiance = vec3_splat(0.0);
	result.hit_distance = 0.0;
	float radius = LumenRcReprojectionRadius(clipmap);
	vec2 final_size = vec2(textureSize(s_lumen_rc_final, 0));
	for(int corner = 0; corner < 8; ++corner)
	{
		LumenRcSample shaded =
		    LumenRcShadeCorner(LumenRcInterpolationCorner(origin, clipmap, corner), direction, radius, final_size);
		result.radiance += shaded.radiance;
		result.hit_distance += shaded.hit_distance;
	}
	return result;
}

#endif // __LUMEN_RADIANCE_CACHE_SAMPLE_SH__
