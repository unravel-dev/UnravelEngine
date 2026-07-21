using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{

    /// <summary>
    /// Inverse-kinematics helpers for skeletal chains.
    /// </summary>
    public static class IK
    {
        /// <summary>
        /// Solves a CCD IK chain so the end effector reaches <paramref name="target"/>.
        /// <paramref name="pole"/> is a world-space hint for which side of the
        /// (root -> end) line intermediate joints should bend toward - typically a
        /// point in front of the character at knee height for legs. Pass
        /// <c>Vector3.zero</c> to disable the constraint.
        /// </summary>
        public static void SetIKPositionCCD(Entity entity, Vector3 target, Vector3 pole,
                                            int numBonesInChain,
                                            int maxIterations = 10,
                                            float threshold = 0.001f)
        {
            internal_m2n_utils_set_ik_posiiton_ccd(entity, target, pole, numBonesInChain, maxIterations, threshold);
        }

        /// <summary>
        /// Solves a FABRIK IK chain so the end effector reaches <paramref name="target"/>.
        /// <paramref name="pole"/> is a world-space hint for which side of the
        /// (root -> end) line intermediate joints should bend toward. Without a pole,
        /// FABRIK may pick any geometrically valid bend direction - which on steep
        /// slopes causes knees to collapse inward. Pass <c>Vector3.zero</c> to
        /// disable the constraint.
        /// </summary>
        public static void SetIKPositionFabrik(Entity entity, Vector3 target, Vector3 pole,
                                               int numBonesInChain,
                                               int maxIterations = 10,
                                               float threshold = 0.001f)
        {
            internal_m2n_utils_set_ik_posiiton_fabrik(entity, target, pole, numBonesInChain, maxIterations, threshold);
        }

        /// <summary>
        /// Analytical two-bone IK (hip-knee-foot or shoulder-elbow-hand).
        /// <paramref name="pole"/> decides which side of the leg plane the knee
        /// (or elbow) bends to - typically a point in front of the character for
        /// legs, or off to the side for arms.
        /// </summary>
        public static void SetIKPositionTwoBone(Entity entity, Vector3 target, Vector3 pole,
                                                float weight = 1.0f,
                                                float soften = 1.0f)
        {
            internal_m2n_utils_set_ik_posiiton_two_bone(entity, target, pole, weight, soften);
        }

        /// <summary>
        /// Orients <paramref name="entity"/> to look toward <paramref name="target"/> with the given blend weight.
        /// </summary>
        public static void SetIKLookAtPosition(Entity entity, Vector3 target, float weight = 1.0f)
        {
            internal_m2n_utils_set_ik_look_at_posiiton(entity, target, weight);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_utils_set_ik_posiiton_ccd(Entity entity, Vector3 target, Vector3 pole, int numBonesInChain, int maxIterations, float threshold);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_utils_set_ik_posiiton_fabrik(Entity entity, Vector3 target, Vector3 pole, int numBonesInChain, int maxIterations, float threshold);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_utils_set_ik_posiiton_two_bone(Entity entity, Vector3 target, Vector3 pole, float weight, float soften);


        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_utils_set_ik_look_at_posiiton(Entity entity, Vector3 target, float weight);

    }



}



