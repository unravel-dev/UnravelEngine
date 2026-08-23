#pragma once
#include <base/basetypes.hpp>
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/light.h>
#include <engine/rendering/shadow.h>
#include <engine/rendering/cloud_noise.h>
#include <cmath>

namespace unravel
{

/**
 * @class light_component
 * @brief Class that contains core light data, used for rendering and other purposes.
 */
class light_component : public component_crtp<light_component>
{
public:
    /**
     * @brief Gets the light object.
     * @return A constant reference to the light object.
     */
    auto get_light() const -> const light&;

    /**
     * @brief Sets the light object.
     * @param[in] l The light object to set.
     */
    void set_light(const light& l);

    /**
     * @brief Gets the bounding box of the light object.
     */
    auto get_bounds() const -> math::bbox;
    auto get_bounds_sphere() const -> math::bsphere;

    /**
     * @brief Gets the bounding box of the light object.
     */
    auto get_bounds_precise(const math::vec3& light_direction) const -> math::bbox;
    auto get_bounds_sphere_precise(const math::vec3& light_direction) const -> math::bsphere;

    /**
     * @brief Computes the projected sphere rectangle.
     * @param[out] rect Reference to the rectangle to be computed.
     * @param[in] light_position The position of the light.
     * @param[in] light_direction The direction of the light.
     * @param[in] view_origin The origin of the view.
     * @param[in] view The view transform.
     * @param[in] proj The projection transform.
     * @return An integer indicating the result of the computation.
     */
    auto compute_projected_sphere_rect(irect32_t& rect,
                                       const math::vec3& light_position,
                                       const math::vec3& light_direction,
                                       const math::vec3& view_origin,
                                       const math::transform& view,
                                       const math::transform& proj) -> int;

    /**
     * @brief Gets the shadow map generator.
     * @return A reference to the shadow map generator.
     */
    auto get_shadowmap_generator() -> shadow::shadowmap_generator&;

private:
    auto get_bounds_sphere_impl(const math::vec3* light_direction) const -> math::bsphere;

    /**
     * @brief The light object this component represents.
     */
    light light_;

    /**
     * @brief The shadow map generator.
     */
    std::shared_ptr<shadow::shadowmap_generator> shadowmap_generator_ = std::make_shared<shadow::shadowmap_generator>();
};

/**
 * @class skylight_component
 * @brief Class that contains sky light data.
 */
class skylight_component : public component_crtp<skylight_component>
{
public:
    /**
     * @enum sky_mode
     * @brief Enumeration for sky modes.
     */
    enum class sky_mode
    {
        /// Reserved sky mode
        reserved_0 = 0,
        /// Perez sky mode
        perez = 1,
        /// Skybox
        skybox = 2,
    };

    /**
     * @enum irradiance_quality
     * @brief Directional variation of the indirect diffuse ambient.
     * @note Orthogonal to sky contribution (see get_irradiance_use_sky). This axis only
     *       controls whether the ambient varies with the surface normal.
     */
    enum class irradiance_quality
    {
        /// Flat ambient: only the constant SH band (L0) is written, same color for every normal.
        flat,
        /// Directional ambient: full L0-L2 spherical harmonics, varies with the surface normal.
        directional,
    };

    enum class cloud_mode
    {
        /// No clouds rendered
        none = 0,
        /// Flat projected clouds (cheap, single-sample scattering)
        flat = 1,
        /// Volumetric raymarched clouds (full scattering, half-res with temporal)
        volumetric = 2,
    };

    /**
     * @brief Gets the current sky mode.
     * @return A constant reference to the current sky mode.
     */
    auto get_mode() const noexcept -> const sky_mode&;

    /**
     * @brief Sets the sky mode.
     * @param[in] mode The sky mode to set.
     */
    void set_mode(const sky_mode& mode);

    /**
     * @brief Gets the current turbidity value.
     * @return The current turbidity value.
     */
    auto get_turbidity() const noexcept -> float;

    /**
     * @brief Sets the turbidity value.
     * @param[in] turbidity The turbidity value to set, in the range 1.9f-10.0f.
     */
    void set_turbidity(float turbidity);

    /**
     * @brief Gets the cloud rendering mode.
     * @return The current cloud mode (none, flat, volumetric).
     */
    auto get_cloud_mode() const noexcept -> cloud_mode;

    /**
     * @brief Sets the cloud rendering mode.
     * @param[in] mode The cloud mode to set.
     */
    void set_cloud_mode(cloud_mode mode);

    /**
     * @brief Gets the cloud coverage value.
     * @return The current cloud coverage value [0.0 = clear sky, 1.0 = overcast].
     */
    auto get_cloud_coverage() const noexcept -> float;

    /**
     * @brief Sets the cloud coverage value.
     * @param[in] coverage The cloud coverage value to set, in the range 0.0f-1.0f.
     */
    void set_cloud_coverage(float coverage);

