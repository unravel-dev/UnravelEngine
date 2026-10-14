#ifndef __GI_MOTION_SH__
#define __GI_MOTION_SH__

/*
 * Where the surface seen at a pixel was last frame, for the GI's reprojections. A pixel the velocity pass drew with
 * object motion (velocity_encoding.sh B > 0) follows the velocity buffer: last frame's position is the point at last
 * frame's view depth (A) on last frame's camera ray through uv - RG. Every other surface did not move and reprojects
 * through last frame's view projection alone.
 *
 * The includer defines GI_VELOCITY_STAGE to bind the velocity buffer (s_gi_velocity) at that stage and
 * includes gi_common.sh; without the stage every surface counts as static.
 */

#include "../velocity/velocity_encoding.sh"

/// Last frame's TAA-unjittered view projection and its inverse.
uniform mat4 u_gi_prev_view_proj;
uniform mat4 u_gi_prev_inv_view_proj;
/// x > 0 when s_gi_velocity holds this frame's velocity buffer, y > 0 when the probe trace marks screen hits on
/// surfaces moving against the probe (the fast update); z, w unused.
uniform vec4 u_gi_motion;

#define u_gi_has_velocity (u_gi_motion.x > 0.0)
#define u_gi_fast_update     (u_gi_motion.y > 0.0)

#ifdef GI_VELOCITY_STAGE
SAMPLER2D(s_gi_velocity, GI_VELOCITY_STAGE);
#endif

/// The world point at view depth @p view_depth on last frame's camera ray through screen @p uv; @p fallback when the
/// projection has no depth along its rays (orthographic).
vec3 GiPrevWorldFromViewDepth(vec2 uv, float view_depth, vec3 fallback)
{
	vec2 clip = clipTransform(vec3(uv * 2.0 - 1.0, 0.0)).xy;
	vec3 near_point = clipToWorld(u_gi_prev_inv_view_proj, vec3(clip, toClipSpaceDepth(0.0)));
	vec3 far_point = clipToWorld(u_gi_prev_inv_view_proj, vec3(clip, toClipSpaceDepth(0.5)));
	float near_depth = mul(u_gi_prev_view_proj, vec4(near_point, 1.0)).w;
	float far_depth = mul(u_gi_prev_view_proj, vec4(far_point, 1.0)).w;
	float span = far_depth - near_depth;
	if(abs(span) < 1e-6)
	{
		return fallback;
	}
	return mix(near_point, far_point, (view_depth - near_depth) / span);
}

/// Last frame's world position of the surface point @p world seen at screen @p uv this frame: @p world itself
/// unless the velocity buffer marks the pixel with object motion.
vec3 GiPrevWorldPosition(vec2 uv, vec3 world)
{
#ifdef GI_VELOCITY_STAGE
	BRANCH
	if(u_gi_has_velocity)
	{
		ivec2 size = textureSize(s_gi_velocity, 0);
		ivec2 texel = clamp(ivec2(uv * vec2(size)), ivec2(0, 0), size - ivec2(1, 1));
		vec4 velocity = texelFetch(s_gi_velocity, texel, 0);
		float prev_depth = VelocityPrevViewDepth(velocity);
		BRANCH
		if(VelocityObjectMotionPixels(velocity) > 0.0 && prev_depth > 0.0)
		{
			return GiPrevWorldFromViewDepth(uv - velocity.xy, prev_depth, world);
		}
	}
#endif
	return world;
}

#endif // __GI_MOTION_SH__
