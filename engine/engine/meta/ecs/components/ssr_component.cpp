#include "ssr_component.hpp"
#include <engine/meta/core/math/vector.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT_INLINE(ssr_pass::fidelityfx_ssr_settings)
{
    using fidelityfx_settings = ssr_pass::fidelityfx_ssr_settings;
    using cone_tracing_settings = ssr_pass::fidelityfx_ssr_settings::cone_tracing_settings;
    using temporal_settings = ssr_pass::fidelityfx_ssr_settings::temporal_settings;

    // Predicate for cone tracing settings visibility
    auto cone_tracing_predicate = rttr::property_predicate([](rttr::instance& obj)
    {
        auto data = obj.try_convert<fidelityfx_settings>();
        return data->enable_cone_tracing;
    });

    // Predicate for temporal settings visibility
    auto temporal_predicate = rttr::property_predicate([](rttr::instance& obj) -> bool
    {
        if(auto data = obj.try_convert<fidelityfx_settings>())
        {
            return data->enable_temporal_accumulation;
        }
        return false;
    });

    // First register the nested cone_tracing_settings struct
    rttr::registration::class_<cone_tracing_settings>("ssr_pass::fidelityfx_ssr_settings::cone_tracing_settings")
        .constructor<>()(rttr::metadata("pretty_name", "Cone Tracing Settings"))
        .property("cone_angle_bias", &cone_tracing_settings::cone_angle_bias)(
            rttr::metadata("pretty_name", "Cone Angle Bias"),
            rttr::metadata("min", 0.001f),
            rttr::metadata("max", 0.1f),
            rttr::metadata("tooltip", "Controls cone growth rate for glossy reflections"))
        .property("max_mip_level", &cone_tracing_settings::max_mip_level)(
            rttr::metadata("pretty_name", "Max Mip Level"),
            rttr::metadata("min", 1),
            rttr::metadata("max", 10),
            rttr::metadata("tooltip", "Number of blur mip levels - 1"))
        .property("blur_base_sigma", &cone_tracing_settings::blur_base_sigma)(
            rttr::metadata("pretty_name", "Blur Base Sigma"),
            rttr::metadata("min", 0.1f),
            rttr::metadata("max", 5.0f),
            rttr::metadata("tooltip", "Base blur sigma for mip generation (CPU-side only)"));

    // Register cone_tracing_settings with entt
    entt::meta_factory<cone_tracing_settings>{}
        .type("ssr_pass::fidelityfx_ssr_settings::cone_tracing_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Cone Tracing Settings"},
        })
        .data<&cone_tracing_settings::cone_angle_bias>("cone_angle_bias"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Cone Angle Bias"},
            entt::attribute{"min", 0.001f},
            entt::attribute{"max", 0.1f},
            entt::attribute{"tooltip", "Controls cone growth rate for glossy reflections"},
        })
        .data<&cone_tracing_settings::max_mip_level>("max_mip_level"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Max Mip Level"},
            entt::attribute{"min", 1},
            entt::attribute{"max", 10},
            entt::attribute{"tooltip", "Number of blur mip levels - 1"},
        })
        .data<&cone_tracing_settings::blur_base_sigma>("blur_base_sigma"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Blur Base Sigma"},
            entt::attribute{"min", 0.1f},
            entt::attribute{"max", 5.0f},
            entt::attribute{"tooltip", "Base blur sigma for mip generation (CPU-side only)"},
        });

    // -------------------------------------------------------------------------
    //  Temporal Accumulation Settings  (matches ApplyTemporalAccumulation v2)
    // -------------------------------------------------------------------------
    rttr::registration::class_<temporal_settings>(
        "ssr_pass::fidelityfx_ssr_settings::temporal_settings")
    .constructor<>()(
        rttr::metadata("pretty_name", "Temporal Accumulation Settings"))
    .property("history_strength", &temporal_settings::history_strength)(
        rttr::metadata("pretty_name", "History Strength"),
        rttr::metadata("min", 0.0f),          // 0 → no accumulation
        rttr::metadata("max", 1.0f),          // 1 → very long memory
        rttr::metadata("tooltip",
                    "Controls how long reflections keep history.\n"
                    "0 = real-time only   ·   1 = maximum denoise"))
    .property("depth_threshold", &temporal_settings::depth_threshold)(
        rttr::metadata("pretty_name", "Edge Threshold"),
        rttr::metadata("min", 0.000f),        // clip-space: 0 … 0.03
        rttr::metadata("max", 0.030f),
        rttr::metadata("tooltip",
                    "Depth difference allowed before history is discarded.\n"
                    "Lower = crisper edges, higher = smoother but risk of bleed"))
    .property("roughness_sensitivity",
            &temporal_settings::roughness_sensitivity)(
        rttr::metadata("pretty_name", "Material Sensitivity"),
        rttr::metadata("min", 0.0f),          // 0 = ignore roughness
        rttr::metadata("max", 1.0f),          // 1 = full influence
        rttr::metadata("tooltip",
                    "How strongly rough surfaces shorten history.\n"
                    "0 = same for every material   ·   1 = glossy keeps more history"))
    .property("motion_scale_pixels", &temporal_settings::motion_scale_pixels)(
        rttr::metadata("pretty_name", "Motion Scale Pixels"),
        rttr::metadata("min", 0.0f),
        rttr::metadata("max", 1000.0f),
        rttr::metadata("tooltip", "Motion scale in pixels"))
    .property("normal_dot_threshold", &temporal_settings::normal_dot_threshold)(
        rttr::metadata("pretty_name", "Normal Dot Threshold"),
        rttr::metadata("min", 0.0f),
        rttr::metadata("max", 1.0f),
        rttr::metadata("tooltip", "Normal dot threshold for motion detection"))
    .property("max_accum_frames", &temporal_settings::max_accum_frames)(
        rttr::metadata("pretty_name", "Max Accum Frames"),
        rttr::metadata("min", 1),
        rttr::metadata("max", 16),
        rttr::metadata("tooltip", "Maximum accumulation frames"));

    // Register temporal_settings with entt
    entt::meta_factory<temporal_settings>{}
        .type("ssr_pass::fidelityfx_ssr_settings::temporal_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Temporal Accumulation Settings"},
        })
        .data<&temporal_settings::history_strength>("history_strength"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "History Strength"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "Controls how long reflections keep history.\n0 = real-time only   ·   1 = maximum denoise"},
        })
        .data<&temporal_settings::depth_threshold>("depth_threshold"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Edge Threshold"},
            entt::attribute{"min", 0.000f},
            entt::attribute{"max", 0.030f},
            entt::attribute{"tooltip", "Depth difference allowed before history is discarded.\nLower = crisper edges, higher = smoother but risk of bleed"},
        })
        .data<&temporal_settings::roughness_sensitivity>("roughness_sensitivity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Material Sensitivity"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "How strongly rough surfaces shorten history.\n0 = same for every material   ·   1 = glossy keeps more history"},
        })
        .data<&temporal_settings::motion_scale_pixels>("motion_scale_pixels"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Motion Scale Pixels"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1000.0f},
            entt::attribute{"tooltip", "Motion scale in pixels"},
        })
        .data<&temporal_settings::normal_dot_threshold>("normal_dot_threshold"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Normal Dot Threshold"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "Normal dot threshold for motion detection"},
        })
        .data<&temporal_settings::max_accum_frames>("max_accum_frames"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Max Accum Frames"},
            entt::attribute{"min", 1},
            entt::attribute{"max", 16},
            entt::attribute{"tooltip", "Maximum accumulation frames"},
        });

    rttr::registration::class_<fidelityfx_settings>("ssr_pass::fidelityfx_ssr_settings")
        .constructor<>()(rttr::metadata("pretty_name", "FidelityFX SSR Settings"))
        .property("max_steps", &fidelityfx_settings::max_steps)(
            rttr::metadata("pretty_name", "Max Steps"),
            rttr::metadata("min", 8),
            rttr::metadata("max", 200),
            rttr::metadata("tooltip", "Maximum ray marching steps for hierarchical traversal"))
        .property("max_rays", &fidelityfx_settings::max_rays)(
            rttr::metadata("pretty_name", "Max Rays"),
            rttr::metadata("min", 1),
            rttr::metadata("max", 64),
            rttr::metadata("tooltip", "Maximum rays for rough surfaces (future: cone tracing)"))
        .property("depth_tolerance", &fidelityfx_settings::depth_tolerance)(
            rttr::metadata("pretty_name", "Depth Tolerance"),
            rttr::metadata("min", 0.01f),
            rttr::metadata("max", 2.0f),
            rttr::metadata("tooltip", "Depth tolerance for hit validation"))
        .property("brightness", &fidelityfx_settings::brightness)(
            rttr::metadata("pretty_name", "Brightness"),
            rttr::metadata("min", 0.1f),
            rttr::metadata("max", 3.0f),
            rttr::metadata("tooltip", "Reflection brightness multiplier"))
        .property("facing_reflections_fading", &fidelityfx_settings::facing_reflections_fading)(
            rttr::metadata("pretty_name", "Facing Reflections Fading"),
            rttr::metadata("min", 0.0f),
            rttr::metadata("max", 1.0f),
            rttr::metadata("tooltip", "Fade factor for camera-facing reflections"))
        .property("roughness_depth_tolerance", &fidelityfx_settings::roughness_depth_tolerance)(
            rttr::metadata("pretty_name", "Roughness Depth Tolerance"),
            rttr::metadata("min", 0.0f),
            rttr::metadata("max", 2.0f),
            rttr::metadata("tooltip", "Additional depth tolerance for rough surfaces"))
        .property("fade_in_start", &fidelityfx_settings::fade_in_start)(
            rttr::metadata("pretty_name", "Fade In Start"),
            rttr::metadata("min", 0.0f),
            rttr::metadata("max", 1.0f),
            rttr::metadata("tooltip", "Screen edge fade start"))
        .property("fade_in_end", &fidelityfx_settings::fade_in_end)(
            rttr::metadata("pretty_name", "Fade In End"),
            rttr::metadata("min", 0.0f),
            rttr::metadata("max", 1.0f),
            rttr::metadata("tooltip", "Screen edge fade end"))
        .property("enable_half_res", &fidelityfx_settings::enable_half_res)(
            rttr::metadata("pretty_name", "Enable Half Res"),
            rttr::metadata("tooltip", "Enable half resolution for SSR buffers"))
        .property("enable_cone_tracing", &fidelityfx_settings::enable_cone_tracing)(
            rttr::metadata("pretty_name", "Enable Cone Tracing"),
            rttr::metadata("tooltip", "Enable cone tracing for glossy reflections"))
        .property("cone_tracing", &fidelityfx_settings::cone_tracing)(
            rttr::metadata("predicate", cone_tracing_predicate),
            rttr::metadata("pretty_name", "Cone Tracing"),
            rttr::metadata("tooltip", "Cone tracing specific settings"),
            rttr::metadata("flattable", true))
        .property("enable_temporal_accumulation", &fidelityfx_settings::enable_temporal_accumulation)(
            rttr::metadata("pretty_name", "Enable Temporal Accumulation"),
            rttr::metadata("tooltip", "Enable temporal accumulation to reduce noise over multiple frames"))
        .property("temporal", &fidelityfx_settings::temporal)(
            rttr::metadata("predicate", temporal_predicate),
            rttr::metadata("pretty_name", "Temporal Accumulation"),
            rttr::metadata("tooltip", "Temporal accumulation settings"),
            rttr::metadata("flattable", true));

    // Register fidelityfx_ssr_settings with entt
    entt::meta_factory<fidelityfx_settings>{}
        .type("ssr_pass::fidelityfx_ssr_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "FidelityFX SSR Settings"},
        })
        .data<&fidelityfx_settings::max_steps>("max_steps"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Max Steps"},
            entt::attribute{"min", 8},
            entt::attribute{"max", 200},
            entt::attribute{"tooltip", "Maximum ray marching steps for hierarchical traversal"},
        })
        .data<&fidelityfx_settings::max_rays>("max_rays"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Max Rays"},
            entt::attribute{"min", 1},
            entt::attribute{"max", 64},
            entt::attribute{"tooltip", "Maximum rays for rough surfaces (future: cone tracing)"},
        })
        .data<&fidelityfx_settings::depth_tolerance>("depth_tolerance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Depth Tolerance"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"max", 2.0f},
            entt::attribute{"tooltip", "Depth tolerance for hit validation"},
        })
        .data<&fidelityfx_settings::brightness>("brightness"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Brightness"},
            entt::attribute{"min", 0.1f},
            entt::attribute{"max", 3.0f},
            entt::attribute{"tooltip", "Reflection brightness multiplier"},
        })
        .data<&fidelityfx_settings::facing_reflections_fading>("facing_reflections_fading"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Facing Reflections Fading"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "Fade factor for camera-facing reflections"},
        })
        .data<&fidelityfx_settings::roughness_depth_tolerance>("roughness_depth_tolerance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Roughness Depth Tolerance"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 2.0f},
            entt::attribute{"tooltip", "Additional depth tolerance for rough surfaces"},
        })
        .data<&fidelityfx_settings::fade_in_start>("fade_in_start"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Fade In Start"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "Screen edge fade start"},
        })
        .data<&fidelityfx_settings::fade_in_end>("fade_in_end"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Fade In End"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "Screen edge fade end"},
        })
        .data<&fidelityfx_settings::enable_half_res>("enable_half_res"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Enable Half Res"},
            entt::attribute{"tooltip", "Enable half resolution for SSR buffers"},
        })
        .data<&fidelityfx_settings::enable_cone_tracing>("enable_cone_tracing"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Enable Cone Tracing"},
            entt::attribute{"tooltip", "Enable cone tracing for glossy reflections"},
        })
        .data<&fidelityfx_settings::cone_tracing>("cone_tracing"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"predicate", cone_tracing_predicate},
            entt::attribute{"pretty_name", "Cone Tracing"},
            entt::attribute{"tooltip", "Cone tracing specific settings"},
            entt::attribute{"flattable", true},
        })
        .data<&fidelityfx_settings::enable_temporal_accumulation>("enable_temporal_accumulation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Enable Temporal Accumulation"},
            entt::attribute{"tooltip", "Enable temporal accumulation to reduce noise over multiple frames"},
        })
        .data<&fidelityfx_settings::temporal>("temporal"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"predicate", temporal_predicate},
            entt::attribute{"pretty_name", "Temporal Accumulation"},
            entt::attribute{"tooltip", "Temporal accumulation settings"},
            entt::attribute{"flattable", true},
        });

}

