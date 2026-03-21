#pragma once

#include <engine/engine_export.h>

#include <cstdint>
#include <math/math.h>

namespace unravel
{
/**
 * @brief Enum representing the type of light.
 */
enum class light_type : uint8_t
{
    spot = 0,
    point = 1,
    directional = 2,

    count
};

/**
 * @brief Enum representing the depth method for shadow mapping.
 */
enum class sm_depth : uint8_t
{
    invz = 0,
    linear = 1,

    count
};

/**
 * @brief Enum representing the packing method for depth in shadow mapping.
 */
enum class pack_depth : uint8_t
{
    rgba = 0,
    vsm = 1,

    count
};

/**
 * @brief Enum representing the implementation type for shadow mapping.
 */
enum class sm_impl : uint8_t
{
    hard = 0,
    pcf = 1,
    pcss = 2,
    vsm = 3,
    esm = 4,

    count,
};

/**
 * @brief Enum representing the type of shadow map.
 */
enum class sm_type : uint8_t
{
    single = 0,
    omni = 1,
    cascade = 2,

    count
};

/**
 * @brief Enum representing the resolution of shadow maps.
 */
enum class sm_resolution : uint8_t
{
    low,
    medium,
    high,
    very_high,

    count
};

/**
 * @brief Struct representing a light.
 */
struct light
{
    /// The type of the light.
    light_type type = light_type::directional;

    /**
     * @brief Struct representing spot light specific properties.
     */
    struct spot
    {
        /**
         * @brief Sets the range of the spot light.
         * @param r The range to set.
         */
        void set_range(float r);

        /**
         * @brief Gets the range of the spot light.
         * @return The range of the spot light.
         */
        float get_range() const
        {
            return range;
        }

        /**
         * @brief Sets the outer angle of the spot light.
         * @param angle The outer angle to set.
         */
        void set_outer_angle(float angle);

        /**
         * @brief Gets the outer angle of the spot light.
         * @return The outer angle of the spot light.
         */
        float get_outer_angle() const
        {
            return outer_angle;
        }

        /**
         * @brief Sets the inner angle of the spot light.
         * @param angle The inner angle to set.
         */
        void set_inner_angle(float angle);

        /**
         * @brief Gets the inner angle of the spot light.
         * @return The inner angle of the spot light.
         */
        float get_inner_angle() const
        {
            return inner_angle;
        }

        /// The range of the spot light.
        float range = 10.0f;
        /// The outer angle of the spot light.
        float outer_angle = 60.0f;
        /// The inner angle of the spot light.
        float inner_angle = 30.0f;

        /**
         * @brief Struct representing shadow map parameters for spot lights.
         */
        // struct shadowmap_params
        // {
        // } shadow_params{};
    };

    /**
     * @brief Struct representing point light specific properties.
     */
    struct point
    {
        /// The range of the point light.
        float range = 10.0f;
        /// The exponent falloff for the point light.
        float exponent_falloff = 1.0f;

        /**
         * @brief Struct representing shadow map parameters for point lights.
         */
        // struct shadowmap_params
        // {
        //     /// Field of view x-axis adjustment.
        //     float fov_x_adjust = 0.0f;
        //     /// Field of view y-axis adjustment.
        //     float fov_y_adjust = 0.0f;
        //     /// Whether to use stencil packing.
        //     bool stencil_pack = false;

        // } shadow_params{};
    };

    /**
     * @brief Struct representing directional light specific properties.
     */
    struct directional
    {
        /**
         * @brief Struct representing shadow map parameters for directional lights.
         */
        // struct shadowmap_params
        // {
        //     /// Split distribution for cascade shadow maps.
        //     float split_distribution = 0.8f;
        //     /// Number of splits for cascade shadow maps.
        //     uint8_t num_splits = 4;
        //     /// Whether to stabilize the shadow map.
        //     bool stabilize = true;
        // } shadow_params{};
    };

    /// Data specific to spot lights.
    spot spot_data;
    /// Data specific to point lights.
    point point_data;
    /// Data specific to directional lights.
    directional directional_data;
    /// The color of the light.
    math::color color = {1.0f, 1.0f, 1.0f, 1.0f};
    /// The intensity of the light.
    float intensity = 2.5f;

    /// Whether the light casts shadows.
    bool casts_shadows{true};

