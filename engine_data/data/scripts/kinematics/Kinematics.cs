using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{

    /// <summary>
    /// Inverse-kinematics helpers for skeletal chains.
    ///
    /// Call these from <c>OnUpdate</c>. Scripts run after the animation system
    /// has evaluated the pose and before skinning, so bone writes made there are
    /// what gets rendered. Writing bones from <c>OnFixedUpdate</c> does not work -
    /// animation will overwrite them later in the frame.
    ///
    /// Every method returns <c>true</c> when a pose was written. A <c>false</c>
    /// means the chain was too short, the goal was not finite, or the weight was
    /// zero - not that the target was out of reach (an unreachable target still
    /// produces a valid stretched pose).
    ///
    /// The position solvers only orient the bones that drive the chain, never the
    /// end effector itself, so a solved foot keeps the orientation animation gave
    /// it. Follow a position solve with <see cref="SetIKRotation"/> to pin a foot
    /// to a slope or keep an aimed hand level.
    ///
    /// Poles must come from a reference the solve does not move - the character
    /// root, or a dedicated hint entity. A pole derived from the end effector's
    /// own transform closes a feedback loop: the solve rotates the effector, the
    /// pole follows it, the chain chases the pole, and the limb spins.
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
        /// <param name="weight">Blend against the animated pose, 0..1.</param>
        public static bool SetIKPositionCCD(Entity entity, Vector3 target, Vector3 pole,
                                            int numBonesInChain,
                                            int maxIterations = 20,
                                            float threshold = 0.001f,
                                            float weight = 1.0f)
        {
            return internal_m2n_utils_set_ik_position_ccd(entity, target, pole, numBonesInChain,
                                                          maxIterations, threshold, weight);
        }

        /// <summary>
        /// Solves a FABRIK IK chain so the end effector reaches <paramref name="target"/>.
        /// <paramref name="pole"/> is a world-space hint for which side of the
        /// (root -> end) line intermediate joints should bend toward. Without a pole,
        /// FABRIK may pick any geometrically valid bend direction - which on steep
        /// slopes causes knees to collapse inward. Pass <c>Vector3.zero</c> to
        /// disable the constraint.
        /// </summary>
        /// <param name="weight">Blend against the animated pose, 0..1.</param>
        public static bool SetIKPositionFabrik(Entity entity, Vector3 target, Vector3 pole,
                                               int numBonesInChain,
                                               int maxIterations = 20,
                                               float threshold = 0.001f,
                                               float weight = 1.0f)
        {
            return internal_m2n_utils_set_ik_position_fabrik(entity, target, pole, numBonesInChain,
                                                             maxIterations, threshold, weight);
        }

        /// <summary>
        /// Analytical two-bone IK (hip-knee-foot or shoulder-elbow-hand). Prefer this
        /// over FABRIK for limbs: it is exact, single-pass, and cannot oscillate.
        /// <paramref name="pole"/> decides which side of the limb plane the knee
        /// (or elbow) bends to - typically a point in front of the character for
        /// legs, or off to the side for arms.
        ///
        /// Requires exactly three joints above and including <paramref name="entity"/>
        /// and returns <c>false</c> otherwise; it will not silently fall back to a
        /// different solver.
        /// </summary>
        /// <param name="weight">Blend against the animated pose, 0..1.</param>
        /// <param name="soften">
        /// Fraction of limb length over which reach eases out before the joint locks
        /// straight, 0..1. Softening always costs reach - at 1 a straight leg lands
        /// several centimetres short of the target - so it defaults to off and should
        /// only be dialled in to hide a snapping knee.
        /// </param>
        public static bool SetIKPositionTwoBone(Entity entity, Vector3 target, Vector3 pole,
                                                float weight = 1.0f,
                                                float soften = 0.0f)
        {
            return internal_m2n_utils_set_ik_position_two_bone(entity, target, pole, weight, soften);
        }

        /// <summary>
        /// Blends <paramref name="entity"/> toward a world-space orientation.
        /// Use after a position solve to orient a foot to the ground normal or to
        /// keep an aimed hand level.
        /// </summary>
        public static bool SetIKRotation(Entity entity, Quaternion rotation, float weight = 1.0f)
        {
            return internal_m2n_utils_set_ik_rotation(entity, rotation, weight);
        }

        /// <summary>
        /// Points a bone-local axis at <paramref name="target"/>.
        ///
        /// Imported rigs rarely run their bones along +Z - Mixamo bones run along
        /// local +Y - so pass the rig's actual axis, or <see cref="GetBoneAxis"/> to
        /// derive it from the skeleton.
        /// </summary>
        /// <param name="forwardAxis">Bone-local axis that ends up pointing at the target.</param>
        /// <param name="upAxis">Bone-local axis kept close to <paramref name="worldUp"/>.</param>
        /// <param name="worldUp">
        /// World-space up reference. Pass <c>Vector3.zero</c> to keep the bone's
        /// existing twist instead of forcing a roll - the safer default for spines.
        /// </param>
        /// <param name="maxAngleDegrees">
        /// Cone limit measured from the bone's current forward. 0 leaves the aim
        /// unlimited. The swing is clamped to the cone, so the bone eases to a stop
        /// at the limit rather than the caller having to switch the solve off.
        /// </param>
        public static bool SetIKAim(Entity entity, Vector3 target,
                                    Vector3 forwardAxis,
                                    Vector3 upAxis,
                                    Vector3 worldUp,
                                    float maxAngleDegrees = 0.0f,
                                    float weight = 1.0f)
        {
            return internal_m2n_utils_set_ik_aim_position(entity, target, forwardAxis, upAxis, worldUp,
                                                          maxAngleDegrees * Mathf.Deg2Rad, weight);
        }

        /// <summary>
        /// Orients <paramref name="entity"/> so its local +Z axis points at
        /// <paramref name="target"/>, blended by <paramref name="weight"/> and
        /// preserving the bone's twist. Matches the engine's entity convention;
        /// for skeleton bones prefer <see cref="SetIKAim"/>.
        /// </summary>
        public static bool SetIKLookAtPosition(Entity entity, Vector3 target, float weight = 1.0f)
        {
            return internal_m2n_utils_set_ik_look_at_position(entity, target, weight);
        }

        /// <summary>
        /// Bone-local direction from <paramref name="entity"/> toward its first child,
        /// i.e. the axis the bone visually runs ALONG. Returns <c>Vector3.forward</c>
        /// for leaf bones.
        ///
        /// Use this with <see cref="SetIKAim"/> only when you want to point the bone
        /// itself at something - a barrel, a tentacle, a limb. It is the wrong axis
        /// for a "face the target" aim on a spine, chest or head: aiming a spine
        /// along its own length points the torso at the target and folds the
        /// character in half. Which axis represents a bone's FACING cannot be derived
        /// from the skeleton (that is what a humanoid avatar mapping is for), so pass
        /// it explicitly - <c>Vector3.forward</c> for most rigs.
        /// </summary>
        public static Vector3 GetBoneAxis(Entity entity)
        {
            return internal_m2n_utils_get_ik_bone_axis(entity);
        }

        /// <summary>
        /// Bone-local axis of <paramref name="entity"/> that currently points along
        /// <paramref name="worldDirection"/>, snapped to the nearest signed unit axis.
        ///
        /// This is how you get the <c>forwardAxis</c> for <see cref="SetIKAim"/> on a
        /// torso or head without guessing. A rig authored with the chest's local -Z
        /// out of the front is structurally identical to one with +Z out of the
        /// front, and importers do not normalise it - so ask the rig which local axis
        /// lines up with the character's forward:
        /// <code>
        /// spineAimAxis = IK.GetFacingAxis(Spine, transform.forward);
        /// </code>
        /// Sample it in a neutral pose (OnStart is fine). Snapping means a spine
        /// slightly bent by the current animation frame still resolves cleanly.
        ///
        /// Contrast with <see cref="GetBoneAxis"/>, which returns the axis the bone
        /// runs ALONG - a different question with a different answer.
        /// </summary>
        public static Vector3 GetFacingAxis(Entity entity, Vector3 worldDirection)
        {
            return internal_m2n_utils_get_ik_facing_axis(entity, worldDirection);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_utils_set_ik_position_ccd(Entity entity, Vector3 target, Vector3 pole, int numBonesInChain, int maxIterations, float threshold, float weight);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_utils_set_ik_position_fabrik(Entity entity, Vector3 target, Vector3 pole, int numBonesInChain, int maxIterations, float threshold, float weight);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_utils_set_ik_position_two_bone(Entity entity, Vector3 target, Vector3 pole, float weight, float soften);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_utils_set_ik_rotation(Entity entity, Quaternion rotation, float weight);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_utils_set_ik_aim_position(Entity entity, Vector3 target, Vector3 forwardAxis, Vector3 upAxis, Vector3 worldUp, float maxAngleRadians, float weight);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_utils_set_ik_look_at_position(Entity entity, Vector3 target, float weight);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_utils_get_ik_bone_axis(Entity entity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_utils_get_ik_facing_axis(Entity entity, Vector3 worldDirection);

    }

}
