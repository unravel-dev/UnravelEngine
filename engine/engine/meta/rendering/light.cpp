#include "light.hpp"
#include <engine/meta/core/math/vector.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{
REFLECT(light)
{
    
    
    auto directional_predicate_entt = entt::property_predicate<bool>(
        [](const entt::meta_any& obj)
        {
            auto data = obj.try_cast<light>();
            if(!data)
            {
                return false;
            }
            return data->type == light_type::directional;
        });   
    auto point_predicate_entt = entt::property_predicate<bool>(
        [](const entt::meta_any& obj)
        {
            auto data = obj.try_cast<light>();
            if(!data)
            {
                return false;
            }
            return data->type == light_type::point;
        });
    auto spot_predicate_entt = entt::property_predicate<bool>(
        [](const entt::meta_any& obj)
        {
            auto data = obj.try_cast<light>();
            if(!data)
            {
                return false;
            }
            return data->type == light_type::spot;
        });

    auto casts_shadows_predicate_entt = entt::property_predicate<bool>(
        [](const entt::meta_any& obj)
        {
            auto data = obj.try_cast<light>();
            if(!data)
            {
                return false;
            }
            return data->casts_shadows;
        });
    auto casts_shadows_and_is_directional_predicate_entt = entt::property_predicate<bool>(
        [](const entt::meta_any& obj)
        {
            auto data = obj.try_cast<light>();
            if(!data)
            {
                return false;
            }
            return data->casts_shadows && data->type == light_type::directional;
        });

        auto casts_shadows_and_is_point_predicate_entt = entt::property_predicate<bool>(
            [](const entt::meta_any& obj)
            {
                auto data = obj.try_cast<light>();
                if(!data)
                {
                    return false;
                }
                return data->casts_shadows && data->type == light_type::point;
            });

        auto casts_shadows_and_is_spot_predicate_entt = entt::property_predicate<bool>(
            [](const entt::meta_any& obj)
            {
                auto data = obj.try_cast<light>();
                if(!data)
                {
                    return false;
                }
                return data->casts_shadows && data->type == light_type::spot;
            });

    entt::meta_factory<light::spot_shadowmap_params>{}
        .type("light::spot_shadowmap_params"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "light::spot_shadowmap_params"},
            entt::attribute{"pretty_name", "Spot Shadowmap Params"},
        });

    entt::meta_factory<light::spot>{}
        .type("light::spot"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "light::spot"},
            entt::attribute{"pretty_name", "Spot"},
        })
        .data<&light::spot::set_range, &light::spot::get_range>("range"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "range"},
            entt::attribute{"pretty_name", "Range"},
            entt::attribute{"min", 0.1f},
            entt::attribute{"tooltip", "Light's range from its origin."},
        })
        .data<&light::spot::set_inner_angle, &light::spot::get_inner_angle>("inner_angle"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "inner_angle"},
            entt::attribute{"pretty_name", "Inner Angle"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 85.0f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"tooltip", "Spot light inner cone angle."},
        })
        .data<&light::spot::set_outer_angle, &light::spot::get_outer_angle>("outer_angle"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "outer_angle"},
            entt::attribute{"pretty_name", "Outer Angle"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 90.0f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"tooltip", "Spot light outer cone angle."},
        });

    entt::meta_factory<light::point_shadowmap_params>{}
        .type("light::point_shadowmap_params"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "light::point_shadowmap_params"},
            entt::attribute{"pretty_name", "Point Shadowmap Params"},
        })
        .data<&light::point_shadowmap_params::fov_x_adjust>("fov_x_adjust"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fov_x_adjust"},
            entt::attribute{"pretty_name", "FovX Adjust"},
            entt::attribute{"min", -20.0f},
            entt::attribute{"max", 20.0f},
            entt::attribute{"step", 0.0001f},
            entt::attribute{"tooltip", "Shadowmap field of view adjust."},
        })
        .data<&light::point_shadowmap_params::fov_y_adjust>("fov_y_adjust"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fov_y_adjust"},
            entt::attribute{"pretty_name", "FovY Adjust"},
            entt::attribute{"min", -20.0f},
            entt::attribute{"max", 20.0f},
            entt::attribute{"step", 0.0001f},
            entt::attribute{"tooltip", "Shadowmap field of view adjust."},
        })
        .data<&light::point_shadowmap_params::stencil_pack>("stencil_pack"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "stencil_pack"},
            entt::attribute{"pretty_name", "Stencil Pack"},
            entt::attribute{"tooltip", "Shadowmap stencil packing algorithm."},
        });

    entt::meta_factory<light::point>{}
        .type("point"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "point"},
            entt::attribute{"pretty_name", "Point"},
        })
        .data<&light::point::range>("range"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "range"},
            entt::attribute{"pretty_name", "Range"},
            entt::attribute{"min", 0.1f},
            entt::attribute{"tooltip", "Light's range from its origin."},
        })
        .data<&light::point::exponent_falloff>("exponent_falloff"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "exponent_falloff"},
            entt::attribute{"pretty_name", "Exponent Falloff"},
            entt::attribute{"min", 0.1f},
            entt::attribute{"max", 10.0f},
            entt::attribute{"tooltip", "The falloff factor nearing the range edge."},
        });

    entt::meta_factory<light::directional_shadowmap_params>{}
        .type("light::directional_shadowmap_params"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "light::directional_shadowmap_params"},
            entt::attribute{"pretty_name", "Directional Shadowmap Params"},
        })
        .data<&light::directional_shadowmap_params::num_splits>("splits"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "splits"},
            entt::attribute{"pretty_name", "Splits"},
            entt::attribute{"min", 1},
            entt::attribute{"max", 4},
            entt::attribute{"tooltip", "Number of cascades."},
        })
        .data<&light::directional_shadowmap_params::split_distribution>("distribution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "distribution"},
            entt::attribute{"pretty_name", "Distribution"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.001f},
            entt::attribute{"tooltip", "?"},
        })
        .data<&light::directional_shadowmap_params::stabilize>("stabilize"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "stabilize"},
            entt::attribute{"pretty_name", "Stabilize"},
            entt::attribute{"tooltip", "Stabilize the shadowmaps."},
        });

    entt::meta_factory<light::directional>{}
        .type("light::directional"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "directional"},
            entt::attribute{"pretty_name", "Directional"},
        });

    entt::meta_factory<light_type>{}
        .type("light_type"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "light_type"},
            entt::attribute{"pretty_name", "Light Type"},
        })
        .data<light_type::spot>("spot"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "spot"},
            entt::attribute{"pretty_name", "Spot"} 
        })
        .data<light_type::point>("point"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "point"},
            entt::attribute{"pretty_name", "Point"} 
        })
        .data<light_type::directional>("directional"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "directional"},
            entt::attribute{"pretty_name", "Directional"} 
        });

    entt::meta_factory<sm_depth>{}
        .type("sm_depth"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sm_depth"},
            entt::attribute{"pretty_name", "Shadowmap Depth"},
        })
        .data<sm_depth::invz>("invz"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "invz"},
            entt::attribute{"pretty_name", "InvZ"} 
        })
        .data<sm_depth::linear>("linear"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "linear"},
            entt::attribute{"pretty_name", "Linear"} 
        });

    entt::meta_factory<sm_impl>{}
        .type("sm_impl"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sm_impl"},
            entt::attribute{"pretty_name", "Shadowmap Implementation"},
        })
        .data<sm_impl::hard>("hard"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "hard"},
            entt::attribute{"pretty_name", "Hard"} 
        })
        .data<sm_impl::pcf>("pcf"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "pcf"},
            entt::attribute{"pretty_name", "Pcf"} 
        })
        .data<sm_impl::pcss>("pcss"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "pcss"},
            entt::attribute{"pretty_name", "Pcss"} 
        })
        .data<sm_impl::vsm>("vsm"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "vsm"},
            entt::attribute{"pretty_name", "Vsm"} 
        })
        .data<sm_impl::esm>("esm"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "esm"},
            entt::attribute{"pretty_name", "Esm"} 
        });

    // Predicates for sm_impl types
    auto sm_impl_hard_predicate_entt = entt::property_predicate<bool>(
        [](const entt::meta_any& obj)
        {
            auto data = obj.try_cast<light::shadowmap_params>();
            if(!data)
            {
                return false;
            }
            return data->type == sm_impl::hard;
        });
    auto sm_impl_pcf_predicate_entt = entt::property_predicate<bool>(
        [](const entt::meta_any& obj)
        {
            auto data = obj.try_cast<light::shadowmap_params>();
            if(!data)
            {
                return false;
            }
            return data->type == sm_impl::pcf;
        });
    auto sm_impl_pcss_predicate_entt = entt::property_predicate<bool>(
        [](const entt::meta_any& obj)
        {
            auto data = obj.try_cast<light::shadowmap_params>();
            if(!data)
            {
                return false;
            }
            return data->type == sm_impl::pcss;
        });
    auto sm_impl_vsm_predicate_entt = entt::property_predicate<bool>(
        [](const entt::meta_any& obj)
        {
            auto data = obj.try_cast<light::shadowmap_params>();
            if(!data)
            {
                return false;
            }
            return data->type == sm_impl::vsm;
        });
    auto sm_impl_esm_predicate_entt = entt::property_predicate<bool>(
        [](const entt::meta_any& obj)
        {
            auto data = obj.try_cast<light::shadowmap_params>();
            if(!data)
            {
                return false;
            }
            return data->type == sm_impl::esm;
        });

    // Reflection for hard_impl_params
    entt::meta_factory<light::shadowmap_params::hard_impl_params>{}
        .type("light::shadowmap_params::hard_impl_params"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "hard_impl_params"},
            entt::attribute{"pretty_name", "Hard Implementation Params"},
        });

    // Reflection for pcf_impl_params
    entt::meta_factory<light::shadowmap_params::pcf_impl_params>{}
        .type("light::shadowmap_params::pcf_impl_params"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "pcf_impl_params"},
            entt::attribute{"pretty_name", "PCF Implementation Params"},
        })
        .data<&light::shadowmap_params::pcf_impl_params::x_offset>("x_offset"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "x_offset"},
            entt::attribute{"pretty_name", "X Offset"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 3.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Offset along the x-axis for PCF sampling."},
        })
        .data<&light::shadowmap_params::pcf_impl_params::y_offset>("y_offset"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "y_offset"},
            entt::attribute{"pretty_name", "Y Offset"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 3.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Offset along the y-axis for PCF sampling."},
        });

    // Reflection for pcss_impl_params
    entt::meta_factory<light::shadowmap_params::pcss_impl_params>{}
        .type("light::shadowmap_params::pcss_impl_params"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "pcss_impl_params"},
            entt::attribute{"pretty_name", "PCSS Implementation Params"},
        })
        .data<&light::shadowmap_params::pcss_impl_params::penumbra_x_offset>("penumbra_x_offset"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "penumbra_x_offset"},
            entt::attribute{"pretty_name", "Penumbra X Offset"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 10.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Offset along the x-axis for PCSS sampling."},
        })
        .data<&light::shadowmap_params::pcss_impl_params::penumbra_y_offset>("penumbra_y_offset"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "penumbra_y_offset"},
            entt::attribute{"pretty_name", "Penumbra Y Offset"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 10.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Offset along the y-axis for PCSS sampling."},
        });

    // Reflection for vsm_impl_params
    entt::meta_factory<light::shadowmap_params::vsm_impl_params>{}
        .type("light::shadowmap_params::vsm_impl_params"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "vsm_impl_params"},
            entt::attribute{"pretty_name", "VSM Implementation Params"},
        })
        .data<&light::shadowmap_params::vsm_impl_params::min_variance>("min_variance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "min_variance"},
            entt::attribute{"pretty_name", "Min Variance"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 0.1f},
            entt::attribute{"step", 0.00001f},
            entt::attribute{"tooltip", "Minimum variance for VSM filtering."},
        })
        .data<&light::shadowmap_params::vsm_impl_params::depth_multiplier>("depth_multiplier"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "depth_multiplier"},
            entt::attribute{"pretty_name", "Depth Multiplier"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 1000.0f},
            entt::attribute{"step", 1.0f},
            entt::attribute{"tooltip", "Depth multiplier for VSM."},
        })
        .data<&light::shadowmap_params::vsm_impl_params::do_blur>("do_blur"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "do_blur"},
            entt::attribute{"pretty_name", "Blur Shadow Map"},
            entt::attribute{"tooltip", "Whether to blur the shadow map."},
        })
        .data<&light::shadowmap_params::vsm_impl_params::blur_x_offset>("blur_x_offset"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "blur_x_offset"},
            entt::attribute{"pretty_name", "Blur X Offset"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 3.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Blur offset along the x-axis."},
        })
        .data<&light::shadowmap_params::vsm_impl_params::blur_y_offset>("blur_y_offset"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "blur_y_offset"},
            entt::attribute{"pretty_name", "Blur Y Offset"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 3.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Blur offset along the y-axis."},
        });

    // Reflection for esm_impl_params
    entt::meta_factory<light::shadowmap_params::esm_impl_params>{}
        .type("light::shadowmap_params::esm_impl_params"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "esm_impl_params"},
            entt::attribute{"pretty_name", "ESM Implementation Params"},
        })
        .data<&light::shadowmap_params::esm_impl_params::hardness>("hardness"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "hardness"},
            entt::attribute{"pretty_name", "ESM Hardness"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "ESM hardness parameter."},
        })
        .data<&light::shadowmap_params::esm_impl_params::depth_multiplier>("depth_multiplier"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "depth_multiplier"},
            entt::attribute{"pretty_name", "Depth Multiplier"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 15000.0f},
            entt::attribute{"step", 1.0f},
            entt::attribute{"tooltip", "Depth multiplier for ESM."},
        })
        .data<&light::shadowmap_params::esm_impl_params::do_blur>("do_blur"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "do_blur"},
            entt::attribute{"pretty_name", "Blur Shadow Map"},
            entt::attribute{"tooltip", "Whether to blur the shadow map."},
        })
        .data<&light::shadowmap_params::esm_impl_params::blur_x_offset>("blur_x_offset"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "blur_x_offset"},
            entt::attribute{"pretty_name", "Blur X Offset"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 3.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Blur offset along the x-axis."},
        })
        .data<&light::shadowmap_params::esm_impl_params::blur_y_offset>("blur_y_offset"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "blur_y_offset"},
            entt::attribute{"pretty_name", "Blur Y Offset"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 3.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Blur offset along the y-axis."},
        });

    entt::meta_factory<sm_resolution>{}
        .type("sm_resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sm_resolution"},
            entt::attribute{"pretty_name", "Shadowmap Resolution"},
        })
        .data<sm_resolution::low>("low"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "low"},
            entt::attribute{"pretty_name", "Low"} 
        })
        .data<sm_resolution::medium>("medium"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "medium"},
            entt::attribute{"pretty_name", "Medium"} 
        })
        .data<sm_resolution::high>("high"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "high"},
            entt::attribute{"pretty_name", "High"} 
        })
        .data<sm_resolution::very_high>("very_high"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "very_high"},
            entt::attribute{"pretty_name", "Very High"} 
        });

    entt::meta_factory<light::shadowmap_params>{}
        .type("light::shadowmap_params"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "shadowmap_params"},
            entt::attribute{"pretty_name", "Shadowmap Params"},
        })
        .data<&light::shadowmap_params::type>("type"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "type"},
            entt::attribute{"pretty_name", "Type"},
            entt::attribute{"tooltip", "Shadowmap implementation type."},
        })
        .data<&light::shadowmap_params::depth>("depth"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "depth"},
            entt::attribute{"pretty_name", "Depth"},
            entt::attribute{"tooltip", "Shadowmap depth pack algorithm."},
        })
        .data<&light::shadowmap_params::resolution>("resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "resolution"},
            entt::attribute{"pretty_name", "Resolution"},
            entt::attribute{"tooltip", "Shadowmap resolution."},
        })
        .data<&light::shadowmap_params::bias>("bias"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bias"},
            entt::attribute{"pretty_name", "Depth Bias"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 10.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Constant receiver depth bias, in shadow-map texels."},
        })
        .data<&light::shadowmap_params::slope_bias>("slope_bias"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "slope_bias"},
            entt::attribute{"pretty_name", "Slope Bias"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 10.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Slope-scaled depth bias, in shadow-map texels per unit slope toward the light."},
        })
        .data<&light::shadowmap_params::normal_bias>("normal_bias"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "normal_bias"},
            entt::attribute{"pretty_name", "Normal Bias"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 10.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Receiver offset along the surface normal, in shadow-map texels (zero when facing the light)."},
        })
        .data<&light::shadowmap_params::near_plane>("near_plane"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "near_plane"},
            entt::attribute{"pretty_name", "Near Plane"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"max", 10.0f},
            entt::attribute{"tooltip", "Shadowmap near plane"},
        })
        .data<&light::shadowmap_params::far_plane>("far_plane"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "far_plane"},
            entt::attribute{"pretty_name", "Far Plane"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"max", 10000.0f},
            entt::attribute{"tooltip", "Shadowmap far plane"},
        })
        .data<&light::shadowmap_params::show_coverage>("show_coverage"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "show_coverage"},
            entt::attribute{"pretty_name", "Show Coverage"},
            entt::attribute{"tooltip", "Show shadowmap coverage in view."},
        })
        .data<&light::shadowmap_params::hard>("hard"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "hard"},
            entt::attribute{"pretty_name", "Hard"},
            // entt::attribute{"flattable", true},
            entt::attribute{"predicate", sm_impl_hard_predicate_entt},
        })
        .data<&light::shadowmap_params::pcf>("pcf"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "pcf"},
            entt::attribute{"pretty_name", "PCF"},
            // entt::attribute{"flattable", true},
            entt::attribute{"predicate", sm_impl_pcf_predicate_entt},
        })
        .data<&light::shadowmap_params::pcss>("pcss"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "pcss"},
            entt::attribute{"pretty_name", "PCSS"},
            // entt::attribute{"flattable", true},
            entt::attribute{"predicate", sm_impl_pcss_predicate_entt},
        })
        .data<&light::shadowmap_params::vsm>("vsm"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "vsm"},
            entt::attribute{"pretty_name", "VSM"},
            // entt::attribute{"flattable", true},
            entt::attribute{"predicate", sm_impl_vsm_predicate_entt},
        })
        .data<&light::shadowmap_params::esm>("esm"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "esm"},
            entt::attribute{"pretty_name", "ESM"},
            // entt::attribute{"flattable", true},
            entt::attribute{"predicate", sm_impl_esm_predicate_entt},
        });


    auto contact_shadow_enabled_predicate_entt = entt::property_predicate<bool>(
        [](const entt::meta_any& obj)
        {
            auto data = obj.try_cast<light::contact_shadow_params>();
            if(!data)
            {
                return false;
            }
            return data->enabled;
        });

    entt::meta_factory<light::contact_shadow_params>{}
        .type("light::contact_shadow_params"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "contact_shadow_params"},
            entt::attribute{"pretty_name", "Contact Shadow Params"},
        })
        .data<&light::contact_shadow_params::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Enable screen-space contact shadows for fine detail at object contact points."},
        })
        .data<&light::contact_shadow_params::ray_length>("ray_length"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ray_length"},
            entt::attribute{"pretty_name", "Ray Length"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"max", 2.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Ray length in world units along the light. Longer rays catch occluders farther from the contact; near the camera the ray is shortened to a bounded number of pixels."},
            entt::attribute{"predicate", contact_shadow_enabled_predicate_entt},
        })
        .data<&light::contact_shadow_params::thickness>("thickness"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "thickness"},
            entt::attribute{"pretty_name", "Occluder Thickness"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.005f},
            entt::attribute{"tooltip", "Minimum occluder thickness in world units. Larger values let thin objects cast longer contact shadows but darken the ground beside objects."},
            entt::attribute{"predicate", contact_shadow_enabled_predicate_entt},
        })
        .data<&light::contact_shadow_params::max_distance>("max_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_distance"},
            entt::attribute{"pretty_name", "Max Distance"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1000.0f},
            entt::attribute{"step", 1.0f},
            entt::attribute{"tooltip", "Distance from the camera in world units where contact shadows fade out. 0 = unlimited."},
            entt::attribute{"predicate", contact_shadow_enabled_predicate_entt},
        })
        .data<&light::contact_shadow_params::opacity>("opacity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "opacity"},
            entt::attribute{"pretty_name", "Opacity"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Strength of the contact shadow."},
            entt::attribute{"predicate", contact_shadow_enabled_predicate_entt},
        });

    entt::meta_factory<light>{}
        .type("light"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "light"},
            entt::attribute{"pretty_name", "Light"},
        })
        .data<&light::color>("color"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "color"},
            entt::attribute{"pretty_name", "Color"} 
        })
        .data<&light::intensity>("intensity"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "intensity"},
            entt::attribute{"pretty_name", "Intensity"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.05f},
        })
        .data<&light::type>("type"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "type"},
            entt::attribute{"pretty_name", "Type"} 
        })
        .data<&light::directional_data>("directional_data"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "directional_data"},
            entt::attribute{"pretty_name", "Directional"},
            entt::attribute{"flattable", true},
            entt::attribute{"predicate", directional_predicate_entt}
        })
        .data<&light::point_data>("point_data"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "point_data"},
            entt::attribute{"pretty_name", "Point"},
            entt::attribute{"flattable", true},
            entt::attribute{"predicate", point_predicate_entt}
        })
        .data<&light::spot_data>("spot_data"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "spot_data"},
            entt::attribute{"pretty_name", "Spot"},
            entt::attribute{"flattable", true},
            entt::attribute{"predicate", spot_predicate_entt}
        })
        .data<&light::casts_shadows>("casts_shadows"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "casts_shadows"},
            entt::attribute{"pretty_name", "Casts Shadows"} 
        })
        .data<&light::shadow_params>("shadow_params"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "shadow_params"},
            entt::attribute{"pretty_name", "Common Shadow Params"},
            entt::attribute{"tooltip", "Shadow map parameters."},
            entt::attribute{"predicate", casts_shadows_predicate_entt}
        })
        .data<&light::directional_shadow_params>("directional_shadow_params"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "directional_shadow_params"},
            entt::attribute{"pretty_name", "Directional Shadow Params"},
            entt::attribute{"tooltip", "Directional light shadow map parameters."},
            entt::attribute{"predicate", casts_shadows_and_is_directional_predicate_entt}

        })
        .data<&light::point_shadow_params>("point_shadow_params"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "point_shadow_params"},
            entt::attribute{"pretty_name", "Point Shadow Params"},
            entt::attribute{"tooltip", "Point light shadow map parameters."},
            entt::attribute{"predicate", casts_shadows_and_is_point_predicate_entt}

        })
        .data<&light::spot_shadow_params>("spot_shadow_params"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "spot_shadow_params"},
            entt::attribute{"pretty_name", "Spot Shadow Params"},
            entt::attribute{"tooltip", "Spot light shadow map parameters."},
            entt::attribute{"predicate", casts_shadows_and_is_spot_predicate_entt}

        })
        .data<&light::contact_shadow>("contact_shadow"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "contact_shadow"},
            entt::attribute{"pretty_name", "Contact Shadow"},
            entt::attribute{"tooltip", "Screen-space contact shadows for fine shadow detail at object contact points."},
        });
}

