#include "tonemapping_component.hpp"

#include "engine/meta/core/math/vector.hpp"

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT_INLINE(tonemapping_pass::settings)
{
    entt::meta_factory<tonemapping_method>{}
        .type("tonemapping_method"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "tonemapping_method"},
            entt::attribute{"pretty_name", "Tonemapping Method"},
        })
        .data<tonemapping_method::none>("none"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "none"},
            entt::attribute{"pretty_name", "None"},
        })
        .data<tonemapping_method::exponential>("exponential"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "exponential"},
            entt::attribute{"pretty_name", "Exponential"},
        })
        .data<tonemapping_method::reinhard>("reinhard"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "reinhard"},
            entt::attribute{"pretty_name", "Reinhard"},
        })
        .data<tonemapping_method::reinhard_lum>("reinhard_lum"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "reinhard_lum"},
            entt::attribute{"pretty_name", "Reinhard Lum"},
        })
        .data<tonemapping_method::hable>("hable"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "hable"},
            entt::attribute{"pretty_name", "Hable"},
        })
        .data<tonemapping_method::filmic>("filmic"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "filmic"},
            entt::attribute{"pretty_name", "Filmic"},
        })
        .data<tonemapping_method::aces>("aces"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "aces"},
            entt::attribute{"pretty_name", "ACES"},
        })
        .data<tonemapping_method::aces_lum>("aces_lum"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "aces_lum"},
            entt::attribute{"pretty_name", "ACES Lum"},
        })
        .data<tonemapping_method::reinhard2>("reinhard2"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "reinhard2"},
            entt::attribute{"pretty_name", "Reinhard2"},
        })
        .data<tonemapping_method::unreal3>("unreal3"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "unreal3"},
            entt::attribute{"pretty_name", "Unreal3"},
        })
        .data<tonemapping_method::lottes>("lottes"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lottes"},
            entt::attribute{"pretty_name", "Lottes"},
        })
        .data<tonemapping_method::uchimura>("uchimura"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "uchimura"},
            entt::attribute{"pretty_name", "Uchimura"},
        })
        .data<tonemapping_method::neutral>("neutral"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "neutral"},
            entt::attribute{"pretty_name", "Neutral"},
        })
        .data<tonemapping_method::agx>("agx"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "agx"},
            entt::attribute{"pretty_name", "AgX"},
        })
        .data<tonemapping_method::agx_golden>("agx_golden"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "agx_golden"},
            entt::attribute{"pretty_name", "AgX Golden"},
        })
        .data<tonemapping_method::agx_punchy>("agx_punchy"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "agx_punchy"},
            entt::attribute{"pretty_name", "AgX Punchy"},
        });

    // Register tonemapping_pass::settings class with entt
    entt::meta_factory<tonemapping_pass::settings>{}
        .type("tonemapping_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "tonemapping_settings"},
            entt::attribute{"pretty_name", "Tonemapping Settings"},
        })
        .data<&tonemapping_pass::settings::exposure>("exposure"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "exposure"},
            entt::attribute{"pretty_name", "Exposure"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.1f},
        })
        .data<&tonemapping_pass::settings::method>("method"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "method"},
            entt::attribute{"pretty_name", "Method"},
        })
        .data<&tonemapping_pass::settings::temperature>("temperature"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "temperature"},
            entt::attribute{"pretty_name", "Temperature"},
            entt::attribute{"min", -1.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "White balance temperature shift: positive = warmer (orange), "
                "negative = cooler (blue). Applied in linear space before the tone curve."},
        })
        .data<&tonemapping_pass::settings::tint>("tint"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "tint"},
            entt::attribute{"pretty_name", "Tint"},
            entt::attribute{"min", -1.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "White balance tint shift: positive = magenta, negative = green. "
                "Applied in linear space before the tone curve."},
        })
        .data<&tonemapping_pass::settings::contrast>("contrast"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "contrast"},
            entt::attribute{"pretty_name", "Contrast"},
            entt::attribute{"min", 0.3f},
            entt::attribute{"max", 2.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Log-space contrast around 18% mid-gray: mids keep their exposure "
                "while stops above/below expand (>1) or compress (<1). 1 = neutral."},
        })
        .data<&tonemapping_pass::settings::saturation>("saturation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "saturation"},
            entt::attribute{"pretty_name", "Saturation"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 2.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Color saturation around Rec.709 luma. 0 = grayscale, 1 = neutral."},
        })
        .data<&tonemapping_pass::settings::lift>("lift"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lift"},
            entt::attribute{"pretty_name", "Lift (Shadows)"},
            entt::attribute{"tooltip", "Shadow color offset (video-grading lift). Neutral is mid-gray "
                "(0.5, 0.5, 0.5): brighter lifts blacks, darker crushes them; pushing a channel tints "
                "the shadows (e.g. slightly blue lift = teal shadows)."},
        })
        .data<&tonemapping_pass::settings::gamma>("gamma"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gamma"},
            entt::attribute{"pretty_name", "Gamma (Midtones)"},
            entt::attribute{"tooltip", "Midtone response (video-grading gamma). Neutral is mid-gray "
                "(0.5, 0.5, 0.5): brighter raises mids, darker deepens them; per-channel shifts tint "
                "the midtones."},
        })
        .data<&tonemapping_pass::settings::gain>("gain"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gain"},
            entt::attribute{"pretty_name", "Gain (Highlights)"},
            entt::attribute{"tooltip", "Highlight multiplier (video-grading gain). Neutral is mid-gray "
                "(0.5, 0.5, 0.5): brighter boosts highlights, darker pulls them down; per-channel shifts "
                "tint the highlights (e.g. warm gain = golden highlights)."},
        })
        .data<&tonemapping_pass::settings::vignette_intensity>("vignette_intensity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "vignette_intensity"},
            entt::attribute{"pretty_name", "Vignette"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Lens vignette strength. Applied in linear light before the tone "
                "curve, so darkened edges keep natural highlight response. 0 = off; 0.2-0.4 is a typical "
                "subtle cinematic amount."},
        })
        .data<&tonemapping_pass::settings::vignette_smoothness>("vignette_smoothness"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "vignette_smoothness"},
            entt::attribute{"pretty_name", "Vignette Smoothness"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "How gradually the vignette falls off: low = tight ring near the "
                "corners, high = falloff starting close to the center."},
        })
        .data<&tonemapping_pass::settings::grain_intensity>("grain_intensity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "grain_intensity"},
            entt::attribute{"pretty_name", "Film Grain"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Animated film grain, luma-weighted so highlights stay clean. "
                "0 = off; 0.1-0.3 is a typical subtle amount."},
        })
        .data<&tonemapping_pass::settings::dithering>("dithering"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "dithering"},
            entt::attribute{"pretty_name", "Dithering"},
            entt::attribute{"tooltip", "Triangular-PDF dither before 8-bit quantization. Removes banding "
                "in smooth gradients (skies, walls) at zero visible cost. Leave on."},
        });
}

