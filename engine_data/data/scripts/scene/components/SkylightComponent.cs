using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Sky rendering mode for <see cref="SkylightComponent"/>.
    /// </summary>
    public enum SkyMode : int
    {
        /// <summary>Reserved / unused sky mode.</summary>
        Reserved0 = 0,
        /// <summary>Analytic Perez atmospheric sky.</summary>
        Perez = 1,
        /// <summary>Cubemap skybox.</summary>
        Skybox = 2,
    }

    /// <summary>
    /// Indirect diffuse irradiance quality.
    /// </summary>
    public enum IrradianceQuality : int
    {
        /// <summary>Flat ambient using only the constant SH band.</summary>
        Flat = 0,
        /// <summary>Directional ambient using full L0-L2 spherical harmonics.</summary>
        Directional = 1,
    }

    /// <summary>
    /// Cloud rendering mode.
    /// </summary>
    public enum CloudMode : int
    {
        /// <summary>No clouds.</summary>
        None = 0,
        /// <summary>Flat projected clouds.</summary>
        Flat = 1,
        /// <summary>Volumetric raymarched clouds.</summary>
        Volumetric = 2,
    }

    /// <summary>
    /// Environment / sky light component.
    /// </summary>
    public class SkylightComponent : Component
    {
        /// <summary>
        /// Sky rendering mode (Perez atmosphere or skybox cubemap).
        /// </summary>
        public SkyMode mode
        {
            get => (SkyMode)internal_m2n_skylight_get_mode(owner);
            set => internal_m2n_skylight_set_mode(owner, (int)value);
        }

        /// <summary>
        /// Atmospheric turbidity used by Perez sky (typical range about 1.9 to 10).
        /// </summary>
        public float turbidity
        {
            get => internal_m2n_skylight_get_turbidity(owner);
            set => internal_m2n_skylight_set_turbidity(owner, value);
        }

        /// <summary>
        /// Cloud rendering mode.
        /// </summary>
        public CloudMode cloudMode
        {
            get => (CloudMode)internal_m2n_skylight_get_cloud_mode(owner);
            set => internal_m2n_skylight_set_cloud_mode(owner, (int)value);
        }

        /// <summary>
        /// Cloud coverage from 0 (clear) to 1 (overcast).
        /// </summary>
        public float cloudCoverage
        {
            get => internal_m2n_skylight_get_cloud_coverage(owner);
            set => internal_m2n_skylight_set_cloud_coverage(owner, value);
        }

        /// <summary>
        /// Strength of indirect diffuse irradiance.
        /// </summary>
        public float irradianceIntensity
        {
            get => internal_m2n_skylight_get_irradiance_intensity(owner);
            set => internal_m2n_skylight_set_irradiance_intensity(owner, value);
        }

        /// <summary>
        /// Whether ambient irradiance is flat or directionally varying.
        /// </summary>
        public IrradianceQuality irradianceQuality
        {
            get => (IrradianceQuality)internal_m2n_skylight_get_irradiance_quality(owner);
            set => internal_m2n_skylight_set_irradiance_quality(owner, (int)value);
        }

        /// <summary>
        /// When true, sky/environment color contributes to ambient irradiance.
        /// </summary>
        public bool irradianceUseSky
        {
            get => internal_m2n_skylight_get_irradiance_use_sky(owner);
            set => internal_m2n_skylight_set_irradiance_use_sky(owner, value);
        }

        /// <summary>
        /// Sky brightness multiplier (1.0 is neutral).
        /// </summary>
        public float skyBrightness
        {
            get => internal_m2n_skylight_get_sky_brightness(owner);
            set => internal_m2n_skylight_set_sky_brightness(owner, value);
        }

        /// <summary>
        /// Cubemap texture used when <see cref="mode"/> is <see cref="SkyMode.Skybox"/>.
        /// </summary>
        public Texture cubemap
        {
            get
            {
                var uid = internal_m2n_skylight_get_cubemap(owner);
                if (uid == Guid.Empty)
                {
                    return null;
                }
                return new Texture { uid = uid };
            }
            set => internal_m2n_skylight_set_cubemap(owner, value?.uid ?? Guid.Empty);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_skylight_get_mode(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_skylight_set_mode(Entity eid, int mode);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_skylight_get_turbidity(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_skylight_set_turbidity(Entity eid, float turbidity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_skylight_get_cloud_mode(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_skylight_set_cloud_mode(Entity eid, int mode);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_skylight_get_cloud_coverage(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_skylight_set_cloud_coverage(Entity eid, float coverage);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_skylight_get_irradiance_intensity(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_skylight_set_irradiance_intensity(Entity eid, float intensity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_skylight_get_irradiance_quality(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_skylight_set_irradiance_quality(Entity eid, int quality);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_skylight_get_irradiance_use_sky(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_skylight_set_irradiance_use_sky(Entity eid, bool useSky);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_skylight_get_sky_brightness(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_skylight_set_sky_brightness(Entity eid, float brightness);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Guid internal_m2n_skylight_get_cubemap(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_skylight_set_cubemap(Entity eid, Guid uid);
    }
}