    /**
     * @brief Gets the cloud layer base altitude above the ground reference (world y = 0, or
     * the camera when world-space altitude is off). Volumetric: shell bottom. Flat: plane.
     */
    auto get_cloud_base_altitude() const noexcept -> float;
    void set_cloud_base_altitude(float altitude);

    /**
     * @brief Gets the cloud layer thickness in world units (base to top).
     */
    auto get_cloud_thickness() const noexcept -> float;
    void set_cloud_thickness(float thickness);

    /**
     * @brief Gets the typical size of a cloud mass in world units (the noise unit).
     */
    auto get_cloud_size() const noexcept -> float;
    void set_cloud_size(float size);

    /**
     * @brief Gets the edge softness: width of the density ramp; lower = crisper silhouettes.
     */
    auto get_cloud_softness() const noexcept -> float;
    void set_cloud_softness(float softness);

    /**
     * @brief Gets the detail erosion strength (small-scale holes and wisps at the edges).
     */
    auto get_cloud_detail_erode() const noexcept -> float;
    void set_cloud_detail_erode(float erode);

    /**
     * @brief Gets the weather-scale coverage variation: 0 = uniform sheet, 1 = strong clear
     * and dense patches.
     */
    auto get_cloud_macro_variation() const noexcept -> float;
    void set_cloud_macro_variation(float variation);

    /**
     * @brief Gets the wind speed in km/h.
     */
    auto get_cloud_speed() const noexcept -> float;
    void set_cloud_speed(float speed);

    /**
     * @brief Gets the wind direction in degrees (0 = +X, 90 = +Z); clouds drift toward it.
     */
    auto get_cloud_wind_direction() const noexcept -> float;
    void set_cloud_wind_direction(float degrees);

    /**
     * @brief Gets the cloud density: extinction scale, higher = more opaque.
     */
    auto get_cloud_density() const noexcept -> float;
    void set_cloud_density(float density);

    /**
     * @brief Gets the shadow strength: fraction of the view extinction applied along the sun
     * path; lower lets light reach deeper into the cloud (approximates multiple scattering).
     */
    auto get_cloud_shadow_strength() const noexcept -> float;
    void set_cloud_shadow_strength(float strength);

    /**
     * @brief Whether the layer altitudes are measured from world y = 0 (true: the camera can
     * fly into and above the layer) or from the camera (false: the layer always floats above it).
     */
    auto get_cloud_world_space_altitude() const noexcept -> bool;
    void set_cloud_world_space_altitude(bool enabled);

    /**
     * @brief Whether the cloud layer casts a shadow on the scene (directional light).
     */
    auto get_cloud_shadows() const noexcept -> bool;
    void set_cloud_shadows(bool enabled);

    /**
     * @brief Gets the opacity of the projected cloud shadow [0, 1].
     */
    auto get_cloud_shadow_opacity() const noexcept -> float;
    void set_cloud_shadow_opacity(float opacity);

    /**
     * @brief Gets the irradiance intensity (strength of indirect diffuse).
     * @return The irradiance intensity value.
     */
    auto get_irradiance_intensity() const noexcept -> float;

    /**
     * @brief Sets the irradiance intensity.
     * @param[in] intensity The irradiance intensity to set.
     */
    void set_irradiance_intensity(float intensity);

    /**
     * @brief Gets the optional irradiance tint override (default from sky when white).
     * @return The irradiance tint.
     */
    auto get_irradiance_tint() const noexcept -> const math::color&;

    /**
     * @brief Sets the irradiance tint override.
     * @param[in] color The irradiance tint to set (white = use sky-derived color).
     */
    void set_irradiance_tint(const math::color& color);

    /**
     * @brief Gets the irradiance quality for indirect diffuse.
     * @return The irradiance quality.
     */
    auto get_irradiance_quality() const noexcept -> irradiance_quality;

    /**
     * @brief Sets the irradiance quality.
     * @param[in] quality The irradiance quality to set.
     */
    void set_irradiance_quality(irradiance_quality quality);

    /**
     * @brief Gets whether the sky color contributes to the ambient irradiance.
     * @return true if the sky/environment color is used; false for a flat tint-only ambient.
     */
    auto get_irradiance_use_sky() const noexcept -> bool;

    /**
     * @brief Sets whether the sky color contributes to the ambient irradiance.
     * @param[in] use_sky true to tint the ambient by the sky/environment color, false to use
     *            only the irradiance tint as a flat artist-defined ambient (sky ignored).
     */
    void set_irradiance_use_sky(bool use_sky);

    /**
     * @brief Gets the sky brightness multiplier (affects atmospheric pass and irradiance).
     * @return The sky brightness value (1.0 = neutral).
     */
    auto get_sky_brightness() const noexcept -> float;

    /**
     * @brief Sets the sky brightness multiplier.
     * @param[in] brightness The sky brightness to set (1.0 = neutral, &gt;1 brighter, &lt;1 darker).
     */
    void set_sky_brightness(float brightness);

