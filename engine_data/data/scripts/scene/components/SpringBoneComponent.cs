using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Secondary motion for a chain of bones hanging from the bone it sits on (tails, ears,
    /// ponytails, cloth strips): every joint is a damped particle pulled back to its animated
    /// direction, pushed by gravity and kept out of colliders, simulated at a fixed rate.
    /// </summary>
    public class SpringBoneComponent : Component
    {
        /// <summary>
        /// Pull back toward the animated direction, per second.
        /// </summary>
        public float stiffness
        {
            get => internal_m2n_spring_bone_get_stiffness(owner);
            set => internal_m2n_spring_bone_set_stiffness(owner, value);
        }

        /// <summary>
        /// Fraction of velocity lost per simulation step, 0..1.
        /// </summary>
        public float damping
        {
            get => internal_m2n_spring_bone_get_damping(owner);
            set => internal_m2n_spring_bone_set_damping(owner, value);
        }

        /// <summary>
        /// World-space gravity acceleration applied to the joints.
        /// </summary>
        public Vector3 gravity
        {
            get => internal_m2n_spring_bone_get_gravity(owner);
            set => internal_m2n_spring_bone_set_gravity(owner, value);
        }

        /// <summary>
        /// Joint radius used against colliders.
        /// </summary>
        public float radius
        {
            get => internal_m2n_spring_bone_get_radius(owner);
            set => internal_m2n_spring_bone_set_radius(owner, value);
        }

        /// <summary>
        /// Bones simulated below this one.
        /// </summary>
        public int chainLength
        {
            get => internal_m2n_spring_bone_get_chain_length(owner);
            set => internal_m2n_spring_bone_set_chain_length(owner, value);
        }

        /// <summary>
        /// Blend against the animated pose, 0..1.
        /// </summary>
        public float weight
        {
            get => internal_m2n_spring_bone_get_weight(owner);
            set => internal_m2n_spring_bone_set_weight(owner, value);
        }

        /// <summary>
        /// The chain simulated and wrote a pose on the last frame.
        /// </summary>
        public bool isSolved => internal_m2n_spring_bone_is_solved(owner);

        /// <summary>
        /// Number of sphere colliders the chain is kept out of.
        /// </summary>
        public int colliderCount => internal_m2n_spring_bone_get_collider_count(owner);

        /// <summary>
        /// Adds a sphere collider attached to an entity (usually a bone).
        /// </summary>
        /// <param name="entity">Entity the sphere follows.</param>
        /// <param name="radius">Sphere radius.</param>
        /// <param name="offset">Sphere center in the entity's local frame.</param>
        public void AddCollider(Entity entity, float radius, Vector3 offset)
        {
            internal_m2n_spring_bone_add_collider(owner, entity, radius, offset);
        }

        /// <summary>
        /// Removes every collider.
        /// </summary>
        public void ClearColliders()
        {
            internal_m2n_spring_bone_clear_colliders(owner);
        }

        /// <summary>
        /// Drops the simulation state; the chain restarts from its animated pose (after a teleport).
        /// </summary>
        public void ResetSimulation()
        {
            internal_m2n_spring_bone_reset_simulation(owner);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_spring_bone_get_stiffness(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_spring_bone_set_stiffness(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_spring_bone_get_damping(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_spring_bone_set_damping(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_spring_bone_get_gravity(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_spring_bone_set_gravity(Entity eid, Vector3 value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_spring_bone_get_radius(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_spring_bone_set_radius(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_spring_bone_get_chain_length(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_spring_bone_set_chain_length(Entity eid, int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_spring_bone_get_weight(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_spring_bone_set_weight(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_spring_bone_is_solved(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_spring_bone_get_collider_count(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_spring_bone_add_collider(Entity eid, Entity collider, float radius, Vector3 offset);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_spring_bone_clear_colliders(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_spring_bone_reset_simulation(Entity eid);
    }
}
