using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Camera projection mode.
    /// </summary>
    public enum ProjectionMode : uint
    {
        /// <summary>Perspective projection using field of view.</summary>
        Perspective = 0,
        /// <summary>Orthographic projection using orthographic size.</summary>
        Orthographic = 1,
    }

    /// <summary>
    /// Camera component for viewport projection and screen-space queries.
    /// </summary>
    public class CameraComponent : Component
    {
        /// <summary>
        /// Vertical field of view in degrees (perspective mode).
        /// </summary>
        public float fov
        {
            get => internal_m2n_camera_get_fov(owner);
            set => internal_m2n_camera_set_fov(owner, value);
        }

        /// <summary>
        /// Near clipping plane distance.
        /// </summary>
        public float nearClipPlane
        {
            get => internal_m2n_camera_get_near_clip(owner);
            set => internal_m2n_camera_set_near_clip(owner, value);
        }

        /// <summary>
        /// Far clipping plane distance.
        /// </summary>
        public float farClipPlane
        {
            get => internal_m2n_camera_get_far_clip(owner);
            set => internal_m2n_camera_set_far_clip(owner, value);
        }

        /// <summary>
        /// Projection mode (perspective or orthographic).
        /// </summary>
        public ProjectionMode projectionMode
        {
            get => (ProjectionMode)internal_m2n_camera_get_projection_mode(owner);
            set => internal_m2n_camera_set_projection_mode(owner, (uint)value);
        }

        /// <summary>
        /// Orthographic size (orthographic mode).
        /// </summary>
        public float orthographicSize
        {
            get => internal_m2n_camera_get_ortho_size(owner);
            set => internal_m2n_camera_set_ortho_size(owner, value);
        }

        /// <summary>
        /// Layers included when this camera renders.
        /// </summary>
        public LayerMask cullingMask
        {
            get
            {
                LayerMask mask = default;
                mask.value = internal_m2n_camera_get_include_mask(owner);
                return mask;
            }
            set => internal_m2n_camera_set_include_mask(owner, value);
        }

        /// <summary>
        /// Layers excluded when this camera renders.
        /// </summary>
        public LayerMask excludeMask
        {
            get
            {
                LayerMask mask = default;
                mask.value = internal_m2n_camera_get_exclude_mask(owner);
                return mask;
            }
            set => internal_m2n_camera_set_exclude_mask(owner, value);
        }

        /// <summary>
        /// Size in pixels of the viewport this camera renders, in the same space as
        /// <see cref="Input.mousePosition"/>. Each camera has its own; the host that shows the
        /// game sizes its cameras to <see cref="Screen.width"/> x <see cref="Screen.height"/>.
        /// Until then it holds the size saved with the scene.
        /// </summary>
        public Vector2 viewportSize
        {
            get => internal_m2n_camera_get_viewport_size(owner);
        }

        /// <summary>
        /// Width divided by height of the camera's projection (perspective mode uses it for the
        /// horizontal field of view). Follows <see cref="viewportSize"/>.
        /// </summary>
        public float aspect
        {
            get => internal_m2n_camera_get_aspect(owner);
        }

        /// <summary>
        /// Converts a position in screen space to a ray in 3D space.
        /// </summary>
        /// <param name="pos">The position in screen space, typically in pixel coordinates.</param>
        /// <param name="ray">When this method returns, contains the ray in 3D space corresponding to the screen space position.</param>
        /// <returns>
        /// <c>true</c> if the ray was successfully calculated; otherwise, <c>false</c>
        /// (also while the camera has no viewport size yet).
        /// </returns>
        public bool ScreenPointToRay(Vector2 pos, out Ray ray)
        {
            return internal_m2n_camera_screen_point_to_ray(owner, pos, out ray);
        }

        /// <summary>
        /// Converts a 2D screen-space position to a world-space point on the camera's near plane.
        /// </summary>
        /// <param name="pos">The position in screen space, typically in pixel coordinates.</param>
        /// <returns>The corresponding world-space position.</returns>
        public Vector3 ScreenPointToWorld(Vector2 pos)
        {
            return internal_m2n_camera_screen_point_to_world_2d(owner, pos);
        }

        /// <summary>
        /// Converts a screen position with a depth to a world-space point; the inverse of
        /// <see cref="WorldToScreenPoint"/>.
        /// </summary>
        /// <param name="pos">x, y in pixels (the space of <see cref="Input.mousePosition"/>); z is the
        /// distance in front of the camera along its view direction, in world units.</param>
        /// <returns>The corresponding world-space position.</returns>
        public Vector3 ScreenPointToWorld(Vector3 pos)
        {
            return internal_m2n_camera_screen_point_to_world(owner, pos);
        }

        /// <summary>
        /// Projects a world-space point onto the screen. Use it to place UI over world objects
        /// (a screen-space UI document uses the same pixels) or to test what is on screen.
        /// </summary>
        /// <param name="world">The world-space position.</param>
        /// <returns>x, y in pixels from the top left (the space of <see cref="Input.mousePosition"/>);
        /// z is the distance in front of the camera in world units. When z is 0 or less the point is
        /// behind the camera and x, y are meaningless.</returns>
        public Vector3 WorldToScreenPoint(Vector3 world)
        {
            return internal_m2n_camera_world_to_screen_point(owner, world);
        }

        /// <summary>
        /// Projects a world-space point into the viewport: (0, 0) top left, (1, 1) bottom right.
        /// </summary>
        /// <param name="world">The world-space position.</param>
        /// <returns>x, y in viewport units; z is the distance in front of the camera in world units
        /// (0 or less: behind the camera, x and y meaningless). The point is on screen when x and y
        /// are within 0..1 and z is positive.</returns>
        public Vector3 WorldToViewportPoint(Vector3 world)
        {
            return internal_m2n_camera_world_to_viewport_point(owner, world);
        }

        /// <summary>
        /// Converts a viewport position with a depth to a world-space point; the inverse of
        /// <see cref="WorldToViewportPoint"/>.
        /// </summary>
        /// <param name="viewport">x, y with (0, 0) top left and (1, 1) bottom right; z is the distance
        /// in front of the camera in world units.</param>
        public Vector3 ViewportToWorldPoint(Vector3 viewport)
        {
            return internal_m2n_camera_viewport_to_world_point(owner, viewport);
        }

        /// <summary>
        /// A ray from the camera through a viewport position ((0, 0) top left, (1, 1) bottom right).
        /// </summary>
        /// <returns><c>false</c> while the camera has no viewport size yet.</returns>
        public bool ViewportPointToRay(Vector2 viewport, out Ray ray)
        {
            return internal_m2n_camera_viewport_point_to_ray(owner, viewport, out ray);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_camera_get_fov(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_camera_set_fov(Entity eid, float fov);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_camera_get_near_clip(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_camera_set_near_clip(Entity eid, float distance);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_camera_get_far_clip(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_camera_set_far_clip(Entity eid, float distance);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern uint internal_m2n_camera_get_projection_mode(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_camera_set_projection_mode(Entity eid, uint mode);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_camera_get_ortho_size(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_camera_set_ortho_size(Entity eid, float size);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_camera_get_include_mask(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_camera_set_include_mask(Entity eid, int mask);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_camera_get_exclude_mask(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_camera_set_exclude_mask(Entity eid, int mask);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector2 internal_m2n_camera_get_viewport_size(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_camera_get_aspect(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_camera_screen_point_to_ray(Entity eid, Vector2 pos, out Ray ray);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_camera_screen_point_to_world_2d(Entity eid, Vector2 pos);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_camera_screen_point_to_world(Entity eid, Vector3 pos);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_camera_viewport_point_to_ray(Entity eid, Vector2 viewport, out Ray ray);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_camera_world_to_screen_point(Entity eid, Vector3 world);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_camera_world_to_viewport_point(Entity eid, Vector3 world);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_camera_viewport_to_world_point(Entity eid, Vector3 viewport);
    }
}
