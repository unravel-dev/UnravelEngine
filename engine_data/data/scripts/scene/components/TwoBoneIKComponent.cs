using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Analytical two-bone IK on the bone it sits on (a hand or a foot): the bone reaches its
    /// target, bending its parent (elbow, knee) toward the pole. Runs after scripts, before
    /// skinning, on top of the animated pose, and leaves the animated pose untouched.
    /// </summary>
    public class TwoBoneIKComponent : Component
    {
        /// <summary>
        /// Entity the bone reaches for; its rotation is the goal of rotationWeight.
        /// </summary>
        public Entity target
        {
            get => internal_m2n_two_bone_ik_get_target(owner);
            set => internal_m2n_two_bone_ik_set_target(owner, value);
        }

        /// <summary>
        /// Entity the middle joint bends toward. Invalid keeps the animated bend plane.
        /// </summary>
        public Entity pole
        {
            get => internal_m2n_two_bone_ik_get_pole(owner);
            set => internal_m2n_two_bone_ik_set_pole(owner, value);
        }

        /// <summary>
        /// Blend against the animated pose, 0..1.
        /// </summary>
        public float weight
        {
            get => internal_m2n_two_bone_ik_get_weight(owner);
            set => internal_m2n_two_bone_ik_set_weight(owner, value);
        }

        /// <summary>
        /// How much the bone also takes the target rotation, 0..1 (hands gripping a handle).
        /// </summary>
        public float rotationWeight
        {
            get => internal_m2n_two_bone_ik_get_rotation_weight(owner);
            set => internal_m2n_two_bone_ik_set_rotation_weight(owner, value);
        }

        /// <summary>
        /// Fraction of the limb length over which reach eases out before the joint locks, 0..1.
        /// </summary>
        public float soften
        {
            get => internal_m2n_two_bone_ik_get_soften(owner);
            set => internal_m2n_two_bone_ik_set_soften(owner, value);
        }

        /// <summary>
        /// Time constant (seconds) the goal eases toward the target with; 0 follows it exactly.
        /// </summary>
        public float smoothing
        {
            get => internal_m2n_two_bone_ik_get_smoothing(owner);
            set => internal_m2n_two_bone_ik_set_smoothing(owner, value);
        }

        /// <summary>
        /// Animation curve the weight is multiplied by (0 while no playing clip has it). Empty for none.
        /// </summary>
        public string weightCurve
        {
            get => internal_m2n_two_bone_ik_get_weight_curve(owner);
            set => internal_m2n_two_bone_ik_set_weight_curve(owner, value);
        }

        /// <summary>
        /// The modifier ran and wrote a pose on the last frame.
        /// </summary>
        public bool isSolved => internal_m2n_two_bone_ik_is_solved(owner);

        /// <summary>
        /// The effector reached its goal on the last frame.
        /// </summary>
        public bool isReached => internal_m2n_two_bone_ik_is_reached(owner);

        /// <summary>
        /// Weight the last solve was applied with (weight x weight curve x fades).
        /// </summary>
        public float effectiveWeight => internal_m2n_two_bone_ik_get_effective_weight(owner);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_two_bone_ik_get_target(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_two_bone_ik_set_target(Entity eid, Entity value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_two_bone_ik_get_pole(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_two_bone_ik_set_pole(Entity eid, Entity value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_two_bone_ik_get_weight(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_two_bone_ik_set_weight(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_two_bone_ik_get_rotation_weight(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_two_bone_ik_set_rotation_weight(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_two_bone_ik_get_soften(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_two_bone_ik_set_soften(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_two_bone_ik_get_smoothing(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_two_bone_ik_set_smoothing(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_two_bone_ik_get_weight_curve(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_two_bone_ik_set_weight_curve(Entity eid, string value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_two_bone_ik_is_solved(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_two_bone_ik_is_reached(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_two_bone_ik_get_effective_weight(Entity eid);
    }
}
