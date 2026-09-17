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
        .data<&auto_exposure_pass::settings::compensation>("compensation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "compensation"},
            entt::attribute{"pretty_name", "Exposure Compensation"},
            entt::attribute{"min", -15.0f},
            entt::attribute{"max", 15.0f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"tooltip", "Makes the whole image brighter or darker in stops. "
                "The metered scene brightness is shown at 18% grey times 2^compensation. "
                "+1 is twice as bright, -1 is half as bright."},
        })
        .data<&auto_exposure_pass::settings::min_ev>("min_ev"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "min_ev"},
            entt::attribute{"pretty_name", "Min Brightness (EV100)"},
            entt::attribute{"min", -10.0f},
            entt::attribute{"max", 20.0f},
            entt::attribute{"step", 0.5f},
            entt::attribute{"tooltip", "Darkest scene brightness auto exposure adapts to, in EV100. "
                "Darker scenes stop getting brighter here. How far dark scenes brighten also depends on Dark Adaptation."},
        })
        .data<&auto_exposure_pass::settings::max_ev>("max_ev"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_ev"},
            entt::attribute{"pretty_name", "Max Brightness (EV100)"},
            entt::attribute{"min", -10.0f},
            entt::attribute{"max", 20.0f},
            entt::attribute{"step", 0.5f},
            entt::attribute{"tooltip", "Brightest scene brightness auto exposure adapts to, in EV100. "
                "Brighter scenes stop getting darker here. Set Min equal to Max for a fixed exposure."},
        })
        .data<&auto_exposure_pass::settings::low_percentile>("low_percentile"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "low_percentile"},
            entt::attribute{"pretty_name", "Low Percent"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Share of the darkest pixels ignored when metering. "
                "Higher values let bright areas decide the exposure."},
        })
        .data<&auto_exposure_pass::settings::high_percentile>("high_percentile"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "high_percentile"},
            entt::attribute{"pretty_name", "High Percent"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Share of pixels kept before the brightest are ignored (sun, specular sparks). "
                "Lower this if small bright spots make the scene too dark."},
        })
        .data<&auto_exposure_pass::settings::speed_up>("speed_up"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "speed_up"},
            entt::attribute{"pretty_name", "Speed Up"},
            entt::attribute{"min", 0.02f},
            entt::attribute{"max", 20.0f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"tooltip", "Adaptation speed in stops per second when the scene gets brighter "
                "(e.g. walking outdoors). Near the target the exposure settles gradually."},
        })
        .data<&auto_exposure_pass::settings::speed_down>("speed_down"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "speed_down"},
            entt::attribute{"pretty_name", "Speed Down"},
            entt::attribute{"min", 0.02f},
            entt::attribute{"max", 20.0f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"tooltip", "Adaptation speed in stops per second when the scene gets darker "
                "(e.g. walking indoors)."},
        })
        .data<&auto_exposure_pass::settings::dark_adaptation>("dark_adaptation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "dark_adaptation"},
            entt::attribute{"pretty_name", "Dark Adaptation"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "How much dark scenes are brightened. "
                "1 adapts fully, 0 keeps darkness as it is. Bright scenes are unaffected."},
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
        })
        .data<&auto_exposure_pass::settings::local_highlight_contrast>("local_highlight_contrast"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "local_highlight_contrast"},
            entt::attribute{"pretty_name", "Local Highlight Contrast"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "Local exposure for bright areas: below 1 pulls bright neighbourhoods "
                "back toward mid grey, so a sunlit window keeps detail instead of blowing out. "
                "1 turns local exposure off (with the shadow and detail settings at 1). Try 0.6-1.0."},
        })
        .data<&auto_exposure_pass::settings::local_shadow_contrast>("local_shadow_contrast"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "local_shadow_contrast"},
            entt::attribute{"pretty_name", "Local Shadow Contrast"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "Local exposure for dark areas: below 1 lifts dark neighbourhoods "
                "toward mid grey, so shadows stay readable under a bright sky. 1 leaves them alone."},
        })
        .data<&auto_exposure_pass::settings::local_detail_strength>("local_detail_strength"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "local_detail_strength"},
            entt::attribute{"pretty_name", "Local Detail Strength"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 2.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "How much fine detail survives the local contrast change. "
                "1 keeps it exactly; above 1 sharpens local contrast, below 1 flattens it."},
        })
        .data<&auto_exposure_pass::settings::local_blurred_blend>("local_blurred_blend"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "local_blurred_blend"},
            entt::attribute{"pretty_name", "Local Blurred Blend"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "Blends the edge-aware local level toward a plain blur. "
                "Higher hides halos around strong edges; lower keeps more local contrast."},
        })
        .data<&auto_exposure_pass::settings::local_blurred_kernel_percent>("local_blurred_kernel_percent"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "local_blurred_kernel_percent"},
            entt::attribute{"pretty_name", "Local Blur Size (%)"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 100.0f},
            entt::attribute{"step", 1.0f},
            entt::attribute{"tooltip", "Size of that blur as a percentage of the view width."},
        })
        .data<&auto_exposure_pass::settings::local_middle_grey_bias>("local_middle_grey_bias"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "local_middle_grey_bias"},
            entt::attribute{"pretty_name", "Local Middle Grey Bias"},
            entt::attribute{"min", -3.0f},
            entt::attribute{"max", 3.0f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"tooltip", "Shifts the brightness local exposure pivots around, in stops. "
                "Negative pulls the pivot down, so more of the image counts as a highlight."},
        });
}

