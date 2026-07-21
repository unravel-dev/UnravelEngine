using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
/// <summary>
/// Draws debug primitives in the scene for visualization during development.
/// </summary>
public static class Gizmos
{
    /// <summary>
    /// Draws a debug sphere at the specified world position.
    /// </summary>
    /// <param name="color">The sphere color.</param>
    /// <param name="position">World-space center of the sphere.</param>
    /// <param name="radius">Sphere radius.</param>
    public static void AddSphere(Color color, Vector3 position, float radius)
    {
        internal_m2n_gizmos_add_sphere(color, position, radius);
    }

    /// <summary>
    /// Draws a debug ray starting at <paramref name="position"/> in the given direction.
    /// </summary>
    /// <param name="color">The ray color.</param>
    /// <param name="position">World-space origin of the ray.</param>
    /// <param name="direction">Ray direction (need not be normalized).</param>
    /// <param name="maxDistance">Maximum ray length.</param>
    public static void AddRay(Color color, Vector3 position, Vector3 direction, float maxDistance = 99999.0f)
    {
        internal_m2n_gizmos_add_ray(color, position, direction, maxDistance);
    }

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_gizmos_add_sphere(Color color, Vector3 position, float radius);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_gizmos_add_ray(Color color, Vector3 position, Vector3 direction, float maxDistance);
}

}
