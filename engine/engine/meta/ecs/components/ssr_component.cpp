#include "ssr_component.hpp"
#include <engine/meta/core/math/vector.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT_INLINE(trace_resolution)
{
    entt::meta_factory<trace_resolution>{}
        .type("trace_resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "trace_resolution"},
            entt::attribute{"pretty_name", "Trace Resolution"},
        })
        .data<trace_resolution::full>("full"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "full"},
            entt::attribute{"pretty_name", "Full"},
        })
        .data<trace_resolution::half>("half"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "half"},
            entt::attribute{"pretty_name", "Half (1/2)"},
        })
        .data<trace_resolution::quarter>("quarter"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "quarter"},
            entt::attribute{"pretty_name", "Quarter (1/4)"},
        });
}

REFLECT_INLINE(ssr_pass::fidelityfx_ssr_settings)
{
    using fidelityfx_settings = ssr_pass::fidelityfx_ssr_settings;
    using cone_tracing_settings = ssr_pass::fidelityfx_ssr_settings::cone_tracing_settings;
    using temporal_settings = ssr_pass::fidelityfx_ssr_settings::temporal_settings;
    using spatial_denoise_settings = ssr_pass::fidelityfx_ssr_settings::spatial_denoise_settings;

    auto cone_tracing_predicate_entt = entt::property_predicate<bool>([](const entt::meta_any& obj)
    {
        auto data = obj.try_cast<fidelityfx_settings>();
        return data->enable_cone_tracing;
    });

    auto temporal_predicate_entt = entt::property_predicate<bool>([](const entt::meta_any& obj)
    {
        if(auto data = obj.try_cast<fidelityfx_settings>())
        {
            return data->enable_temporal_accumulation;
        }
        return false;
    });

    auto spatial_denoise_predicate_entt = entt::property_predicate<bool>([](const entt::meta_any& obj)
    {
        if(auto data = obj.try_cast<fidelityfx_settings>())
        {
            return data->enable_spatial_denoise;
        }
        return false;
    });

    entt::meta_factory<cone_tracing_settings>{}
        .type("ssr_pass::fidelityfx_ssr_settings::cone_tracing_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ssr_pass::fidelityfx_ssr_settings::cone_tracing_settings"},
            entt::attribute{"pretty_name", "Cone Tracing Settings"},
        })
        .data<&cone_tracing_settings::cone_angle_bias>("cone_angle_bias"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cone_angle_bias"},
            entt::attribute{"pretty_name", "Cone Angle Bias"},
            entt::attribute{"min", 0.001f},
            entt::attribute{"max", 0.1f},
            entt::attribute{"tooltip", "Controls cone growth rate for glossy reflections"},
        })
        .data<&cone_tracing_settings::max_mip_level>("max_mip_level"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_mip_level"},
            entt::attribute{"pretty_name", "Max Mip Level"},
            entt::attribute{"min", 1},
            entt::attribute{"max", 10},
            entt::attribute{"tooltip", "Number of blur mip levels - 1"},
        })
        .data<&cone_tracing_settings::blur_base_sigma>("blur_base_sigma"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "blur_base_sigma"},
            entt::attribute{"pretty_name", "Blur Base Sigma"},
            entt::attribute{"min", 0.1f},
            entt::attribute{"max", 5.0f},
            entt::attribute{"tooltip", "Base blur sigma for mip generation (CPU-side only)"},
        });

    // -------------------------------------------------------------------------
    //  Temporal Accumulation Settings  (matches ApplyTemporalAccumulation v2)
    // -------------------------------------------------------------------------
    
    entt::meta_factory<temporal_settings>{}
        .type("ssr_pass::fidelityfx_ssr_settings::temporal_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ssr_pass::fidelityfx_ssr_settings::temporal_settings"},
            entt::attribute{"pretty_name", "Temporal Accumulation Settings"},
        })
        .data<&temporal_settings::history_strength>("history_strength"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "history_strength"},
            entt::attribute{"pretty_name", "History Strength"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "Controls how long reflections keep history.\n0 = real-time only   ·   1 = maximum denoise"},
        })
        .data<&temporal_settings::depth_threshold>("depth_threshold"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "depth_threshold"},
            entt::attribute{"pretty_name", "Edge Threshold"},
            entt::attribute{"min", 0.000f},
            entt::attribute{"max", 0.030f},
            entt::attribute{"tooltip", "Depth difference allowed before history is discarded.\nLower = crisper edges, higher = smoother but risk of bleed"},
        })
        .data<&temporal_settings::roughness_sensitivity>("roughness_sensitivity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "roughness_sensitivity"},
            entt::attribute{"pretty_name", "Material Sensitivity"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "How strongly rough surfaces shorten history.\n0 = same for every material   ·   1 = glossy keeps more history"},
        })
        .data<&temporal_settings::motion_scale_pixels>("motion_scale_pixels"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "motion_scale_pixels"},
            entt::attribute{"pretty_name", "Motion Scale Pixels"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1000.0f},
            entt::attribute{"tooltip", "Motion scale in pixels"},
        })
        .data<&temporal_settings::normal_dot_threshold>("normal_dot_threshold"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "normal_dot_threshold"},
            entt::attribute{"pretty_name", "Normal Dot Threshold"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "Normal dot threshold for motion detection"},
        })
        .data<&temporal_settings::max_accum_frames>("max_accum_frames"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_accum_frames"},
            entt::attribute{"pretty_name", "Max Accum Frames"},
            entt::attribute{"min", 1},
            entt::attribute{"max", 16},
            entt::attribute{"tooltip", "Maximum temporal accumulation frames"},
        });

    // -------------------------------------------------------------------------
    //  Spatial Denoise Settings  (a-trous wavelet filter before temporal resolve)
    // -------------------------------------------------------------------------
    entt::meta_factory<spatial_denoise_settings>{}
        .type("ssr_pass::fidelityfx_ssr_settings::spatial_denoise_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ssr_pass::fidelityfx_ssr_settings::spatial_denoise_settings"},
            entt::attribute{"pretty_name", "Spatial Denoise Settings"},
        })
        .data<&spatial_denoise_settings::depth_sigma>("depth_sigma"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "depth_sigma"},
            entt::attribute{"pretty_name", "Depth Sigma"},
            entt::attribute{"min", 0.005f},
            entt::attribute{"max", 0.05f},
            entt::attribute{"tooltip", "Depth edge-stopping threshold. Lower = stricter edges, higher = looser"},
        })
        .data<&spatial_denoise_settings::normal_power>("normal_power"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "normal_power"},
            entt::attribute{"pretty_name", "Normal Power"},
            entt::attribute{"min", 16.0f},
            entt::attribute{"max", 128.0f},
            entt::attribute{"tooltip", "Normal edge-stopping exponent. Higher = stricter edges"},
        })
        .data<&spatial_denoise_settings::luma_sigma>("luma_sigma"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "luma_sigma"},
            entt::attribute{"pretty_name", "Luma Sigma"},
            entt::attribute{"min", 0.3f},
            entt::attribute{"max", 2.0f},
            entt::attribute{"tooltip", "Luminance edge-stopping threshold. Lower = sharper, higher = smoother"},
        })
        .data<&spatial_denoise_settings::passes>("passes"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "passes"},
            entt::attribute{"pretty_name", "Passes"},
            entt::attribute{"min", 1},
            entt::attribute{"max", 5},
            entt::attribute{"tooltip", "Number of a-trous filter passes. More passes = wider blur reach but higher cost"},
        });

    entt::meta_factory<fidelityfx_settings>{}
        .type("ssr_pass::fidelityfx_ssr_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ssr_pass::fidelityfx_ssr_settings"},
            entt::attribute{"pretty_name", "FidelityFX SSR Settings"},
        })
        .data<&fidelityfx_settings::max_rays>("max_rays"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_rays"},
            entt::attribute{"pretty_name", "Max Rays"},
            entt::attribute{"min", 1},
            entt::attribute{"max", 64},
            entt::attribute{"tooltip", "Maximum rays for rough surfaces (future: cone tracing)"},
        })
        .data<&fidelityfx_settings::max_steps>("max_steps"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_steps"},
            entt::attribute{"pretty_name", "Max Steps"},
            entt::attribute{"min", 8},
            entt::attribute{"max", 200},
            entt::attribute{"tooltip", "Maximum ray marching steps for hierarchical traversal"},
        })
        .data<&fidelityfx_settings::depth_tolerance>("depth_tolerance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "depth_tolerance"},
            entt::attribute{"pretty_name", "Depth Tolerance"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"max", 2.0f},
            entt::attribute{"tooltip", "Depth tolerance for hit validation"},
        })
        .data<&fidelityfx_settings::brightness>("brightness"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "brightness"},
            entt::attribute{"pretty_name", "Brightness"},
            entt::attribute{"min", 0.1f},
            entt::attribute{"max", 3.0f},
            entt::attribute{"tooltip", "Reflection brightness multiplier"},
        })
        .data<&fidelityfx_settings::facing_reflections_fading>("facing_reflections_fading"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "facing_reflections_fading"},
            entt::attribute{"pretty_name", "Facing Reflections Fading"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip",
                            "Angular dead cone toward the view axis where SSR fades out "
                            "(reflections of the camera's own position). 0 = off, 0.1 fades "
                            "only rays nearly straight back at the camera, 1 fades the whole "
                            "toward-camera hemisphere. Validated hits outside the cone "
                            "composite at full strength."},
        })
        .data<&fidelityfx_settings::roughness_depth_tolerance>("roughness_depth_tolerance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "roughness_depth_tolerance"},
            entt::attribute{"pretty_name", "Roughness Depth Tolerance"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 2.0f},
            entt::attribute{"tooltip", "Additional depth tolerance for rough surfaces"},
        })
        .data<&fidelityfx_settings::screen_edge_fade>("screen_edge_fade"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "screen_edge_fade"},
            entt::attribute{"pretty_name", "Screen Edge Fade"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 0.5f},
            entt::attribute{"tooltip",
                            "Border band (fraction of the screen) over which reflections "
                            "sourced near the frame edge fade toward the fallback layer, "
                            "softening the transition where rays leave the screen."},
        })
        .data<&fidelityfx_settings::resolution>("resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "resolution"},
            entt::attribute{"pretty_name", "Trace Resolution"},
            entt::attribute{"tooltip", "Downscale divisor for SSR trace buffers.\nSSR is capped at Half at runtime - Quarter degrades Hi-Z traversal, temporal clamp and the denoiser severely."},
        })
        // .data<&fidelityfx_settings::enable_cone_tracing>("enable_cone_tracing"_hs)
        // .custom<entt::attributes>(entt::attributes{
        //     entt::attribute{"name", "enable_cone_tracing"},
        //     entt::attribute{"pretty_name", "Enable Cone Tracing"},
        //     entt::attribute{"tooltip", "Enable cone tracing for glossy reflections"},
        // })
        // .data<&fidelityfx_settings::cone_tracing>("cone_tracing"_hs)
        // .custom<entt::attributes>(entt::attributes{
        //     entt::attribute{"name", "cone_tracing"},
        //     entt::attribute{"predicate", cone_tracing_predicate_entt},
        //     entt::attribute{"pretty_name", "Cone Tracing"},
        //     entt::attribute{"tooltip", "Cone tracing specific settings"},
        //     entt::attribute{"flattable", true},
        // })
        .data<&fidelityfx_settings::enable_spatial_denoise>("enable_spatial_denoise"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enable_spatial_denoise"},
            entt::attribute{"pretty_name", "Enable Spatial Denoise"},
            entt::attribute{"tooltip", "Enable spatial denoising (a-trous wavelet) before temporal resolve"},
        })
        .data<&fidelityfx_settings::spatial_denoise>("spatial_denoise"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "spatial_denoise"},
            entt::attribute{"predicate", spatial_denoise_predicate_entt},
            entt::attribute{"pretty_name", "Spatial Denoise"},
            entt::attribute{"tooltip", "Spatial denoise settings (edge-preserving a-trous filter)"},
            // entt::attribute{"flattable", true},
        })
        .data<&fidelityfx_settings::enable_temporal_accumulation>("enable_temporal_accumulation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enable_temporal_accumulation"},
            entt::attribute{"pretty_name", "Enable Temporal"},
            entt::attribute{"tooltip", "Enable temporal accumulation to reduce noise over multiple frames"},
        })
        .data<&fidelityfx_settings::temporal>("temporal"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "temporal"},
            entt::attribute{"predicate", temporal_predicate_entt},
            entt::attribute{"pretty_name", "Temporal Accumulation"},
            entt::attribute{"tooltip", "Temporal accumulation settings"},
            // entt::attribute{"flattable", true},
        });


}

