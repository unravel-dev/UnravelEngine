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
        /// Converts a position in screen space to a ray in 3D space.
        /// </summary>
        /// <param name="pos">The position in screen space, typically in pixel coordinates.</param>
        /// <param name="ray">When this method returns, contains the ray in 3D space corresponding to the screen space position.</param>
        /// <returns>
        /// <c>true</c> if the ray was successfully calculated; otherwise, <c>false</c>.
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
        /// Converts a screen-space position with depth (z) to a world-space point.
        /// </summary>
        /// <param name="pos">The position in screen space; z is depth.</param>
        /// <returns>The corresponding world-space position.</returns>
        public Vector3 ScreenPointToWorld(Vector3 pos)
        {
            return internal_m2n_camera_screen_point_to_world(owner, pos);
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
        private static extern bool internal_m2n_camera_screen_point_to_ray(Entity eid, Vector2 pos, out Ray ray);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_camera_screen_point_to_world_2d(Entity eid, Vector2 pos);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_camera_screen_point_to_world(Entity eid, Vector3 pos);
    }
}