SAVE_INLINE(tonemapping_pass::settings)
{
    try_save(ar, ser20::make_nvp("exposure", obj.exposure));
    try_save(ar, ser20::make_nvp("method", obj.method));
    try_save(ar, ser20::make_nvp("temperature", obj.temperature));
    try_save(ar, ser20::make_nvp("tint", obj.tint));
    try_save(ar, ser20::make_nvp("contrast", obj.contrast));
    try_save(ar, ser20::make_nvp("saturation", obj.saturation));
    try_save(ar, ser20::make_nvp("lift", obj.lift));
    try_save(ar, ser20::make_nvp("gamma", obj.gamma));
    try_save(ar, ser20::make_nvp("gain", obj.gain));
    try_save(ar, ser20::make_nvp("vignette_intensity", obj.vignette_intensity));
    try_save(ar, ser20::make_nvp("vignette_smoothness", obj.vignette_smoothness));
    try_save(ar, ser20::make_nvp("grain_intensity", obj.grain_intensity));
    try_save(ar, ser20::make_nvp("dithering", obj.dithering));
}
SAVE_INSTANTIATE(tonemapping_pass::settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(tonemapping_pass::settings, ser20::oarchive_binary_t);

LOAD_INLINE(tonemapping_pass::settings)
{
    try_load(ar, ser20::make_nvp("exposure", obj.exposure));
    try_load(ar, ser20::make_nvp("method", obj.method));
    try_load(ar, ser20::make_nvp("temperature", obj.temperature));
    try_load(ar, ser20::make_nvp("tint", obj.tint));
    try_load(ar, ser20::make_nvp("contrast", obj.contrast));
    try_load(ar, ser20::make_nvp("saturation", obj.saturation));
    try_load(ar, ser20::make_nvp("lift", obj.lift));
    try_load(ar, ser20::make_nvp("gamma", obj.gamma));
    try_load(ar, ser20::make_nvp("gain", obj.gain));
    try_load(ar, ser20::make_nvp("vignette_intensity", obj.vignette_intensity));
    try_load(ar, ser20::make_nvp("vignette_smoothness", obj.vignette_smoothness));
    try_load(ar, ser20::make_nvp("grain_intensity", obj.grain_intensity));
    try_load(ar, ser20::make_nvp("dithering", obj.dithering));
}
LOAD_INSTANTIATE(tonemapping_pass::settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(tonemapping_pass::settings, ser20::iarchive_binary_t);

REFLECT(tonemapping_component)
{
    entt::meta_factory<tonemapping_component>{}
        .type("tonemapping_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "tonemapping_component"},
            entt::attribute{"category", "RENDERING"},
            entt::attribute{"pretty_name", "Tonemapping"},
        })
        .func<&component_meta<tonemapping_component>::exists>("component_exists"_hs)
        .func<&component_meta<tonemapping_component>::add>("component_add"_hs)
        .func<&component_meta<tonemapping_component>::remove>("component_remove"_hs)
        .func<&component_meta<tonemapping_component>::save>("component_save"_hs)
        .func<&component_meta<tonemapping_component>::load>("component_load"_hs)
        .data<&tonemapping_component::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Enable/disable tonemapping"},
        })
        .data<&tonemapping_component::settings>("settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "settings"},
            entt::attribute{"pretty_name", "Settings"},
            entt::attribute{"flattable", true},
        });
}

SAVE(tonemapping_component)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("settings", obj.settings));
}
SAVE_INSTANTIATE(tonemapping_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(tonemapping_component, ser20::oarchive_binary_t);

LOAD(tonemapping_component)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("settings", obj.settings));
}
LOAD_INSTANTIATE(tonemapping_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(tonemapping_component, ser20::iarchive_binary_t);
} // namespace unravel
