using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Spatial volume mode for post-process blending.
    /// </summary>
    public enum VolumeMode : byte
    {
        /// <summary>Local box volume centered on the entity.</summary>
        Local = 0,
        /// <summary>Global volume that affects the camera everywhere.</summary>
        Global = 1,
    }

    /// <summary>
    /// Spatial volume that applies post-processing effects from sibling components.
    /// </summary>
    public class VolumeComponent : Component
    {
        /// <summary>
        /// Whether this volume is local (bounded) or global.
        /// </summary>
        public VolumeMode mode
        {
            get => (VolumeMode)internal_m2n_volume_get_mode(owner);
            set => internal_m2n_volume_set_mode(owner, (byte)value);
        }

        /// <summary>
        /// Blend priority when multiple volumes overlap. Higher values win.
        /// </summary>
        public int priority
        {
            get => internal_m2n_volume_get_priority(owner);
            set => internal_m2n_volume_set_priority(owner, value);
        }

        /// <summary>
        /// Influence multiplier in the range [0, 1].
        /// </summary>
        public float weight
        {
            get => internal_m2n_volume_get_weight(owner);
            set => internal_m2n_volume_set_weight(owner, value);
        }

        /// <summary>
        /// Distance outside a local volume over which influence ramps to zero.
        /// </summary>
        public float blendDistance
        {
            get => internal_m2n_volume_get_blend_distance(owner);
            set => internal_m2n_volume_set_blend_distance(owner, value);
        }

        /// <summary>
        /// Half-extents of the local box bounds, centered at the entity origin.
        /// </summary>
        public Vector3 extents
        {
            get => internal_m2n_volume_get_extents(owner);
            set => internal_m2n_volume_set_extents(owner, value);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte internal_m2n_volume_get_mode(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_volume_set_mode(Entity eid, byte mode);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_volume_get_priority(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_volume_set_priority(Entity eid, int priority);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_volume_get_weight(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_volume_set_weight(Entity eid, float weight);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_volume_get_blend_distance(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_volume_set_blend_distance(Entity eid, float distance);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_volume_get_extents(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_volume_set_extents(Entity eid, Vector3 extents);
    }
}
