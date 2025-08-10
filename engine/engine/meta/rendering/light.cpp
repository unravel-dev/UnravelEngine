#include "light.hpp"
#include <engine/meta/core/math/vector.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{
REFLECT(light)
{
    rttr::registration::class_<light::spot::shadowmap_params>("light::spot::shadowmap_params");

    rttr::registration::class_<light::spot>("light::spot")(rttr::metadata("pretty_name", "Spot"))
        .property("range", &light::spot::get_range, &light::spot::set_range)(
            rttr::metadata("pretty_name", "Range"),
            rttr::metadata("min", 0.1f),
            rttr::metadata("tooltip", "Light's range from its origin."))
        .property("inner_angle", &light::spot::get_inner_angle, &light::spot::set_inner_angle)(
            rttr::metadata("pretty_name", "Inner Angle"),
            rttr::metadata("min", 1.0f),
            rttr::metadata("max", 85.0f),
            rttr::metadata("step", 0.1f),
            rttr::metadata("tooltip", "Spot light inner cone angle."))
        .property("outer_angle", &light::spot::get_outer_angle, &light::spot::set_outer_angle)(
            rttr::metadata("pretty_name", "Outer Angle"),
            rttr::metadata("min", 1.0f),
            rttr::metadata("max", 90.0f),
            rttr::metadata("step", 0.1f),
            rttr::metadata("tooltip", "Spot light outer cone angle."));

    rttr::registration::class_<light::point::shadowmap_params>("light::point::shadowmap_params")
        .property("fovx_adjust", &light::point::shadowmap_params::fov_x_adjust)(
            rttr::metadata("pretty_name", "FovX Adjust"),
            rttr::metadata("min", -20.0f),
            rttr::metadata("max", 20.0f),
            rttr::metadata("step", 0.0001f),
            rttr::metadata("tooltip", "Shadowmap field of view adjust."))
        .property("fovy_adjust", &light::point::shadowmap_params::fov_y_adjust)(
            rttr::metadata("pretty_name", "FovY Adjust"),
            rttr::metadata("min", -20.0f),
            rttr::metadata("max", 20.0f),
            rttr::metadata("step", 0.0001f),
            rttr::metadata("tooltip", "Shadowmap field of view adjust."))
        .property("stencil_pack", &light::point::shadowmap_params::stencil_pack)(
            rttr::metadata("pretty_name", "Stencil Pack"),
            rttr::metadata("tooltip", "Shadowmap stencil packing algorithm."));

    rttr::registration::class_<light::point>("point")(rttr::metadata("pretty_name", "Point"))
        .property("range", &light::point::range)(rttr::metadata("pretty_name", "Range"),
                                                 rttr::metadata("min", 0.1f),
                                                 rttr::metadata("tooltip", "Light's range from its origin."))
        .property("exponent_falloff", &light::point::exponent_falloff)(
            rttr::metadata("pretty_name", "Exponent Falloff"),
            rttr::metadata("min", 0.1f),
            rttr::metadata("max", 10.0f),
            rttr::metadata("tooltip", "The falloff factor nearing the range edge."));

    rttr::registration::class_<light::directional::shadowmap_params>("light::directional::shadowmap_params")
        .property("splits",
                  &light::directional::shadowmap_params::num_splits)(rttr::metadata("pretty_name", "Splits"),
                                                                     rttr::metadata("min", 1),
                                                                     rttr::metadata("max", 4),
                                                                     rttr::metadata("tooltip", "Number of cascades."))
        .property("distribution", &light::directional::shadowmap_params::split_distribution)(
            rttr::metadata("pretty_name", "Distribution"),
            rttr::metadata("min", 0.0f),
            rttr::metadata("max", 1.0f),
            rttr::metadata("step", 0.001f),
            rttr::metadata("tooltip", "?"))
        .property("stabilize", &light::directional::shadowmap_params::stabilize)(
            rttr::metadata("pretty_name", "Stabilize"),
            rttr::metadata("tooltip", "Stabilize the shadowmaps."));

    rttr::registration::class_<light::directional>("light::directional")(rttr::metadata("pretty_name", "Directional"));

    rttr::registration::enumeration<light_type>("light_type")(rttr::value("Spot", light_type::spot),
                                                              rttr::value("Point", light_type::point),
                                                              rttr::value("Directional", light_type::directional));
    rttr::registration::enumeration<sm_depth>("sm_depth")(rttr::value("InvZ", sm_depth::invz),
                                                          rttr::value("Linear", sm_depth::linear));
    rttr::registration::enumeration<sm_impl>("sm_impl")(rttr::value("Hard", sm_impl::hard),
                                                        rttr::value("Pcf", sm_impl::pcf),
                                                        rttr::value("Pcss", sm_impl::pcss),
                                                        rttr::value("Vsm", sm_impl::vsm),
                                                        rttr::value("Esm", sm_impl::esm));
    rttr::registration::enumeration<sm_resolution>("sm_resolution")(rttr::value("Low", sm_resolution::low),
                                                                    rttr::value("Medium", sm_resolution::medium),
                                                                    rttr::value("High", sm_resolution::high),
                                                                    rttr::value("Very High", sm_resolution::very_high));

    // EnTT meta registration mirroring RTTR
    entt::meta_factory<light::spot::shadowmap_params>{}
        .type("light::spot::shadowmap_params"_hs);

    entt::meta_factory<light::spot>{}
        .type("light::spot"_hs)
        .data<&light::spot::set_range, &light::spot::get_range>("range"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Range"},
            entt::attribute{"min", 0.1f},
            entt::attribute{"tooltip", "Light's range from its origin."},
        })
        .data<&light::spot::set_inner_angle, &light::spot::get_inner_angle>("inner_angle"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Inner Angle"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 85.0f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"tooltip", "Spot light inner cone angle."},
        })
        .data<&light::spot::set_outer_angle, &light::spot::get_outer_angle>("outer_angle"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Outer Angle"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 90.0f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"tooltip", "Spot light outer cone angle."},
        });

    entt::meta_factory<light::point::shadowmap_params>{}
        .type("light::point::shadowmap_params"_hs)
        .data<&light::point::shadowmap_params::fov_x_adjust>("fov_x_adjust"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "FovX Adjust"},
            entt::attribute{"min", -20.0f},
            entt::attribute{"max", 20.0f},
            entt::attribute{"step", 0.0001f},
            entt::attribute{"tooltip", "Shadowmap field of view adjust."},
        })
        .data<&light::point::shadowmap_params::fov_y_adjust>("fov_y_adjust"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "FovY Adjust"},
            entt::attribute{"min", -20.0f},
            entt::attribute{"max", 20.0f},
            entt::attribute{"step", 0.0001f},
            entt::attribute{"tooltip", "Shadowmap field of view adjust."},
        })
        .data<&light::point::shadowmap_params::stencil_pack>("stencil_pack"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Stencil Pack"},
            entt::attribute{"tooltip", "Shadowmap stencil packing algorithm."},
        });

    entt::meta_factory<light::point>{}
        .type("point"_hs)
        .data<&light::point::range>("range"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Range"},
            entt::attribute{"min", 0.1f},
            entt::attribute{"tooltip", "Light's range from its origin."},
        })
        .data<&light::point::exponent_falloff>("exponent_falloff"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Exponent Falloff"},
            entt::attribute{"min", 0.1f},
            entt::attribute{"max", 10.0f},
            entt::attribute{"tooltip", "The falloff factor nearing the range edge."},
        });

    entt::meta_factory<light::directional::shadowmap_params>{}
        .type("light::directional::shadowmap_params"_hs)
        .data<&light::directional::shadowmap_params::num_splits>("splits"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Splits"},
            entt::attribute{"min", 1},
            entt::attribute{"max", 4},
            entt::attribute{"tooltip", "Number of cascades."},
        })
        .data<&light::directional::shadowmap_params::split_distribution>("distribution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Distribution"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.001f},
            entt::attribute{"tooltip", "?"},
        })
        .data<&light::directional::shadowmap_params::stabilize>("stabilize"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Stabilize"},
            entt::attribute{"tooltip", "Stabilize the shadowmaps."},
        });

    entt::meta_factory<light::directional>{}
        .type("light::directional"_hs);

    entt::meta_factory<light_type>{}
        .type("light_type"_hs)
        .data<light_type::spot>("spot"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Spot"} })
        .data<light_type::point>("point"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Point"} })
        .data<light_type::directional>("directional"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Directional"} });

    entt::meta_factory<sm_depth>{}
        .type("sm_depth"_hs)
        .data<sm_depth::invz>("invz"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "InvZ"} })
        .data<sm_depth::linear>("linear"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Linear"} });

    entt::meta_factory<sm_impl>{}
        .type("sm_impl"_hs)
        .data<sm_impl::hard>("hard"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Hard"} })
        .data<sm_impl::pcf>("pcf"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Pcf"} })
        .data<sm_impl::pcss>("pcss"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Pcss"} })
        .data<sm_impl::vsm>("vsm"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Vsm"} })
        .data<sm_impl::esm>("esm"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Esm"} });

    entt::meta_factory<sm_resolution>{}
        .type("sm_resolution"_hs)
        .data<sm_resolution::low>("low"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Low"} })
        .data<sm_resolution::medium>("medium"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Medium"} })
        .data<sm_resolution::high>("high"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "High"} })
        .data<sm_resolution::very_high>("very_high"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Very High"} });

    entt::meta_factory<light::shadowmap_params>{}
        .type("light::shadowmap_params"_hs)
        .data<&light::shadowmap_params::type>("type"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Type"},
            entt::attribute{"tooltip", "Shadowmap implementation type."},
        })
        .data<&light::shadowmap_params::depth>("depth"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Depth"},
            entt::attribute{"tooltip", "Shadowmap depth pack algorithm."},
        })
        .data<&light::shadowmap_params::resolution>("resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Resolution"},
            entt::attribute{"tooltip", "Shadowmap resolution."},
        })
        .data<&light::shadowmap_params::bias>("bias"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Bias"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 0.01f},
            entt::attribute{"step", 0.00001f},
            entt::attribute{"tooltip", "Shadowmap bias offset."},
        })
        .data<&light::shadowmap_params::normal_bias>("normal_bias"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Normal Bias"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 0.25f},
            entt::attribute{"step", 0.00001f},
            entt::attribute{"tooltip", "Shadowmap normal bias offset"},
        })
        .data<&light::shadowmap_params::near_plane>("near_plane"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Near Plane"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"max", 10.0f},
            entt::attribute{"tooltip", "Shadowmap near plane"},
        })
        .data<&light::shadowmap_params::far_plane>("far_plane"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Far Plane"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"max", 10000.0f},
            entt::attribute{"tooltip", "Shadowmap far plane"},
        })
        .data<&light::shadowmap_params::show_coverage>("show_coverage"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Show Coverage"},
            entt::attribute{"tooltip", "Show shadowmap coverage in view."},
        });

    entt::meta_factory<light>{}
        .type("light"_hs)
        .data<&light::color>("color"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Color"} })
        .data<&light::intensity>("intensity"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Intensity"} })
        .data<&light::ambient_intensity>("ambient_intensity"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Ambient Intensity"} })
        .data<&light::type>("type"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Type"} })
        .data<&light::casts_shadows>("casts_shadows"_hs)
        .custom<entt::attributes>(entt::attributes{ entt::attribute{"pretty_name", "Casts Shadows"} });

    // rttr::registration::class_<light::shadowmap_params::impl_params>("light::shadowmap_params::impl_params")
    //     .property("hardness",
    //               &light::shadowmap_params::impl_params::hardness)(rttr::metadata("pretty_name", "Hardness"),
    //                                                                rttr::metadata("tooltip", "Missing"))
    //     .property("depth_multiplier",
    //               &light::shadowmap_params::impl_params::depth_multiplier)(rttr::metadata("pretty_name", "Depth Multiplier"),
    //                                                                rttr::metadata("tooltip", "Missing"))
    //     .property("blur_x_num",
    //               &light::shadowmap_params::impl_params::blur_x_num)(rttr::metadata("pretty_name", "Blur X Num"),
    //                                                                  rttr::metadata("tooltip", "Missing"))
    //     .property("blur_y_num",
    //               &light::shadowmap_params::impl_params::blur_y_num)(rttr::metadata("pretty_name", "Blur Y Num"),
    //                                                                  rttr::metadata("tooltip", "Missing"))
    //     .property("blur_x_offset",
    //               &light::shadowmap_params::impl_params::blur_x_offset)(rttr::metadata("pretty_name", "Blur X Offset"),
    //                                                                     rttr::metadata("tooltip", "Missing"))
    //     .property("blur_y_offset",
    //               &light::shadowmap_params::impl_params::blur_y_offset)(rttr::metadata("pretty_name", "Blur Y Offset"),
    //                                                                     rttr::metadata("tooltip", "Missing"));

    rttr::registration::class_<light::shadowmap_params>("light::shadowmap_params")

        .property("type", &light::shadowmap_params::type)(rttr::metadata("pretty_name", "Type"),
                                                          rttr::metadata("tooltip", "Shadowmap implementation type."))
        .property("depth",
                  &light::shadowmap_params::depth)(rttr::metadata("pretty_name", "Depth"),
                                                   rttr::metadata("tooltip", "Shadowmap depth pack algorithm."))

        .property("resolution",
                  &light::shadowmap_params::resolution)(rttr::metadata("pretty_name", "Resolution"),
                                                        rttr::metadata("tooltip", "Shadowmap resolution."))
        .property("bias", &light::shadowmap_params::bias)(rttr::metadata("pretty_name", "Bias"),
                                                          rttr::metadata("min", 0.0f),
                                                          rttr::metadata("max", 0.01f),
                                                          rttr::metadata("step", 0.00001f),
                                                          rttr::metadata("tooltip", "Shadowmap bias offset."))
        .property("normal_bias",
                  &light::shadowmap_params::normal_bias)(rttr::metadata("pretty_name", "Normal Bias"),
                                                         rttr::metadata("min", 0.0f),
                                                         rttr::metadata("max", 0.25f),
                                                         rttr::metadata("step", 0.00001f),
                                                         rttr::metadata("tooltip", "Shadowmap normal bias offset"))
        .property("near_plane", &light::shadowmap_params::near_plane)(rttr::metadata("pretty_name", "Near Plane"),
                                                                      rttr::metadata("min", 0.01f),
                                                                      rttr::metadata("max", 10.0f),
                                                                      rttr::metadata("tooltip", "Shadowmap near plane"))
        .property("far_plane", &light::shadowmap_params::far_plane)(rttr::metadata("pretty_name", "Far Plane"),
                                                                      rttr::metadata("min", 0.01f),
                                                                      rttr::metadata("max", 10000.0f),
                                                                      rttr::metadata("tooltip", "Shadowmap far plane"))
        .property("show_coverage", &light::shadowmap_params::show_coverage)(
            rttr::metadata("pretty_name", "Show Coverage"),
            rttr::metadata("tooltip", "Show shadowmap coverage in view."));

    rttr::registration::class_<light>("light")
        .property("color", &light::color)(rttr::metadata("pretty_name", "Color"),
                                          rttr::metadata("tooltip", "Light's color."))
        .property("intensity", &light::intensity)(rttr::metadata("pretty_name", "Intensity"),
                                                  rttr::metadata("min", 0.0f),
                                                  rttr::metadata("max", 20.0f),
                                                  rttr::metadata("tooltip", "Light's intensity."))
        .property("ambient_intensity", &light::ambient_intensity)(rttr::metadata("pretty_name", "Ambient Intensity"),
                                                  rttr::metadata("min", 0.0f),
                                                  rttr::metadata("max", 0.2f),
                                                  rttr::metadata("tooltip", "Light's ambient intensity."))
        .property("type", &light::type)(rttr::metadata("pretty_name", "Type"),
                                        rttr::metadata("tooltip", "Light's type."))
        .property("casts_shadows", &light::casts_shadows)(rttr::metadata("pretty_name", "Casts Shadows"),
                                                          rttr::metadata("tooltip", "Is this light casting shadows."));
}

SAVE(light::spot::shadowmap_params)
{
}
SAVE_INSTANTIATE(light::spot::shadowmap_params, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(light::spot::shadowmap_params, ser20::oarchive_binary_t);

SAVE(light::spot)
{
    try_save(ar, ser20::make_nvp("range", obj.range));
    try_save(ar, ser20::make_nvp("inner_angle", obj.inner_angle));
    try_save(ar, ser20::make_nvp("outer_angle", obj.outer_angle));
    try_save(ar, ser20::make_nvp("shadow_params", obj.shadow_params));
}
SAVE_INSTANTIATE(light::spot, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(light::spot, ser20::oarchive_binary_t);

SAVE(light::point::shadowmap_params)
{
    try_save(ar, ser20::make_nvp("fov_x_adjust", obj.fov_x_adjust));
    try_save(ar, ser20::make_nvp("fov_y_adjust", obj.fov_y_adjust));
    try_save(ar, ser20::make_nvp("stencil_pack", obj.stencil_pack));
}
SAVE_INSTANTIATE(light::point::shadowmap_params, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(light::point::shadowmap_params, ser20::oarchive_binary_t);

SAVE(light::point)
{
    try_save(ar, ser20::make_nvp("range", obj.range));
    try_save(ar, ser20::make_nvp("exponent_falloff", obj.exponent_falloff));
    try_save(ar, ser20::make_nvp("shadow_params", obj.shadow_params));
}
SAVE_INSTANTIATE(light::point, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(light::point, ser20::oarchive_binary_t);

SAVE(light::directional::shadowmap_params)
{
    try_save(ar, ser20::make_nvp("num_splits", obj.num_splits));
    try_save(ar, ser20::make_nvp("split_distribution", obj.split_distribution));
    try_save(ar, ser20::make_nvp("stabilize", obj.stabilize));
}
SAVE_INSTANTIATE(light::directional::shadowmap_params, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(light::directional::shadowmap_params, ser20::oarchive_binary_t);

SAVE(light::directional)
{
    try_save(ar, ser20::make_nvp("shadow_params", obj.shadow_params));
}
SAVE_INSTANTIATE(light::directional, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(light::directional, ser20::oarchive_binary_t);

SAVE(light::shadowmap_params)
{
    try_save(ar, ser20::make_nvp("type", obj.type));
    try_save(ar, ser20::make_nvp("depth", obj.depth));
    try_save(ar, ser20::make_nvp("resolution", obj.resolution));
    try_save(ar, ser20::make_nvp("bias", obj.bias));
    try_save(ar, ser20::make_nvp("normal_bias", obj.normal_bias));
    try_save(ar, ser20::make_nvp("near_plane", obj.near_plane));
    try_save(ar, ser20::make_nvp("far_plane", obj.far_plane));


}
SAVE_INSTANTIATE(light::shadowmap_params, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(light::shadowmap_params, ser20::oarchive_binary_t);

SAVE(light)
{
    try_save(ar, ser20::make_nvp("type", obj.type));
    try_save(ar, ser20::make_nvp("intensity", obj.intensity));
    try_save(ar, ser20::make_nvp("ambient_intensity", obj.ambient_intensity));
    try_save(ar, ser20::make_nvp("color", obj.color));
    try_save(ar, ser20::make_nvp("casts_shadows", obj.casts_shadows));

    try_save(ar, ser20::make_nvp("shadow_params", obj.shadow_params));

    if(obj.type == light_type::spot)
    {
        try_save(ar, ser20::make_nvp("spot_data", obj.spot_data));
    }
    else if(obj.type == light_type::point)
    {
        try_save(ar, ser20::make_nvp("point_data", obj.point_data));
    }
    else if(obj.type == light_type::directional)
    {
        try_save(ar, ser20::make_nvp("directional_data", obj.directional_data));
    }
}
SAVE_INSTANTIATE(light, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(light, ser20::oarchive_binary_t);

LOAD(light::spot::shadowmap_params)
{
}
LOAD_INSTANTIATE(light::spot::shadowmap_params, ser20::oarchive_associative_t);
LOAD_INSTANTIATE(light::spot::shadowmap_params, ser20::oarchive_binary_t);

LOAD(light::spot)
{
    try_load(ar, ser20::make_nvp("range", obj.range));
    try_load(ar, ser20::make_nvp("inner_angle", obj.inner_angle));
    try_load(ar, ser20::make_nvp("outer_angle", obj.outer_angle));
    try_load(ar, ser20::make_nvp("shadow_params", obj.shadow_params));
}
LOAD_INSTANTIATE(light::spot, ser20::oarchive_associative_t);
LOAD_INSTANTIATE(light::spot, ser20::oarchive_binary_t);

LOAD(light::point::shadowmap_params)
{
    try_load(ar, ser20::make_nvp("fov_x_adjust", obj.fov_x_adjust));
    try_load(ar, ser20::make_nvp("fov_y_adjust", obj.fov_y_adjust));
    try_load(ar, ser20::make_nvp("stencil_pack", obj.stencil_pack));
}
LOAD_INSTANTIATE(light::point::shadowmap_params, ser20::oarchive_associative_t);
LOAD_INSTANTIATE(light::point::shadowmap_params, ser20::oarchive_binary_t);

LOAD(light::point)
{
    try_load(ar, ser20::make_nvp("range", obj.range));
    try_load(ar, ser20::make_nvp("exponent_falloff", obj.exponent_falloff));
    try_load(ar, ser20::make_nvp("shadow_params", obj.shadow_params));
}
LOAD_INSTANTIATE(light::point, ser20::oarchive_associative_t);
LOAD_INSTANTIATE(light::point, ser20::oarchive_binary_t);

LOAD(light::directional::shadowmap_params)
{
    try_load(ar, ser20::make_nvp("num_splits", obj.num_splits));
    try_load(ar, ser20::make_nvp("split_distribution", obj.split_distribution));
    try_load(ar, ser20::make_nvp("stabilize", obj.stabilize));
}
LOAD_INSTANTIATE(light::directional::shadowmap_params, ser20::oarchive_associative_t);
LOAD_INSTANTIATE(light::directional::shadowmap_params, ser20::oarchive_binary_t);

LOAD(light::directional)
{
    try_load(ar, ser20::make_nvp("shadow_params", obj.shadow_params));
}
LOAD_INSTANTIATE(light::directional, ser20::oarchive_associative_t);
LOAD_INSTANTIATE(light::directional, ser20::oarchive_binary_t);

LOAD(light::shadowmap_params)
{
    try_load(ar, ser20::make_nvp("type", obj.type));
    try_load(ar, ser20::make_nvp("depth", obj.depth));
    try_load(ar, ser20::make_nvp("resolution", obj.resolution));
    try_load(ar, ser20::make_nvp("bias", obj.bias));
    try_load(ar, ser20::make_nvp("normal_bias", obj.normal_bias));
    try_load(ar, ser20::make_nvp("near_plane", obj.near_plane));
    try_load(ar, ser20::make_nvp("far_plane", obj.far_plane));
}
LOAD_INSTANTIATE(light::shadowmap_params, ser20::oarchive_associative_t);
LOAD_INSTANTIATE(light::shadowmap_params, ser20::oarchive_binary_t);

LOAD(light)
{
    try_load(ar, ser20::make_nvp("type", obj.type));
    try_load(ar, ser20::make_nvp("intensity", obj.intensity));
    try_load(ar, ser20::make_nvp("ambient_intensity", obj.ambient_intensity));
    try_load(ar, ser20::make_nvp("color", obj.color));
    try_load(ar, ser20::make_nvp("casts_shadows", obj.casts_shadows));
    try_load(ar, ser20::make_nvp("shadow_params", obj.shadow_params));

    if(obj.type == light_type::spot)
    {
        try_load(ar, ser20::make_nvp("spot_data", obj.spot_data));
    }
    else if(obj.type == light_type::point)
    {
        try_load(ar, ser20::make_nvp("point_data", obj.point_data));
    }
    else if(obj.type == light_type::directional)
    {
        try_load(ar, ser20::make_nvp("directional_data", obj.directional_data));
    }
}
LOAD_INSTANTIATE(light, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(light, ser20::iarchive_binary_t);
} // namespace unravel
