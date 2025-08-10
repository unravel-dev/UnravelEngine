using System;
using System.Runtime.CompilerServices;

namespace Ace
{
namespace Core
{
public static class Gizmos
{
    public static void AddSphere(Color color, Vector3 position, float radius)
    {
        internal_m2n_gizmos_add_sphere(color, position, radius);
    }
   
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

}


