#ifndef __VELOCITY_ENCODING_SH__
#define __VELOCITY_ENCODING_SH__

/*
 * The velocity buffer (RGBA16F, full camera resolution), written by fs_velocity_camera.sc for every pixel and
 * fs_velocity.sc over movers:
 *   RG = total motion in uv: uv_curr - uv_prev; consumers reproject as prev_uv = uv - RG.
 *   B  = the object-only part of the motion in pixels: |uv_prev(current pose) - uv_prev(previous pose)| x the
 *        target size, 0 for every pixel the camera alone moved; consumers classify object motion from it.
 *   A  = last frame's view depth of the pixel's surface (the previous clip w; UE's velocity z): the expected
 *        history depth of a moving surface. 0 when the surface was behind last frame's camera.
 */

/// The object-only motion of a velocity texel in pixels.
float VelocityObjectMotionPixels(vec4 velocity)
{
	return velocity.z;
}

/// Last frame's view depth of a velocity texel's surface.
float VelocityPrevViewDepth(vec4 velocity)
{
	return velocity.w;
}

#endif // __VELOCITY_ENCODING_SH__
