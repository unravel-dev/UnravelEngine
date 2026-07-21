using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{

    /// <summary>
    /// Represents a camera component that allows interaction with the camera, such as converting screen space positions to rays in 3D space.
    /// </summary>
    public class CameraComponent : Component
    {
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
        private static extern bool internal_m2n_camera_screen_point_to_ray(Entity eid, Vector2 pos, out Ray ray);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_camera_screen_point_to_world_2d(Entity eid, Vector2 pos);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_camera_screen_point_to_world(Entity eid, Vector3 pos);
    }
}

