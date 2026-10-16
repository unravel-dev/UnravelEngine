using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Plants a character feet on uneven ground: probes the ground under each foot, lowers the
    /// pelvis to the lowest planted foot, solves each leg with two-bone IK and tilts the feet onto
    /// the slope. Sits on the character root; the probes need physics, so it acts in play mode.
    /// </summary>
    public class FootIKComponent : Component
    {
        /// <summary>
        /// Left foot bone.
        /// </summary>
        public Entity leftFoot
        {
            get => internal_m2n_foot_ik_get_left_foot(owner);
            set => internal_m2n_foot_ik_set_left_foot(owner, value);
        }

        /// <summary>
        /// Right foot bone.
        /// </summary>
        public Entity rightFoot
        {
            get => internal_m2n_foot_ik_get_right_foot(owner);
            set => internal_m2n_foot_ik_set_right_foot(owner, value);
        }

        /// <summary>
        /// Pelvis (hips) bone lowered to the lower foot.
        /// </summary>
        public Entity pelvis
        {
            get => internal_m2n_foot_ik_get_pelvis(owner);
            set => internal_m2n_foot_ik_set_pelvis(owner, value);
        }

        /// <summary>
        /// Physics layers the ground probes hit.
        /// </summary>
        public LayerMask groundLayers
        {
            get => internal_m2n_foot_ik_get_ground_layers(owner);
            set => internal_m2n_foot_ik_set_ground_layers(owner, value);
        }

        /// <summary>
        /// Probe start above the animated foot, in meters.
        /// </summary>
        public float probeUp
        {
            get => internal_m2n_foot_ik_get_probe_up(owner);
            set => internal_m2n_foot_ik_set_probe_up(owner, value);
        }

        /// <summary>
        /// Probe reach below the animated foot, in meters.
        /// </summary>
        public float probeDown
        {
            get => internal_m2n_foot_ik_get_probe_down(owner);
            set => internal_m2n_foot_ik_set_probe_down(owner, value);
        }

        /// <summary>
        /// Foot height above the root where the IK starts fading out (swing).
        /// </summary>
        public float liftStart
        {
            get => internal_m2n_foot_ik_get_lift_start(owner);
            set => internal_m2n_foot_ik_set_lift_start(owner, value);
        }

        /// <summary>
        /// Foot height above the root with no IK left.
        /// </summary>
        public float liftEnd
        {
            get => internal_m2n_foot_ik_get_lift_end(owner);
            set => internal_m2n_foot_ik_set_lift_end(owner, value);
        }

        /// <summary>
        /// Ground this far below a foot starts fading its IK out (ledges).
        /// </summary>
        public float dropStart
        {
            get => internal_m2n_foot_ik_get_drop_start(owner);
            set => internal_m2n_foot_ik_set_drop_start(owner, value);
        }

        /// <summary>
        /// Ground this far below a foot has no IK left.
        /// </summary>
        public float dropEnd
        {
            get => internal_m2n_foot_ik_get_drop_end(owner);
            set => internal_m2n_foot_ik_set_drop_end(owner, value);
        }

        /// <summary>
        /// The pelvis drops at most this far to reach the lower foot.
        /// </summary>
        public float maxPelvisDrop
        {
            get => internal_m2n_foot_ik_get_max_pelvis_drop(owner);
            set => internal_m2n_foot_ik_set_max_pelvis_drop(owner, value);
        }

        /// <summary>
        /// Time constant of the pelvis and foot weight smoothing, in seconds.
        /// </summary>
        public float smoothing
        {
            get => internal_m2n_foot_ik_get_smoothing(owner);
            set => internal_m2n_foot_ik_set_smoothing(owner, value);
        }

        /// <summary>
        /// How much the feet tilt onto the ground slope, 0..1.
        /// </summary>
        public float alignToGround
        {
            get => internal_m2n_foot_ik_get_align_to_ground(owner);
            set => internal_m2n_foot_ik_set_align_to_ground(owner, value);
        }

        /// <summary>
        /// For legs the animation holds straight: knee pole distance in front of the pelvis, in
        /// meters. Bent legs keep their animated bend.
        /// </summary>
        public float kneeForward
        {
            get => internal_m2n_foot_ik_get_knee_forward(owner);
            set => internal_m2n_foot_ik_set_knee_forward(owner, value);
        }

        /// <summary>
        /// For legs the animation holds straight: knee pole distance out to each side, in meters.
        /// </summary>
        public float kneeOutward
        {
            get => internal_m2n_foot_ik_get_knee_outward(owner);
            set => internal_m2n_foot_ik_set_knee_outward(owner, value);
        }

        /// <summary>
        /// Blend against the animated pose, 0..1.
        /// </summary>
        public float weight
        {
            get => internal_m2n_foot_ik_get_weight(owner);
            set => internal_m2n_foot_ik_set_weight(owner, value);
        }

        /// <summary>
        /// Animation curve the weight is multiplied by (0 while no playing clip has it). Empty for none.
        /// </summary>
        public string weightCurve
        {
            get => internal_m2n_foot_ik_get_weight_curve(owner);
            set => internal_m2n_foot_ik_set_weight_curve(owner, value);
        }

        /// <summary>
        /// Current vertical pelvis offset, in meters.
        /// </summary>
        public float pelvisOffset => internal_m2n_foot_ik_get_pelvis_offset(owner);

        /// <summary>
        /// Current IK weight of the left leg.
        /// </summary>
        public float leftFootWeight => internal_m2n_foot_ik_get_left_weight(owner);

        /// <summary>
        /// Current IK weight of the right leg.
        /// </summary>
        public float rightFootWeight => internal_m2n_foot_ik_get_right_weight(owner);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_foot_ik_get_left_foot(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_foot_ik_set_left_foot(Entity eid, Entity value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_foot_ik_get_right_foot(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_foot_ik_set_right_foot(Entity eid, Entity value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Entity internal_m2n_foot_ik_get_pelvis(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_foot_ik_set_pelvis(Entity eid, Entity value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern LayerMask internal_m2n_foot_ik_get_ground_layers(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_foot_ik_set_ground_layers(Entity eid, LayerMask value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_foot_ik_get_probe_up(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_foot_ik_set_probe_up(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_foot_ik_get_probe_down(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_foot_ik_set_probe_down(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_foot_ik_get_lift_start(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_foot_ik_set_lift_start(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_foot_ik_get_lift_end(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_foot_ik_set_lift_end(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_foot_ik_get_drop_start(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_foot_ik_set_drop_start(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_foot_ik_get_drop_end(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_foot_ik_set_drop_end(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_foot_ik_get_max_pelvis_drop(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_foot_ik_set_max_pelvis_drop(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_foot_ik_get_smoothing(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_foot_ik_set_smoothing(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_foot_ik_get_align_to_ground(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_foot_ik_set_align_to_ground(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_foot_ik_get_knee_forward(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_foot_ik_set_knee_forward(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_foot_ik_get_knee_outward(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_foot_ik_set_knee_outward(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_foot_ik_get_weight(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_foot_ik_set_weight(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_foot_ik_get_weight_curve(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_foot_ik_set_weight_curve(Entity eid, string value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_foot_ik_get_pelvis_offset(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_foot_ik_get_left_weight(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_foot_ik_get_right_weight(Entity eid);
    }
}
