using System;
using System.Globalization;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Unravel.Core
{
    /// <summary>
    /// Per-body opt-in for contact bookkeeping.
    /// </summary>
    /// <remarks>
    /// A pair is tracked only when at least one side opts into that event kind, and an exit is
    /// synthesized on removal only when at least one side opts into it. Pairs nobody asks for are
    /// never tracked, so they cost nothing to maintain and nothing to tear down.
    /// </remarks>
    [Flags]
    public enum ContactEventFlags : byte
    {
        None = 0,
        /// <summary>Track sensor overlaps and deliver OnSensorEnter/OnSensorExit while both sides live.</summary>
        SensorEvents = 1 << 0,
        /// <summary>Track solid contacts and deliver OnCollisionEnter/OnCollisionExit while both sides live.</summary>
        CollisionEvents = 1 << 1,
        /// <summary>Deliver OnSensorExit when either side is destroyed or deactivated while overlapping.</summary>
        SensorExitOnDestroy = 1 << 2,
        /// <summary>Deliver OnCollisionExit when either side is destroyed or deactivated while touching.</summary>
        CollisionExitOnDestroy = 1 << 3,
    }

    /// <summary>
    /// Provides physics functionality for an entity.
    /// </summary>
    public class PhysicsComponent : Component
    {

        /// <summary>
        /// Layers that this body should collide with (include filter).
        /// </summary>
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
        /// <summary>
        /// Layers that this body should ignore (exclude filter).
        /// </summary>
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

        /// <summary>
        /// Effective collision layer mask after applying include and exclude filters.
        /// </summary>
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
        /// Which contact events this body takes part in, and whether removing it while still
        /// overlapping synthesizes an exit.
        /// </summary>
        public ContactEventFlags contactEventFlags
        {
            get
            {
                return (ContactEventFlags)internal_m2n_physics_get_contact_event_flags(owner);
            }
            set
            {
                internal_m2n_physics_set_contact_event_flags(owner, (byte)value);
            }
        }

        /// <summary>
        /// Whether sensor overlaps involving this body are tracked and reported.
        /// </summary>
        public bool sensorEventsEnabled
        {
            get { return (contactEventFlags & ContactEventFlags.SensorEvents) != 0; }
            set { SetContactEventFlag(ContactEventFlags.SensorEvents, value); }
        }

        /// <summary>
        /// Whether solid contacts involving this body are tracked and reported.
        /// </summary>
        public bool collisionEventsEnabled
        {
            get { return (contactEventFlags & ContactEventFlags.CollisionEvents) != 0; }
            set { SetContactEventFlag(ContactEventFlags.CollisionEvents, value); }
        }

        /// <summary>
        /// Whether destroying or deactivating this body while it overlaps a sensor still delivers
        /// <see cref="ScriptComponent.OnSensorExit"/>.
        /// </summary>
        public bool sensorExitOnDestroy
        {
            get { return (contactEventFlags & ContactEventFlags.SensorExitOnDestroy) != 0; }
            set { SetContactEventFlag(ContactEventFlags.SensorExitOnDestroy, value); }
        }

        /// <summary>
        /// Whether destroying or deactivating this body while it is touching something still
        /// delivers <see cref="ScriptComponent.OnCollisionExit"/>. Off by default.
        /// </summary>
        public bool collisionExitOnDestroy
        {
            get { return (contactEventFlags & ContactEventFlags.CollisionExitOnDestroy) != 0; }
            set { SetContactEventFlag(ContactEventFlags.CollisionExitOnDestroy, value); }
        }

        private void SetContactEventFlag(ContactEventFlags flag, bool enabled)
        {
            ContactEventFlags flags = contactEventFlags;
            contactEventFlags = enabled ? (flags | flag) : (flags & ~flag);
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
        /// Simulation role: Static (teleport + AABB), Kinematic (ECS-driven, pushes dynamics), or Dynamic.
        /// </summary>
        public RigidbodyType bodyType
        {
            get
            {
                return internal_m2n_physics_get_body_type(owner);
            }
            set
            {
                internal_m2n_physics_set_body_type(owner, value);
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
        private static extern byte internal_m2n_physics_get_contact_event_flags(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_physics_set_contact_event_flags(Entity eid, byte flags);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_physics_get_mass(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_physics_set_mass(Entity eid, float mass);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern RigidbodyType internal_m2n_physics_get_body_type(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_physics_set_body_type(Entity eid, RigidbodyType type);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_physics_get_use_gravity(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_physics_set_use_gravity(Entity eid, bool useGravity);
    }
}
