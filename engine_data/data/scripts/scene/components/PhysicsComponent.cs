using System;
using System.Globalization;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Unravel.Core
{
    /// <summary>
    /// Provides physics functionality for an entity.
    /// </summary>
    public class PhysicsComponent : Component
    {
    
        public LayerMask includeLayers
        {
            get
            {
                return internal_m2n_physics_get_include_layers(owner);
            }
            set
            {
                internal_m2n_physics_set_include_layers(owner, value);
            }
        }
        public LayerMask excludeLayers
        {
            get
            {
                return internal_m2n_physics_get_exclude_layers(owner);
            }
            set
            {
                internal_m2n_physics_set_exclude_layers(owner, value);
            }
        }

        public LayerMask collisionLayers
        {
            get
            {
                return internal_m2n_physics_get_collision_layers(owner);
            }
        }

        /// <summary>
        /// Whether this physics component is a sensor (triggers collision events but doesn't generate physical responses).
        /// </summary>
        public bool isSensor
        {
            get
            {
                return internal_m2n_physics_get_is_sensor(owner);
            }
            set
            {
                internal_m2n_physics_set_is_sensor(owner, value);
            }
        }

        /// <summary>
        /// The mass of the rigidbody. Mass determines how much force is needed to move the object.
        /// </summary>
        public float mass
        {
            get
            {
                return internal_m2n_physics_get_mass(owner);
            }
            set
            {
                internal_m2n_physics_set_mass(owner, value);
            }
        }

        /// <summary>
        /// Whether this physics component is kinematic. Kinematic objects are moved by script and don't respond to forces.
        /// </summary>
        public bool isKinematic
        {
            get
            {
                return internal_m2n_physics_get_is_kinematic(owner);
            }
            set
            {
                internal_m2n_physics_set_is_kinematic(owner, value);
            }
        }

        /// <summary>
        /// Whether this physics component uses gravity.
        /// </summary>
        public bool useGravity
        {
            get
            {
                return internal_m2n_physics_get_use_gravity(owner);
            }
            set
            {
                internal_m2n_physics_set_use_gravity(owner, value);
            }
        }

        /// <summary>
        /// The velocity vector of the rigidbody. It represents the rate of change of Rigidbody position.
        /// In most cases you should not modify the velocity directly, as this can result in unrealistic 
        /// behaviour - use AddForce instead Do not set the velocity of an object every physics step,
        /// this will lead to unrealistic physics simulation. A typical usage is where you would change the velocity
        /// is when jumping in a first person shooter, because you want an immediate change in velocity.
        /// position.
        /// </summary>
        public Vector3 velocity
        {
            get
            {
                return internal_m2n_physics_get_velocity(owner);
            }
            set
            {
                internal_m2n_physics_set_velocity(owner, value);
            }
        }

        /// <summary>
        /// The angular velocity vector of the rigidbody measured in radians per second.
        /// In most cases you should not modify it directly, as this can result in unrealistic behaviour.
        /// Note that if the Rigidbody has rotational constraints set, the corresponding angular velocity
        /// components are set to zero in the mass space (ie relative to the inertia tensor rotation) at
        /// the time of the call. Additionally, setting the angular velocity of a kinematic rigidbody
        /// is not allowed and will have no effect.
        /// </summary>
        public Vector3 angularVelocity
        {
            get
            {
                return internal_m2n_physics_get_angular_velocity(owner);
            }
            set
            {
                internal_m2n_physics_set_angular_velocity(owner, value);
            }
        }

        /// <summary>
        /// Applies an explosion force to the entity.
        /// </summary>
        /// <param name="explosionForce">The force of the explosion.</param>
        /// <param name="explosionPosition">The center of the explosion.</param>
        /// <param name="explosionRadius">The radius of the explosion.</param>
        /// <param name="upwardsModifier">Adjusts the upward direction of the explosion force.</param>
        /// <param name="mode">The force mode to apply.</param>
        public void ApplyExplosionForce(float explosionForce, Vector3 explosionPosition, float explosionRadius, float upwardsModifier = 0.0f, ForceMode mode = ForceMode.Force)
        {
            internal_m2n_physics_apply_explosion_force(owner, explosionForce, explosionPosition, explosionRadius, upwardsModifier, mode);
        }

        /// <summary>
        /// Applies a force to the entity.
        /// </summary>
        /// <param name="force">The force to apply.</param>
        /// <param name="mode">The force mode to apply.</param>
        public void ApplyForce(Vector3 force, ForceMode mode = ForceMode.Force)
        {
            internal_m2n_physics_apply_force(owner, force, mode);
        }

        /// <summary>
        /// Applies a torque to the entity.
        /// </summary>
        /// <param name="torque">The torque to apply.</param>
        /// <param name="mode">The force mode to apply.</param>
        public void ApplyTorque(Vector3 torque, ForceMode mode = ForceMode.Force)
        {
            internal_m2n_physics_apply_torque(owner, torque, mode);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_physics_apply_explosion_force(Entity eid, float explosionForce, Vector3 explosionPosition, float explosionRadius, float upwardsModifier, ForceMode mode);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_physics_apply_force(Entity eid, Vector3 force, ForceMode mode);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_physics_apply_torque(Entity eid, Vector3 torque, ForceMode mode);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_physics_get_velocity(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_physics_set_velocity(Entity eid, Vector3 velocity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_physics_get_angular_velocity(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_physics_set_angular_velocity(Entity eid, Vector3 velocity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern LayerMask internal_m2n_physics_get_include_layers(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_physics_set_include_layers(Entity eid, LayerMask mask);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern LayerMask internal_m2n_physics_get_exclude_layers(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_physics_set_exclude_layers(Entity eid, LayerMask mask);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern LayerMask internal_m2n_physics_get_collision_layers(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_physics_get_is_sensor(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_physics_set_is_sensor(Entity eid, bool sensor);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_physics_get_mass(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_physics_set_mass(Entity eid, float mass);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_physics_get_is_kinematic(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_physics_set_is_kinematic(Entity eid, bool kinematic);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_physics_get_use_gravity(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_physics_set_use_gravity(Entity eid, bool useGravity);
    }
}
