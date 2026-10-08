using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Type of light source.
    /// </summary>
    public enum LightType : byte
    {
        /// <summary>Spot light with cone angles and range.</summary>
        Spot = 0,
        /// <summary>Omnidirectional point light with range.</summary>
        Point = 1,
        /// <summary>Infinite directional light (sun).</summary>
        Directional = 2,
    }

    /// <summary>
    /// Light source component attached to an entity.
    /// </summary>
    public class LightComponent : Component
    {
        /// <summary>
        /// Color of the light.
        /// </summary>
        public Color color
        {
            get => internal_m2n_light_get_color(owner);
            set => internal_m2n_light_set_color(owner, value);
        }

        /// <summary>
        /// Light type (spot, point, or directional).
        /// </summary>
        public LightType type
        {
            get => (LightType)internal_m2n_light_get_type(owner);
            set => internal_m2n_light_set_type(owner, (byte)value);
        }

        /// <summary>
        /// Light intensity.
        /// </summary>
        public float intensity
        {
            get => internal_m2n_light_get_intensity(owner);
            set => internal_m2n_light_set_intensity(owner, value);
        }

        /// <summary>
        /// Scale of the light in the global illumination (what it bounces); 0 leaves it out.
        /// </summary>
        public float indirectIntensity
        {
            get => internal_m2n_light_get_indirect_intensity(owner);
            set => internal_m2n_light_set_indirect_intensity(owner, value);
        }

        /// <summary>
        /// Whether the light casts shadows.
        /// </summary>
        public bool castsShadows
        {
            get => internal_m2n_light_get_casts_shadows(owner);
            set => internal_m2n_light_set_casts_shadows(owner, value);
        }

        /// <summary>
        /// Range for point and spot lights.
        /// </summary>
        public float range
        {
            get => internal_m2n_light_get_range(owner);
            set => internal_m2n_light_set_range(owner, value);
        }

        /// <summary>
        /// Spot light outer angle in degrees.
        /// </summary>
        public float spotOuterAngle
        {
            get => internal_m2n_light_get_spot_outer_angle(owner);
            set => internal_m2n_light_set_spot_outer_angle(owner, value);
        }

        /// <summary>
        /// Spot light inner angle in degrees.
        /// </summary>
        public float spotInnerAngle
        {
            get => internal_m2n_light_get_spot_inner_angle(owner);
            set => internal_m2n_light_set_spot_inner_angle(owner, value);
        }

        /// <summary>
        /// Radius of a point or spot light's emitting sphere, in world units. Widens and softens
        /// the highlight without changing the light emitted. 0 = a point source.
        /// </summary>
        public float sourceRadius
        {
            get => internal_m2n_light_get_source_radius(owner);
            set => internal_m2n_light_set_source_radius(owner, value);
        }

        /// <summary>
        /// Length of a point or spot light's emitting tube along its local Y axis, in world units.
        /// 0 = a sphere.
        /// </summary>
        public float sourceLength
        {
            get => internal_m2n_light_get_source_length(owner);
            set => internal_m2n_light_set_source_length(owner, value);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Color internal_m2n_light_get_color(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_light_set_color(Entity eid, Color color);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte internal_m2n_light_get_type(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_light_set_type(Entity eid, byte type);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_light_get_intensity(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_light_set_intensity(Entity eid, float intensity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_light_get_indirect_intensity(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_light_set_indirect_intensity(Entity eid, float indirectIntensity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_light_get_casts_shadows(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_light_set_casts_shadows(Entity eid, bool castsShadows);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_light_get_range(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_light_set_range(Entity eid, float range);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_light_get_spot_outer_angle(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_light_set_spot_outer_angle(Entity eid, float angle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_light_get_spot_inner_angle(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_light_set_spot_inner_angle(Entity eid, float angle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_light_get_source_radius(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_light_set_source_radius(Entity eid, float radius);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_light_get_source_length(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_light_set_source_length(Entity eid, float length);
    }
}