    /**
     * @brief Struct representing common shadow map parameters.
     */
    struct shadowmap_params
    {
        /// Depth method for shadow mapping.
        sm_depth depth = sm_depth::invz;
        /// Implementation type for shadow mapping.
        sm_impl type = sm_impl::pcf;
        /// Resolution of the shadow map.
        sm_resolution resolution = sm_resolution::high;
        /// Near plane distance for shadow mapping.
        float near_plane{0.2f};
        /// Far plane distance for shadow mapping.
        float far_plane{550.0f};
        /// Bias for shadow mapping.
        float bias{0.00100f};
        /// Normal bias for shadow mapping.
        float normal_bias{0.1300f};

        /**
         * @brief Struct representing implementation parameters for hard shadow mapping.
         */
        struct hard_impl_params
        {
            // Hard shadows don't require any special parameters
        };

        /**
         * @brief Struct representing implementation parameters for PCF shadow mapping.
         */
        struct pcf_impl_params
        {
            /// Offset along the x-axis for PCF sampling.
            float x_offset{1.0f};
            /// Offset along the y-axis for PCF sampling.
            float y_offset{1.0f};
        };

        /**
         * @brief Struct representing implementation parameters for PCSS shadow mapping.
         */
        struct pcss_impl_params
        {
            /// Offset along the x-axis for PCSS sampling.
            float penumbra_x_offset{1.0f};
            /// Offset along the y-axis for PCSS sampling.
            float penumbra_y_offset{1.5f};
        };

        /**
         * @brief Struct representing implementation parameters for VSM shadow mapping.
         */
        struct vsm_impl_params
        {
            /// Minimum variance for VSM filtering.
            float min_variance{0.02f};
            /// Depth multiplier for VSM.
            float depth_multiplier{450.0f};
            /// Whether to blur the shadow map.
            bool do_blur{true};
            /// Blur offset along the x-axis.
            float blur_x_offset{1.0f};
            /// Blur offset along the y-axis.
            float blur_y_offset{1.0f};
        };

        /**
         * @brief Struct representing implementation parameters for ESM shadow mapping.
         */
        struct esm_impl_params
        {
            /// ESM hardness parameter.
            float hardness{0.7f};
            /// Depth multiplier for ESM.
            float depth_multiplier{9000.0f};
            /// Whether to blur the shadow map.
            bool do_blur{true};
            /// Blur offset along the x-axis.
            float blur_x_offset{1.0f};
            /// Blur offset along the y-axis.
            float blur_y_offset{1.0f};
        };

    
        hard_impl_params hard;
        pcf_impl_params pcf;
        pcss_impl_params pcss;
        vsm_impl_params vsm;
        esm_impl_params esm;


        /// Whether to show shadow map coverage.
        bool show_coverage{false};

       

    } shadow_params;

    
    struct directional_shadowmap_params
    {
        /// Split distribution for cascade shadow maps.
        float split_distribution = 0.8f;
        /// Number of splits for cascade shadow maps.
        uint8_t num_splits = 4;
        /// Whether to stabilize the shadow map.
        bool stabilize = true;
    } directional_shadow_params{};

    struct point_shadowmap_params
    {
        /// Field of view x-axis adjustment.
        float fov_x_adjust = 0.0f;
        /// Field of view y-axis adjustment.
        float fov_y_adjust = 0.0f;
        /// Whether to use stencil packing.
        bool stencil_pack = false;

    } point_shadow_params{};
    
    struct spot_shadowmap_params
    {
    } spot_shadow_params{};

    /**
     * @brief Struct representing contact shadow parameters.
     * Screen-space ray-marched shadows that add fine detail at object contact
     * points where shadow maps lack resolution.
     */
    struct contact_shadow_params
    {
        /// Whether contact shadows are enabled for this light.
        bool enabled{false};
        /// Maximum ray length in view-space units. Shorter = tighter contact detail,
        /// longer = catches more occluders but may produce artifacts on thin geometry.
        float ray_length{0.25f};
        /// View-space depth comparison thickness for ray hits. Zero uses automatic
        /// thickness from ray length (legacy 15 percent of ray length).
        float thickness{0.0f};
        /// N·L smoothstep low edge: below this, contact shadow fades out (surface faces away from light).
        float n_dot_l_fade_start{0.0f};
        /// N·L smoothstep high edge: at or above this, contact shadow has full strength.
        float n_dot_l_fade_end{0.2f};
        /// If in [0, 1], reject ray hits whose reconstructed occluder normal faces the light
        /// more strongly than this cosine (reduces false self-contact). Below zero disables.
        float normal_facing_reject{-1.0f};
    } contact_shadow;
};

} // namespace unravel
