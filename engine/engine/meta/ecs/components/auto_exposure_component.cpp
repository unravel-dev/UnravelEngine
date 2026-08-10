#include "auto_exposure_component.hpp"

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT_INLINE(auto_exposure_pass::settings)
{
    entt::meta_factory<exposure_metering_mode>{}
        .type("exposure_metering_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "exposure_metering_mode"},
            entt::attribute{"pretty_name", "Metering Mode"},
        })
        .data<exposure_metering_mode::average>("average"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "average"},
            entt::attribute{"pretty_name", "Average"},
        })
        .data<exposure_metering_mode::center_weighted>("center_weighted"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "center_weighted"},
            entt::attribute{"pretty_name", "Center Weighted"},
        })
        .data<exposure_metering_mode::spot>("spot"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "spot"},
            entt::attribute{"pretty_name", "Spot"},
        });

    entt::meta_factory<auto_exposure_pass::settings>{}
        .type("auto_exposure_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "auto_exposure_settings"},
            entt::attribute{"pretty_name", "Auto Exposure Settings"},
        })
        .data<&auto_exposure_pass::settings::min_ev>("min_ev"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "min_ev"},
            entt::attribute{"pretty_name", "Min EV"},
            entt::attribute{"min", -10.0f},
            entt::attribute{"max", 20.0f},
            entt::attribute{"step", 0.5f},
            entt::attribute{"tooltip", "Lower clamp for metered scene brightness (EV100). "
                "Controls how much the system can BRIGHTEN dark scenes: a LOWER value allows "
                "more brightening (more upward adaptation headroom when moving into shadow/interiors). "
                "Default -6 gives interiors/night scenes real headroom. Only engages when the "
                "scene meters below this value."},
        })
        .data<&auto_exposure_pass::settings::max_ev>("max_ev"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_ev"},
            entt::attribute{"pretty_name", "Max EV"},
            entt::attribute{"min", -10.0f},
            entt::attribute{"max", 24.0f},
            entt::attribute{"step", 0.5f},
            entt::attribute{"tooltip", "Upper clamp for metered scene brightness (EV100). "
                "Controls how much the system can DARKEN bright scenes: a HIGHER value allows "
                "more darkening. Default 16 covers physical daylight and effectively lets the "
                "meter float freely, so exposure anchors the image to mid-gray on its own. "
                "Lower it only to pin very bright scenes at an authored level. "
                "Use Compensation to shift overall brightness."},
        })
        .data<&auto_exposure_pass::settings::compensation>("compensation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "compensation"},
            entt::attribute{"pretty_name", "Compensation"},
            entt::attribute{"min", -10.0f},
            entt::attribute{"max", 10.0f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"tooltip", "Global exposure bias in EV stops, applied on top of the auto-exposure result. "
                "Use this to make the scene uniformly brighter or darker: "
                "+1 = twice as bright, -1 = half as bright. "
                "The default +1 anchors the metered average at ~18% mid-gray (the tone curves' "
                "linear section); 0 would land it at ~10%. "
                "This is the main knob for adjusting overall scene brightness."},
        })
        .data<&auto_exposure_pass::settings::adaptation_speed_up>("adaptation_speed_up"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "adaptation_speed_up"},
            entt::attribute{"pretty_name", "Adaptation Time Up"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"tooltip", "Time constant in SECONDS for exposure to increase (scene getting darker, e.g. walking indoors). "
                "Higher = slower adaptation, mimicking human eye dilation. Adaptation happens in EV/log space. "
                "Typically 2-5 seconds."},
        })
        .data<&auto_exposure_pass::settings::adaptation_speed_down>("adaptation_speed_down"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "adaptation_speed_down"},
            entt::attribute{"pretty_name", "Adaptation Time Down"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"tooltip", "Time constant in SECONDS for exposure to decrease (scene getting brighter, e.g. walking outdoors). "
                "Lower = faster adaptation, mimicking quick pupil constriction. Adaptation happens in EV/log space. "
                "Typically 0.5-2 seconds."},
        })
        .data<&auto_exposure_pass::settings::low_percentile>("low_percentile"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "low_percentile"},
            entt::attribute{"pretty_name", "Low Percentile"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 0.95f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Fraction of darkest pixels to exclude from the exposure calculation. "
                "Higher values ignore more shadows, keeping them dark and natural. "
                "Default 0.80 meters only the brightest quintile (highlight-protecting, like Unreal): "
                "shadows cannot drag the exposure up and wash out lit areas. "
                "Lower it toward 0.4-0.5 if dark scenes should adapt brighter."},
        })
        .data<&auto_exposure_pass::settings::high_percentile>("high_percentile"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "high_percentile"},
            entt::attribute{"pretty_name", "High Percentile"},
            entt::attribute{"min", 0.5f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Fraction of pixels to include before cutting off the brightest. "
                "Pixels above this percentile (sun disc, specular pinpoints) are excluded. "
                "Default 0.98 keeps sky IN the metering (so it exposes saturated instead of washed) "
                "while rejecting true peaks. "
                "Lower this if bright sky or highlights are driving the exposure too low."},
        })
        .data<&auto_exposure_pass::settings::metering_mode>("metering_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "metering_mode"},
            entt::attribute{"pretty_name", "Metering Mode"},
            entt::attribute{"tooltip", "How pixels are spatially weighted when measuring scene brightness. "
                "Average = whole frame equally. "
                "Center Weighted = smooth falloff toward the edges (recommended, avoids sky/edges dominating). "
                "Spot = only a central circle is measured."},
        })
        .data<&auto_exposure_pass::settings::metering_area>("metering_area"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "metering_area"},
            entt::attribute{"pretty_name", "Metering Area"},
            entt::attribute{"min", 0.05f},
            entt::attribute{"max", 1.5f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "Size of the metering region (in screen-normalized units). "
                "For Center Weighted this is the Gaussian falloff radius; for Spot it is the hard cutoff radius. "
                "Has no effect in Average mode."},
        });
}

