#ifndef __VIEW_ORIGIN_SH__
#define __VIEW_ORIGIN_SH__

/*
 * The world position the pass's view draws around (gfx::set_view_origin, a per-view uniform). The view's matrices
 * (u_view, u_viewProj and their inverses) take offsets from it, so the GPU works with numbers the size of the scene
 * around the camera wherever the camera is. A view that never sets it draws around the world origin (0), where an
 * offset is the absolute position.
 *
 * Absolute positions (object matrices, instance rows, skinning palettes, light and probe positions) cross into the
 * view's space by subtracting the origin from a point: never by a matrix product on an absolute position, which would
 * form the large number first and keep only its rounding.
 */

/// The view's origin in world space (xyz; w unused).
uniform vec4 u_view_origin;

/// The offset from the view origin of local position @p p under the local-to-world matrix @p m: the rotation and
/// scale first, then the translation minus the origin.
vec3 mulWorldOffset(mat4 m, vec3 p)
{
	return mul(m, vec4(p, 0.0)).xyz + (mul(m, vec4(0.0, 0.0, 0.0, 1.0)).xyz - u_view_origin.xyz);
}

/// The absolute world position @p world as an offset from the view origin.
vec3 worldToViewOffset(vec3 world)
{
	return world - u_view_origin.xyz;
}

/// The absolute world position of @p offset, an offset from the view origin.
vec3 viewOffsetToWorld(vec3 offset)
{
	return offset + u_view_origin.xyz;
}

#endif // __VIEW_ORIGIN_SH__
