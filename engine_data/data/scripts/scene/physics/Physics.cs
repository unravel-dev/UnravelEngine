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
    /// <remarks>
    /// Every cast normalizes its direction, so only the way it points counts, not its length. How far a cast travels
    /// defaults to <see cref="Mathf.Infinity"/>; an infinite or very large distance is shortened to the longest query
    /// the engine runs, 10 kilometers. A zero or non-finite direction, or a negative distance, hits nothing. A hit's
    /// <see cref="RaycastHit.point"/> is where the cast touched the entity, for sphere, box and capsule casts as well,
    /// and its <see cref="RaycastHit.distance"/> is how far the cast traveled along its direction.
    /// <para>
    /// The casts that return every hit and the overlaps return a new array on every call. Their NonAlloc forms fill an
    /// array the caller keeps and return how many entries they wrote, allocating nothing: hits come nearest first, and
    /// when the array is too small the nearest ones that fit are kept; an overlap keeps the first entities it found.
    /// </para>
    /// </remarks>
    public static class Physics
    {
        /// <summary>
        /// The default layer mask used for raycasting.
        /// </summary>
        public const int DefaultRaycastLayers = -1;

        /// <summary>
        /// The shape of a box or capsule query, as the engine reads it: query_shape in the native bindings has the
        /// same values.
        /// </summary>
        private enum QueryShape : int
        {
            /// <summary>The size is the box's full extents.</summary>
            Box,
            /// <summary>The size's x is the radius, its y the length of the segment between the sphere centers.</summary>
            Capsule,
        }

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
        /// Pushes the dynamic bodies within <paramref name="radius"/> of <paramref name="center"/> away from it, by the
        /// area each one turns to it: a wide crate takes more than a thin pole.
        /// </summary>
        /// <remarks>
        /// On Box3D, the default backend, sphere, capsule and hull shapes (boxes and cylinders are hulls) take the push;
        /// mesh shapes are not pushed. On Bullet every dynamic body is pushed as a sphere of its size.
        /// </remarks>
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
        /// Sweeps a box along a direction and returns the first object it touches, if any.
        /// </summary>
        /// <param name="center">Where the box starts.</param>
        /// <param name="halfExtents">Half the box's size along each of its axes.</param>
        /// <param name="direction">The direction of the sweep.</param>
        /// <param name="orientation">The box's rotation.</param>
        /// <param name="maxDistance">How far the box travels, in meters.</param>
        /// <param name="layerMask">The layers it hits.</param>
        /// <param name="querySensors">Whether it hits sensors.</param>
        public static RaycastHit? BoxCast(Vector3 center, Vector3 halfExtents, Vector3 direction, Quaternion orientation, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            RaycastHit hit;
            if (internal_m2n_physics_shape_cast(out hit, QueryShape.Box, halfExtents * 2.0f, center, orientation, direction, maxDistance, layerMask, querySensors))
            {
                return hit;
            }
            return null;
        }

        /// <summary>
        /// Sweeps a box along a direction and returns everything it touches, nearest first, in a new array.
        /// </summary>
        /// <param name="center">Where the box starts.</param>
        /// <param name="halfExtents">Half the box's size along each of its axes.</param>
        /// <param name="direction">The direction of the sweep.</param>
        /// <param name="orientation">The box's rotation.</param>
        /// <param name="maxDistance">How far the box travels, in meters.</param>
        /// <param name="layerMask">The layers it hits.</param>
        /// <param name="querySensors">Whether it hits sensors.</param>
        public static RaycastHit[] BoxCastAll(Vector3 center, Vector3 halfExtents, Vector3 direction, Quaternion orientation, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            byte[] rawHits = internal_m2n_physics_shape_cast_all(QueryShape.Box, halfExtents * 2.0f, center, orientation, direction, maxDistance, layerMask, querySensors);
            return rawHits.ToStructArray<RaycastHit>();
        }

        /// <summary>
        /// Sweeps a box along a direction and writes what it touches into <paramref name="results"/>, nearest first,
        /// without allocating.
        /// </summary>
        /// <param name="center">Where the box starts.</param>
        /// <param name="halfExtents">Half the box's size along each of its axes.</param>
        /// <param name="direction">The direction of the sweep.</param>
        /// <param name="orientation">The box's rotation.</param>
        /// <param name="results">The array the hits are written to; when it is too small, the nearest that fit.</param>
        /// <param name="maxDistance">How far the box travels, in meters.</param>
        /// <param name="layerMask">The layers it hits.</param>
        /// <param name="querySensors">Whether it hits sensors.</param>
        /// <returns>The number of hits written.</returns>
        public static int BoxCastNonAlloc(Vector3 center, Vector3 halfExtents, Vector3 direction, Quaternion orientation, RaycastHit[] results, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            return HasRoom(results)
                ? internal_m2n_physics_shape_cast_non_alloc(ref results[0], results.Length, QueryShape.Box, halfExtents * 2.0f, center, orientation, direction, maxDistance, layerMask, querySensors)
                : 0;
        }

        /// <summary>
        /// The entities whose shapes touch a box, in a new array.
        /// </summary>
        public static Entity[] BoxOverlap(Vector3 center, Vector3 halfExtents, Quaternion orientation, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            byte[] rawHits = internal_m2n_physics_shape_overlap(QueryShape.Box, halfExtents * 2.0f, center, orientation, layerMask, querySensors);
            return rawHits.ToStructArray<Entity>();
        }

        /// <summary>
        /// Writes the entities whose shapes touch a box into <paramref name="results"/>, without allocating.
        /// </summary>
        /// <returns>The number of entities written.</returns>
        public static int BoxOverlapNonAlloc(Vector3 center, Vector3 halfExtents, Quaternion orientation, Entity[] results, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            return HasRoom(results)
                ? internal_m2n_physics_shape_overlap_non_alloc(ref results[0], results.Length, QueryShape.Box, halfExtents * 2.0f, center, orientation, layerMask, querySensors)
                : 0;
        }

        /// <summary>
        /// Sweeps a capsule between two sphere centers along a direction and returns the first object it touches, if any.
        /// </summary>
        /// <param name="point1">The center of one end sphere.</param>
        /// <param name="point2">The center of the other end sphere.</param>
        /// <param name="radius">The capsule's radius.</param>
        /// <param name="direction">The direction of the sweep.</param>
        /// <param name="maxDistance">How far the capsule travels, in meters.</param>
        /// <param name="layerMask">The layers it hits.</param>
        /// <param name="querySensors">Whether it hits sensors.</param>
        public static RaycastHit? CapsuleCast(Vector3 point1, Vector3 point2, float radius, Vector3 direction, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            CapsulePose(point1, point2, out Vector3 center, out Quaternion orientation, out float length);
            RaycastHit hit;
            if (internal_m2n_physics_shape_cast(out hit, QueryShape.Capsule, new Vector3(radius, length, 0.0f), center, orientation, direction, maxDistance, layerMask, querySensors))
            {
                return hit;
            }
            return null;
        }

        /// <summary>
        /// Sweeps a capsule between two sphere centers along a direction and returns everything it touches, nearest
        /// first, in a new array.
        /// </summary>
        /// <param name="point1">The center of one end sphere.</param>
        /// <param name="point2">The center of the other end sphere.</param>
        /// <param name="radius">The capsule's radius.</param>
        /// <param name="direction">The direction of the sweep.</param>
        /// <param name="maxDistance">How far the capsule travels, in meters.</param>
        /// <param name="layerMask">The layers it hits.</param>
        /// <param name="querySensors">Whether it hits sensors.</param>
        public static RaycastHit[] CapsuleCastAll(Vector3 point1, Vector3 point2, float radius, Vector3 direction, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            CapsulePose(point1, point2, out Vector3 center, out Quaternion orientation, out float length);
            byte[] rawHits = internal_m2n_physics_shape_cast_all(QueryShape.Capsule, new Vector3(radius, length, 0.0f), center, orientation, direction, maxDistance, layerMask, querySensors);
            return rawHits.ToStructArray<RaycastHit>();
        }

        /// <summary>
        /// Sweeps a capsule between two sphere centers along a direction and writes what it touches into
        /// <paramref name="results"/>, nearest first, without allocating.
        /// </summary>
        /// <param name="point1">The center of one end sphere.</param>
        /// <param name="point2">The center of the other end sphere.</param>
        /// <param name="radius">The capsule's radius.</param>
        /// <param name="direction">The direction of the sweep.</param>
        /// <param name="results">The array the hits are written to; when it is too small, the nearest that fit.</param>
        /// <param name="maxDistance">How far the capsule travels, in meters.</param>
        /// <param name="layerMask">The layers it hits.</param>
        /// <param name="querySensors">Whether it hits sensors.</param>
        /// <returns>The number of hits written.</returns>
        public static int CapsuleCastNonAlloc(Vector3 point1, Vector3 point2, float radius, Vector3 direction, RaycastHit[] results, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            if (!HasRoom(results))
            {
                return 0;
            }
            CapsulePose(point1, point2, out Vector3 center, out Quaternion orientation, out float length);
            return internal_m2n_physics_shape_cast_non_alloc(ref results[0], results.Length, QueryShape.Capsule, new Vector3(radius, length, 0.0f), center, orientation, direction, maxDistance, layerMask, querySensors);
        }

        /// <summary>
        /// The entities whose shapes touch a capsule between two sphere centers, in a new array.
        /// </summary>
        public static Entity[] CapsuleOverlap(Vector3 point1, Vector3 point2, float radius, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            CapsulePose(point1, point2, out Vector3 center, out Quaternion orientation, out float length);
            byte[] rawHits = internal_m2n_physics_shape_overlap(QueryShape.Capsule, new Vector3(radius, length, 0.0f), center, orientation, layerMask, querySensors);
            return rawHits.ToStructArray<Entity>();
        }

        /// <summary>
        /// Writes the entities whose shapes touch a capsule between two sphere centers into <paramref name="results"/>,
        /// without allocating.
        /// </summary>
        /// <returns>The number of entities written.</returns>
        public static int CapsuleOverlapNonAlloc(Vector3 point1, Vector3 point2, float radius, Entity[] results, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            if (!HasRoom(results))
            {
                return 0;
            }
            CapsulePose(point1, point2, out Vector3 center, out Quaternion orientation, out float length);
            return internal_m2n_physics_shape_overlap_non_alloc(ref results[0], results.Length, QueryShape.Capsule, new Vector3(radius, length, 0.0f), center, orientation, layerMask, querySensors);
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
        /// Whether a NonAlloc call has room to write to. A null array throws, as an argument the call cannot use.
        /// </summary>
        private static bool HasRoom<T>(T[] results) where T : struct
        {
            ArgumentNullException.ThrowIfNull(results);
            return results.Length > 0;
        }

        /// <summary>
        /// Casts a ray and returns the first object hit, if any.
        /// </summary>
        /// <param name="ray">The ray to cast.</param>
        /// <param name="maxDistance">How far the ray reaches, in meters. Defaults to <see cref="Mathf.Infinity"/>.</param>
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
        /// <param name="maxDistance">How far the ray reaches, in meters. Defaults to <see cref="Mathf.Infinity"/>.</param>
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
        /// Casts a ray and returns all objects hit along the ray's path, nearest first, in a new array.
        /// </summary>
        /// <param name="ray">The ray to cast.</param>
        /// <param name="maxDistance">How far the ray reaches, in meters. Defaults to <see cref="Mathf.Infinity"/>.</param>
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
        /// Casts a ray from a specific origin in a specific direction and returns all objects hit along the ray's path,
        /// nearest first, in a new array.
        /// </summary>
        /// <param name="origin">The origin of the ray.</param>
        /// <param name="direction">The direction in which to cast the ray.</param>
        /// <param name="maxDistance">How far the ray reaches, in meters. Defaults to <see cref="Mathf.Infinity"/>.</param>
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
        /// Casts a ray and writes the objects hit along its path into <paramref name="results"/>, nearest first,
        /// without allocating.
        /// </summary>
        /// <param name="ray">The ray to cast.</param>
        /// <param name="results">The array the hits are written to; when it is too small, the nearest that fit.</param>
        /// <param name="maxDistance">How far the ray reaches, in meters.</param>
        /// <param name="layerMask">The layers it hits.</param>
        /// <param name="querySensors">Whether it hits sensors.</param>
        /// <returns>The number of hits written.</returns>
        public static int RaycastNonAlloc(Ray ray, RaycastHit[] results, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            return RaycastNonAlloc(ray.origin, ray.direction, results, maxDistance, layerMask, querySensors);
        }

        /// <summary>
        /// Casts a ray from a specific origin in a specific direction and writes the objects hit along its path into
        /// <paramref name="results"/>, nearest first, without allocating.
        /// </summary>
        /// <param name="origin">The origin of the ray.</param>
        /// <param name="direction">The direction in which to cast the ray.</param>
        /// <param name="results">The array the hits are written to; when it is too small, the nearest that fit.</param>
        /// <param name="maxDistance">How far the ray reaches, in meters.</param>
        /// <param name="layerMask">The layers it hits.</param>
        /// <param name="querySensors">Whether it hits sensors.</param>
        /// <returns>The number of hits written.</returns>
        public static int RaycastNonAlloc(Vector3 origin, Vector3 direction, RaycastHit[] results, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            return HasRoom(results)
                ? internal_m2n_physics_ray_cast_non_alloc(ref results[0], results.Length, origin, direction, maxDistance, layerMask, querySensors)
                : 0;
        }

        /// <summary>
        /// Sweeps a sphere along a ray and returns the first object it touches, if any.
        /// </summary>
        /// <param name="ray">The ray the sphere's center starts on and travels along.</param>
        /// <param name="radius">The radius of the sphere to cast.</param>
        /// <param name="maxDistance">How far the sphere travels, in meters. Defaults to <see cref="Mathf.Infinity"/>.</param>
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
        /// Sweeps a sphere from a specific origin in a specific direction and returns the first object it touches, if any.
        /// </summary>
        /// <param name="origin">Where the sphere's center starts.</param>
        /// <param name="direction">The direction of the sweep.</param>
        /// <param name="radius">The radius of the sphere to cast.</param>
        /// <param name="maxDistance">How far the sphere travels, in meters. Defaults to <see cref="Mathf.Infinity"/>.</param>
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
        /// Sweeps a sphere along a ray and returns everything it touches, nearest first, in a new array.
        /// </summary>
        /// <param name="ray">The ray the sphere's center starts on and travels along.</param>
        /// <param name="radius">The radius of the sphere to cast.</param>
        /// <param name="maxDistance">How far the sphere travels, in meters. Defaults to <see cref="Mathf.Infinity"/>.</param>
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
        /// Sweeps a sphere from a specific origin in a specific direction and returns everything it touches, nearest
        /// first, in a new array.
        /// </summary>
        /// <param name="origin">Where the sphere's center starts.</param>
        /// <param name="direction">The direction of the sweep.</param>
        /// <param name="radius">The radius of the sphere to cast.</param>
        /// <param name="maxDistance">How far the sphere travels, in meters. Defaults to <see cref="Mathf.Infinity"/>.</param>
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
        /// Sweeps a sphere along a ray and writes what it touches into <paramref name="results"/>, nearest first,
        /// without allocating.
        /// </summary>
        /// <param name="ray">The ray the sphere's center starts on and travels along.</param>
        /// <param name="radius">The radius of the sphere to cast.</param>
        /// <param name="results">The array the hits are written to; when it is too small, the nearest that fit.</param>
        /// <param name="maxDistance">How far the sphere travels, in meters.</param>
        /// <param name="layerMask">The layers it hits.</param>
        /// <param name="querySensors">Whether it hits sensors.</param>
        /// <returns>The number of hits written.</returns>
        public static int SphereCastNonAlloc(Ray ray, float radius, RaycastHit[] results, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            return SphereCastNonAlloc(ray.origin, ray.direction, radius, results, maxDistance, layerMask, querySensors);
        }

        /// <summary>
        /// Sweeps a sphere from a specific origin in a specific direction and writes what it touches into
        /// <paramref name="results"/>, nearest first, without allocating.
        /// </summary>
        /// <param name="origin">Where the sphere's center starts.</param>
        /// <param name="direction">The direction of the sweep.</param>
        /// <param name="radius">The radius of the sphere to cast.</param>
        /// <param name="results">The array the hits are written to; when it is too small, the nearest that fit.</param>
        /// <param name="maxDistance">How far the sphere travels, in meters.</param>
        /// <param name="layerMask">The layers it hits.</param>
        /// <param name="querySensors">Whether it hits sensors.</param>
        /// <returns>The number of hits written.</returns>
        public static int SphereCastNonAlloc(Vector3 origin, Vector3 direction, float radius, RaycastHit[] results, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            return HasRoom(results)
                ? internal_m2n_physics_sphere_cast_non_alloc(ref results[0], results.Length, origin, direction, radius, maxDistance, layerMask, querySensors)
                : 0;
        }

        /// <summary>
        /// Tests a sphere from a specific origin and returns all objects touching or inside it, in a new array.
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

        /// <summary>
        /// Writes the entities touching or inside a sphere into <paramref name="results"/>, without allocating.
        /// </summary>
        /// <param name="origin">The center of the sphere.</param>
        /// <param name="radius">The radius of the sphere.</param>
        /// <param name="results">The array the entities are written to; when it is too small, the first found.</param>
        /// <param name="layerMask">The layers it finds.</param>
        /// <param name="querySensors">Whether it finds sensors.</param>
        /// <returns>The number of entities written.</returns>
        public static int SphereOverlapNonAlloc(Vector3 origin, float radius, Entity[] results, int layerMask = DefaultRaycastLayers, bool querySensors = false)
        {
            return HasRoom(results)
                ? internal_m2n_physics_sphere_overlap_non_alloc(ref results[0], results.Length, origin, radius, layerMask, querySensors)
                : 0;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_physics_ray_cast(out RaycastHit hit, Vector3 origin, Vector3 direction, float maxDistance, int layerMask, bool querySensors);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_physics_ray_cast_all(Vector3 origin, Vector3 direction, float maxDistance, int layerMask, bool querySensors);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_physics_ray_cast_non_alloc(ref RaycastHit results, int capacity, Vector3 origin, Vector3 direction, float maxDistance, int layerMask, bool querySensors);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_physics_sphere_cast(out RaycastHit hit, Vector3 origin, Vector3 direction, float radius, float maxDistance, int layerMask, bool querySensors);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_physics_sphere_cast_all(Vector3 origin, Vector3 direction, float radius, float maxDistance, int layerMask, bool querySensors);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_physics_sphere_cast_non_alloc(ref RaycastHit results, int capacity, Vector3 origin, Vector3 direction, float radius, float maxDistance, int layerMask, bool querySensors);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_physics_sphere_overlap(Vector3 origin, float radius, int layerMask, bool querySensors);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_physics_sphere_overlap_non_alloc(ref Entity results, int capacity, Vector3 origin, float radius, int layerMask, bool querySensors);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_physics_get_gravity();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_physics_set_gravity(Vector3 gravity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_physics_explode(Vector3 center, float radius, float falloff, float impulsePerArea, int layerMask);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_physics_shape_cast(out RaycastHit hit, QueryShape kind, Vector3 size, Vector3 origin, Quaternion rotation, Vector3 direction, float maxDistance, int layerMask, bool querySensors);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_physics_shape_cast_all(QueryShape kind, Vector3 size, Vector3 origin, Quaternion rotation, Vector3 direction, float maxDistance, int layerMask, bool querySensors);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_physics_shape_cast_non_alloc(ref RaycastHit results, int capacity, QueryShape kind, Vector3 size, Vector3 origin, Quaternion rotation, Vector3 direction, float maxDistance, int layerMask, bool querySensors);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_physics_shape_overlap(QueryShape kind, Vector3 size, Vector3 origin, Quaternion rotation, int layerMask, bool querySensors);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_physics_shape_overlap_non_alloc(ref Entity results, int capacity, QueryShape kind, Vector3 size, Vector3 origin, Quaternion rotation, int layerMask, bool querySensors);
    }
}
