#include "ssil_component.hpp"
#include <engine/meta/core/math/vector.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT_INLINE(ssil_pass::spatial_denoise_settings)
{
    using spatial_denoise_settings = ssil_pass::spatial_denoise_settings;

    entt::meta_factory<spatial_denoise_settings>{}
        .type("ssil_pass::spatial_denoise_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ssil_pass::spatial_denoise_settings"},
            entt::attribute{"pretty_name", "Spatial Denoise Settings"},
        })
        .data<&spatial_denoise_settings::depth_sigma>("depth_sigma"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "depth_sigma"},
            entt::attribute{"pretty_name", "Depth Sigma"},
            entt::attribute{"min", 0.005f},
            entt::attribute{"max", 0.05f},
            entt::attribute{"tooltip", "Depth edge-stopping threshold. Lower = stricter edges"},
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
}

REFLECT_INLINE(ssil_pass::temporal_settings)
{
    using temporal_settings = ssil_pass::temporal_settings;

    entt::meta_factory<temporal_settings>{}
        .type("ssil_pass::temporal_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ssil_pass::temporal_settings"},
            entt::attribute{"pretty_name", "Temporal Accumulation Settings"},
        })
        .data<&temporal_settings::history_strength>("history_strength"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "history_strength"},
            entt::attribute{"pretty_name", "History Strength"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "Controls how long indirect lighting keeps history.\n0 = real-time only, 1 = maximum denoise"},
        })
        .data<&temporal_settings::depth_threshold>("depth_threshold"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "depth_threshold"},
            entt::attribute{"pretty_name", "Edge Threshold"},
            entt::attribute{"min", 0.000f},
            entt::attribute{"max", 0.030f},
            entt::attribute{"tooltip", "Depth difference allowed before history is discarded"},
        })
        .data<&temporal_settings::max_accum_frames>("max_accum_frames"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_accum_frames"},
            entt::attribute{"pretty_name", "Max Accum Frames"},
            entt::attribute{"min", 1},
            entt::attribute{"max", 16},
            entt::attribute{"tooltip", "Maximum temporal accumulation frames"},
        });
}

