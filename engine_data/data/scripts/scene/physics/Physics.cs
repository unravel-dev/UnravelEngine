using System;
using System.Globalization;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Ace
{
namespace Core
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
    }
}
}
