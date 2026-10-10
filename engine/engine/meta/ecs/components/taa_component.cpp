#include "taa_component.hpp"
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/pipeline/passes/taa_pass.h>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT_INLINE(taa_pass::settings)
{
    entt::meta_factory<taa_pass::settings>{}
        .type("taa_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "taa_settings"},
            entt::attribute{"pretty_name", "Temporal AA Settings"},
        })
        .data<&taa_pass::settings::temporal_sample_count>("temporal_sample_count"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "temporal_sample_count"},
            entt::attribute{"pretty_name", "Temporal Samples"},
            entt::attribute{"min", 2},
            entt::attribute{"max", 16},
            entt::attribute{"tooltip", "When >1, enables subpixel jitter. Cycle length of the Halton sequence (default 8)."},
        })
        .data<&taa_pass::settings::jitter_amplitude>("jitter_amplitude"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "jitter_amplitude"},
            entt::attribute{"pretty_name", "Jitter Amplitude"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.5f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "Scales subpixel camera jitter (1 = full +-0.5 px); lower covers less of the pixel and aliases more."},
        })
        .data<&taa_pass::settings::history_blend>("history_blend"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "history_blend"},
            entt::attribute{"pretty_name", "History blend"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Blend toward reprojected history when valid (higher = more stable, more blur)."},
        })
        .data<&taa_pass::settings::sharpen>("sharpen"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sharpen"},
            entt::attribute{"pretty_name", "Sharpen"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 2.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "Display-only unsharp against the previous history's neighborhood (0 = off)."},
        })
        .data<&taa_pass::settings::depth_reject_scale>("depth_reject_scale"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "depth_reject_scale"},
            entt::attribute{"pretty_name", "Depth Reject Scale"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"max", 4.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "Scales depth disocclusion rejection (higher = stricter)."},
        })
        .data<&taa_pass::settings::variance_clip_scale>("variance_clip_scale"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "variance_clip_scale"},
            entt::attribute{"pretty_name", "Variance Clip"},
            entt::attribute{"min", 0.5f},
            entt::attribute{"max", 2.5f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "RGB variance clip width in std-devs (wider = less ghosting, slightly softer)."},
        });
}

SAVE_INLINE(taa_pass::settings)
{
    std::uint32_t version = taa_pass::settings_version;
    try_save(ar, ser20::make_nvp("settings_version", version));
    try_save(ar, ser20::make_nvp("temporal_sample_count", obj.temporal_sample_count));
    try_save(ar, ser20::make_nvp("jitter_amplitude", obj.jitter_amplitude));
    try_save(ar, ser20::make_nvp("history_blend", obj.history_blend));
    try_save(ar, ser20::make_nvp("sharpen", obj.sharpen));
    try_save(ar, ser20::make_nvp("depth_reject_scale", obj.depth_reject_scale));
    try_save(ar, ser20::make_nvp("variance_clip_scale", obj.variance_clip_scale));
}
SAVE_INSTANTIATE(taa_pass::settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(taa_pass::settings, ser20::oarchive_binary_t);

LOAD_INLINE(taa_pass::settings)
{
    // Settings saved by an earlier resolve (no version, or an older one) were tuned for different behavior; they
    // are ignored so the scene picks up the current defaults.
    std::uint32_t version = 0;
    try_load(ar, ser20::make_nvp("settings_version", version));
    if(version != taa_pass::settings_version)
    {
        return;
    }
    try_load(ar, ser20::make_nvp("temporal_sample_count", obj.temporal_sample_count));
    try_load(ar, ser20::make_nvp("jitter_amplitude", obj.jitter_amplitude));
    try_load(ar, ser20::make_nvp("history_blend", obj.history_blend));
    try_load(ar, ser20::make_nvp("sharpen", obj.sharpen));
    try_load(ar, ser20::make_nvp("depth_reject_scale", obj.depth_reject_scale));
    try_load(ar, ser20::make_nvp("variance_clip_scale", obj.variance_clip_scale));
}
LOAD_INSTANTIATE(taa_pass::settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(taa_pass::settings, ser20::iarchive_binary_t);

REFLECT(taa_component)
{
    entt::meta_factory<taa_component>{}
        .type("taa_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "taa_component"},
            entt::attribute{"category", "Rendering/Post Processing"},
            entt::attribute{"pretty_name", "Temporal AA"},
        })
        .func<&component_meta<taa_component>::exists>("component_exists"_hs)
        .func<&component_meta<taa_component>::add>("component_add"_hs)
        .func<&component_meta<taa_component>::save>("component_save"_hs)
        .func<&component_meta<taa_component>::load>("component_load"_hs)
        .func<&component_meta<taa_component>::remove>("component_remove"_hs)
        .data<&taa_component::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "HDR temporal anti-aliasing (before tonemap); disables FXAA when on"},
        })
        .data<&taa_component::settings>("settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "settings"},
            entt::attribute{"pretty_name", "Settings"},
            entt::attribute{"flattable", true},
        });
}

SAVE(taa_component)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("settings", obj.settings));
}
SAVE_INSTANTIATE(taa_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(taa_component, ser20::oarchive_binary_t);

LOAD(taa_component)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("settings", obj.settings));
}
LOAD_INSTANTIATE(taa_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(taa_component, ser20::iarchive_binary_t);

} // namespace unravel