REFLECT_INLINE(ssil_pass::ssil_settings)
{
    using ssil_settings = ssil_pass::ssil_settings;

    auto multi_bounce_predicate_entt = entt::property_predicate<bool>([](const entt::meta_any& obj)
    {
        if(auto data = obj.try_cast<ssil_settings>())
        {
            return data->enable_multi_bounce;
        }
        return false;
    });

    auto temporal_predicate_entt = entt::property_predicate<bool>([](const entt::meta_any& obj)
    {
        if(auto data = obj.try_cast<ssil_settings>())
        {
            return data->enable_temporal_accumulation;
        }
        return false;
    });

    auto spatial_denoise_predicate_entt = entt::property_predicate<bool>([](const entt::meta_any& obj)
    {
        if(auto data = obj.try_cast<ssil_settings>())
        {
            return data->enable_spatial_denoise;
        }
        return false;
    });

    entt::meta_factory<ssil_settings>{}
        .type("ssil_pass::ssil_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ssil_pass::ssil_settings"},
            entt::attribute{"pretty_name", "SSIL Settings"},
        })
        .data<&ssil_settings::max_rays>("max_rays"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_rays"},
            entt::attribute{"pretty_name", "Max Rays"},
            entt::attribute{"min", 1},
            entt::attribute{"max", 16},
            entt::attribute{"tooltip", "Number of hemisphere rays per pixel. More = better quality, higher cost"},
        })
        .data<&ssil_settings::max_steps>("max_steps"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_steps"},
            entt::attribute{"pretty_name", "Max Steps"},
            entt::attribute{"min", 4},
            entt::attribute{"max", 256},
            entt::attribute{"tooltip", "Maximum Hi-Z ray marching steps per ray"},
        })
        .data<&ssil_settings::depth_tolerance>("depth_tolerance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "depth_tolerance"},
            entt::attribute{"pretty_name", "Depth Tolerance"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "Depth tolerance for hit validation"},
        })
        .data<&ssil_settings::brightness>("brightness"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "brightness"},
            entt::attribute{"pretty_name", "Brightness"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 5.0f},
            entt::attribute{"tooltip", "SSIL brightness multiplier"},
        })
        .data<&ssil_settings::max_distance>("max_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_distance"},
            entt::attribute{"pretty_name", "Max Distance"},
            entt::attribute{"min", 0.5f},
            entt::attribute{"max", 100.0f},
            entt::attribute{"tooltip", "Maximum ray distance in world units. SSIL captures local bounce; distant GI comes from probes"},
        })
        .data<&ssil_settings::resolution>("resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "resolution"},
            entt::attribute{"pretty_name", "Trace Resolution"},
            entt::attribute{"tooltip", "Downscale divisor for SSIL trace buffers.\nFull = reference quality, Half = ~4x faster, Quarter = ~16x faster (viable for indirect lighting because it is inherently low-frequency).\nThe indirect pass samples the upscaled result through the same full-res G-buffer UV."},
        })
        .data<&ssil_settings::enable_multi_bounce>("enable_multi_bounce"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enable_multi_bounce"},
            entt::attribute{"pretty_name", "Enable Multi-Bounce"},
            entt::attribute{"tooltip", "Feed previous frame's SSIL back into the trace for multi-bounce indirect lighting. Light propagates deeper into corridors and shadowed areas over several frames. Each bounce is attenuated by the hit surface's diffuse albedo for energy conservation."},
        })
        .data<&ssil_settings::multi_bounce_intensity>("multi_bounce_intensity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "multi_bounce_intensity"},
            entt::attribute{"pretty_name", "Multi-Bounce Intensity"},
            entt::attribute{"predicate", multi_bounce_predicate_entt},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "Controls how much indirect light is carried between bounces. Acts as an extra decay factor on top of surface albedo attenuation. 0 = single bounce only, 0.5 = balanced (default), 1.0 = full physical albedo-only attenuation."},
        })
        .data<&ssil_settings::enable_spatial_denoise>("enable_spatial_denoise"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enable_spatial_denoise"},
            entt::attribute{"pretty_name", "Enable Spatial Denoise"},
            entt::attribute{"tooltip", "Enable edge-preserving spatial denoising (a-trous wavelet)"},
        })
        .data<&ssil_settings::spatial_denoise>("spatial_denoise"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "spatial_denoise"},
            entt::attribute{"predicate", spatial_denoise_predicate_entt},
            entt::attribute{"pretty_name", "Spatial Denoise"},
            entt::attribute{"tooltip", "Spatial denoise settings"},
            // entt::attribute{"flattable", true},
        })
        .data<&ssil_settings::enable_temporal_accumulation>("enable_temporal_accumulation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enable_temporal_accumulation"},
            entt::attribute{"pretty_name", "Enable Temporal"},
            entt::attribute{"tooltip", "Enable temporal accumulation to reduce noise over multiple frames"},
        })
        .data<&ssil_settings::temporal>("temporal"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "temporal"},
            entt::attribute{"predicate", temporal_predicate_entt},
            entt::attribute{"pretty_name", "Temporal Accumulation"},
            entt::attribute{"tooltip", "Temporal accumulation settings"},
            // entt::attribute{"flattable", true},
        });
}

// --- Serialization: spatial_denoise_settings ---

