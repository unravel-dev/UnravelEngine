#ifndef FOG_COMPOSITE_SH_HEADER_GUARD
#define FOG_COMPOSITE_SH_HEADER_GUARD

/*
 * Height fog over the lit scene (fog_pass, fog.sh): writes (pre-exposed in-scatter, transmittance), blended
 * ONE / SRC_ALPHA, so the frame becomes in-scatter + frame x transmittance. Runs after the sky and the clouds and
 * before the particles, which fog themselves. fs_fog.sc is the analytic fog alone; fs_fog_volumetric.sc reads the
 * froxel column in front of it (FOG_VOLUME_SAMPLER).
 */

#include "../common.sh"

SAMPLER2D(s_fog_depth, 0);
// The view's environment SH (IRRADIANCE_SH): the sky the fog scatters, and in texel 9 the GPU's share of the
// pre-exposure.
SAMPLER2D(s_fog_environment, 1);
#ifdef FOG_VOLUME_SAMPLER
// The integrated froxel column (cs_fog_integrate.sc).
SAMPLER3D(s_fog_volume, 2);
#endif
#define PRE_EXPOSURE_ENVIRONMENT s_fog_environment
#include "../pre_exposure.sh"
#include "fog.sh"

/// Device depth of the sky: the depth clear, which the sky pass draws at.
#define FOG_SKY_DEPTH 1.0

void main()
{
	vec2 uv = gl_FragCoord.xy * u_viewTexel.xy;
	float device_depth = texture2DLod(s_fog_depth, uv, 0.0).x;
	// The view is camera-relative (fog_pass::run): the reconstructed position is the pixel's offset from the camera.
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(device_depth)));
	vec3 offset = clipToWorld(u_invViewProj, clip);
	float point_distance = length(offset);
	vec3 view_direction = offset / max(point_distance, 1e-6);
	gl_FragColor = fog_to_point(s_fog_environment, uv, view_direction, point_distance, device_depth >= FOG_SKY_DEPTH, u_pre_exposure_value);
}

#endif // FOG_COMPOSITE_SH_HEADER_GUARD