SAVE_INLINE(auto_exposure_pass::settings)
{
    std::uint32_t version = auto_exposure_pass::settings_version;
    try_save(ar, ser20::make_nvp("settings_version", version));
    try_save(ar, ser20::make_nvp("compensation", obj.compensation));
    try_save(ar, ser20::make_nvp("min_ev", obj.min_ev));
    try_save(ar, ser20::make_nvp("max_ev", obj.max_ev));
    try_save(ar, ser20::make_nvp("low_percentile", obj.low_percentile));
    try_save(ar, ser20::make_nvp("high_percentile", obj.high_percentile));
    try_save(ar, ser20::make_nvp("speed_up", obj.speed_up));
    try_save(ar, ser20::make_nvp("speed_down", obj.speed_down));
    try_save(ar, ser20::make_nvp("dark_adaptation", obj.dark_adaptation));
    try_save(ar, ser20::make_nvp("metering_mode", obj.metering_mode));
    try_save(ar, ser20::make_nvp("metering_area", obj.metering_area));
    // Local exposure, appended to version 2: a document written before it simply leaves these
    // at their neutral defaults, which is the same as not having them.
    try_save(ar, ser20::make_nvp("local_highlight_contrast", obj.local_highlight_contrast));
    try_save(ar, ser20::make_nvp("local_shadow_contrast", obj.local_shadow_contrast));
    try_save(ar, ser20::make_nvp("local_detail_strength", obj.local_detail_strength));
    try_save(ar, ser20::make_nvp("local_blurred_blend", obj.local_blurred_blend));
    try_save(ar, ser20::make_nvp("local_blurred_kernel_percent", obj.local_blurred_kernel_percent));
    try_save(ar, ser20::make_nvp("local_middle_grey_bias", obj.local_middle_grey_bias));
}
SAVE_INSTANTIATE(auto_exposure_pass::settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(auto_exposure_pass::settings, ser20::oarchive_binary_t);

LOAD_INLINE(auto_exposure_pass::settings)
{
    // Settings saved before the UE exposure model (no version) were tuned for different units
    // and metering; they are ignored so the scene picks up the current defaults.
    std::uint32_t version = 0;
    try_load(ar, ser20::make_nvp("settings_version", version));
    if(version != auto_exposure_pass::settings_version)
    {
        return;
    }
    try_load(ar, ser20::make_nvp("compensation", obj.compensation));
    try_load(ar, ser20::make_nvp("min_ev", obj.min_ev));
    try_load(ar, ser20::make_nvp("max_ev", obj.max_ev));
    try_load(ar, ser20::make_nvp("low_percentile", obj.low_percentile));
    try_load(ar, ser20::make_nvp("high_percentile", obj.high_percentile));
    try_load(ar, ser20::make_nvp("speed_up", obj.speed_up));
    try_load(ar, ser20::make_nvp("speed_down", obj.speed_down));
    try_load(ar, ser20::make_nvp("dark_adaptation", obj.dark_adaptation));
    try_load(ar, ser20::make_nvp("metering_mode", obj.metering_mode));
    try_load(ar, ser20::make_nvp("metering_area", obj.metering_area));
    try_load(ar, ser20::make_nvp("local_highlight_contrast", obj.local_highlight_contrast));
    try_load(ar, ser20::make_nvp("local_shadow_contrast", obj.local_shadow_contrast));
    try_load(ar, ser20::make_nvp("local_detail_strength", obj.local_detail_strength));
    try_load(ar, ser20::make_nvp("local_blurred_blend", obj.local_blurred_blend));
    try_load(ar, ser20::make_nvp("local_blurred_kernel_percent", obj.local_blurred_kernel_percent));
    try_load(ar, ser20::make_nvp("local_middle_grey_bias", obj.local_middle_grey_bias));
}
LOAD_INSTANTIATE(auto_exposure_pass::settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(auto_exposure_pass::settings, ser20::iarchive_binary_t);

REFLECT(auto_exposure_component)
{
    entt::meta_factory<auto_exposure_component>{}
        .type("auto_exposure_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "auto_exposure_component"},
            entt::attribute{"category", "Rendering/Post Processing"},
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
