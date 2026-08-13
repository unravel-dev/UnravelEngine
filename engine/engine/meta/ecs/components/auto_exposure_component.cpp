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
            entt::attribute{"tooltip", "Lowest brightness auto exposure will adapt to. "
                "Lower values let dark interiors get brighter. Raise this to keep dark rooms looking dark."},
        })
        .data<&auto_exposure_pass::settings::max_ev>("max_ev"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_ev"},
            entt::attribute{"pretty_name", "Max EV"},
            entt::attribute{"min", -10.0f},
            entt::attribute{"max", 24.0f},
            entt::attribute{"step", 0.5f},
            entt::attribute{"tooltip", "Highest brightness auto exposure will adapt to. "
                "Higher values let bright exteriors get darker. Lower this to keep very bright scenes from going too dark."},
        })
        .data<&auto_exposure_pass::settings::compensation>("compensation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "compensation"},
            entt::attribute{"pretty_name", "Compensation"},
            entt::attribute{"min", -10.0f},
            entt::attribute{"max", 10.0f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"tooltip", "Makes the whole image brighter or darker on top of auto exposure. "
                "+1 is twice as bright, -1 is half as bright."},
        })
        .data<&auto_exposure_pass::settings::dark_adaptation>("dark_adaptation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "dark_adaptation"},
            entt::attribute{"pretty_name", "Dark Adaptation"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "How much the eye brightens dark scenes. "
                "0 keeps darkness as-is. 1 lifts dark rooms toward normal brightness. "
                "Bright scenes are unaffected."},
        })
        .data<&auto_exposure_pass::settings::adaptation_speed_up>("adaptation_speed_up"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "adaptation_speed_up"},
            entt::attribute{"pretty_name", "Adaptation Time Up"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"tooltip", "Seconds for exposure to rise when the scene gets darker (e.g. walking indoors). "
                "Higher is slower."},
        })
        .data<&auto_exposure_pass::settings::adaptation_speed_down>("adaptation_speed_down"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "adaptation_speed_down"},
            entt::attribute{"pretty_name", "Adaptation Time Down"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"tooltip", "Seconds for exposure to fall when the scene gets brighter (e.g. walking outdoors). "
                "Lower is faster."},
        })
        .data<&auto_exposure_pass::settings::low_percentile>("low_percentile"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "low_percentile"},
            entt::attribute{"pretty_name", "Low Percentile"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 0.95f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "How much of the darkest pixels to ignore when metering. "
                "Higher keeps shadows from pulling the rest of the image brighter."},
        })
        .data<&auto_exposure_pass::settings::high_percentile>("high_percentile"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "high_percentile"},
            entt::attribute{"pretty_name", "High Percentile"},
            entt::attribute{"min", 0.5f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Cuts off the brightest pixels (sun, specular sparks) so they do not "
                "drag the rest of the image darker. Lower this if a bright sky is making the scene too dark."},
        })
        .data<&auto_exposure_pass::settings::metering_mode>("metering_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "metering_mode"},
            entt::attribute{"pretty_name", "Metering Mode"},
            entt::attribute{"tooltip", "Where brightness is measured. "
                "Average uses the whole frame. "
                "Center Weighted favors the middle. "
                "Spot uses only a central circle."},
        })
        .data<&auto_exposure_pass::settings::metering_area>("metering_area"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "metering_area"},
            entt::attribute{"pretty_name", "Metering Area"},
            entt::attribute{"min", 0.05f},
            entt::attribute{"max", 1.5f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "Size of the metering region. "
                "Used by Center Weighted and Spot. Has no effect in Average mode."},
        });
}

SAVE_INLINE(auto_exposure_pass::settings)
{
    try_save(ar, ser20::make_nvp("min_ev", obj.min_ev));
    try_save(ar, ser20::make_nvp("max_ev", obj.max_ev));
    try_save(ar, ser20::make_nvp("compensation", obj.compensation));
    try_save(ar, ser20::make_nvp("dark_adaptation", obj.dark_adaptation));
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
    try_load(ar, ser20::make_nvp("dark_adaptation", obj.dark_adaptation));
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
            entt::attribute{"tooltip", "When enabled, the camera adjusts exposure to scene brightness over time."},
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
