using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// How a joint lets its two bodies move against each other. The joint's axis is the X axis of its frame.
    /// </summary>
    public enum JointType : byte
    {
        /// <summary>Holds the bodies together rigidly.</summary>
        Fixed,
        /// <summary>Turns about the axis (doors, levers, wheels on an axle).</summary>
        Hinge,
        /// <summary>Slides along the axis without turning (pistons, lifts).</summary>
        Slider,
        /// <summary>Turns any way about the anchor, limited to a cone and a twist range (chains, shoulders).</summary>
        Ball,
        /// <summary>Keeps two points at a distance (ropes, springs).</summary>
        Distance,
        /// <summary>Spins about the axis on a suspension along the frame's Y axis and steers about it (vehicles).</summary>
        Wheel,
    }

    /// <summary>
    /// The range a joint moves in. Angles are in degrees, distances in meters.
    /// </summary>
    public struct JointLimits
    {
        /// <summary>Keeps the joint inside its range.</summary>
        public bool enabled;
        /// <summary>Hinge and ball twist: lowest angle. Slider and wheel suspension: lowest position. Distance: shortest length.</summary>
        public float lower;
        /// <summary>Hinge and ball twist: highest angle. Slider and wheel suspension: highest position. Distance: longest length.</summary>
        public float upper;
        /// <summary>Ball: half angle of the cone the axis may swing in.</summary>
        public float coneAngle;
    }

    /// <summary>
    /// Pulls a joint towards a target, stated as a frequency and a damping ratio, so it does not depend on the masses.
    /// </summary>
    public struct JointSpring
    {
        /// <summary>Pulls the joint towards its target. A fixed joint with a spring is soft.</summary>
        public bool enabled;
        /// <summary>Oscillations per second. Higher is stiffer.</summary>
        public float frequency;
        /// <summary>0 bounces forever, 1 settles without overshoot.</summary>
        public float dampingRatio;
        /// <summary>Hinge: angle in degrees. Slider: position in meters.</summary>
        public float target;
        /// <summary>Ball: the rotation of body B against body A the spring turns towards, Euler angles in degrees.</summary>
        public Vector3 targetRotation;
    }

    /// <summary>
    /// Drives a joint at a speed with at most a force (or torque).
    /// </summary>
    public struct JointMotor
    {
        /// <summary>Drives the joint at its speed.</summary>
        public bool enabled;
        /// <summary>Hinge and wheel: degrees per second. Slider and distance: meters per second.</summary>
        public float speed;
        /// <summary>Hinge and wheel: newton meters. Slider and distance: newtons.</summary>
        public float maxForce;
    }

    /// <summary>
    /// Turns a wheel about its frame's Y axis, like a car's front wheels.
    /// </summary>
    public struct JointSteering
    {
        /// <summary>Lets the wheel steer; otherwise it points straight ahead.</summary>
        public bool enabled;
        /// <summary>The angle the wheel steers to, in degrees.</summary>
        public float angle;
        /// <summary>The largest angle either way, in degrees.</summary>
        public float limit;
        /// <summary>Newton meters available to steer with.</summary>
        public float maxTorque;
    }

    /// <summary>
    /// Connects two bodies so they move together in a constrained way.
    /// </summary>
    /// <remarks>
    /// A joint is an entity of its own: its transform, moved by <see cref="anchor"/> and turned by
    /// <see cref="axisRotation"/>, is the joint frame, read when the joint is created and fixed to both bodies from then
    /// on. <see cref="bodyA"/> is the body the joint hangs on (none means the world); <see cref="bodyB"/> is the body it
    /// holds (none means the joint's own entity, which then needs a physics component).
    /// </remarks>
    public class JointComponent : Component
    {
        /// <summary>How the joint lets its bodies move.</summary>
        public JointType type
        {
            get => (JointType)internal_m2n_joint_get_type(owner);
            set => internal_m2n_joint_set_type(owner, (byte)value);
        }

        /// <summary>The body the joint hangs on. An invalid entity hangs it on the world.</summary>
        public Entity bodyA
        {
            get => internal_m2n_joint_get_body_a(owner);
            set => internal_m2n_joint_set_body_a(owner, value);
        }

        /// <summary>The body the joint holds. An invalid entity takes the joint's own entity.</summary>
        public Entity bodyB
        {
            get => internal_m2n_joint_get_body_b(owner);
            set => internal_m2n_joint_set_body_b(owner, value);
        }

        /// <summary>Offset of the joint frame from the entity, in the entity's space.</summary>
        public Vector3 anchor
        {
            get => internal_m2n_joint_get_anchor(owner);
            set => internal_m2n_joint_set_anchor(owner, value);
        }

        /// <summary>Turn of the joint frame against the entity, Euler angles in degrees.</summary>
        public Vector3 axisRotation
        {
            get => internal_m2n_joint_get_axis_rotation(owner);
            set => internal_m2n_joint_set_axis_rotation(owner, value);
        }

        /// <summary>Whether the two jointed bodies still collide with each other.</summary>
        public bool collideConnected
        {
            get => internal_m2n_joint_get_collide_connected(owner);
            set => internal_m2n_joint_set_collide_connected(owner, value);
        }

        /// <summary>The range the joint moves in.</summary>
        public JointLimits limits
        {
            get
            {
                JointLimits result;
                result.enabled = internal_m2n_joint_get_limits_enabled(owner);
                result.lower = internal_m2n_joint_get_limits_lower(owner);
                result.upper = internal_m2n_joint_get_limits_upper(owner);
                result.coneAngle = internal_m2n_joint_get_limits_cone(owner);
                return result;
            }
            set => internal_m2n_joint_set_limits(owner, value.enabled, value.lower, value.upper, value.coneAngle);
        }

        /// <summary>The spring that pulls the joint to a target; for a wheel, the suspension.</summary>
        public JointSpring spring
        {
            get
            {
                JointSpring result;
                result.enabled = internal_m2n_joint_get_spring_enabled(owner);
                result.frequency = internal_m2n_joint_get_spring_frequency(owner);
                result.dampingRatio = internal_m2n_joint_get_spring_damping(owner);
                result.target = internal_m2n_joint_get_spring_target(owner);
                result.targetRotation = internal_m2n_joint_get_spring_target_rotation(owner);
                return result;
            }
            set => internal_m2n_joint_set_spring(owner, value.enabled, value.frequency, value.dampingRatio, value.target, value.targetRotation);
        }

        /// <summary>The motor that drives the joint; for a wheel, its spin.</summary>
        public JointMotor motor
        {
            get
            {
                JointMotor result;
                result.enabled = internal_m2n_joint_get_motor_enabled(owner);
                result.speed = internal_m2n_joint_get_motor_speed(owner);
                result.maxForce = internal_m2n_joint_get_motor_force(owner);
                return result;
            }
            set => internal_m2n_joint_set_motor(owner, value.enabled, value.speed, value.maxForce);
        }

        /// <summary>Wheel steering.</summary>
        public JointSteering steering
        {
            get
            {
                JointSteering result;
                result.enabled = internal_m2n_joint_get_steering_enabled(owner);
                result.angle = internal_m2n_joint_get_steering_angle(owner);
                result.limit = internal_m2n_joint_get_steering_limit(owner);
                result.maxTorque = internal_m2n_joint_get_steering_torque(owner);
                return result;
            }
            set => internal_m2n_joint_set_steering(owner, value.enabled, value.angle, value.limit, value.maxTorque);
        }

        /// <summary>Resistance to motion while the motor is off, in newton meters (newtons for a slider).</summary>
        public float friction
        {
            get => internal_m2n_joint_get_friction(owner);
            set => internal_m2n_joint_set_friction(owner, value);
        }

        /// <summary>Distance joint: the length it keeps, in meters. 0 takes the distance between the anchors at the start.</summary>
        public float distance
        {
            get => internal_m2n_joint_get_distance(owner);
            set => internal_m2n_joint_set_distance(owner, value);
        }

        /// <summary>Force in newtons above which the joint breaks; 0 never breaks.</summary>
        public float breakForce
        {
            get => internal_m2n_joint_get_break_force(owner);
            set => internal_m2n_joint_set_break_force(owner, value);
        }

        /// <summary>Torque in newton meters above which the joint breaks; 0 never breaks.</summary>
        public float breakTorque
        {
            get => internal_m2n_joint_get_break_torque(owner);
            set => internal_m2n_joint_set_break_torque(owner, value);
        }

        /// <summary>The joint exists in the running simulation.</summary>
        public bool isActive => internal_m2n_joint_is_active(owner);

        /// <summary>The joint broke and stays broken until <see cref="Repair"/> or the end of play.</summary>
        public bool isBroken => internal_m2n_joint_is_broken(owner);

        /// <summary>Hinge angle, ball twist or wheel steering angle, in degrees.</summary>
        public float angle => internal_m2n_joint_get_angle(owner);

        /// <summary>Slider position or distance joint length, in meters.</summary>
        public float position => internal_m2n_joint_get_position(owner);

        /// <summary>Hinge or wheel spin speed in degrees per second, slider speed in meters per second.</summary>
        public float speed => internal_m2n_joint_get_speed(owner);

        /// <summary>Force the joint applies to hold its bodies, in newtons, world space.</summary>
        public Vector3 reactionForce => internal_m2n_joint_get_reaction_force(owner);

        /// <summary>Torque the joint applies to hold its bodies, in newton meters, world space.</summary>
        public Vector3 reactionTorque => internal_m2n_joint_get_reaction_torque(owner);

        /// <summary>Breaks the joint as if a force broke it: <see cref="ScriptComponent.OnJointBreak"/> follows.</summary>
        public void Break()
        {
            internal_m2n_joint_break(owner);
        }

        /// <summary>A broken joint comes back, holding its bodies where they are now.</summary>
        public void Repair()
        {
            internal_m2n_joint_repair(owner);
        }

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern byte internal_m2n_joint_get_type(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_set_type(Entity eid, byte value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern Entity internal_m2n_joint_get_body_a(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_set_body_a(Entity eid, Entity value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern Entity internal_m2n_joint_get_body_b(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_set_body_b(Entity eid, Entity value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern Vector3 internal_m2n_joint_get_anchor(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_set_anchor(Entity eid, Vector3 value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern Vector3 internal_m2n_joint_get_axis_rotation(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_set_axis_rotation(Entity eid, Vector3 value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern bool internal_m2n_joint_get_collide_connected(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_set_collide_connected(Entity eid, bool value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern bool internal_m2n_joint_get_limits_enabled(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_joint_get_limits_lower(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_joint_get_limits_upper(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_joint_get_limits_cone(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_set_limits(Entity eid, bool enabled, float lower, float upper, float coneAngle);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern bool internal_m2n_joint_get_spring_enabled(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_joint_get_spring_frequency(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_joint_get_spring_damping(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_joint_get_spring_target(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern Vector3 internal_m2n_joint_get_spring_target_rotation(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_set_spring(Entity eid, bool enabled, float frequency, float dampingRatio, float target, Vector3 targetRotation);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern bool internal_m2n_joint_get_motor_enabled(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_joint_get_motor_speed(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_joint_get_motor_force(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_set_motor(Entity eid, bool enabled, float speed, float maxForce);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern bool internal_m2n_joint_get_steering_enabled(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_joint_get_steering_angle(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_joint_get_steering_limit(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_joint_get_steering_torque(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_set_steering(Entity eid, bool enabled, float angle, float limit, float maxTorque);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_joint_get_friction(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_set_friction(Entity eid, float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_joint_get_distance(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_set_distance(Entity eid, float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_joint_get_break_force(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_set_break_force(Entity eid, float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_joint_get_break_torque(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_set_break_torque(Entity eid, float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern bool internal_m2n_joint_is_active(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern bool internal_m2n_joint_is_broken(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_joint_get_angle(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_joint_get_position(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_joint_get_speed(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern Vector3 internal_m2n_joint_get_reaction_force(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern Vector3 internal_m2n_joint_get_reaction_torque(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_break(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_repair(Entity eid);
    }
}
