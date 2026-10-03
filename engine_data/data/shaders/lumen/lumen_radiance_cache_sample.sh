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
 * The includer declares b_lumen_rc_indirection (uint), s_lumen_rc_final (bordered radiance atlas, bilinear)
 * and s_lumen_rc_depth (hit distances) and includes lumen_common.sh and lumen_radiance_cache_common.sh.
 */

struct LumenRcSample
{
	/// Cached lighting (GI_CACHED_LIGHTING_PRE_EXPOSURE, gi_pre_exposure.sh).
	vec3 radiance;
	/// The probes' interpolated hit distance along the lookup directions.
	float hit_distance;
};

/// The screen ray's hand-off distance for a probe in @p clipmap: TMin plus the cell diagonal, so every probe
/// a lookup interpolates has its unsampled ball inside the part of the ray the screen probe traced itself.
float LumenRcHandOffDistance(int clipmap)
{
	return LumenRcTMin(clipmap) + 1.7320508 * LumenRcCellSize(clipmap);
}

LumenRcSample LumenRcSampleInterpolated(vec3 origin, vec3 direction, int clipmap)
{
	LumenRcSample result;
	result.radiance = vec3_splat(0.0);
	result.hit_distance = 0.0;
	vec4 clip = u_lumen_rc_clipmaps[clipmap];
	vec3 lattice = (origin - clip.xyz) / clip.w - 0.5;
	vec3 base_float = floor(lattice);
	ivec3 base = ivec3(base_float);
	vec3 fraction = lattice - base_float;
	float radius = LUMEN_RADIANCE_CACHE_REPROJECTION_RADIUS * LumenRcTMin(clipmap);
	vec2 final_size = vec2(textureSize(s_lumen_rc_final, 0));
	for(int corner = 0; corner < 8; ++corner)
	{
		ivec3 step_xyz = ivec3(corner & 1, (corner >> 1) & 1, corner >> 2);
		vec3 axis_weight = mix(vec3_splat(1.0) - fraction, fraction, vec3(step_xyz));
		float weight = axis_weight.x * axis_weight.y * axis_weight.z;
		ivec3 cell = base + step_xyz;
		uint probe = b_lumen_rc_indirection[LumenRcIndirectionIndex(cell, clipmap)];
		if(probe == LUMEN_RC_INVALID || probe == LUMEN_RC_USED)
		{
			continue;
		}
		vec3 to_origin = origin - LumenRcProbePosition(cell, clipmap);
		float b = dot(to_origin, direction);
		float c = dot(to_origin, to_origin) - radius * radius;
		float exit_t = -b + sqrt(max(b * b - c, 0.0));
		vec3 lookup = to_origin + direction * exit_t;
		float parallax = exit_t * exit_t / (radius * max(dot(lookup, direction), 1e-4));
		vec2 uv = LumenInverseEquiAreaSphericalMapping(lookup);
		vec2 final_texel = vec2(LumenRcProbeTileOrigin(probe, LUMEN_RC_FINAL_RES)) + 1.0 +
		                   uv * float(LUMEN_RADIANCE_CACHE_PROBE_RES);
		result.radiance += weight * parallax * texture2DLod(s_lumen_rc_final, final_texel / final_size, 0.0).xyz;
		ivec2 depth_texel = LumenRcProbeTileOrigin(probe, LUMEN_RADIANCE_CACHE_PROBE_RES) +
		                    min(ivec2(uv * float(LUMEN_RADIANCE_CACHE_PROBE_RES)),
		                        ivec2(LUMEN_RADIANCE_CACHE_PROBE_RES - 1, LUMEN_RADIANCE_CACHE_PROBE_RES - 1));
		result.hit_distance += weight * texelFetch(s_lumen_rc_depth, depth_texel, 0).x;
	}
	return result;
}

#endif // __LUMEN_RADIANCE_CACHE_SAMPLE_SH__
