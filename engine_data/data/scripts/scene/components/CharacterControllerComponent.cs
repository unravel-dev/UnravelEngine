using System;
using System.Numerics;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Provides character controller physics functionality for an entity.
    /// Uses a capsule shape internally with sweep-based movement.
    /// </summary>
    public class CharacterControllerComponent : Component
    {
        /// <summary>
        /// Whether the character is currently touching the ground.
        /// </summary>
        public bool isGrounded
        {
            get { return internal_m2n_cc_get_is_grounded(owner); }
        }

        /// <summary>
        /// Whether the character can currently jump (equivalent to isGrounded).
        /// </summary>
        public bool canJump
        {
            get { return internal_m2n_cc_get_can_jump(owner); }
        }

        /// <summary>
        /// The current velocity of the character controller (read-only, synced from physics).
        /// </summary>
        public Vector3 velocity
        {
            get { return internal_m2n_cc_get_velocity(owner); }
        }

        /// <summary>
        /// The linear velocity of the character controller. Setting this directly overrides the physics velocity.
        /// </summary>
        public Vector3 linearVelocity
        {
            get { return internal_m2n_cc_get_linear_velocity(owner); }
            set { internal_m2n_cc_set_linear_velocity(owner, value); }
        }

        /// <summary>
        /// The radius of the character capsule.
        /// </summary>
        public float radius
        {
            get { return internal_m2n_cc_get_radius(owner); }
            set { internal_m2n_cc_set_radius(owner, value); }
        }

        /// <summary>
        /// The total height of the character capsule.
        /// </summary>
        public float height
        {
            get { return internal_m2n_cc_get_height(owner); }
            set { internal_m2n_cc_set_height(owner, value); }
        }

        /// <summary>
        /// The center offset of the capsule relative to the entity transform.
        /// </summary>
        public Vector3 center
        {
            get { return internal_m2n_cc_get_center(owner); }
            set { internal_m2n_cc_set_center(owner, value); }
        }

        /// <summary>
        /// Maximum height of obstacles the character can step over.
        /// </summary>
        public float stepHeight
        {
            get { return internal_m2n_cc_get_step_height(owner); }
            set { internal_m2n_cc_set_step_height(owner, value); }
        }

        /// <summary>
        /// Maximum slope angle in degrees the character can walk up.
        /// </summary>
        public float slopeLimit
        {
            get { return internal_m2n_cc_get_slope_limit(owner); }
            set { internal_m2n_cc_set_slope_limit(owner, value); }
        }

        /// <summary>
        /// Collision skin width around the character capsule.
        /// </summary>
        public float skinWidth
        {
            get { return internal_m2n_cc_get_skin_width(owner); }
            set { internal_m2n_cc_set_skin_width(owner, value); }
        }

        /// <summary>
        /// Multiplier for world gravity applied to this controller.
        /// </summary>
        public float gravityScale
        {
            get { return internal_m2n_cc_get_gravity_scale(owner); }
            set { internal_m2n_cc_set_gravity_scale(owner, value); }
        }

        /// <summary>
        /// Initial vertical speed when jumping.
        /// </summary>
        public float jumpSpeed
        {
            get { return internal_m2n_cc_get_jump_speed(owner); }
            set { internal_m2n_cc_set_jump_speed(owner, value); }
        }

        /// <summary>
        /// Maximum terminal velocity when falling.
        /// </summary>
        public float fallSpeed
        {
            get { return internal_m2n_cc_get_fall_speed(owner); }
            set { internal_m2n_cc_set_fall_speed(owner, value); }
        }

        /// <summary>
        /// Maximum height the character can reach when jumping. 0 means unlimited.
        /// </summary>
        public float maxJumpHeight
        {
            get { return internal_m2n_cc_get_max_jump_height(owner); }
            set { internal_m2n_cc_set_max_jump_height(owner, value); }
        }

        /// <summary>
        /// Damping applied to linear velocity each step. 0 = no damping, 1 = full damping.
        /// </summary>
        public float linearDamping
        {
            get { return internal_m2n_cc_get_linear_damping(owner); }
            set { internal_m2n_cc_set_linear_damping(owner, value); }
        }

        public LayerMask includeLayers
        {
            get { return internal_m2n_cc_get_include_layers(owner); }
            set { internal_m2n_cc_set_include_layers(owner, value); }
        }

        public LayerMask excludeLayers
        {
            get { return internal_m2n_cc_get_exclude_layers(owner); }
            set { internal_m2n_cc_set_exclude_layers(owner, value); }
        }

        public LayerMask collisionLayers
        {
            get { return internal_m2n_cc_get_collision_layers(owner); }
        }

        /// <summary>
        /// Moves the character by a displacement vector. The movement is constrained by collisions.
        /// </summary>
        /// <param name="displacement">The world-space displacement to apply.</param>
        public void Move(Vector3 displacement)
        {
            internal_m2n_cc_move(owner, displacement);
        }

        /// <summary>
        /// Makes the character jump. Pass Vector3.zero for a default upward jump,
        /// or a direction vector for a directional jump.
        /// </summary>
        /// <param name="direction">The jump direction and magnitude. Use Vector3.zero for default upward jump.</param>
        public void Jump(Vector3 direction)
        {
            internal_m2n_cc_jump(owner, direction);
        }

        public void Jump()
        {
            Jump(Vector3.up);
        }

        /// <summary>
        /// Applies an impulse to the character controller.
        /// </summary>
        /// <param name="impulse">The impulse vector to apply.</param>
        public void ApplyImpulse(Vector3 impulse)
        {
            internal_m2n_cc_apply_impulse(owner, impulse);
        }

        /// <summary>
        /// Teleports the character to a new position instantly, bypassing collision detection.
        /// </summary>
        /// <param name="position">The target world position.</param>
        public void Warp(Vector3 position)
        {
            internal_m2n_cc_warp(owner, position);
        }

        // Internal calls
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cc_move(Entity eid, Vector3 displacement);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cc_jump(Entity eid, Vector3 direction);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cc_apply_impulse(Entity eid, Vector3 impulse);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cc_warp(Entity eid, Vector3 position);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_cc_get_is_grounded(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_cc_get_can_jump(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_cc_get_velocity(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_cc_get_linear_velocity(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cc_set_linear_velocity(Entity eid, Vector3 velocity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_cc_get_radius(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cc_set_radius(Entity eid, float radius);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_cc_get_height(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cc_set_height(Entity eid, float height);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_cc_get_center(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cc_set_center(Entity eid, Vector3 center);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_cc_get_step_height(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cc_set_step_height(Entity eid, float stepHeight);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_cc_get_slope_limit(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cc_set_slope_limit(Entity eid, float slopeLimit);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_cc_get_skin_width(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cc_set_skin_width(Entity eid, float skinWidth);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_cc_get_gravity_scale(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cc_set_gravity_scale(Entity eid, float scale);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_cc_get_jump_speed(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cc_set_jump_speed(Entity eid, float speed);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_cc_get_fall_speed(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cc_set_fall_speed(Entity eid, float speed);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_cc_get_max_jump_height(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cc_set_max_jump_height(Entity eid, float height);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_cc_get_linear_damping(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cc_set_linear_damping(Entity eid, float damping);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern LayerMask internal_m2n_cc_get_include_layers(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cc_set_include_layers(Entity eid, LayerMask mask);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern LayerMask internal_m2n_cc_get_exclude_layers(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cc_set_exclude_layers(Entity eid, LayerMask mask);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern LayerMask internal_m2n_cc_get_collision_layers(Entity eid);
    }
}
