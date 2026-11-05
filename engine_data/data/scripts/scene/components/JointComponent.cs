using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

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
    /// <remarks>
    /// <c>new JointLimits()</c>, with or without an object initializer, starts from the engine's defaults;
    /// <c>default(JointLimits)</c> and array elements start from zero.
    /// </remarks>
    [StructLayout(LayoutKind.Sequential)]
    public struct JointLimits
    {
        /// <summary>Keeps the joint inside its range.</summary>
        public bool enabled = false;
        /// <summary>Hinge and ball twist: lowest angle. Slider and wheel suspension: lowest position. Distance: shortest length.</summary>
        public float lower = -45.0f;
        /// <summary>Hinge and ball twist: highest angle. Slider and wheel suspension: highest position. Distance: longest length.</summary>
        public float upper = 45.0f;
        /// <summary>Ball: half angle of the cone the axis may swing in, 0 to 90 degrees.</summary>
        public float coneAngle = 45.0f;

        /// <summary>Limits with the engine's defaults: off, -45 to 45, a 45 degree cone.</summary>
        public JointLimits()
        {
        }
    }

    /// <summary>
    /// Pulls a joint towards a target, stated as a frequency and a damping ratio, so it does not depend on the masses.
    /// </summary>
    /// <remarks>
    /// <c>new JointSpring()</c>, with or without an object initializer, starts from the engine's defaults;
    /// <c>default(JointSpring)</c> and array elements start from zero.
    /// </remarks>
    [StructLayout(LayoutKind.Sequential)]
    public struct JointSpring
    {
        /// <summary>Pulls the joint towards its target. A fixed joint with a spring is soft.</summary>
        public bool enabled = false;
        /// <summary>Oscillations per second. Higher is stiffer. Negative values count as 0.</summary>
        public float frequency = 2.0f;
        /// <summary>0 bounces forever, 1 settles without overshoot. Negative values count as 0.</summary>
        public float dampingRatio = 0.7f;
        /// <summary>Hinge: angle in degrees. Slider: position in meters.</summary>
        public float target = 0.0f;
        /// <summary>Ball: the rotation of body B against body A the spring turns towards, Euler angles in degrees.</summary>
        public Vector3 targetRotation = Vector3.zero;

        /// <summary>A spring with the engine's defaults: off, 2 oscillations per second, damping ratio 0.7.</summary>
        public JointSpring()
        {
        }
    }

    /// <summary>
    /// Drives a joint at a speed with at most a force (or torque).
    /// </summary>
    /// <remarks>
    /// <c>new JointMotor { enabled = true, speed = 45 }</c> keeps the engine's default <see cref="maxForce"/>;
    /// <c>default(JointMotor)</c> and array elements start from zero, which drives with no force.
    /// </remarks>
    [StructLayout(LayoutKind.Sequential)]
    public struct JointMotor
    {
        /// <summary>Drives the joint at its speed.</summary>
        public bool enabled = false;
        /// <summary>Hinge and wheel: degrees per second. Slider and distance: meters per second.</summary>
        public float speed = 90.0f;
        /// <summary>Hinge and wheel: newton meters. Slider and distance: newtons. Negative values count as 0.</summary>
        public float maxForce = 100.0f;

        /// <summary>A motor with the engine's defaults: off, 90 per second, 100 newton meters.</summary>
        public JointMotor()
        {
        }
    }

    /// <summary>
    /// Turns a wheel about its frame's Y axis, like a car's front wheels.
    /// </summary>
    /// <remarks>
    /// <c>new JointSteering()</c>, with or without an object initializer, starts from the engine's defaults;
    /// <c>default(JointSteering)</c> and array elements start from zero, which steers with no torque.
    /// </remarks>
    [StructLayout(LayoutKind.Sequential)]
    public struct JointSteering
    {
        /// <summary>Lets the wheel steer; otherwise it points straight ahead.</summary>
        public bool enabled = false;
        /// <summary>The angle the wheel steers to, in degrees.</summary>
        public float angle = 0.0f;
        /// <summary>The largest angle either way, in degrees. Negative values count as 0.</summary>
        public float limit = 35.0f;
        /// <summary>Newton meters available to steer with. Negative values count as 0.</summary>
        public float maxTorque = 500.0f;

        /// <summary>Steering with the engine's defaults: off, straight, 35 degrees either way, 500 newton meters.</summary>
        public JointSteering()
        {
        }
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

        /// <summary>
        /// The body the joint hangs on. <see cref="Entity.Invalid"/> hangs it on the world, and reads back for a body
        /// that was destroyed.
        /// </summary>
        public Entity bodyA
        {
            get => internal_m2n_joint_get_body_a(owner);
            set => internal_m2n_joint_set_body_a(owner, value);
        }

        /// <summary>
        /// The body the joint holds. <see cref="Entity.Invalid"/> takes the joint's own entity, and reads back for a
        /// body that was destroyed.
        /// </summary>
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

        /// <summary>Distance joint: the point it holds on body B, in body B's space.</summary>
        public Vector3 connectedAnchor
        {
            get => internal_m2n_joint_get_connected_anchor(owner);
            set => internal_m2n_joint_set_connected_anchor(owner, value);
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
                internal_m2n_joint_get_limits(owner, out JointLimits result);
                return result;
            }
            set => internal_m2n_joint_set_limits(owner, value);
        }

        /// <summary>The spring that pulls the joint to a target; for a wheel, the suspension.</summary>
        public JointSpring spring
        {
            get
            {
                internal_m2n_joint_get_spring(owner, out JointSpring result);
                return result;
            }
            set => internal_m2n_joint_set_spring(owner, value);
        }

        /// <summary>The motor that drives the joint; for a wheel, its spin.</summary>
        public JointMotor motor
        {
            get
            {
                internal_m2n_joint_get_motor(owner, out JointMotor result);
                return result;
            }
            set => internal_m2n_joint_set_motor(owner, value);
        }

        /// <summary>Wheel steering.</summary>
        public JointSteering steering
        {
            get
            {
                internal_m2n_joint_get_steering(owner, out JointSteering result);
                return result;
            }
            set => internal_m2n_joint_set_steering(owner, value);
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
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern Vector3 internal_m2n_joint_get_connected_anchor(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_set_connected_anchor(Entity eid, Vector3 value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern bool internal_m2n_joint_get_collide_connected(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_set_collide_connected(Entity eid, bool value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_get_limits(Entity eid, out JointLimits value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_set_limits(Entity eid, JointLimits value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_get_spring(Entity eid, out JointSpring value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_set_spring(Entity eid, JointSpring value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_get_motor(Entity eid, out JointMotor value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_set_motor(Entity eid, JointMotor value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_get_steering(Entity eid, out JointSteering value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_joint_set_steering(Entity eid, JointSteering value);
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
