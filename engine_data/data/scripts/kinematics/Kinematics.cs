using System;
using System.Runtime.CompilerServices;

namespace Ace
{
namespace Core
{
public static class IK
{

    public static void SetIKPositionCCD(Entity entity, Vector3 target, int numBonesInChain, float threshold = 0.001f, int maxIterations = 10)
    {
        internal_m2n_utils_set_ik_posiiton_ccd(entity, target, numBonesInChain, threshold, maxIterations);
    }

    public static void SetIKPositionFabrik(Entity entity, Vector3 target, int numBonesInChain, float threshold = 0.001f, int maxIterations = 10)
    {
        internal_m2n_utils_set_ik_posiiton_fabrik(entity, target, numBonesInChain, threshold, maxIterations);
    }
    
    public static void SetIKPositionTwoBone(Entity entity, Vector3 target, Vector3 forward, float weight = 1.0f, float soften = 1.0f, int maxIterations = 10)
    {
        internal_m2n_utils_set_ik_posiiton_two_bone(entity, target, forward, weight, soften, maxIterations);
    }

    public static void SetIKLookAtPosition(Entity entity, Vector3 target, float weight = 1.0f)
    {
        internal_m2n_utils_set_ik_look_at_posiiton(entity, target, weight);
    }

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_utils_set_ik_posiiton_ccd(Entity entity, Vector3 target, int numBonesInChain, float threshold, int maxIterations);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_utils_set_ik_posiiton_fabrik(Entity entity, Vector3 target, int numBonesInChain, float threshold, int maxIterations);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_utils_set_ik_posiiton_two_bone(Entity entity, Vector3 target, Vector3 forward, float weight, float soften, int maxIterations);


    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern void internal_m2n_utils_set_ik_look_at_posiiton(Entity entity, Vector3 target, float weight);

}



}

}


