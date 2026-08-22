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
        /// Weather-scale coverage variation: 0 = uniform sheet, 1.5 = strong clear and dense patches.
        /// </summary>
        public float cloudMacroVariation
        {
            get => internal_m2n_skylight_get_cloud_macro_variation(owner);
            set => internal_m2n_skylight_set_cloud_macro_variation(owner, value);
        }

        /// <summary>
        /// Cloud layer base, height above the camera in world units.
        /// </summary>
        public float cloudBaseAltitude
        {
            get => internal_m2n_skylight_get_cloud_base_altitude(owner);
            set => internal_m2n_skylight_set_cloud_base_altitude(owner, value);
        }

        /// <summary>
        /// Cloud layer thickness (base to top) in world units.
        /// </summary>
        public float cloudThickness
        {
            get => internal_m2n_skylight_get_cloud_thickness(owner);
            set => internal_m2n_skylight_set_cloud_thickness(owner, value);
        }

        /// <summary>
        /// Typical size of a cloud mass in world units.
        /// </summary>
        public float cloudSize
        {
            get => internal_m2n_skylight_get_cloud_size(owner);
            set => internal_m2n_skylight_set_cloud_size(owner, value);
        }

        /// <summary>
        /// Extinction scale along the view ray; higher = more opaque.
        /// </summary>
        public float cloudDensity
        {
            get => internal_m2n_skylight_get_cloud_density(owner);
            set => internal_m2n_skylight_set_cloud_density(owner, value);
        }

        /// <summary>
        /// Fraction of the view extinction applied along the sun path (self-shadowing).
        /// </summary>
        public float cloudShadowStrength
        {
            get => internal_m2n_skylight_get_cloud_shadow_strength(owner);
            set => internal_m2n_skylight_set_cloud_shadow_strength(owner, value);
        }

        /// <summary>
        /// When true the layer altitudes are measured from world y = 0 (the camera can fly into
        /// and above the clouds); when false they are measured from the camera.
        /// </summary>
        public bool cloudWorldSpaceAltitude
        {
            get => internal_m2n_skylight_get_cloud_world_space_altitude(owner);
            set => internal_m2n_skylight_set_cloud_world_space_altitude(owner, value);
        }

        /// <summary>
        /// Whether the cloud layer casts a soft shadow on the scene (directional light).
        /// </summary>
        public bool cloudShadows
        {
            get => internal_m2n_skylight_get_cloud_shadows(owner);
            set => internal_m2n_skylight_set_cloud_shadows(owner, value);
        }

        /// <summary>
        /// Opacity of the projected cloud shadow, 0 to 1.
        /// </summary>
        public float cloudShadowOpacity
        {
            get => internal_m2n_skylight_get_cloud_shadow_opacity(owner);
            set => internal_m2n_skylight_set_cloud_shadow_opacity(owner, value);
        }

        /// <summary>
        /// Width of the density ramp at the cloud edge; lower = crisper silhouettes.
        /// </summary>
        public float cloudSoftness
        {
            get => internal_m2n_skylight_get_cloud_softness(owner);
            set => internal_m2n_skylight_set_cloud_softness(owner, value);
        }

        /// <summary>
        /// Small-scale erosion strength at the cloud edges.
        /// </summary>
        public float cloudDetailErode
        {
            get => internal_m2n_skylight_get_cloud_detail_erode(owner);
            set => internal_m2n_skylight_set_cloud_detail_erode(owner, value);
        }

        /// <summary>
        /// Wind speed in km/h.
        /// </summary>
        public float cloudSpeed
        {
            get => internal_m2n_skylight_get_cloud_speed(owner);
            set => internal_m2n_skylight_set_cloud_speed(owner, value);
        }

        /// <summary>
        /// Wind direction in degrees (0 = +X, 90 = +Z); clouds drift toward it.
        /// </summary>
        public float cloudWindDirection
        {
            get => internal_m2n_skylight_get_cloud_wind_direction(owner);
            set => internal_m2n_skylight_set_cloud_wind_direction(owner, value);
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
        private static extern float internal_m2n_skylight_get_cloud_macro_variation(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_skylight_set_cloud_macro_variation(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_skylight_get_cloud_base_altitude(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_skylight_set_cloud_base_altitude(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_skylight_get_cloud_thickness(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_skylight_set_cloud_thickness(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_skylight_get_cloud_size(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_skylight_set_cloud_size(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_skylight_get_cloud_density(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_skylight_set_cloud_density(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_skylight_get_cloud_shadow_strength(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_skylight_set_cloud_shadow_strength(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_skylight_get_cloud_world_space_altitude(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_skylight_set_cloud_world_space_altitude(Entity eid, bool value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_skylight_get_cloud_shadows(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_skylight_set_cloud_shadows(Entity eid, bool value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_skylight_get_cloud_shadow_opacity(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_skylight_set_cloud_shadow_opacity(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_skylight_get_cloud_softness(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_skylight_set_cloud_softness(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_skylight_get_cloud_detail_erode(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_skylight_set_cloud_detail_erode(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_skylight_get_cloud_speed(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_skylight_set_cloud_speed(Entity eid, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_skylight_get_cloud_wind_direction(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_skylight_set_cloud_wind_direction(Entity eid, float value);

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
