using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Turns a chain of bones (head, neck, chest) toward a target, the lower bones taking a
    /// smaller share and the tip finishing the aim, each limited to a cone around its animated
    /// facing. Smoothed, and faded out when the target goes behind the character.
    /// </summary>
    public class LookAtIKComponent : Component
    {
        /// <summary>
        /// Entity to look at. Invalid fades the look out.
        /// </summary>
        public Entity target
        {
            get => internal_m2n_look_at_ik_get_target(owner);
            set => internal_m2n_look_at_ik_set_target(owner, value);
        }

        /// <summary>
        /// World-space offset added to the target position (look at a face, not the feet).
        /// </summary>
        public Vector3 targetOffset
        {
            get => internal_m2n_look_at_ik_get_target_offset(owner);
            set => internal_m2n_look_at_ik_set_target_offset(owner, value);
        }

        /// <summary>
        /// Bones turned, counted up from this bone (head, neck, chest = 3).
        /// </summary>
        public int chainLength
        {
            get => internal_m2n_look_at_ik_get_chain_length(owner);
            set => internal_m2n_look_at_ik_set_chain_length(owner, value);
        }

        /// <summary>
        /// Blend against the animated pose, 0..1.
        /// </summary>
        public float weight
        {
            get => internal_m2n_look_at_ik_get_weight(owner);
            set => internal_m2n_look_at_ik_set_weight(owner, value);
        }

        /// <summary>
        /// Share of the turn the lowest bone takes; the tip completes the rest.
        /// </summary>
        public float rootShare
        {
            get => internal_m2n_look_at_ik_get_root_share(owner);
            set => internal_m2n_look_at_ik_set_root_share(owner, value);
        }

        /// <summary>
        /// Largest turn of each bone away from its animated facing, in degrees.
        /// </summary>
        public float maxBoneAngle
        {
            get => internal_m2n_look_at_ik_get_max_bone_angle(owner);
            set => internal_m2n_look_at_ik_set_max_bone_angle(owner, value);
        }

        /// <summary>
        /// Targets further than this from the character forward fade the look out, in degrees.
        /// </summary>
        public float maxTurnAngle
        {
            get => internal_m2n_look_at_ik_get_max_turn_angle(owner);
            set => internal_m2n_look_at_ik_set_max_turn_angle(owner, value);
        }

        /// <summary>
        /// Time constant of the target and weight smoothing, in seconds. 0 snaps.
        /// </summary>
        public float smoothing
        {
            get => internal_m2n_look_at_ik_get_smoothing(owner);
            set => internal_m2n_look_at_ik_set_smoothing(owner, value);
        }

        /// <summary>
        /// Bone-local axis the bones look along; zero resolves it from the character forward.
        /// </summary>
        public Vector3 forwardAxis
        {
            get => internal_m2n_look_at_ik_get_forward_axis(owner);
            set => internal_m2n_look_at_ik_set_forward_axis(owner, value);
        }

        /// <summary>
        /// Animation curve the weight is multiplied by (0 while no playing clip has it). Empty for none.
        /// </summary>
        public string weightCurve
        {
            get => internal_m2n_look_at_ik_get_weight_curve(owner);
            set => internal_m2n_look_at_ik_set_weight_curve(owner, value);
        }

        /// <summary>
        /// The modifier ran and wrote a pose on the last frame.
        /// </summary>
        public bool isSolved => internal_m2n_look_at_ik_is_solved(owner);

        /// <summary>
        /// The effector reached its goal on the last frame.
        /// </summary>
        public bool isReached => internal_m2n_look_at_ik_is_reached(owner);

        /// <summary>
        /// Weight the last solve was applied with (weight x weight curve x fades).
        /// </summary>
        public float effectiveWeight => internal_m2n_look_at_ik_get_effective_weight(owner);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_look_at_ik_get_target(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_look_at_ik_set_target(Entity eid, Entity value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_look_at_ik_get_target_offset(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_look_at_ik_set_target_offset(Entity eid, Vector3 value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_look_at_ik_get_chain_length(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_look_at_ik_set_chain_length(Entity eid, int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_look_at_ik_get_weight(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_look_at_ik_set_weight(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_look_at_ik_get_root_share(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_look_at_ik_set_root_share(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_look_at_ik_get_max_bone_angle(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_look_at_ik_set_max_bone_angle(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_look_at_ik_get_max_turn_angle(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_look_at_ik_set_max_turn_angle(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_look_at_ik_get_smoothing(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_look_at_ik_set_smoothing(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_look_at_ik_get_forward_axis(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_look_at_ik_set_forward_axis(Entity eid, Vector3 value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_look_at_ik_get_weight_curve(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_look_at_ik_set_weight_curve(Entity eid, string value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_look_at_ik_is_solved(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_look_at_ik_is_reached(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_look_at_ik_get_effective_weight(Entity eid);
    }
}
