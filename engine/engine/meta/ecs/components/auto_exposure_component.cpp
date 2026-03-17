#include "auto_exposure_component.hpp"

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT_INLINE(auto_exposure_pass::settings)
{
    entt::meta_factory<auto_exposure_pass::settings>{}
        .type("auto_exposure_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "auto_exposure_settings"},
            entt::attribute{"pretty_name", "Auto Exposure Settings"},
        })
        .data<&auto_exposure_pass::settings::min_ev>("min_ev"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "min_ev"},
            entt::attribute{"pretty_name", "Min EV100"},
            entt::attribute{"min", -10.0f},
            entt::attribute{"max", 20.0f},
            entt::attribute{"step", 0.5f},
            entt::attribute{"tooltip", "Lower limit for metered scene brightness in EV100 (ISO 100). "
                "Controls how much the system can BRIGHTEN dark scenes. "
                "Lower value = more brightening allowed. "
                "Reference values: -4 = moonless night, 1 = dim indoor, "
                "5 = home interior, 8 = office/overcast, 12 = daylight. "
                "Only activates when the scene EV100 is below this threshold."},
        })
        .data<&auto_exposure_pass::settings::max_ev>("max_ev"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_ev"},
            entt::attribute{"pretty_name", "Max EV100"},
            entt::attribute{"min", -10.0f},
            entt::attribute{"max", 24.0f},
            entt::attribute{"step", 0.5f},
            entt::attribute{"tooltip", "Upper limit for metered scene brightness in EV100 (ISO 100). "
                "Controls how much the system can DARKEN bright scenes. "
                "Reference values: 12 = daylight, 15 = bright sun, "
                "16 = snow/beach, 20 = extreme glare. "
                "Only activates when the scene EV100 is above this threshold. "
                "Use Compensation instead to shift overall brightness."},
        })
        .data<&auto_exposure_pass::settings::compensation>("compensation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "compensation"},
            entt::attribute{"pretty_name", "Compensation"},
            entt::attribute{"min", -10.0f},
            entt::attribute{"max", 10.0f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"tooltip", "Global exposure bias in EV stops, applied on top of the auto-exposure result."
                "Use this to make the scene uniformly brighter or darker."
                "+1 = twice as bright, -1 = half as bright."
                "This is the main knob for adjusting overall scene brightness."},
        })
        .data<&auto_exposure_pass::settings::adaptation_speed_up>("adaptation_speed_up"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "adaptation_speed_up"},
            entt::attribute{"pretty_name", "Speed Up"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"tooltip", "Time in seconds for exposure to increase (scene getting darker, e.g. walking indoors)."
                "Higher = slower adaptation, mimicking human eye dilation."
                "Typically 2-5 seconds."},
        })
        .data<&auto_exposure_pass::settings::adaptation_speed_down>("adaptation_speed_down"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "adaptation_speed_down"},
            entt::attribute{"pretty_name", "Speed Down"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"tooltip", "Time in seconds for exposure to decrease (scene getting brighter, e.g. walking outdoors)."
                "Lower = faster adaptation, mimicking quick pupil constriction."
                "Typically 0.5-2 seconds."},
        })
        .data<&auto_exposure_pass::settings::low_percentile>("low_percentile"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "low_percentile"},
            entt::attribute{"pretty_name", "Low Percentile"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 0.95f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Fraction of darkest pixels to exclude from the exposure calculation."
                "Higher values ignore more shadows, keeping them dark and natural."
                "At 0.50 the darkest half of all pixels are excluded."
                "Unreal uses ~0.80, Unity uses ~0.40."
                "Increase this if auto-exposure is washing out shadows."},
        })
        .data<&auto_exposure_pass::settings::high_percentile>("high_percentile"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "high_percentile"},
            entt::attribute{"pretty_name", "High Percentile"},
            entt::attribute{"min", 0.5f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Fraction of pixels to include before cutting off the brightest."
                "Pixels above this percentile (sky, sun, specular highlights) are excluded."
                "At 0.95 the brightest 5% of pixels are ignored."
                "Lower this if bright sky or highlights are driving the exposure too low."},
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
