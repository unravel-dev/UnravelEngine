using System;
using System.Runtime.CompilerServices;

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
    /// Physics bodies on the bones of a skinned model, held together by joints.
    /// </summary>
    /// <remarks>
    /// Bodies are bound to bones by name, in the bones' space. While playing, an animated ragdoll's bodies follow the
    /// animation as kinematic hitboxes; <see cref="Simulate"/> hands the bones to physics with the momentum the
    /// animation gave them; <see cref="Recover"/> blends them back. A ray cast reports the bone entity it hit: pass it to
    /// <see cref="FindBody(Entity)"/> to push that body.
    /// </remarks>
    public class RagdollComponent : Component
    {
        /// <summary>Who moves the bones now. Setting it blends back to the animation over <see cref="blendTime"/>.</summary>
        public RagdollState state
        {
            get => (RagdollState)internal_m2n_ragdoll_get_state(owner);
            set => internal_m2n_ragdoll_set_state(owner, (byte)value, -1.0f);
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

        /// <summary>Joint springs towards the animated rotation against the parent body, oscillations per second.</summary>
        public float localDriveFrequency
        {
            get => internal_m2n_ragdoll_get_local_drive(owner);
            set => internal_m2n_ragdoll_set_local_drive(owner, value);
        }

        /// <summary>Pull of every powered body towards its animated place in the world, oscillations per second. Keeps it standing.</summary>
        public float worldDriveFrequency
        {
            get => internal_m2n_ragdoll_get_world_drive(owner);
            set => internal_m2n_ragdoll_set_world_drive(owner, value);
        }

        /// <summary>Number of bodies.</summary>
        public int bodyCount => internal_m2n_ragdoll_get_body_count(owner);

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

        /// <summary>The name of the bone a body moves.</summary>
        public string GetBodyBone(int body)
        {
            return internal_m2n_ragdoll_get_body_bone(owner, body);
        }

        /// <summary>The world position of a body.</summary>
        public Vector3 GetBodyPosition(int body)
        {
            return internal_m2n_ragdoll_get_body_position(owner, body);
        }

        /// <summary>The velocity of a body in meters per second.</summary>
        public Vector3 GetBodyVelocity(int body)
        {
            return internal_m2n_ragdoll_get_body_velocity(owner, body);
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
            position = internal_m2n_ragdoll_get_recovery_position(owner, headDirection);
            rotation = internal_m2n_ragdoll_get_recovery_rotation(owner, headDirection);
        }

        /// <summary>Rebuilds the bodies of a humanoid from its skeleton and skin, like the inspector's Auto Setup.</summary>
        /// <param name="totalMass">Kilograms shared out over the bodies.</param>
        /// <returns>False when the model is not a humanoid or its mesh is not loaded yet.</returns>
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
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_ragdoll_get_local_drive(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_set_local_drive(Entity eid, float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern float internal_m2n_ragdoll_get_world_drive(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_set_world_drive(Entity eid, float value);
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
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern Vector3 internal_m2n_ragdoll_get_body_position(Entity eid, int body);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern Vector3 internal_m2n_ragdoll_get_body_velocity(Entity eid, int body);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_add_impulse_at(Entity eid, int body, Vector3 impulse, Vector3 point);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_add_impulse(Entity eid, int body, Vector3 impulse);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_add_velocity(Entity eid, Vector3 velocityChange);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_add_angular_velocity(Entity eid, Vector3 angularVelocity, Vector3 pivot);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_add_explosion(Entity eid, Vector3 center, float radius, float impulse);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void internal_m2n_ragdoll_reset_to_animation(Entity eid);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern Vector3 internal_m2n_ragdoll_get_recovery_position(Entity eid, Vector3 headDirection);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern Quaternion internal_m2n_ragdoll_get_recovery_rotation(Entity eid, Vector3 headDirection);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern bool internal_m2n_ragdoll_auto_setup(Entity eid, float totalMass);
    }
}
