#include "bloom_component.hpp"
#include <engine/rendering/pipeline/passes/bloom_pass.h>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include "engine/meta/core/math/vector.hpp"
namespace unravel
{

REFLECT_INLINE(bloom_pass::settings)
{
    entt::meta_factory<bloom_pass::settings>{}
        .type("bloom_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bloom_settings"},
            entt::attribute{"pretty_name", "Bloom Settings"},
        })
        .data<&bloom_pass::settings::threshold>("threshold"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "threshold"},
            entt::attribute{"pretty_name", "Threshold"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"tooltip", "Minimum brightness (HDR luminance) for a pixel to contribute to bloom. Higher values restrict bloom to only the brightest highlights. 0 = all pixels bloom (not recommended). Typical range: 0.5 - 2.0."},
        })
        .data<&bloom_pass::settings::soft_knee>("soft_knee"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "soft_knee"},
            entt::attribute{"pretty_name", "Soft Knee"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "Controls the transition width around the threshold. 0 = hard cutoff (sharp bloom boundary, can cause flickering), 1 = maximum smoothing (gradual fade-in, reduces specular flicker). Keep at 1.0 unless you specifically want a harder cutoff."},
        })
        .data<&bloom_pass::settings::clamp>("clamp"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "clamp"},
            entt::attribute{"pretty_name", "Clamp"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 1.0f},
            entt::attribute{"tooltip", "Soft compression limit for pixel brightness before bloom processing. Uses Reinhard-style compression to smoothly attenuate extreme values. Prevents firefly artifacts from very bright sub-pixel highlights (e.g. sun reflections, emissives). 0 = disabled. Typical range: 5 - 50."},
        })
        .data<&bloom_pass::settings::intensity>("intensity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "intensity"},
            entt::attribute{"pretty_name", "Intensity"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"tooltip", "Global multiplier for the entire bloom effect. Scales all per-mip contributions uniformly. 0 = no bloom, 1 = standard, >1 = exaggerated glow. This is the main knob for overall bloom strength."},
        })
        .data<&bloom_pass::settings::mip_count>("mip_count"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mip_count"},
            entt::attribute{"pretty_name", "Mip Count"},
            entt::attribute{"min", 2},
            entt::attribute{"max", 10},
            entt::attribute{"tooltip", "Number of downsample levels in the bloom pyramid. More mips = wider bloom spread but more GPU cost. 5-7 is typical. Each additional mip doubles the maximum bloom radius."},
        })
        .data<&bloom_pass::settings::mip0_tint>("mip0_tint"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mip0_tint"},
            entt::attribute{"pretty_name", "Mip 0 Tint (1/2)"},
            entt::attribute{"group", "Per-Mip Tint"},
            entt::attribute{"tooltip", "Tint and weight for the 1/2 resolution bloom layer (tightest halo). RGB controls color, Alpha controls contribution weight (0 = off, 1 = full). This is the sharpest, closest bloom around bright objects."},
        })
        .data<&bloom_pass::settings::mip1_tint>("mip1_tint"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mip1_tint"},
            entt::attribute{"pretty_name", "Mip 1 Tint (1/4)"},
            entt::attribute{"group", "Per-Mip Tint"},
            entt::attribute{"tooltip", "Tint and weight for the 1/4 resolution bloom layer. RGB controls color, Alpha controls contribution weight. Produces a medium-tight bloom halo."},
        })
        .data<&bloom_pass::settings::mip2_tint>("mip2_tint"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mip2_tint"},
            entt::attribute{"pretty_name", "Mip 2 Tint (1/8)"},
            entt::attribute{"group", "Per-Mip Tint"},
            entt::attribute{"tooltip", "Tint and weight for the 1/8 resolution bloom layer. RGB controls color, Alpha controls contribution weight. Mid-range bloom spread."},
        })
        .data<&bloom_pass::settings::mip3_tint>("mip3_tint"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mip3_tint"},
            entt::attribute{"pretty_name", "Mip 3 Tint (1/16)"},
            entt::attribute{"group", "Per-Mip Tint"},
            entt::attribute{"tooltip", "Tint and weight for the 1/16 resolution bloom layer. RGB controls color, Alpha controls contribution weight. Wide soft glow."},
        })
        .data<&bloom_pass::settings::mip4_tint>("mip4_tint"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mip4_tint"},
            entt::attribute{"pretty_name", "Mip 4 Tint (1/32)"},
            entt::attribute{"group", "Per-Mip Tint"},
            entt::attribute{"tooltip", "Tint and weight for the 1/32 resolution bloom layer. RGB controls color, Alpha controls contribution weight. Very wide atmospheric glow."},
        })
        .data<&bloom_pass::settings::mip5_tint>("mip5_tint"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mip5_tint"},
            entt::attribute{"pretty_name", "Mip 5 Tint (1/64)"},
            entt::attribute{"group", "Per-Mip Tint"},
            entt::attribute{"tooltip", "Tint and weight for the 1/64 resolution bloom layer (widest bloom). RGB controls color, Alpha controls contribution weight. Controls the large-scale ambient glow that fills the screen. Reduce alpha to tighten bloom, increase for a dreamy look."},
        })
        .data<&bloom_pass::settings::dirt_intensity>("dirt_intensity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "dirt_intensity"},
            entt::attribute{"pretty_name", "Dirt Intensity"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"tooltip", "Strength of the lens dirt mask effect. Modulates bloom through a screen-space dirt texture to simulate smudges and scratches on the camera lens. 0 = disabled. Requires a dirt mask texture to be assigned (future feature)."},
        });
}