SAVE(light::spot_shadowmap_params)
{
}
SAVE_INSTANTIATE(light::spot_shadowmap_params, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(light::spot_shadowmap_params, ser20::oarchive_binary_t);

SAVE(light::spot)
{
    try_save(ar, ser20::make_nvp("range", obj.range));
    try_save(ar, ser20::make_nvp("inner_angle", obj.inner_angle));
    try_save(ar, ser20::make_nvp("outer_angle", obj.outer_angle));
}
SAVE_INSTANTIATE(light::spot, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(light::spot, ser20::oarchive_binary_t);

SAVE(light::point_shadowmap_params)
{
    try_save(ar, ser20::make_nvp("fov_x_adjust", obj.fov_x_adjust));
    try_save(ar, ser20::make_nvp("fov_y_adjust", obj.fov_y_adjust));
    try_save(ar, ser20::make_nvp("stencil_pack", obj.stencil_pack));
}
SAVE_INSTANTIATE(light::point_shadowmap_params, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(light::point_shadowmap_params, ser20::oarchive_binary_t);

SAVE(light::point)
{
    try_save(ar, ser20::make_nvp("range", obj.range));
    try_save(ar, ser20::make_nvp("exponent_falloff", obj.exponent_falloff));
}
SAVE_INSTANTIATE(light::point, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(light::point, ser20::oarchive_binary_t);

SAVE(light::directional_shadowmap_params)
{
    try_save(ar, ser20::make_nvp("num_splits", obj.num_splits));
    try_save(ar, ser20::make_nvp("split_distribution", obj.split_distribution));
    try_save(ar, ser20::make_nvp("stabilize", obj.stabilize));
}
SAVE_INSTANTIATE(light::directional_shadowmap_params, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(light::directional_shadowmap_params, ser20::oarchive_binary_t);

SAVE(light::directional)
{
}
SAVE_INSTANTIATE(light::directional, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(light::directional, ser20::oarchive_binary_t);

SAVE(light::shadowmap_params)
{
    try_save(ar, ser20::make_nvp("type", obj.type));
    try_save(ar, ser20::make_nvp("depth", obj.depth));
    try_save(ar, ser20::make_nvp("resolution", obj.resolution));
    try_save(ar, ser20::make_nvp("depth_bias_texels", obj.bias));
    try_save(ar, ser20::make_nvp("slope_bias_texels", obj.slope_bias));
    try_save(ar, ser20::make_nvp("normal_bias_texels", obj.normal_bias));
    try_save(ar, ser20::make_nvp("near_plane", obj.near_plane));
    try_save(ar, ser20::make_nvp("far_plane", obj.far_plane));
    try_save(ar, ser20::make_nvp("show_coverage", obj.show_coverage));

    // Serialize union members based on type
    switch(obj.type)
    {
        case sm_impl::hard:
            // Hard shadows don't have parameters
            break;
        case sm_impl::pcf:
            try_save(ar, ser20::make_nvp("pcf_x_offset", obj.pcf.x_offset));
            try_save(ar, ser20::make_nvp("pcf_y_offset", obj.pcf.y_offset));
            break;
        case sm_impl::pcss:
            try_save(ar, ser20::make_nvp("pcss_penumbra_x_offset", obj.pcss.penumbra_x_offset));
            try_save(ar, ser20::make_nvp("pcss_penumbra_y_offset", obj.pcss.penumbra_y_offset));
            break;
        case sm_impl::vsm:
            try_save(ar, ser20::make_nvp("vsm_min_variance", obj.vsm.min_variance));
            try_save(ar, ser20::make_nvp("vsm_depth_multiplier", obj.vsm.depth_multiplier));
            try_save(ar, ser20::make_nvp("vsm_do_blur", obj.vsm.do_blur));
            if(obj.vsm.do_blur)
            {
                try_save(ar, ser20::make_nvp("vsm_blur_x_offset", obj.vsm.blur_x_offset));
                try_save(ar, ser20::make_nvp("vsm_blur_y_offset", obj.vsm.blur_y_offset));
            }
            break;
        case sm_impl::esm:
            try_save(ar, ser20::make_nvp("esm_hardness", obj.esm.hardness));
            try_save(ar, ser20::make_nvp("esm_depth_multiplier", obj.esm.depth_multiplier));
            try_save(ar, ser20::make_nvp("esm_do_blur", obj.esm.do_blur));
            if(obj.esm.do_blur)
            {
                try_save(ar, ser20::make_nvp("esm_blur_x_offset", obj.esm.blur_x_offset));
                try_save(ar, ser20::make_nvp("esm_blur_y_offset", obj.esm.blur_y_offset));
            }
            break;
        default:
            break;
    }
}
SAVE_INSTANTIATE(light::shadowmap_params, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(light::shadowmap_params, ser20::oarchive_binary_t);

// "occluder_thickness" replaced "thickness" together with the hit model: documents carrying the
// old window value load the new default.
SAVE(light::contact_shadow_params)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("ray_length", obj.ray_length));
    try_save(ar, ser20::make_nvp("occluder_thickness", obj.thickness));
    try_save(ar, ser20::make_nvp("max_distance", obj.max_distance));
    try_save(ar, ser20::make_nvp("opacity", obj.opacity));
}
SAVE_INSTANTIATE(light::contact_shadow_params, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(light::contact_shadow_params, ser20::oarchive_binary_t);

SAVE(light)
{
    try_save(ar, ser20::make_nvp("type", obj.type));
    try_save(ar, ser20::make_nvp("intensity", obj.intensity));
    try_save(ar, ser20::make_nvp("color", obj.color));
    try_save(ar, ser20::make_nvp("casts_shadows", obj.casts_shadows));

    try_save(ar, ser20::make_nvp("shadow_params", obj.shadow_params));
    try_save(ar, ser20::make_nvp("contact_shadow", obj.contact_shadow));

    if(obj.type == light_type::spot)
    {
        try_save(ar, ser20::make_nvp("spot_data", obj.spot_data));
        try_save(ar, ser20::make_nvp("spot_shadow_params", obj.spot_shadow_params));
    }
    else if(obj.type == light_type::point)
    {
        try_save(ar, ser20::make_nvp("point_data", obj.point_data));
        try_save(ar, ser20::make_nvp("point_shadow_params", obj.point_shadow_params));
    }
    else if(obj.type == light_type::directional)
    {
        try_save(ar, ser20::make_nvp("directional_data", obj.directional_data));
        try_save(ar, ser20::make_nvp("directional_shadow_params", obj.directional_shadow_params));
    }
}
SAVE_INSTANTIATE(light, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(light, ser20::oarchive_binary_t);

LOAD(light::spot_shadowmap_params)
{
}
LOAD_INSTANTIATE(light::spot_shadowmap_params, ser20::oarchive_associative_t);
LOAD_INSTANTIATE(light::spot_shadowmap_params, ser20::oarchive_binary_t);

LOAD(light::spot)
{
    try_load(ar, ser20::make_nvp("range", obj.range));
    try_load(ar, ser20::make_nvp("inner_angle", obj.inner_angle));
    try_load(ar, ser20::make_nvp("outer_angle", obj.outer_angle));
}
LOAD_INSTANTIATE(light::spot, ser20::oarchive_associative_t);
LOAD_INSTANTIATE(light::spot, ser20::oarchive_binary_t);

LOAD(light::point_shadowmap_params)
{
    try_load(ar, ser20::make_nvp("fov_x_adjust", obj.fov_x_adjust));
    try_load(ar, ser20::make_nvp("fov_y_adjust", obj.fov_y_adjust));
    try_load(ar, ser20::make_nvp("stencil_pack", obj.stencil_pack));
}
LOAD_INSTANTIATE(light::point_shadowmap_params, ser20::oarchive_associative_t);
LOAD_INSTANTIATE(light::point_shadowmap_params, ser20::oarchive_binary_t);

LOAD(light::point)
{
    try_load(ar, ser20::make_nvp("range", obj.range));
    try_load(ar, ser20::make_nvp("exponent_falloff", obj.exponent_falloff));
}
LOAD_INSTANTIATE(light::point, ser20::oarchive_associative_t);
LOAD_INSTANTIATE(light::point, ser20::oarchive_binary_t);

LOAD(light::directional_shadowmap_params)
{
    try_load(ar, ser20::make_nvp("num_splits", obj.num_splits));
    try_load(ar, ser20::make_nvp("split_distribution", obj.split_distribution));
    try_load(ar, ser20::make_nvp("stabilize", obj.stabilize));
}
LOAD_INSTANTIATE(light::directional_shadowmap_params, ser20::oarchive_associative_t);
LOAD_INSTANTIATE(light::directional_shadowmap_params, ser20::oarchive_binary_t);

LOAD(light::directional)
{
}
LOAD_INSTANTIATE(light::directional, ser20::oarchive_associative_t);
LOAD_INSTANTIATE(light::directional, ser20::oarchive_binary_t);

LOAD(light::shadowmap_params)
{
    try_load(ar, ser20::make_nvp("type", obj.type));
    try_load(ar, ser20::make_nvp("depth", obj.depth));
    try_load(ar, ser20::make_nvp("resolution", obj.resolution));
    // The biases are keyed by their unit. Documents written before the texel model stored
    // "bias" / "normal_bias" in normalized depth of the packed RGBA8 maps and in world units;
    // those values have no meaning here and are deliberately not read, so the defaults apply.
    try_load(ar, ser20::make_nvp("depth_bias_texels", obj.bias));
    try_load(ar, ser20::make_nvp("slope_bias_texels", obj.slope_bias));
    try_load(ar, ser20::make_nvp("normal_bias_texels", obj.normal_bias));
    try_load(ar, ser20::make_nvp("near_plane", obj.near_plane));
    try_load(ar, ser20::make_nvp("far_plane", obj.far_plane));
    try_load(ar, ser20::make_nvp("show_coverage", obj.show_coverage));

    // Initialize union based on type
    switch(obj.type)
    {
        case sm_impl::hard:
            // Hard shadows don't have parameters
            break;
        case sm_impl::pcf:
            try_load(ar, ser20::make_nvp("pcf_x_offset", obj.pcf.x_offset));
            try_load(ar, ser20::make_nvp("pcf_y_offset", obj.pcf.y_offset));
            break;
        case sm_impl::pcss:
            try_load(ar, ser20::make_nvp("pcss_penumbra_x_offset", obj.pcss.penumbra_x_offset));
            try_load(ar, ser20::make_nvp("pcss_penumbra_y_offset", obj.pcss.penumbra_y_offset));
            break;
        case sm_impl::vsm:
            try_load(ar, ser20::make_nvp("vsm_min_variance", obj.vsm.min_variance));
            try_load(ar, ser20::make_nvp("vsm_depth_multiplier", obj.vsm.depth_multiplier));
            try_load(ar, ser20::make_nvp("vsm_do_blur", obj.vsm.do_blur));
            if(obj.vsm.do_blur)
            {
                try_load(ar, ser20::make_nvp("vsm_blur_x_offset", obj.vsm.blur_x_offset));
                try_load(ar, ser20::make_nvp("vsm_blur_y_offset", obj.vsm.blur_y_offset));
            }
            break;
        case sm_impl::esm:
            try_load(ar, ser20::make_nvp("esm_hardness", obj.esm.hardness));
            try_load(ar, ser20::make_nvp("esm_depth_multiplier", obj.esm.depth_multiplier));
            try_load(ar, ser20::make_nvp("esm_do_blur", obj.esm.do_blur));
            if(obj.esm.do_blur)
            {
                try_load(ar, ser20::make_nvp("esm_blur_x_offset", obj.esm.blur_x_offset));
                try_load(ar, ser20::make_nvp("esm_blur_y_offset", obj.esm.blur_y_offset));
            }
            break;
        default:
            // Default to PCF if type is unknown
            break;
    }
}
LOAD_INSTANTIATE(light::shadowmap_params, ser20::oarchive_associative_t);
LOAD_INSTANTIATE(light::shadowmap_params, ser20::oarchive_binary_t);

LOAD(light::contact_shadow_params)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("ray_length", obj.ray_length));
    try_load(ar, ser20::make_nvp("occluder_thickness", obj.thickness));
    try_load(ar, ser20::make_nvp("max_distance", obj.max_distance));
    try_load(ar, ser20::make_nvp("opacity", obj.opacity));
}
LOAD_INSTANTIATE(light::contact_shadow_params, ser20::oarchive_associative_t);
LOAD_INSTANTIATE(light::contact_shadow_params, ser20::oarchive_binary_t);

LOAD(light)
{
    try_load(ar, ser20::make_nvp("type", obj.type));
    try_load(ar, ser20::make_nvp("intensity", obj.intensity));
    try_load(ar, ser20::make_nvp("color", obj.color));
    try_load(ar, ser20::make_nvp("casts_shadows", obj.casts_shadows));
    try_load(ar, ser20::make_nvp("shadow_params", obj.shadow_params));
    try_load(ar, ser20::make_nvp("contact_shadow", obj.contact_shadow));

    if(obj.type == light_type::spot)
    {
        try_load(ar, ser20::make_nvp("spot_data", obj.spot_data));
        try_load(ar, ser20::make_nvp("spot_shadow_params", obj.spot_shadow_params));
    }
    else if(obj.type == light_type::point)
    {
        try_load(ar, ser20::make_nvp("point_data", obj.point_data));
        try_load(ar, ser20::make_nvp("point_shadow_params", obj.point_shadow_params));
    }
    else if(obj.type == light_type::directional)
    {
        try_load(ar, ser20::make_nvp("directional_data", obj.directional_data));
        try_load(ar, ser20::make_nvp("directional_shadow_params", obj.directional_shadow_params));
    }
}
LOAD_INSTANTIATE(light, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(light, ser20::iarchive_binary_t);
} // namespace unravel
