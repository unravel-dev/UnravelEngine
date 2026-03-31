#include "taa_component.hpp"
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/pipeline/passes/taa_pass.h>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT_INLINE(taa_jitter_mode)
{
    entt::meta_factory<taa_jitter_mode>{}
        .type("taa_jitter_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "taa_jitter_mode"},
            entt::attribute{"pretty_name", "TAA Jitter Mode"},
        })
        .data<taa_jitter_mode::progressive_golden>("progressive_golden"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "progressive_golden"},
            entt::attribute{"pretty_name", "Progressive Golden"},
        })
        .data<taa_jitter_mode::halton_2_3>("halton_2_3"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "halton_2_3"},
            entt::attribute{"pretty_name", "Halton 2,3"},
        })
        .data<taa_jitter_mode::r2_low_discrepancy>("r2_low_discrepancy"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "r2_low_discrepancy"},
            entt::attribute{"pretty_name", "R2 Low Discrepancy"},
        })
        .data<taa_jitter_mode::msaa_2_rotating>("msaa_2_rotating"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "msaa_2_rotating"},
            entt::attribute{"pretty_name", "MSAA 2 Rotating"},
        })
        .data<taa_jitter_mode::msaa_3_rotating>("msaa_3_rotating"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "msaa_3_rotating"},
            entt::attribute{"pretty_name", "MSAA 3 Rotating"},
        })
        .data<taa_jitter_mode::msaa_4_rotating>("msaa_4_rotating"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "msaa_4_rotating"},
            entt::attribute{"pretty_name", "MSAA 4 Rotating"},
        });
}

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
            entt::attribute{"tooltip", "When >1, enables subpixel jitter (full render frame index). Stored count is for UI / future tuning."},
        })
        .data<&taa_pass::settings::jitter_mode>("jitter_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "jitter_mode"},
            entt::attribute{"pretty_name", "Jitter Sequence"},
            entt::attribute{"tooltip", "Which subpixel pattern the camera uses for TAA each frame."},
        })
        .data<&taa_pass::settings::jitter_amplitude>("jitter_amplitude"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "jitter_amplitude"},
            entt::attribute{"pretty_name", "Jitter Amplitude"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.5f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "Scales subpixel camera jitter; lower reduces whole-screen shake (1 = strongest)."},
        })
        .data<&taa_pass::settings::jitter_temporal_phase_scale>("jitter_temporal_phase_scale"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "jitter_temporal_phase_scale"},
            entt::attribute{"pretty_name", "Jitter Temporal Phase Scale"},
            entt::attribute{"min", 0.03f},
            entt::attribute{"max", 1.5f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "Golden/Halton/R2 only: lower = smaller per-frame jitter steps, easier history tracking (1 = legacy speed)."},
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
            entt::attribute{"tooltip", "Unsharp response vs current neighborhood (0 = off)."},
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
    try_save(ar, ser20::make_nvp("temporal_sample_count", obj.temporal_sample_count));
    try_save(ar, ser20::make_nvp("jitter_mode", obj.jitter_mode));
    try_save(ar, ser20::make_nvp("jitter_amplitude", obj.jitter_amplitude));
    try_save(ar, ser20::make_nvp("jitter_temporal_phase_scale", obj.jitter_temporal_phase_scale));
    try_save(ar, ser20::make_nvp("history_blend", obj.history_blend));
    try_save(ar, ser20::make_nvp("sharpen", obj.sharpen));
    try_save(ar, ser20::make_nvp("depth_reject_scale", obj.depth_reject_scale));
    try_save(ar, ser20::make_nvp("variance_clip_scale", obj.variance_clip_scale));
}
SAVE_INSTANTIATE(taa_pass::settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(taa_pass::settings, ser20::oarchive_binary_t);

LOAD_INLINE(taa_pass::settings)
{
    try_load(ar, ser20::make_nvp("temporal_sample_count", obj.temporal_sample_count));
    try_load(ar, ser20::make_nvp("jitter_mode", obj.jitter_mode));
    try_load(ar, ser20::make_nvp("jitter_amplitude", obj.jitter_amplitude));
    try_load(ar, ser20::make_nvp("jitter_temporal_phase_scale", obj.jitter_temporal_phase_scale));
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
            entt::attribute{"category", "RENDERING"},
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
    try_load(ar, ser20::make_nvp("temporal_sample_count", obj.settings.temporal_sample_count));
    try_load(ar, ser20::make_nvp("history_blend", obj.settings.history_blend));
    try_load(ar, ser20::make_nvp("sharpen", obj.settings.sharpen));
    try_load(ar, ser20::make_nvp("depth_reject_scale", obj.settings.depth_reject_scale));
    try_load(ar, ser20::make_nvp("variance_clip_scale", obj.settings.variance_clip_scale));
    try_load(ar, ser20::make_nvp("jitter_mode", obj.settings.jitter_mode));
    try_load(ar, ser20::make_nvp("jitter_amplitude", obj.settings.jitter_amplitude));
    try_load(ar, ser20::make_nvp("jitter_temporal_phase_scale", obj.settings.jitter_temporal_phase_scale));
}
LOAD_INSTANTIATE(taa_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(taa_component, ser20::iarchive_binary_t);

} // namespace unravel
