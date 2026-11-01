using System;
using System.Numerics;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Provides character controller physics functionality for an entity.
    /// Uses a capsule shape internally with sweep-based movement.
    /// </summary>
    /// <remarks>
    /// The character moves on fixed physics steps. While <see cref="interpolate"/> is on, its entity's
    /// <c>transform.position</c> reads and shows a position between the last two steps: it moves evenly at any frame
    /// rate, carries the camera and whatever hangs under it along, and trails this component's <see cref="position"/>
    /// by at most one step. In OnFixedUpdate the two are the same. Writing <c>transform.position</c> or calling <see cref="Warp"/>
    /// teleports at once.
    /// </remarks>
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
        /// Maximum downward fall speed in m/s. Downward vertical velocity is clamped
        /// to this magnitude each simulation step. Default is 55 (skydiver terminal velocity).
        /// </summary>
        public float terminalVelocity
        {
            get { return internal_m2n_cc_get_terminal_velocity(owner); }
            set { internal_m2n_cc_set_terminal_velocity(owner, value); }
        }

        /// <summary>
        /// Damping applied to linear velocity each step. 0 = no damping, 1 = full damping.
        /// </summary>
        public float linearDamping
        {
            get { return internal_m2n_cc_get_linear_damping(owner); }
            set { internal_m2n_cc_set_linear_damping(owner, value); }
        }

        /// <summary>
        /// Whether the entity's transform is placed between the last two physics steps every frame, so the character
        /// moves evenly at any frame rate. On by default. Off, the transform stays at the last step's position and
        /// each move is handed whole to the next step.
        /// </summary>
        public bool interpolate
        {
            get { return internal_m2n_cc_get_interpolate(owner); }
            set { internal_m2n_cc_set_interpolate(owner, value); }
        }

        /// <summary>
        /// Where the last physics step left the character, as an entity position (without <see cref="center"/>). While
        /// <see cref="interpolate"/> is on, <c>transform.position</c> trails this by at most one step. To place the
        /// character, call <see cref="Warp"/>.
        /// </summary>
        public Vector3 position
        {
            get { return internal_m2n_cc_get_position(owner); }
        }

        /// <summary>
        /// Layers that this character should collide with (include filter).
        /// </summary>
        public LayerMask includeLayers
        {
            get { return internal_m2n_cc_get_include_layers(owner); }
            set { internal_m2n_cc_set_include_layers(owner, value); }
        }

        /// <summary>
        /// Layers that this character should ignore (exclude filter).
        /// </summary>
        public LayerMask excludeLayers
        {
            get { return internal_m2n_cc_get_exclude_layers(owner); }
            set { internal_m2n_cc_set_exclude_layers(owner, value); }
        }

        /// <summary>
        /// Effective collision layer mask after applying include and exclude filters.
        /// </summary>
        public LayerMask collisionLayers
        {
            get { return internal_m2n_cc_get_collision_layers(owner); }
        }

        /// <summary>
        /// Moves the character by a displacement vector. The movement is constrained by collisions.
        /// The displacement is applied once, spread over the physics steps that cover the frames it was
        /// queued in; from OnFixedUpdate, in that step. It is not repeated across fixed-step catch-up.
        /// Prefer Move(velocity * Time.deltaTime) from OnUpdate, or Move(velocity * Time.fixedDeltaTime)
        /// from OnFixedUpdate.
        /// </summary>
        /// <param name="displacement">The world-space displacement to apply.</param>
        public void Move(Vector3 displacement)
        {
            internal_m2n_cc_move(owner, displacement);
        }

        /// <summary>
        /// Makes the character jump straight up with the given initial speed (m/s).
        /// </summary>
        /// <param name="speed">Initial upward speed in m/s. Must be greater than 0.</param>
        public void Jump(float speed)
        {
            internal_m2n_cc_jump(owner, new Vector3(0.0f, speed, 0.0f));
        }

        /// <summary>
        /// Makes the character jump with an arbitrary world-space velocity. The vector's
        /// magnitude is the jump speed and its direction is the jump axis.
        /// </summary>
        /// <param name="velocity">World-space jump velocity. Must be non-zero.</param>
        public void Jump(Vector3 velocity)
        {
            internal_m2n_cc_jump(owner, velocity);
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
        private static extern void internal_m2n_cc_jump(Entity eid, Vector3 velocity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cc_apply_impulse(Entity eid, Vector3 impulse);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cc_warp(Entity eid, Vector3 position);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_cc_get_interpolate(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cc_set_interpolate(Entity eid, bool interpolate);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_cc_get_position(Entity eid);

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
        private static extern float internal_m2n_cc_get_terminal_velocity(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cc_set_terminal_velocity(Entity eid, float speed);

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