SAVE_INLINE(auto_exposure_pass::settings)
{
    try_save(ar, ser20::make_nvp("min_ev", obj.min_ev));
    try_save(ar, ser20::make_nvp("max_ev", obj.max_ev));
    try_save(ar, ser20::make_nvp("compensation", obj.compensation));
    try_save(ar, ser20::make_nvp("adaptation_speed_up", obj.adaptation_speed_up));
    try_save(ar, ser20::make_nvp("adaptation_speed_down", obj.adaptation_speed_down));
    try_save(ar, ser20::make_nvp("low_percentile", obj.low_percentile));
    try_save(ar, ser20::make_nvp("high_percentile", obj.high_percentile));
    try_save(ar, ser20::make_nvp("metering_mode", obj.metering_mode));
    try_save(ar, ser20::make_nvp("metering_area", obj.metering_area));
}
SAVE_INSTANTIATE(auto_exposure_pass::settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(auto_exposure_pass::settings, ser20::oarchive_binary_t);

LOAD_INLINE(auto_exposure_pass::settings)
{
    try_load(ar, ser20::make_nvp("min_ev", obj.min_ev));
    try_load(ar, ser20::make_nvp("max_ev", obj.max_ev));
    try_load(ar, ser20::make_nvp("compensation", obj.compensation));
    try_load(ar, ser20::make_nvp("adaptation_speed_up", obj.adaptation_speed_up));
    try_load(ar, ser20::make_nvp("adaptation_speed_down", obj.adaptation_speed_down));
    try_load(ar, ser20::make_nvp("low_percentile", obj.low_percentile));
    try_load(ar, ser20::make_nvp("high_percentile", obj.high_percentile));
    try_load(ar, ser20::make_nvp("metering_mode", obj.metering_mode));
    try_load(ar, ser20::make_nvp("metering_area", obj.metering_area));
}
LOAD_INSTANTIATE(auto_exposure_pass::settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(auto_exposure_pass::settings, ser20::iarchive_binary_t);

REFLECT(auto_exposure_component)
{
    entt::meta_factory<auto_exposure_component>{}
        .type("auto_exposure_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "auto_exposure_component"},
            entt::attribute{"category", "RENDERING"},
            entt::attribute{"pretty_name", "Auto Exposure"},
        })
        .func<&component_meta<auto_exposure_component>::exists>("component_exists"_hs)
        .func<&component_meta<auto_exposure_component>::add>("component_add"_hs)
        .func<&component_meta<auto_exposure_component>::remove>("component_remove"_hs)
        .func<&component_meta<auto_exposure_component>::save>("component_save"_hs)
        .func<&component_meta<auto_exposure_component>::load>("component_load"_hs)
        .data<&auto_exposure_component::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Enable histogram-based auto exposure (eye adaptation). When enabled, the camera dynamically adjusts exposure based on scene brightness using a log2 luminance histogram with percentile trimming."},
        })
        .data<&auto_exposure_component::settings>("settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "settings"},
            entt::attribute{"pretty_name", "Settings"},
            entt::attribute{"flattable", true},
        });
}

SAVE(auto_exposure_component)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("settings", obj.settings));
}
SAVE_INSTANTIATE(auto_exposure_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(auto_exposure_component, ser20::oarchive_binary_t);

LOAD(auto_exposure_component)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("settings", obj.settings));
}
LOAD_INSTANTIATE(auto_exposure_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(auto_exposure_component, ser20::iarchive_binary_t);

} // namespace unravel
