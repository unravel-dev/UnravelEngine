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

/// Last frame's TAA-unjittered view projection; the inverse of its camera-relative form (rotation-only view, inverted
/// in double on the CPU); last frame's camera position (xyz; w unused). Positions rebuilt from last frame's depth go
/// through the relative inverse and add the camera position once: a fused absolute inverse carries the camera position
/// in every element, and the divide by w scales their rounding with the distance.
uniform mat4 u_gi_prev_view_proj;
uniform mat4 u_gi_prev_inv_view_proj_relative;
uniform vec4 u_gi_prev_camera_position;
/// x > 0 when s_gi_velocity holds this frame's velocity buffer, y > 0 when the probe trace marks screen hits on
/// surfaces moving against the probe (the fast update); z, w unused.
uniform vec4 u_gi_motion;

#define u_gi_has_velocity (u_gi_motion.x > 0.0)
#define u_gi_fast_update     (u_gi_motion.y > 0.0)

#ifdef GI_VELOCITY_STAGE
SAMPLER2D(s_gi_velocity, GI_VELOCITY_STAGE);
#endif

/// The offset from last frame's camera of last frame's clip position @p clip, and through @p clip_w that point's clip w
/// (its view depth under a perspective projection): the inverse maps clip / w back to (offset, 1) / w.
vec3 GiPrevOffsetFromClip(vec3 clip, out float clip_w)
{
	vec4 offset = mul(u_gi_prev_inv_view_proj_relative, vec4(clip, 1.0));
	clip_w = 1.0 / offset.w;
	return offset.xyz / offset.w;
}

/// The world position of last frame's clip position @p clip.
vec3 GiPrevWorldFromClip(vec3 clip)
{
	float clip_w;
	return u_gi_prev_camera_position.xyz + GiPrevOffsetFromClip(clip, clip_w);
}

/// The world point at view depth @p view_depth on last frame's camera ray through screen @p uv; @p fallback when the
/// projection has no depth along its rays (orthographic).
vec3 GiPrevWorldFromViewDepth(vec2 uv, float view_depth, vec3 fallback)
{
	vec2 clip = clipTransform(vec3(uv * 2.0 - 1.0, 0.0)).xy;
	float near_depth;
	float far_depth;
	vec3 near_point = GiPrevOffsetFromClip(vec3(clip, DEPTH_NEAR), near_depth);
	vec3 far_point = GiPrevOffsetFromClip(vec3(clip, 0.5), far_depth);
	float span = far_depth - near_depth;
	if(abs(span) < 1e-6)
	{
		return fallback;
	}
	return u_gi_prev_camera_position.xyz + mix(near_point, far_point, (view_depth - near_depth) / span);
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
