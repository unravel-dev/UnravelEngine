using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Reflection probe shape type.
    /// </summary>
    public enum ProbeType : byte
    {
        /// <summary>Axis-aligned box projection.</summary>
        Box = 0,
        /// <summary>Spherical projection.</summary>
        Sphere = 1,
    }

    /// <summary>
    /// Reflection probe capture method.
    /// </summary>
    public enum ReflectMethod : byte
    {
        /// <summary>Capture the full environment.</summary>
        Environment = 0,
        /// <summary>Capture static geometry only.</summary>
        StaticOnly = 1,
    }

    /// <summary>
    /// When the probe refreshes its cubemap.
    /// </summary>
    public enum ProbeUpdateMode : byte
    {
        /// <summary>Refresh only when <see cref="ReflectionProbeComponent.MarkDirty"/> is called.</summary>
        OnDemand = 0,
        /// <summary>Bake once on load or edit, then stop until marked dirty.</summary>
        Once = 1,
        /// <summary>Continuously refresh, time-sliced by faces and update interval.</summary>
        Realtime = 2,
    }

    /// <summary>
    /// Cubemap face resolution for reflection probes.
    /// </summary>
    public enum ProbeResolution : byte
    {
        /// <summary>16x16 faces.</summary>
        Res16 = 0,
        /// <summary>32x32 faces.</summary>
        Res32 = 1,
        /// <summary>64x64 faces.</summary>
        Res64 = 2,
        /// <summary>128x128 faces.</summary>
        Res128 = 3,
        /// <summary>256x256 faces.</summary>
        Res256 = 4,
        /// <summary>512x512 faces.</summary>
        Res512 = 5,
        /// <summary>1024x1024 faces.</summary>
        Res1024 = 6,
    }

    /// <summary>
    /// Captures and provides reflection probe data for an entity.
    /// </summary>
    public class ReflectionProbeComponent : Component
    {
        /// <summary>
        /// Probe projection shape (box or sphere).
        /// </summary>
        public ProbeType type
        {
            get => (ProbeType)internal_m2n_probe_get_type(owner);
            set => internal_m2n_probe_set_type(owner, (byte)value);
        }

        /// <summary>
        /// Capture method used when baking the cubemap.
        /// </summary>
        public ReflectMethod method
        {
            get => (ReflectMethod)internal_m2n_probe_get_method(owner);
            set => internal_m2n_probe_set_method(owner, (byte)value);
        }

        /// <summary>
        /// Reflection intensity multiplier.
        /// </summary>
        public float intensity
        {
            get => internal_m2n_probe_get_intensity(owner);
            set => internal_m2n_probe_set_intensity(owner, value);
        }

        /// <summary>
        /// Policy controlling when the cubemap is refreshed.
        /// </summary>
        public ProbeUpdateMode updateMode
        {
            get => (ProbeUpdateMode)internal_m2n_probe_get_update_mode(owner);
            set => internal_m2n_probe_set_update_mode(owner, (byte)value);
        }

        /// <summary>
        /// Realtime refresh interval in seconds. Zero means every available frame.
        /// </summary>
        public float updateInterval
        {
            get => internal_m2n_probe_get_update_interval(owner);
            set => internal_m2n_probe_set_update_interval(owner, value);
        }

        /// <summary>
        /// Cubemap face resolution.
        /// </summary>
        public ProbeResolution resolution
        {
            get => (ProbeResolution)internal_m2n_probe_get_resolution(owner);
            set => internal_m2n_probe_set_resolution(owner, (byte)value);
        }

        /// <summary>
        /// Box projection extents when <see cref="type"/> is <see cref="ProbeType.Box"/>.
        /// </summary>
        public Vector3 boxExtents
        {
            get => internal_m2n_probe_get_box_extents(owner);
            set => internal_m2n_probe_set_box_extents(owner, value);
        }

        /// <summary>
        /// Sphere projection range when <see cref="type"/> is <see cref="ProbeType.Sphere"/>.
        /// </summary>
        public float sphereRange
        {
            get => internal_m2n_probe_get_sphere_range(owner);
            set => internal_m2n_probe_set_sphere_range(owner, value);
        }

        /// <summary>
        /// Whether the atmospheric sky is included during cubemap capture.
        /// </summary>
        public bool captureSky
        {
            get => internal_m2n_probe_get_capture_sky(owner);
            set => internal_m2n_probe_set_capture_sky(owner, value);
        }

        /// <summary>
        /// Whether shadow maps are rendered during cubemap capture.
        /// </summary>
        public bool captureShadows
        {
            get => internal_m2n_probe_get_capture_shadows(owner);
            set => internal_m2n_probe_set_capture_shadows(owner, value);
        }

        /// <summary>
        /// True when the probe has pending or in-flight cubemap generation work.
        /// </summary>
        public bool isDirty
        {
            get => internal_m2n_probe_is_dirty(owner);
        }

        /// <summary>
        /// Requests a cubemap rebuild.
        /// </summary>
        /// <param name="forceFullFirstFrame">
        /// When <c>true</c>, all six faces bake in a single frame instead of being time-sliced.
        /// </param>
        public void MarkDirty(bool forceFullFirstFrame = false)
        {
            internal_m2n_probe_mark_dirty(owner, forceFullFirstFrame);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte internal_m2n_probe_get_type(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_probe_set_type(Entity eid, byte type);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte internal_m2n_probe_get_method(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_probe_set_method(Entity eid, byte method);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_probe_get_intensity(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_probe_set_intensity(Entity eid, float intensity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte internal_m2n_probe_get_update_mode(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_probe_set_update_mode(Entity eid, byte mode);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_probe_get_update_interval(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_probe_set_update_interval(Entity eid, float seconds);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte internal_m2n_probe_get_resolution(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_probe_set_resolution(Entity eid, byte resolution);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_probe_get_box_extents(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_probe_set_box_extents(Entity eid, Vector3 extents);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_probe_get_sphere_range(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_probe_set_sphere_range(Entity eid, float range);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_probe_get_capture_sky(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_probe_set_capture_sky(Entity eid, bool capture);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_probe_get_capture_shadows(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_probe_set_capture_shadows(Entity eid, bool capture);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_probe_is_dirty(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_probe_mark_dirty(Entity eid, bool forceFullFirstFrame);
    }
}
