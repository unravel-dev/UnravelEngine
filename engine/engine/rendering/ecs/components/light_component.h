#pragma once
#include <base/basetypes.hpp>
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/light.h>
#include <engine/rendering/shadow.h>

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
     * @brief Quality of irradiance for indirect diffuse lighting.
     */
    enum class irradiance_quality
    {
        /// Uniform ambient (Perez luminance or directional light color)
        uniform,
        /// Normal-dependent via spherical harmonics
        normal_dependent,
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
     * @brief Gets the cloud base altitude in world units.
     * @return The current cloud base altitude. Volumetric: slab bottom. Flat: projection height.
     */
    auto get_cloud_base_altitude() const noexcept -> float;

    /**
     * @brief Sets the cloud base altitude.
     * @param[in] altitude The cloud base altitude to set, in world units.
     */
    void set_cloud_base_altitude(float altitude);

    /**
     * @brief Gets the cloud top altitude in world units (volumetric slab top).
     * @return The current cloud top altitude. Flat clouds ignore this.
     */
    auto get_cloud_top_altitude() const noexcept -> float;

    /**
     * @brief Sets the cloud top altitude. Must be greater than base altitude.
     * @param[in] altitude The cloud top altitude to set, in world units.
     */
    void set_cloud_top_altitude(float altitude);

    /**
     * @brief Gets the cloud speed multiplier.
     * @return The current cloud speed multiplier.
     */
    auto get_cloud_speed() const noexcept -> float;

    /**
     * @brief Sets the cloud speed multiplier.
     * @param[in] speed The cloud speed multiplier to set.
     */
    void set_cloud_speed(float speed);

    /**
     * @brief Gets the cloud density multiplier.
     * @return The current cloud density multiplier.
     */
    auto get_cloud_density() const noexcept -> float;

    /**
     * @brief Sets the cloud density multiplier.
     * @param[in] density The cloud density multiplier to set.
     */
    void set_cloud_density(float density);

    /**
     * @brief Gets the cloud absorption coefficient (Beer-Lambert extinction).
     * @return The current absorption value. Higher = more opaque clouds.
     */
    auto get_cloud_absorption() const noexcept -> float;

    /**
     * @brief Sets the cloud absorption coefficient.
     * @param[in] absorption The absorption to set, in the range [0.01, 0.5].
     */
    void set_cloud_absorption(float absorption);

    /**
     * @brief Gets the cloud light absorption (self-shadow strength).
     * @return The current light absorption value. Higher = darker shadowed sides.
     */
    auto get_cloud_light_absorption() const noexcept -> float;

    /**
     * @brief Sets the cloud light absorption (self-shadow strength).
     * @param[in] absorption The light absorption to set, in the range [0.01, 0.5].
     */
    void set_cloud_light_absorption(float absorption);

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

    /**
     * @brief Gets the accumulated time for cloud animation.
     * @return The accumulated time for cloud animation.
     */
    auto get_cloud_time() const noexcept -> float
    {
        return cloud_time_;
    }

    void update(delta_t dt)
    {
        // cloud_speed_ is in km/h. Convert to the noise-UV time scale used by
        // the shader (cloud_time * 0.5 UV/s). Calibrated so that 10-20 km/h =
        // gentle natural drift, ~100 km/h = strong visible wind.
        constexpr float kmh_to_time_scale = 1.0f / 500.0f;
        cloud_time_ += dt.count() * cloud_speed_ * kmh_to_time_scale;
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
    float cloud_coverage_{0.6f};

    /// Cloud base altitude in world units. Vol: slab bottom. Flat: projection height.
    float cloud_base_altitude_{27500.0f};

    /// Cloud top altitude in world units. Vol: slab top. Flat: ignored.
    float cloud_top_altitude_{45000.0f};

    /// Cloud wind speed in km/h [0–200]. ~10 = gentle drift, ~50 = strong wind, ~100+ = storm.
    float cloud_speed_{0.0f};

    /// Cloud density/opacity multiplier.
    float cloud_density_{1.0f};

    /// Beer-Lambert extinction coefficient [0.01-0.5]. Higher = more opaque.
    float cloud_absorption_{0.08f};

    /// Light absorption / self-shadow strength [0.01-0.5]. Higher = darker shadows.
    float cloud_light_absorption_{0.10f};

    /// Accumulated time for cloud animation.
    float cloud_time_{0.0f};

    /**
     * @brief Strength of indirect diffuse lighting.
     */
    float irradiance_intensity_{0.05f};

    /**
     * @brief Optional tint override for irradiance (white = use sky-derived color).
     */
    math::color irradiance_tint_{1.0f, 1.0f, 1.0f, 1.0f};

    /**
     * @brief Quality of irradiance for indirect diffuse.
     */
    irradiance_quality irradiance_quality_{irradiance_quality::normal_dependent};

    /**
     * @brief Sky brightness multiplier (1.0 = neutral). Affects atmospheric pass and irradiance.
     */
    float sky_brightness_{1.0f};

    asset_handle<gfx::texture> cubemap_;
};

} // namespace unravel