    auto get_cubemap() const noexcept -> const asset_handle<gfx::texture>&
    {
        return cubemap_;
    }
    void set_cubemap(const asset_handle<gfx::texture>& cubemap)
    {
        cubemap_ = cubemap;
    }

    /// Period of cloud_time: the shaders only use it for periodic effects (star twinkle),
    /// so it wraps at a multiple of their period instead of growing without bound.
    static constexpr float cloud_time_period = 3600.0f;

    /**
     * @brief Gets the accumulated time (seconds, wrapped at cloud_time_period).
     */
    auto get_cloud_time() const noexcept -> float
    {
        return cloud_time_;
    }

    /**
     * @brief Gets the wind offset of the noise field in noise units (world / cloud_size),
     * wrapped to the noise tile period.
     */
    auto get_cloud_wind_offset() const noexcept -> const math::vec2&
    {
        return cloud_wind_offset_;
    }

    void update(delta_t dt)
    {
        // 1 km/h = 0.278 m/s. The default base altitude (27500 units above the camera) stands
        // for a ~1.5 km cloud base, so a metre is ~18 units and 1 km/h ~ 5 units/s at that
        // scale. The offset lives in noise units and wraps to the tile period, so it never
        // loses precision; the pass unwraps the per-frame delta it uses for reprojection.
        constexpr float world_units_per_kmh = 5.0f;
        constexpr float period = float(cloud_noise_textures::tile_period);
        const float radians = math::radians(cloud_wind_direction_);
        const math::vec2 dir(std::cos(radians), std::sin(radians));
        const float rate = cloud_speed_ * world_units_per_kmh / math::max(cloud_size_, 1.0f);
        cloud_wind_offset_ -= dir * (rate * dt.count());
        cloud_wind_offset_ = math::mod(cloud_wind_offset_, period);
        cloud_time_ = std::fmod(cloud_time_ + dt.count(), cloud_time_period);
    }

private:
    /**
     * @brief The current sky mode.
     */
    sky_mode mode_{sky_mode::perez};

    /**
     * @brief The current turbidity value.
     */
    float turbidity_{1.9};

    /// Cloud rendering mode (none / flat / volumetric).
    cloud_mode cloud_mode_{cloud_mode::volumetric};

    /// Cloud coverage [0.0 = clear sky, 1.0 = overcast]. Controls the density threshold.
    float cloud_coverage_{0.4f};

    /// Cloud base altitude above the ground reference (world y = 0, or the camera when
    /// cloud_world_space_altitude_ is off). Vol: shell bottom. Flat: projection height.
    float cloud_base_altitude_{27500.0f};
    /// Cloud layer thickness (base to top) in world units.
    float cloud_thickness_{40000.0f};
    /// Typical size of a cloud mass in world units (the noise unit).
    float cloud_size_{20000.0f};
    /// Density ramp width; lower = crisper edges.
    float cloud_softness_{0.8f};
    /// Detail erosion strength.
    float cloud_detail_erode_{0.7f};
    /// Weather-scale coverage variation.
    float cloud_macro_variation_{1.5f};
    /// Wind speed in km/h [0-200].
    float cloud_speed_{15.0f};
    /// Wind direction in degrees (0 = +X, 90 = +Z); clouds drift toward it.
    float cloud_wind_direction_{35.0f};
    /// Extinction scale.
    float cloud_density_{1.5f};
    /// Sun-path extinction as a fraction of the view extinction.
    float cloud_shadow_strength_{0.1f};
    /// Layer altitudes from world y = 0 (true) or from the camera (false).
    bool cloud_world_space_altitude_{true};
    /// Project the cloud layer as a shadow on the scene.
    bool cloud_shadows_{false};
    /// Opacity of the projected cloud shadow.
    float cloud_shadow_opacity_{1.0f};

    /// Accumulated time in seconds, wrapped at cloud_time_period.
    float cloud_time_{0.0f};

    /// Wind offset of the noise field in noise units, wrapped to the tile period.
    math::vec2 cloud_wind_offset_{0.0f, 0.0f};

    /**
     * @brief Strength of indirect diffuse lighting.
     */
    float irradiance_intensity_{0.05f};

    /**
     * @brief Optional tint override for irradiance (white = use sky-derived color).
     */
    math::color irradiance_tint_{1.0f, 1.0f, 1.0f, 1.0f};

    /**
     * @brief Directional variation of the indirect diffuse ambient.
     */
    irradiance_quality irradiance_quality_{irradiance_quality::directional};

    /**
     * @brief Whether the sky/environment color contributes to the ambient (false = flat tint only).
     */
    bool irradiance_use_sky_{true};

    /**
     * @brief Sky brightness multiplier (1.0 = neutral). Affects atmospheric pass and irradiance.
     */
    float sky_brightness_{1.0f};

    asset_handle<gfx::texture> cubemap_;
};

} // namespace unravel
