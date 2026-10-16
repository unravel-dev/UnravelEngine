using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// How a chain IK solves.
    /// </summary>
    public enum ChainIKSolver : byte
    {
        /// <summary>Forward and backward reaching: smooth, converges fast, best for long chains.</summary>
        Fabrik = 0,
        /// <summary>Cyclic coordinate descent: bends the joints nearest the tip first.</summary>
        Ccd = 1,
    }

    /// <summary>
    /// Iterative IK over any number of bones ending at the bone it sits on (tails, tentacles,
    /// spines, robot arms).
    /// </summary>
    public class ChainIKComponent : Component
    {
        /// <summary>
        /// Entity the chain tip reaches for.
        /// </summary>
        public Entity target
        {
            get => internal_m2n_chain_ik_get_target(owner);
            set => internal_m2n_chain_ik_set_target(owner, value);
        }

        /// <summary>
        /// Entity the chain bends toward. Invalid keeps the solver's bend.
        /// </summary>
        public Entity pole
        {
            get => internal_m2n_chain_ik_get_pole(owner);
            set => internal_m2n_chain_ik_set_pole(owner, value);
        }

        /// <summary>
        /// Bones in the chain, counted up from this bone.
        /// </summary>
        public int chainLength
        {
            get => internal_m2n_chain_ik_get_chain_length(owner);
            set => internal_m2n_chain_ik_set_chain_length(owner, value);
        }

        /// <summary>
        /// How the chain solves.
        /// </summary>
        public ChainIKSolver solver
        {
            get => (ChainIKSolver)internal_m2n_chain_ik_get_solver(owner);
            set => internal_m2n_chain_ik_set_solver(owner, (byte)value);
        }

        /// <summary>
        /// Solver iterations per frame.
        /// </summary>
        public int iterations
        {
            get => internal_m2n_chain_ik_get_iterations(owner);
            set => internal_m2n_chain_ik_set_iterations(owner, value);
        }

        /// <summary>
        /// Distance at which the solve stops early, in world units.
        /// </summary>
        public float tolerance
        {
            get => internal_m2n_chain_ik_get_tolerance(owner);
            set => internal_m2n_chain_ik_set_tolerance(owner, value);
        }

        /// <summary>
        /// Blend against the animated pose, 0..1.
        /// </summary>
        public float weight
        {
            get => internal_m2n_chain_ik_get_weight(owner);
            set => internal_m2n_chain_ik_set_weight(owner, value);
        }

        /// <summary>
        /// How much the tip also takes the target rotation, 0..1.
        /// </summary>
        public float rotationWeight
        {
            get => internal_m2n_chain_ik_get_rotation_weight(owner);
            set => internal_m2n_chain_ik_set_rotation_weight(owner, value);
        }

        /// <summary>
        /// Time constant (seconds) the goal eases toward the target with; 0 follows it exactly.
        /// </summary>
        public float smoothing
        {
            get => internal_m2n_chain_ik_get_smoothing(owner);
            set => internal_m2n_chain_ik_set_smoothing(owner, value);
        }

        /// <summary>
        /// Animation curve the weight is multiplied by (0 while no playing clip has it). Empty for none.
        /// </summary>
        public string weightCurve
        {
            get => internal_m2n_chain_ik_get_weight_curve(owner);
            set => internal_m2n_chain_ik_set_weight_curve(owner, value);
        }

        /// <summary>
        /// The modifier ran and wrote a pose on the last frame.
        /// </summary>
        public bool isSolved => internal_m2n_chain_ik_is_solved(owner);

        /// <summary>
        /// The effector reached its goal on the last frame.
        /// </summary>
        public bool isReached => internal_m2n_chain_ik_is_reached(owner);

        /// <summary>
        /// Weight the last solve was applied with (weight x weight curve x fades).
        /// </summary>
        public float effectiveWeight => internal_m2n_chain_ik_get_effective_weight(owner);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_chain_ik_get_target(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_chain_ik_set_target(Entity eid, Entity value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_chain_ik_get_pole(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_chain_ik_set_pole(Entity eid, Entity value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_chain_ik_get_chain_length(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_chain_ik_set_chain_length(Entity eid, int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte internal_m2n_chain_ik_get_solver(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_chain_ik_set_solver(Entity eid, byte value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_chain_ik_get_iterations(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_chain_ik_set_iterations(Entity eid, int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_chain_ik_get_tolerance(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_chain_ik_set_tolerance(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_chain_ik_get_weight(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_chain_ik_set_weight(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_chain_ik_get_rotation_weight(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_chain_ik_set_rotation_weight(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_chain_ik_get_smoothing(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_chain_ik_set_smoothing(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_chain_ik_get_weight_curve(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_chain_ik_set_weight_curve(Entity eid, string value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_chain_ik_is_solved(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_chain_ik_is_reached(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_chain_ik_get_effective_weight(Entity eid);
    }
}
