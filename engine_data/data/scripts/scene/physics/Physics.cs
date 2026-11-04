using System;
using System.Globalization;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Unravel.Core
{

    /// <summary>
    /// Provides static methods for performing physics-related operations, such as raycasting.
    /// </summary>
    public static class Physics
    {
        /// <summary>
        /// The default layer mask used for raycasting.
        /// </summary>
        public const int DefaultRaycastLayers = -1;

        /// <summary>
        /// World gravity in meters per second squared. It starts as the project setting; changing it while playing
        /// changes the running world.
        /// </summary>
        public static Vector3 gravity
        {
            get => internal_m2n_physics_get_gravity();
            set => internal_m2n_physics_set_gravity(value);
        }

        /// <summary>
        /// Pushes every dynamic body within <paramref name="radius"/> of <paramref name="center"/> away from it, by the
        /// area each one turns to it: a wide crate takes more than a thin pole.
        /// </summary>
        /// <param name="center">The center of the blast.</param>
        /// <param name="radius">Bodies closer than this take the full push.</param>
        /// <param name="impulsePerArea">Newton seconds per square meter facing the blast; negative pulls in.</param>
        /// <param name="falloff">Distance beyond the radius over which the push fades to nothing.</param>
        /// <param name="layerMask">The layers that feel it.</param>
        public static void Explode(Vector3 center, float radius, float impulsePerArea, float falloff = 0.0f, int layerMask = DefaultRaycastLayers)
        {
            internal_m2n_physics_explode(center, radius, falloff, impulsePerArea, layerMask);
        }

        /// <summary>
        /// Sweeps a box along a direction and returns the first object hit.
        /// </summary>
        /// <param name="center">Where the box starts.</param>
        /// <param name="halfExtents">Half the box's size along each of its axes.</param>
        /// <param name="direction">The direction of the sweep.</param>
        /// <param name="orientation">The box's rotation.</param>
        /// <param name="maxDistance">How far the box travels.</param>
        /// <param name="layerMask">The layers it hits.</param>
        /// <param name="querySensors">Whether it hits sensors.</param>
        public static RaycastHit? BoxCast(Vector3 center, Vector3 halfExtents, Vector3 direction, Quaternion orientation, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            RaycastHit hit;
            if (internal_m2n_physics_shape_cast(out hit, 0, halfExtents * 2.0f, center, orientation, direction, maxDistance, layerMask, querySensors))
            {
                return hit;
            }
            return null;
        }

        /// <summary>
        /// Sweeps a box along a direction and returns everything it hits, nearest first.
        /// </summary>
        public static RaycastHit[] BoxCastAll(Vector3 center, Vector3 halfExtents, Vector3 direction, Quaternion orientation, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            byte[] rawHits = internal_m2n_physics_shape_cast_all(0, halfExtents * 2.0f, center, orientation, direction, maxDistance, layerMask, querySensors);
            return rawHits.ToStructArray<RaycastHit>();
        }

        /// <summary>
        /// The entities whose shapes touch a box.
        /// </summary>
        public static Entity[] BoxOverlap(Vector3 center, Vector3 halfExtents, Quaternion orientation, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            byte[] rawHits = internal_m2n_physics_shape_overlap(0, halfExtents * 2.0f, center, orientation, layerMask, querySensors);
            return rawHits.ToStructArray<Entity>();
        }

        /// <summary>
        /// Sweeps a capsule between two sphere centers along a direction and returns the first object hit.
        /// </summary>
        /// <param name="point1">The center of one end sphere.</param>
        /// <param name="point2">The center of the other end sphere.</param>
        /// <param name="radius">The capsule's radius.</param>
        /// <param name="direction">The direction of the sweep.</param>
        /// <param name="maxDistance">How far the capsule travels.</param>
        /// <param name="layerMask">The layers it hits.</param>
        /// <param name="querySensors">Whether it hits sensors.</param>
        public static RaycastHit? CapsuleCast(Vector3 point1, Vector3 point2, float radius, Vector3 direction, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            CapsulePose(point1, point2, out Vector3 center, out Quaternion orientation, out float length);
            RaycastHit hit;
            if (internal_m2n_physics_shape_cast(out hit, 1, new Vector3(radius, length, 0.0f), center, orientation, direction, maxDistance, layerMask, querySensors))
            {
                return hit;
            }
            return null;
        }

        /// <summary>
        /// The entities whose shapes touch a capsule between two sphere centers.
        /// </summary>
        public static Entity[] CapsuleOverlap(Vector3 point1, Vector3 point2, float radius, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            CapsulePose(point1, point2, out Vector3 center, out Quaternion orientation, out float length);
            byte[] rawHits = internal_m2n_physics_shape_overlap(1, new Vector3(radius, length, 0.0f), center, orientation, layerMask, querySensors);
            return rawHits.ToStructArray<Entity>();
        }

        /// <summary>
        /// A capsule's center, the rotation that turns its local Y axis from one end to the other, and its length.
        /// </summary>
        private static void CapsulePose(Vector3 point1, Vector3 point2, out Vector3 center, out Quaternion orientation, out float length)
        {
            Vector3 axis = point2 - point1;
            length = axis.magnitude;
            center = (point1 + point2) * 0.5f;
            orientation = length > 1e-6f ? Quaternion.FromToRotation(Vector3.up, axis / length) : Quaternion.identity;
        }

        /// <summary>
        /// Casts a ray and returns the first object hit, if any.
        /// </summary>
        /// <param name="ray">The ray to cast.</param>
        /// <param name="maxDistance">The maximum distance the ray should check for collisions. Defaults to <see cref="Mathf.Infinity"/>.</param>
        /// <param name="layerMask">A layer mask that defines which layers to include in the raycast. Defaults to <see cref="DefaultRaycastLayers"/>.</param>
        /// <param name="querySensors">
        /// If <c>true</c>, the raycast will include sensors in its results. Defaults to <c>false</c>.
        /// </param>
        /// <returns>
        /// A nullable <see cref="RaycastHit"/> containing information about the object hit, or <c>null</c> if no object was hit.
        /// </returns>
        public static RaycastHit? Raycast(Ray ray, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            return Raycast(ray.origin, ray.direction, maxDistance, layerMask, querySensors);
        }

        /// <summary>
        /// Casts a ray from a specific origin in a specific direction and returns the first object hit, if any.
        /// </summary>
        /// <param name="origin">The origin of the ray.</param>
        /// <param name="direction">The direction in which to cast the ray.</param>
        /// <param name="maxDistance">The maximum distance the ray should check for collisions. Defaults to <see cref="Mathf.Infinity"/>.</param>
        /// <param name="layerMask">A layer mask that defines which layers to include in the raycast. Defaults to <see cref="DefaultRaycastLayers"/>.</param>
        /// <param name="querySensors">
        /// If <c>true</c>, the raycast will include sensors in its results. Defaults to <c>false</c>.
        /// </param>
        /// <returns>
        /// A nullable <see cref="RaycastHit"/> containing information about the object hit, or <c>null</c> if no object was hit.
        /// </returns>
        public static RaycastHit? Raycast(Vector3 origin, Vector3 direction, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            RaycastHit hit;
            bool hasHit = internal_m2n_physics_ray_cast(out hit, origin, direction, maxDistance, layerMask, querySensors);
            if (hasHit)
            {
                return hit;
            }
            return null;
        }

        /// <summary>
        /// Casts a ray and returns all objects hit along the ray's path.
        /// </summary>
        /// <param name="ray">The ray to cast.</param>
        /// <param name="maxDistance">The maximum distance the ray should check for collisions. Defaults to <see cref="Mathf.Infinity"/>.</param>
        /// <param name="layerMask">A layer mask that defines which layers to include in the raycast. Defaults to <see cref="DefaultRaycastLayers"/>.</param>
        /// <param name="querySensors">
        /// If <c>true</c>, the raycast will include sensors in its results. Defaults to <c>false</c>.
        /// </param>
        /// <returns>
        /// An array of <see cref="RaycastHit"/> objects containing information about each object hit.
        /// </returns>
        public static RaycastHit[] RaycastAll(Ray ray, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            return RaycastAll(ray.origin, ray.direction, maxDistance, layerMask, querySensors);
        }

        /// <summary>
        /// Casts a ray from a specific origin in a specific direction and returns all objects hit along the ray's path.
        /// </summary>
        /// <param name="origin">The origin of the ray.</param>
        /// <param name="direction">The direction in which to cast the ray.</param>
        /// <param name="maxDistance">The maximum distance the ray should check for collisions. Defaults to <see cref="Mathf.Infinity"/>.</param>
        /// <param name="layerMask">A layer mask that defines which layers to include in the raycast. Defaults to <see cref="DefaultRaycastLayers"/>.</param>
        /// <param name="querySensors">
        /// If <c>true</c>, the raycast will include sensors in its results. Defaults to <c>false</c>.
        /// </param>
        /// <returns>
        /// An array of <see cref="RaycastHit"/> objects containing information about each object hit.
        /// </returns>
        public static RaycastHit[] RaycastAll(Vector3 origin, Vector3 direction, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            byte[] rawHits = internal_m2n_physics_ray_cast_all(origin, direction, maxDistance, layerMask, querySensors);
            return rawHits.ToStructArray<RaycastHit>();
        }

        /// <summary>
        /// Casts a sphere along a ray and returns the first object hit, if any.
        /// </summary>
        /// <param name="ray">The ray to cast.</param>
        /// <param name="radius">The radius of the sphere to cast.</param>
        /// <param name="maxDistance">The maximum distance the ray should check for collisions. Defaults to <see cref="Mathf.Infinity"/>.</param>
        /// <param name="layerMask">A layer mask that defines which layers to include in the raycast. Defaults to <see cref="DefaultRaycastLayers"/>.</param>
        /// <param name="querySensors">
        /// If <c>true</c>, the raycast will include sensors in its results. Defaults to <c>false</c>.
        /// </param>
        /// <returns>
        /// A nullable <see cref="RaycastHit"/> containing information about the object hit, or <c>null</c> if no object was hit.
        /// </returns>
        public static RaycastHit? SphereCast(Ray ray, float radius, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            return SphereCast(ray.origin, ray.direction, radius, maxDistance, layerMask, querySensors);
        }

        /// <summary>
        /// Casts a sphere along a ray from a specific origin in a specific direction and returns the first object hit, if any.
        /// </summary>
        /// <param name="origin">The origin of the ray.</param>
        /// <param name="direction">The direction in which to cast the ray.</param>
        /// <param name="radius">The radius of the sphere to cast.</param>
        /// <param name="maxDistance">The maximum distance the ray should check for collisions. Defaults to <see cref="Mathf.Infinity"/>.</param>
        /// <param name="layerMask">A layer mask that defines which layers to include in the raycast. Defaults to <see cref="DefaultRaycastLayers"/>.</param>
        /// <param name="querySensors">
        /// If <c>true</c>, the raycast will include sensors in its results. Defaults to <c>false</c>.
        /// </param>
        /// <returns>
        /// A nullable <see cref="RaycastHit"/> containing information about the object hit, or <c>null</c> if no object was hit.
        /// </returns>
        public static RaycastHit? SphereCast(Vector3 origin, Vector3 direction, float radius, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            RaycastHit hit;
            bool hasHit = internal_m2n_physics_sphere_cast(out hit, origin, direction, radius, maxDistance, layerMask, querySensors);
            if (hasHit)
            {
                return hit;
            }
            return null;
        }

        /// <summary>
        /// Casts a sphere along a ray and returns all objects hit along the ray's path.
        /// </summary>
        /// <param name="ray">The ray to cast.</param>
        /// <param name="radius">The radius of the sphere to cast.</param>
        /// <param name="maxDistance">The maximum distance the ray should check for collisions. Defaults to <see cref="Mathf.Infinity"/>.</param>
        /// <param name="layerMask">A layer mask that defines which layers to include in the raycast. Defaults to <see cref="DefaultRaycastLayers"/>.</param>
        /// <param name="querySensors">
        /// If <c>true</c>, the raycast will include sensors in its results. Defaults to <c>false</c>.
        /// </param>
        /// <returns>
        /// An array of <see cref="RaycastHit"/> objects containing information about each object hit.
        /// </returns>
        public static RaycastHit[] SphereCastAll(Ray ray, float radius, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            return SphereCastAll(ray.origin, ray.direction, radius, maxDistance, layerMask, querySensors);
        }

        /// <summary>
        /// Casts a ray from a specific origin in a specific direction and returns all objects hit along the ray's path.
        /// </summary>
        /// <param name="origin">The origin of the ray.</param>
        /// <param name="direction">The direction in which to cast the ray.</param>
        /// <param name="radius">The radius of the sphere to cast.</param>
        /// <param name="maxDistance">The maximum distance the ray should check for collisions. Defaults to <see cref="Mathf.Infinity"/>.</param>
        /// <param name="layerMask">A layer mask that defines which layers to include in the raycast. Defaults to <see cref="DefaultRaycastLayers"/>.</param>
        /// <param name="querySensors">
        /// If <c>true</c>, the raycast will include sensors in its results. Defaults to <c>false</c>.
        /// </param>
        /// <returns>
        /// An array of <see cref="RaycastHit"/> objects containing information about each object hit.
        /// </returns>
        public static RaycastHit[] SphereCastAll(Vector3 origin, Vector3 direction, float radius, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            byte[] rawHits = internal_m2n_physics_sphere_cast_all(origin, direction, radius, maxDistance, layerMask, querySensors);
            return rawHits.ToStructArray<RaycastHit>();
        }


        /// <summary>
        /// Tests a sphere from a specific origin and returns all objects touching or inside it.
        /// </summary>
        /// <param name="origin">The origin of the sphere.</param>
        /// <param name="radius">The radius of the sphere to cast.</param>
        /// <param name="layerMask">A layer mask that defines which layers to include in the raycast. Defaults to <see cref="DefaultRaycastLayers"/>.</param>
        /// <param name="querySensors">
        /// If <c>true</c>, the cast will include sensors in its results. Defaults to <c>false</c>.
        /// </param>
        /// <returns>
        /// An array of <see cref="Entity"/> objects containing information about each object hit.
        /// </returns>
        public static Entity[] SphereOverlap(Vector3 origin, float radius, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            byte[] rawHits = internal_m2n_physics_sphere_overlap(origin, radius, layerMask, querySensors);
            return rawHits.ToStructArray<Entity>();
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_physics_ray_cast(out RaycastHit hit, Vector3 origin, Vector3 direction, float maxDistance, int layerMask, bool querySensors);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_physics_ray_cast_all(Vector3 origin, Vector3 direction, float maxDistance, int layerMask, bool querySensors);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_physics_sphere_cast(out RaycastHit hit, Vector3 origin, Vector3 direction, float radius, float maxDistance, int layerMask, bool querySensors);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_physics_sphere_cast_all(Vector3 origin, Vector3 direction, float radius, float maxDistance, int layerMask, bool querySensors);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_physics_sphere_overlap(Vector3 origin, float radius, int layerMask, bool querySensors);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_physics_get_gravity();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_physics_set_gravity(Vector3 gravity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_physics_explode(Vector3 center, float radius, float falloff, float impulsePerArea, int layerMask);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_physics_shape_cast(out RaycastHit hit, int kind, Vector3 size, Vector3 origin, Quaternion rotation, Vector3 direction, float maxDistance, int layerMask, bool querySensors);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_physics_shape_cast_all(int kind, Vector3 size, Vector3 origin, Quaternion rotation, Vector3 direction, float maxDistance, int layerMask, bool querySensors);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_physics_shape_overlap(int kind, Vector3 size, Vector3 origin, Quaternion rotation, int layerMask, bool querySensors);
    }
}