SAVE_INLINE(bloom_pass::settings)
{
    try_save(ar, ser20::make_nvp("threshold", obj.threshold));
    try_save(ar, ser20::make_nvp("soft_knee", obj.soft_knee));
    try_save(ar, ser20::make_nvp("clamp", obj.clamp));
    try_save(ar, ser20::make_nvp("intensity", obj.intensity));
    try_save(ar, ser20::make_nvp("mip_count", obj.mip_count));
    try_save(ar, ser20::make_nvp("mip0_tint", obj.mip0_tint));
    try_save(ar, ser20::make_nvp("mip1_tint", obj.mip1_tint));
    try_save(ar, ser20::make_nvp("mip2_tint", obj.mip2_tint));
    try_save(ar, ser20::make_nvp("mip3_tint", obj.mip3_tint));
    try_save(ar, ser20::make_nvp("mip4_tint", obj.mip4_tint));
    try_save(ar, ser20::make_nvp("mip5_tint", obj.mip5_tint));
    try_save(ar, ser20::make_nvp("dirt_intensity", obj.dirt_intensity));
}
SAVE_INSTANTIATE(bloom_pass::settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(bloom_pass::settings, ser20::oarchive_binary_t);

LOAD_INLINE(bloom_pass::settings)
{
    try_load(ar, ser20::make_nvp("threshold", obj.threshold));
    try_load(ar, ser20::make_nvp("soft_knee", obj.soft_knee));
    try_load(ar, ser20::make_nvp("clamp", obj.clamp));
    try_load(ar, ser20::make_nvp("intensity", obj.intensity));
    try_load(ar, ser20::make_nvp("mip_count", obj.mip_count));
    try_load(ar, ser20::make_nvp("mip0_tint", obj.mip0_tint));
    try_load(ar, ser20::make_nvp("mip1_tint", obj.mip1_tint));
    try_load(ar, ser20::make_nvp("mip2_tint", obj.mip2_tint));
    try_load(ar, ser20::make_nvp("mip3_tint", obj.mip3_tint));
    try_load(ar, ser20::make_nvp("mip4_tint", obj.mip4_tint));
    try_load(ar, ser20::make_nvp("mip5_tint", obj.mip5_tint));
    try_load(ar, ser20::make_nvp("dirt_intensity", obj.dirt_intensity));
}
LOAD_INSTANTIATE(bloom_pass::settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(bloom_pass::settings, ser20::iarchive_binary_t);

REFLECT(bloom_component)
{
    entt::meta_factory<bloom_component>{}
        .type("bloom_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bloom_component"},
            entt::attribute{"category", "RENDERING"},
            entt::attribute{"pretty_name", "Bloom"},
        })
        .func<&component_meta<bloom_component>::exists>("component_exists"_hs)
        .func<&component_meta<bloom_component>::add>("component_add"_hs)
        .func<&component_meta<bloom_component>::remove>("component_remove"_hs)
        .func<&component_meta<bloom_component>::save>("component_save"_hs)
        .func<&component_meta<bloom_component>::load>("component_load"_hs)
        .data<&bloom_component::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Enable/disable bloom post-processing"},
        })
        .data<&bloom_component::settings>("settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "settings"},
            entt::attribute{"pretty_name", "Settings"},
            entt::attribute{"flattable", true},
        });
}

SAVE(bloom_component)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("settings", obj.settings));
}
SAVE_INSTANTIATE(bloom_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(bloom_component, ser20::oarchive_binary_t);

LOAD(bloom_component)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("settings", obj.settings));
}
LOAD_INSTANTIATE(bloom_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(bloom_component, ser20::iarchive_binary_t);

} // namespace unravel
