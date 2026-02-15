#include "bloom_component.hpp"
#include <engine/rendering/pipeline/passes/bloom_pass.h>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

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
        })
        .data<&bloom_pass::settings::soft_knee>("soft_knee"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "soft_knee"},
            entt::attribute{"pretty_name", "Soft Knee"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "Smooth transition around threshold. 0 = hard cutoff, 1 = soft. Reduces specular flicker."},
        })
        .data<&bloom_pass::settings::clamp>("clamp"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "clamp"},
            entt::attribute{"pretty_name", "Clamp"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 1.0f},
            entt::attribute{"tooltip", "Max value before threshold. Limits firefly exaggeration. 0 = no clamp."},
        })
        .data<&bloom_pass::settings::intensity>("intensity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "intensity"},
            entt::attribute{"pretty_name", "Intensity"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.1f},
        })
        .data<&bloom_pass::settings::mip_count>("mip_count"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mip_count"},
            entt::attribute{"pretty_name", "Mip Count"},
            entt::attribute{"min", 2},
            entt::attribute{"max", 10},
        });
}

SAVE_INLINE(bloom_pass::settings)
{
    try_save(ar, ser20::make_nvp("threshold", obj.threshold));
    try_save(ar, ser20::make_nvp("soft_knee", obj.soft_knee));
    try_save(ar, ser20::make_nvp("clamp", obj.clamp));
    try_save(ar, ser20::make_nvp("intensity", obj.intensity));
    try_save(ar, ser20::make_nvp("mip_count", obj.mip_count));
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