SAVE_INLINE(ssil_pass::spatial_denoise_settings)
{
    try_save(ar, ser20::make_nvp("depth_sigma", obj.depth_sigma));
    try_save(ar, ser20::make_nvp("normal_power", obj.normal_power));
    try_save(ar, ser20::make_nvp("luma_sigma", obj.luma_sigma));
    try_save(ar, ser20::make_nvp("passes", obj.passes));
}
SAVE_INSTANTIATE(ssil_pass::spatial_denoise_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(ssil_pass::spatial_denoise_settings, ser20::oarchive_binary_t);

LOAD_INLINE(ssil_pass::spatial_denoise_settings)
{
    try_load(ar, ser20::make_nvp("depth_sigma", obj.depth_sigma));
    try_load(ar, ser20::make_nvp("normal_power", obj.normal_power));
    try_load(ar, ser20::make_nvp("luma_sigma", obj.luma_sigma));
    try_load(ar, ser20::make_nvp("passes", obj.passes));
}
LOAD_INSTANTIATE(ssil_pass::spatial_denoise_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(ssil_pass::spatial_denoise_settings, ser20::iarchive_binary_t);

// --- Serialization: temporal_settings ---

SAVE_INLINE(ssil_pass::temporal_settings)
{
    try_save(ar, ser20::make_nvp("history_strength", obj.history_strength));
    try_save(ar, ser20::make_nvp("depth_threshold", obj.depth_threshold));
    try_save(ar, ser20::make_nvp("max_accum_frames", obj.max_accum_frames));
}
SAVE_INSTANTIATE(ssil_pass::temporal_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(ssil_pass::temporal_settings, ser20::oarchive_binary_t);

LOAD_INLINE(ssil_pass::temporal_settings)
{
    try_load(ar, ser20::make_nvp("history_strength", obj.history_strength));
    try_load(ar, ser20::make_nvp("depth_threshold", obj.depth_threshold));
    try_load(ar, ser20::make_nvp("max_accum_frames", obj.max_accum_frames));
}
LOAD_INSTANTIATE(ssil_pass::temporal_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(ssil_pass::temporal_settings, ser20::iarchive_binary_t);

// --- Serialization: ssil_settings ---

SAVE_INLINE(ssil_pass::ssil_settings)
{
    try_save(ar, ser20::make_nvp("max_rays", obj.max_rays));
    try_save(ar, ser20::make_nvp("max_steps", obj.max_steps));
    try_save(ar, ser20::make_nvp("depth_tolerance", obj.depth_tolerance));
    try_save(ar, ser20::make_nvp("brightness", obj.brightness));
    try_save(ar, ser20::make_nvp("max_distance", obj.max_distance));
    try_save(ar, ser20::make_nvp("resolution", obj.resolution));
    try_save(ar, ser20::make_nvp("enable_multi_bounce", obj.enable_multi_bounce));
    try_save(ar, ser20::make_nvp("multi_bounce_intensity", obj.multi_bounce_intensity));
    try_save(ar, ser20::make_nvp("enable_spatial_denoise", obj.enable_spatial_denoise));
    try_save(ar, ser20::make_nvp("spatial_denoise", obj.spatial_denoise));
    try_save(ar, ser20::make_nvp("enable_temporal_accumulation", obj.enable_temporal_accumulation));
    try_save(ar, ser20::make_nvp("temporal", obj.temporal));
}
SAVE_INSTANTIATE(ssil_pass::ssil_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(ssil_pass::ssil_settings, ser20::oarchive_binary_t);

LOAD_INLINE(ssil_pass::ssil_settings)
{
    try_load(ar, ser20::make_nvp("max_rays", obj.max_rays));
    try_load(ar, ser20::make_nvp("max_steps", obj.max_steps));
    try_load(ar, ser20::make_nvp("depth_tolerance", obj.depth_tolerance));
    try_load(ar, ser20::make_nvp("brightness", obj.brightness));
    try_load(ar, ser20::make_nvp("max_distance", obj.max_distance));
    // Backwards compat: legacy scenes store the old boolean under `enable_half_res`.
    // Try the new enum field first; fall back to the bool when missing.
    if(!try_load(ar, ser20::make_nvp("resolution", obj.resolution)))
    {
        bool legacy_half_res = false;
        if(try_load(ar, ser20::make_nvp("enable_half_res", legacy_half_res)))
        {
            obj.resolution = legacy_half_res ? trace_resolution::half : trace_resolution::full;
        }
    }
    try_load(ar, ser20::make_nvp("enable_multi_bounce", obj.enable_multi_bounce));
    try_load(ar, ser20::make_nvp("multi_bounce_intensity", obj.multi_bounce_intensity));
    try_load(ar, ser20::make_nvp("enable_spatial_denoise", obj.enable_spatial_denoise));
    try_load(ar, ser20::make_nvp("spatial_denoise", obj.spatial_denoise));
    try_load(ar, ser20::make_nvp("enable_temporal_accumulation", obj.enable_temporal_accumulation));
    try_load(ar, ser20::make_nvp("temporal", obj.temporal));
}
LOAD_INSTANTIATE(ssil_pass::ssil_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(ssil_pass::ssil_settings, ser20::iarchive_binary_t);

// --- Reflection + Serialization: ssil_component ---

REFLECT(ssil_component)
{
    entt::meta_factory<ssil_component>{}
        .type("ssil_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ssil_component"},
            entt::attribute{"category", "RENDERING"},
            entt::attribute{"pretty_name", "SSIL"},
        })
        .func<&component_meta<ssil_component>::exists>("component_exists"_hs)
        .func<&component_meta<ssil_component>::add>("component_add"_hs)
        .func<&component_meta<ssil_component>::remove>("component_remove"_hs)
        .func<&component_meta<ssil_component>::save>("component_save"_hs)
        .func<&component_meta<ssil_component>::load>("component_load"_hs)
        .data<&ssil_component::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Enable/disable Screen Space Indirect Lighting"},
        })
        .data<&ssil_component::settings>("settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "settings"},
            entt::attribute{"pretty_name", "Settings"},
            entt::attribute{"flattable", true},
        });
}

SAVE(ssil_component)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("settings", obj.settings));
}
SAVE_INSTANTIATE(ssil_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(ssil_component, ser20::oarchive_binary_t);

LOAD(ssil_component)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("settings", obj.settings));
}
LOAD_INSTANTIATE(ssil_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(ssil_component, ser20::iarchive_binary_t);

} // namespace unravel