REFLECT_INLINE(ssr_pass::ssr_settings)
{
    using ssr_settings = ssr_pass::ssr_settings;

    
    entt::meta_factory<ssr_settings>{}
        .type("ssr_pass::ssr_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ssr_pass::ssr_settings"},
            entt::attribute{"pretty_name", "SSR Settings"},
        })
        .data<&ssr_settings::fidelityfx>("fidelityfx"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fidelityfx"},
            entt::attribute{"pretty_name", "FidelityFX Settings"},
            entt::attribute{"tooltip", "Settings for AMD FidelityFX SSSR implementation"},
            entt::attribute{"flattable", true},
        });
}

// Serialization for cone_tracing_settings
SAVE_INLINE(ssr_pass::fidelityfx_ssr_settings::cone_tracing_settings)
{
    try_save(ar, ser20::make_nvp("cone_angle_bias", obj.cone_angle_bias));
    try_save(ar, ser20::make_nvp("max_mip_level", obj.max_mip_level));
    try_save(ar, ser20::make_nvp("blur_base_sigma", obj.blur_base_sigma));
}
SAVE_INSTANTIATE(ssr_pass::fidelityfx_ssr_settings::cone_tracing_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(ssr_pass::fidelityfx_ssr_settings::cone_tracing_settings, ser20::oarchive_binary_t);

LOAD_INLINE(ssr_pass::fidelityfx_ssr_settings::cone_tracing_settings)
{
    try_load(ar, ser20::make_nvp("cone_angle_bias", obj.cone_angle_bias));
    try_load(ar, ser20::make_nvp("max_mip_level", obj.max_mip_level));
    try_load(ar, ser20::make_nvp("blur_base_sigma", obj.blur_base_sigma));
}
LOAD_INSTANTIATE(ssr_pass::fidelityfx_ssr_settings::cone_tracing_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(ssr_pass::fidelityfx_ssr_settings::cone_tracing_settings, ser20::iarchive_binary_t);

// Serialization for temporal_settings
SAVE_INLINE(ssr_pass::fidelityfx_ssr_settings::temporal_settings)
{
    try_save(ar, ser20::make_nvp("history_strength", obj.history_strength));
    try_save(ar, ser20::make_nvp("depth_threshold", obj.depth_threshold));
    try_save(ar, ser20::make_nvp("roughness_sensitivity", obj.roughness_sensitivity));
    try_save(ar, ser20::make_nvp("motion_scale_pixels", obj.motion_scale_pixels));
    try_save(ar, ser20::make_nvp("normal_dot_threshold", obj.normal_dot_threshold));
    try_save(ar, ser20::make_nvp("max_accum_frames", obj.max_accum_frames));
}
SAVE_INSTANTIATE(ssr_pass::fidelityfx_ssr_settings::temporal_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(ssr_pass::fidelityfx_ssr_settings::temporal_settings, ser20::oarchive_binary_t);

LOAD_INLINE(ssr_pass::fidelityfx_ssr_settings::temporal_settings)
{
    try_load(ar, ser20::make_nvp("history_strength", obj.history_strength));
    try_load(ar, ser20::make_nvp("depth_threshold", obj.depth_threshold));
    try_load(ar, ser20::make_nvp("roughness_sensitivity", obj.roughness_sensitivity));
    try_load(ar, ser20::make_nvp("motion_scale_pixels", obj.motion_scale_pixels));
    try_load(ar, ser20::make_nvp("normal_dot_threshold", obj.normal_dot_threshold));
    try_load(ar, ser20::make_nvp("max_accum_frames", obj.max_accum_frames));
}
LOAD_INSTANTIATE(ssr_pass::fidelityfx_ssr_settings::temporal_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(ssr_pass::fidelityfx_ssr_settings::temporal_settings, ser20::iarchive_binary_t);

// Serialization for spatial_denoise_settings
SAVE_INLINE(ssr_pass::fidelityfx_ssr_settings::spatial_denoise_settings)
{
    try_save(ar, ser20::make_nvp("depth_sigma", obj.depth_sigma));
    try_save(ar, ser20::make_nvp("normal_power", obj.normal_power));
    try_save(ar, ser20::make_nvp("luma_sigma", obj.luma_sigma));
    try_save(ar, ser20::make_nvp("passes", obj.passes));
}
SAVE_INSTANTIATE(ssr_pass::fidelityfx_ssr_settings::spatial_denoise_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(ssr_pass::fidelityfx_ssr_settings::spatial_denoise_settings, ser20::oarchive_binary_t);

LOAD_INLINE(ssr_pass::fidelityfx_ssr_settings::spatial_denoise_settings)
{
    try_load(ar, ser20::make_nvp("depth_sigma", obj.depth_sigma));
    try_load(ar, ser20::make_nvp("normal_power", obj.normal_power));
    try_load(ar, ser20::make_nvp("luma_sigma", obj.luma_sigma));
    try_load(ar, ser20::make_nvp("passes", obj.passes));
}
LOAD_INSTANTIATE(ssr_pass::fidelityfx_ssr_settings::spatial_denoise_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(ssr_pass::fidelityfx_ssr_settings::spatial_denoise_settings, ser20::iarchive_binary_t);

// Serialization for fidelityfx_ssr_settings
SAVE_INLINE(ssr_pass::fidelityfx_ssr_settings)
{
    try_save(ar, ser20::make_nvp("max_steps", obj.max_steps));
    try_save(ar, ser20::make_nvp("max_rays", obj.max_rays));
    try_save(ar, ser20::make_nvp("depth_tolerance", obj.depth_tolerance));
    try_save(ar, ser20::make_nvp("brightness", obj.brightness));
    try_save(ar, ser20::make_nvp("facing_reflections_fading", obj.facing_reflections_fading));
    try_save(ar, ser20::make_nvp("roughness_depth_tolerance", obj.roughness_depth_tolerance));
    try_save(ar, ser20::make_nvp("screen_edge_fade", obj.screen_edge_fade));
    try_save(ar, ser20::make_nvp("resolution", obj.resolution));
    try_save(ar, ser20::make_nvp("enable_cone_tracing", obj.enable_cone_tracing));
    try_save(ar, ser20::make_nvp("cone_tracing", obj.cone_tracing));
    try_save(ar, ser20::make_nvp("enable_temporal_accumulation", obj.enable_temporal_accumulation));
    try_save(ar, ser20::make_nvp("temporal", obj.temporal));
    try_save(ar, ser20::make_nvp("enable_spatial_denoise", obj.enable_spatial_denoise));
    try_save(ar, ser20::make_nvp("spatial_denoise", obj.spatial_denoise));
}
SAVE_INSTANTIATE(ssr_pass::fidelityfx_ssr_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(ssr_pass::fidelityfx_ssr_settings, ser20::oarchive_binary_t);

LOAD_INLINE(ssr_pass::fidelityfx_ssr_settings)
{
    try_load(ar, ser20::make_nvp("max_steps", obj.max_steps));
    try_load(ar, ser20::make_nvp("max_rays", obj.max_rays));
    try_load(ar, ser20::make_nvp("depth_tolerance", obj.depth_tolerance));
    try_load(ar, ser20::make_nvp("brightness", obj.brightness));
    try_load(ar, ser20::make_nvp("facing_reflections_fading", obj.facing_reflections_fading));
    try_load(ar, ser20::make_nvp("roughness_depth_tolerance", obj.roughness_depth_tolerance));
    try_load(ar, ser20::make_nvp("screen_edge_fade", obj.screen_edge_fade));
    try_load(ar, ser20::make_nvp("resolution", obj.resolution));
    try_load(ar, ser20::make_nvp("enable_cone_tracing", obj.enable_cone_tracing));
    try_load(ar, ser20::make_nvp("cone_tracing", obj.cone_tracing));
    try_load(ar, ser20::make_nvp("enable_temporal_accumulation", obj.enable_temporal_accumulation));
    try_load(ar, ser20::make_nvp("temporal", obj.temporal));
    try_load(ar, ser20::make_nvp("enable_spatial_denoise", obj.enable_spatial_denoise));
    try_load(ar, ser20::make_nvp("spatial_denoise", obj.spatial_denoise));
}
LOAD_INSTANTIATE(ssr_pass::fidelityfx_ssr_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(ssr_pass::fidelityfx_ssr_settings, ser20::iarchive_binary_t);

// Serialization for ssr_settings
SAVE_INLINE(ssr_pass::ssr_settings)
{
    try_save(ar, ser20::make_nvp("fidelityfx", obj.fidelityfx));
}
SAVE_INSTANTIATE(ssr_pass::ssr_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(ssr_pass::ssr_settings, ser20::oarchive_binary_t);

LOAD_INLINE(ssr_pass::ssr_settings)
{
    try_load(ar, ser20::make_nvp("fidelityfx", obj.fidelityfx));
}
LOAD_INSTANTIATE(ssr_pass::ssr_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(ssr_pass::ssr_settings, ser20::iarchive_binary_t);

REFLECT(ssr_component)
{

    entt::meta_factory<ssr_component>{}
        .type("ssr_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ssr_component"},
            entt::attribute{"category", "RENDERING"},
            entt::attribute{"pretty_name", "SSR"},
        })
        .func<&component_meta<ssr_component>::exists>("component_exists"_hs)
        .func<&component_meta<ssr_component>::add>("component_add"_hs)
        .func<&component_meta<ssr_component>::remove>("component_remove"_hs)
        .func<&component_meta<ssr_component>::save>("component_save"_hs)
        .func<&component_meta<ssr_component>::load>("component_load"_hs)
        .data<&ssr_component::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Enable/disable Screen Space Reflections"},
        })
        .data<&ssr_component::settings>("settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "settings"},
            entt::attribute{"pretty_name", "Settings"},
            entt::attribute{"flattable", true},
        });
}

SAVE(ssr_component)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("settings", obj.settings));
}
SAVE_INSTANTIATE(ssr_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(ssr_component, ser20::oarchive_binary_t);

LOAD(ssr_component)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("settings", obj.settings));
}
LOAD_INSTANTIATE(ssr_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(ssr_component, ser20::iarchive_binary_t);
} // namespace unravel 
