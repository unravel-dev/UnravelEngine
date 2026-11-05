using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Unravel.Core
{
    /// <summary>
    /// Who moves a ragdoll's bones.
    /// </summary>
    public enum RagdollState : byte
    {
        /// <summary>The animation moves the bones; the bodies follow it as hitboxes that push props and stop rays.</summary>
        Animated,
        /// <summary>Physics moves the bones: the body falls, held together by its joints.</summary>
        Simulated,
        /// <summary>Physics moves the bones while drives pull them to the animation: hits knock the body about and it recovers.</summary>
        Powered,
    }

    /// <summary>
    /// How a ragdoll body turns against its parent body.
    /// </summary>
    public enum RagdollJointType : byte
    {
        /// <summary>Swings in a cone about the joint axis and twists about it (shoulders, hips, spine, neck).</summary>
        Ball,
        /// <summary>Turns about the joint axis only (elbows, knees).</summary>
        Hinge,
        /// <summary>Does not turn.</summary>
        Fixed,
    }

    /// <summary>
    /// The settings of one ragdoll body: its mass and its joint to the parent body. Angles are in degrees.
    /// </summary>
    /// <remarks>
    /// The bone a body moves is read with <see cref="RagdollComponent.GetBodyBone"/>. Body shapes are set in the
    /// inspector or by <see cref="RagdollComponent.AutoSetup"/>; scripts do not change them. <c>new RagdollBody()</c>,
    /// with or without an object initializer, starts from the engine's defaults; <c>default(RagdollBody)</c> and array
    /// elements start from zero.
    /// </remarks>
    [StructLayout(LayoutKind.Sequential)]
    public struct RagdollBody
    {
        /// <summary>Mass in kilograms.</summary>
        public float mass = 1.0f;
        /// <summary>How the body turns against its parent body.</summary>
        public RagdollJointType joint = RagdollJointType.Ball;
        /// <summary>Turn of the joint frame against the bone, Euler angles. The frame's X axis is the twist or hinge axis.</summary>
        public Vector3 jointRotation = Vector3.zero;
        /// <summary>
        /// Ball: turn of the swing cone's centre away from the bone's rest direction, Euler angles in the joint frame,
        /// so a limb can rest away from its bind pose (arms hanging from a T-pose).
        /// </summary>
        public Vector3 swingOffset = Vector3.zero;
        /// <summary>Ball: half angle of the swing cone, at most 90.</summary>
        public float swingLimit = 30.0f;
        /// <summary>Ball: lowest twist. Hinge: lowest angle.</summary>
        public float twistLower = -20.0f;
        /// <summary>Ball: highest twist. Hinge: highest angle.</summary>
        public float twistUpper = 20.0f;
        /// <summary>Multiplies the ragdoll's <see cref="RagdollComponent.jointFriction"/> for this joint.</summary>
        public float frictionScale = 1.0f;
        /// <summary>Multiplies the ragdoll's drives for this body.</summary>
        public float driveScale = 1.0f;

        /// <summary>A body with the engine's defaults: 1 kg on a ball joint with a 30 degree cone and a twist of 20 degrees either way.</summary>
        public RagdollBody()
        {
        }
    }

    /// <summary>
    /// How hard a powered ragdoll, or a simulated part of an animated one, follows the animation. Frequencies are
    /// oscillations per second, damping ratios run from 0 (bounces) to 1 (settles without overshoot); negative values
    /// count as 0.
    /// </summary>
    /// <remarks>
    /// <c>new RagdollDrive()</c>, with or without an object initializer, starts from the engine's defaults;
    /// <c>default(RagdollDrive)</c> and array elements start from zero, which turns every drive off.
    /// </remarks>
    [StructLayout(LayoutKind.Sequential)]
    public struct RagdollDrive
    {
        /// <summary>Joint springs towards the animated rotation against the parent body; 0 turns them off.</summary>
        public float localFrequency = 5.0f;
        /// <summary>Damping ratio of the joint springs.</summary>
        public float localDampingRatio = 1.0f;
        /// <summary>Pull of every powered body towards its animated place in the world, which keeps it standing; 0 turns it off.</summary>
        public float worldFrequency = 0.0f;
        /// <summary>Damping ratio of the world pull.</summary>
        public float worldDampingRatio = 1.0f;
        /// <summary>Newtons the world pull uses at most, per body.</summary>
        public float worldMaxForce = 800.0f;
        /// <summary>Newton meters the world pull uses at most, per body.</summary>
        public float worldMaxTorque = 150.0f;

        /// <summary>Drives with the engine's defaults: joint springs at 5, no world pull.</summary>
        public RagdollDrive()
        {
        }
    }

    /// <summary>
    /// Physics bodies on the bones of a skinned model, held together by joints.
    /// </summary>
    /// <remarks>
    /// Bodies are bound to bones by name, in the bones' space. While playing, an animated ragdoll's bodies follow the
    /// animation as kinematic hitboxes; <see cref="Simulate"/> hands the bones to physics with the momentum the
    /// animation gave them; <see cref="Recover"/> blends them back. A ray cast reports the bone entity it hit: pass it to
    /// <see cref="FindBody(Entity)"/> to push that body.
    /// <para>
    /// A body is given by its index, 0 to <see cref="bodyCount"/> - 1; every per-body call ignores another index.
    /// Body poses and velocities need the running ragdoll: they read zero until play has prepared it for its model.
    /// </para>
    /// </remarks>
    public class RagdollComponent : Component
    {
        /// <summary>Who moves the bones now. Setting it blends back to the animation over <see cref="blendTime"/>.</summary>
        public RagdollState state
        {
            get => (RagdollState)internal_m2n_ragdoll_get_state(owner);
            set => internal_m2n_ragdoll_set_state(owner, (byte)value, -1.0f);
        }

        /// <summary>Who moves the bones when play starts.</summary>
        public RagdollState startState
        {
            get => (RagdollState)internal_m2n_ragdoll_get_start_state(owner);
            set => internal_m2n_ragdoll_set_start_state(owner, (byte)value);
        }

        /// <summary>Seconds the bones take to blend back to the animation.</summary>
        public float blendTime
        {
            get => internal_m2n_ragdoll_get_blend_time(owner);
            set => internal_m2n_ragdoll_set_blend_time(owner, value);
        }

        /// <summary>Newton meters a joint resists turning with: low values flop, high values look stiff.</summary>
        public float jointFriction
        {
            get => internal_m2n_ragdoll_get_joint_friction(owner);
            set => internal_m2n_ragdoll_set_joint_friction(owner, value);
        }

        /// <summary>Multiplies every drive, 0..1. Lowering it over time turns a staggering ragdoll into a falling one.</summary>
        public float driveStrength
        {
            get => internal_m2n_ragdoll_get_drive_strength(owner);
            set => internal_m2n_ragdoll_set_drive_strength(owner, value);
        }

        /// <summary>How hard the bodies follow the animation while they simulate.</summary>
        public RagdollDrive drive
        {
            get
            {
                internal_m2n_ragdoll_get_drive(owner, out RagdollDrive result);
                return result;
            }
            set => internal_m2n_ragdoll_set_drive(owner, value);
        }

        /// <summary>Joint springs towards the animated rotation against the parent body, oscillations per second: <see cref="RagdollDrive.localFrequency"/> of <see cref="drive"/>.</summary>
        public float localDriveFrequency
        {
            get => drive.localFrequency;
            set
            {
                RagdollDrive changed = drive;
                changed.localFrequency = value;
                drive = changed;
            }
        }

        /// <summary>Pull of every powered body towards its animated place in the world, oscillations per second. Keeps it standing: <see cref="RagdollDrive.worldFrequency"/> of <see cref="drive"/>.</summary>
        public float worldDriveFrequency
        {
            get => drive.worldFrequency;
            set
            {
                RagdollDrive changed = drive;
                changed.worldFrequency = value;
                drive = changed;
            }
        }

        /// <summary>How fast the bodies lose speed, per second.</summary>
        public float linearDamping
        {
            get => internal_m2n_ragdoll_get_linear_damping(owner);
            set => internal_m2n_ragdoll_set_linear_damping(owner, value);
        }

        /// <summary>How fast the bodies lose spin, per second.</summary>
        public float angularDamping
        {
            get => internal_m2n_ragdoll_get_angular_damping(owner);
            set => internal_m2n_ragdoll_set_angular_damping(owner, value);
        }

        /// <summary>
        /// Whether bodies that are not parent and child collide with each other, so arms do not pass through the chest.
        /// A change during play makes the bodies again.
        /// </summary>
        public bool selfCollision
        {
            get => internal_m2n_ragdoll_get_self_collision(owner);
            set => internal_m2n_ragdoll_set_self_collision(owner, value);
        }

        /// <summary>Continuous collision for the bodies, so a fast fall does not pass through thin geometry.</summary>
        public bool isContinuous
        {
            get => internal_m2n_ragdoll_get_is_continuous(owner);
            set => internal_m2n_ragdoll_set_is_continuous(owner, value);
        }

        /// <summary>Layers the bodies collide with (include filter).</summary>
        public LayerMask includeLayers
        {
            get => internal_m2n_ragdoll_get_include_layers(owner);
            set => internal_m2n_ragdoll_set_include_layers(owner, value);
        }

        /// <summary>Layers the bodies ignore (exclude filter).</summary>
        public LayerMask excludeLayers
        {
            get => internal_m2n_ragdoll_get_exclude_layers(owner);
            set => internal_m2n_ragdoll_set_exclude_layers(owner, value);
        }

        /// <summary>Number of bodies the ragdoll defines.</summary>
        public int bodyCount => internal_m2n_ragdoll_get_body_count(owner);

        /// <summary>Number of bone pairs whose bodies never collide with each other.</summary>
        public int disabledCollisionCount => internal_m2n_ragdoll_get_disabled_collision_count(owner);

        /// <summary>Mean speed of the bodies in meters per second.</summary>
        public float averageSpeed => internal_m2n_ragdoll_get_average_speed(owner);

        /// <summary>Seconds the simulating bodies have been nearly still: wait for it before getting up.</summary>
        public float restTime => internal_m2n_ragdoll_get_rest_time(owner);

        /// <summary>Whether the root body's front faces the sky (the character lies on its back).</summary>
        public bool isFaceUp => internal_m2n_ragdoll_is_face_up(owner);

        /// <summary>Where the root body (the hips) is in the world.</summary>
        public Vector3 rootPosition => internal_m2n_ragdoll_get_root_position(owner);

        /// <summary>The root body's rotation in the world.</summary>
        public Quaternion rootRotation => internal_m2n_ragdoll_get_root_rotation(owner);

        /// <summary>Hands the bones to physics, keeping the momentum the animation gave them.</summary>
        public void Simulate()
        {
            internal_m2n_ragdoll_set_state(owner, (byte)RagdollState.Simulated, -1.0f);
        }

        /// <summary>Hands the bones to physics while drives pull them to the animation.</summary>
        public void Power()
        {
            internal_m2n_ragdoll_set_state(owner, (byte)RagdollState.Powered, -1.0f);
        }

        /// <summary>
        /// Blends the bones back to the animation over <paramref name="blendTime"/> seconds (a negative time takes
        /// <see cref="blendTime"/>).
        /// </summary>
        public void Recover(float blendTime = -1.0f)
        {
            internal_m2n_ragdoll_set_state(owner, (byte)RagdollState.Animated, blendTime);
        }

        /// <summary>
        /// Simulates the body of a bone, and the bodies below it with <paramref name="includeChildren"/>, while the rest
        /// follows the animation: a limp arm, a stagger. Simulated parts of an animated ragdoll are pulled back to the
        /// animation by the local drive; stopping blends them back.
        /// </summary>
        /// <returns>False when no body belongs to the bone.</returns>
        public bool SetBoneSimulated(string bone, bool simulated, bool includeChildren = true, float blendTime = -1.0f)
        {
            return internal_m2n_ragdoll_set_bone_simulated(owner, bone, simulated, includeChildren, blendTime);
        }

        /// <summary>Whether the body at <paramref name="body"/> simulates.</summary>
        public bool IsBodySimulated(int body)
        {
            return internal_m2n_ragdoll_is_body_simulated(owner, body);
        }

        /// <summary>The index of a bone's body by bone name, -1 when it has none.</summary>
        public int FindBody(string bone)
        {
            return internal_m2n_ragdoll_find_body_by_name(owner, bone);
        }

        /// <summary>
        /// The index of the body a bone entity belongs to (its own or its nearest ancestor's), -1 when none: what a
        /// ray cast's hit entity maps to.
        /// </summary>
        public int FindBody(Entity bone)
        {
            return internal_m2n_ragdoll_find_body(owner, bone);
        }

        /// <summary>The name of the bone a body moves; empty for an index out of range.</summary>
        public string GetBodyBone(int body)
        {
            return internal_m2n_ragdoll_get_body_bone(owner, body);
        }

        /// <summary>The settings of a body; the engine's defaults for an index out of range.</summary>
        public RagdollBody GetBody(int body)
        {
            internal_m2n_ragdoll_get_body(owner, body, out RagdollBody result);
            return result;
        }

        /// <summary>
        /// Changes the settings of a body. A change during play makes the bodies again.
        /// </summary>
        /// <returns>False, and nothing changes, for an index out of range or an unknown joint type.</returns>
        public bool SetBody(int body, RagdollBody settings)
        {
            return internal_m2n_ragdoll_set_body(owner, body, settings);
        }

        /// <summary>The world position of a body. Zero until play has prepared the ragdoll.</summary>
        public Vector3 GetBodyPosition(int body)
        {
            return internal_m2n_ragdoll_get_body_position(owner, body);
        }

        /// <summary>The velocity of a body in meters per second. Zero until play has prepared the ragdoll.</summary>
        public Vector3 GetBodyVelocity(int body)
        {
            return internal_m2n_ragdoll_get_body_velocity(owner, body);
        }

        /// <summary>
        /// The bones of a pair whose bodies never collide with each other, <paramref name="pair"/> from 0 to
        /// <see cref="disabledCollisionCount"/> - 1; empty names for an index out of range.
        /// </summary>
        public void GetDisabledCollision(int pair, out string boneA, out string boneB)
        {
            boneA = internal_m2n_ragdoll_get_disabled_collision_bone_a(owner, pair);
            boneB = internal_m2n_ragdoll_get_disabled_collision_bone_b(owner, pair);
        }

        /// <summary>Whether the bodies of two bones, named in either order, never collide with each other.</summary>
        public bool IsCollisionDisabled(string boneA, string boneB)
        {
            return internal_m2n_ragdoll_is_collision_disabled(owner, boneA, boneB);
        }

        /// <summary>
        /// Stops or restores collisions between the bodies of two bones, named in either order, as the inspector's
        /// disabled collision list does. A change during play makes the bodies again.
        /// </summary>
        public void SetCollisionDisabled(string boneA, string boneB, bool disabled)
        {
            internal_m2n_ragdoll_set_collision_disabled(owner, boneA, boneB, disabled);
        }

        /// <summary>Pushes one body at a world point, which also turns it. Only simulating bodies move.</summary>
        public void AddImpulse(int body, Vector3 impulse, Vector3 point)
        {
            internal_m2n_ragdoll_add_impulse_at(owner, body, impulse, point);
        }

        /// <summary>Pushes one body through its center. Only simulating bodies move.</summary>
        public void AddImpulse(int body, Vector3 impulse)
        {
            internal_m2n_ragdoll_add_impulse(owner, body, impulse);
        }

        /// <summary>Gives every simulating body the same change in velocity.</summary>
        public void AddVelocity(Vector3 velocityChange)
        {
            internal_m2n_ragdoll_add_velocity(owner, velocityChange);
        }

        /// <summary>
        /// Turns every simulating body as one about <paramref name="pivot"/>: each gets the angular velocity (radians per
        /// second, the axis by the right-hand rule) and the velocity its place on the turning body has. Rolls a lying
        /// body over.
        /// </summary>
        public void AddAngularVelocity(Vector3 angularVelocity, Vector3 pivot)
        {
            internal_m2n_ragdoll_add_angular_velocity(owner, angularVelocity, pivot);
        }

        /// <summary>Pushes every simulating body away from a point, weaker with distance, shared by mass.</summary>
        public void AddExplosion(Vector3 center, float radius, float impulse)
        {
            internal_m2n_ragdoll_add_explosion(owner, center, radius, impulse);
        }

        /// <summary>Puts every body back on the animated pose, without velocity, at the next step.</summary>
        public void ResetToAnimation()
        {
            internal_m2n_ragdoll_reset_to_animation(owner);
        }

        /// <summary>
        /// The pose a character should stand in to get up from where the ragdoll lies: the point under the hips, turned
        /// so a get-up animation's lying direction matches the body's.
        /// </summary>
        /// <param name="headDirection">The direction from the hips to the head in the get-up animation's first pose, in
        /// the character's space (usually -Z for getting up from the back, +Z from the front).</param>
        /// <param name="position">Where to put the character.</param>
        /// <param name="rotation">How to turn the character.</param>
        public void GetRecoveryPose(Vector3 headDirection, out Vector3 position, out Quaternion rotation)
        {
            internal_m2n_ragdoll_get_recovery_pose(owner, headDirection, out position, out rotation);
        }

        /// <summary>Rebuilds the bodies of a humanoid from its skeleton and skin, like the inspector's Auto Setup.</summary>
        /// <param name="totalMass">Kilograms shared out over the bodies, more than 0.</param>
        /// <returns>False when the mass is not more than 0, the model is not a humanoid or its mesh is not loaded yet.</returns>
        public bool AutoSetup(float totalMass = 70.0f)
        {
            return internal_m2n_ragdoll_auto_setup(owner, totalMass);
        }

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern byte internal_m2n_ragdoll_get_state(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_set_state(Entity eid, byte state, float blendTime);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_ragdoll_get_blend_time(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_set_blend_time(Entity eid, float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_ragdoll_get_joint_friction(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_set_joint_friction(Entity eid, float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_ragdoll_get_drive_strength(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_set_drive_strength(Entity eid, float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_get_drive(Entity eid, out RagdollDrive value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_set_drive(Entity eid, RagdollDrive value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern byte internal_m2n_ragdoll_get_start_state(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_set_start_state(Entity eid, byte state);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_ragdoll_get_linear_damping(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_set_linear_damping(Entity eid, float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_ragdoll_get_angular_damping(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_set_angular_damping(Entity eid, float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern bool internal_m2n_ragdoll_get_self_collision(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_set_self_collision(Entity eid, bool value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern bool internal_m2n_ragdoll_get_is_continuous(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_set_is_continuous(Entity eid, bool value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern LayerMask internal_m2n_ragdoll_get_include_layers(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_set_include_layers(Entity eid, LayerMask mask);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern LayerMask internal_m2n_ragdoll_get_exclude_layers(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_set_exclude_layers(Entity eid, LayerMask mask);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int internal_m2n_ragdoll_get_body_count(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_ragdoll_get_average_speed(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_ragdoll_get_rest_time(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern bool internal_m2n_ragdoll_is_face_up(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern Vector3 internal_m2n_ragdoll_get_root_position(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern Quaternion internal_m2n_ragdoll_get_root_rotation(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern bool internal_m2n_ragdoll_set_bone_simulated(Entity eid, string bone, bool simulated, bool includeChildren, float blendTime);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern bool internal_m2n_ragdoll_is_body_simulated(Entity eid, int body);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int internal_m2n_ragdoll_find_body_by_name(Entity eid, string bone);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int internal_m2n_ragdoll_find_body(Entity eid, Entity bone);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern string internal_m2n_ragdoll_get_body_bone(Entity eid, int body);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_get_body(Entity eid, int body, out RagdollBody value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern bool internal_m2n_ragdoll_set_body(Entity eid, int body, RagdollBody value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern Vector3 internal_m2n_ragdoll_get_body_position(Entity eid, int body);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern Vector3 internal_m2n_ragdoll_get_body_velocity(Entity eid, int body);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int internal_m2n_ragdoll_get_disabled_collision_count(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern string internal_m2n_ragdoll_get_disabled_collision_bone_a(Entity eid, int pair);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern string internal_m2n_ragdoll_get_disabled_collision_bone_b(Entity eid, int pair);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern bool internal_m2n_ragdoll_is_collision_disabled(Entity eid, string boneA, string boneB);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_set_collision_disabled(Entity eid, string boneA, string boneB, bool disabled);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_add_impulse_at(Entity eid, int body, Vector3 impulse, Vector3 point);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_add_impulse(Entity eid, int body, Vector3 impulse);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_add_velocity(Entity eid, Vector3 velocityChange);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_add_angular_velocity(Entity eid, Vector3 angularVelocity, Vector3 pivot);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_add_explosion(Entity eid, Vector3 center, float radius, float impulse);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_reset_to_animation(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_get_recovery_pose(Entity eid, Vector3 headDirection, out Vector3 position, out Quaternion rotation);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern bool internal_m2n_ragdoll_auto_setup(Entity eid, float totalMass);
    }
}