REFLECT_INLINE(ssr_pass::ssr_settings)
{
    using ssr_settings = ssr_pass::ssr_settings;

    rttr::registration::class_<ssr_settings>("ssr_pass::ssr_settings")
        .constructor<>()(rttr::metadata("pretty_name", "SSR Settings"))
        .property("fidelityfx", &ssr_settings::fidelityfx)(
            rttr::metadata("pretty_name", "FidelityFX Settings"),
            rttr::metadata("tooltip", "Settings for AMD FidelityFX SSSR implementation"),
            rttr::metadata("flattable", true));

    // Register ssr_settings with entt
    entt::meta_factory<ssr_settings>{}
        .type("ssr_pass::ssr_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "SSR Settings"},
        })
        .data<&ssr_settings::fidelityfx>("fidelityfx"_hs)
        .custom<entt::attributes>(entt::attributes{
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

// Serialization for fidelityfx_ssr_settings
SAVE_INLINE(ssr_pass::fidelityfx_ssr_settings)
{
    try_save(ar, ser20::make_nvp("max_steps", obj.max_steps));
    try_save(ar, ser20::make_nvp("max_rays", obj.max_rays));
    try_save(ar, ser20::make_nvp("depth_tolerance", obj.depth_tolerance));
    try_save(ar, ser20::make_nvp("brightness", obj.brightness));
    try_save(ar, ser20::make_nvp("facing_reflections_fading", obj.facing_reflections_fading));
    try_save(ar, ser20::make_nvp("roughness_depth_tolerance", obj.roughness_depth_tolerance));
    try_save(ar, ser20::make_nvp("fade_in_start", obj.fade_in_start));
    try_save(ar, ser20::make_nvp("fade_in_end", obj.fade_in_end));
    try_save(ar, ser20::make_nvp("enable_half_res", obj.enable_half_res));
    try_save(ar, ser20::make_nvp("enable_cone_tracing", obj.enable_cone_tracing));
    try_save(ar, ser20::make_nvp("cone_tracing", obj.cone_tracing));
    try_save(ar, ser20::make_nvp("enable_temporal_accumulation", obj.enable_temporal_accumulation));
    try_save(ar, ser20::make_nvp("temporal", obj.temporal));
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
    try_load(ar, ser20::make_nvp("fade_in_start", obj.fade_in_start));
    try_load(ar, ser20::make_nvp("fade_in_end", obj.fade_in_end));
    try_load(ar, ser20::make_nvp("enable_half_res", obj.enable_half_res));
    try_load(ar, ser20::make_nvp("enable_cone_tracing", obj.enable_cone_tracing));
    try_load(ar, ser20::make_nvp("cone_tracing", obj.cone_tracing));
    try_load(ar, ser20::make_nvp("enable_temporal_accumulation", obj.enable_temporal_accumulation));
    try_load(ar, ser20::make_nvp("temporal", obj.temporal));
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
    rttr::registration::class_<ssr_component>("ssr_component")(
        rttr::metadata("category", "RENDERING"),
        rttr::metadata("pretty_name", "SSR"))
        .constructor<>()
        .method("component_exists", &component_exists<ssr_component>)

        .property("enabled", &ssr_component::enabled)(
            rttr::metadata("pretty_name", "Enabled"),
            rttr::metadata("tooltip", "Enable/disable Screen Space Reflections"))
        .property("settings", &ssr_component::settings)(
            rttr::metadata("pretty_name", "Settings"),
            rttr::metadata("flattable", true));

    // Register ssr_component with entt
    entt::meta_factory<ssr_component>{}
        .type("ssr_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"category", "RENDERING"},
            entt::attribute{"pretty_name", "SSR"},
        })
        .func<&component_exists<ssr_component>>("component_exists"_hs)
        .data<&ssr_component::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Enable/disable Screen Space Reflections"},
        })
        .data<&ssr_component::settings>("settings"_hs)
        .custom<entt::attributes>(entt::attributes{
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
