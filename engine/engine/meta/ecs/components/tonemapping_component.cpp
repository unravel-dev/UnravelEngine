#include "tonemapping_component.hpp"

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
        });
}

SAVE_INLINE(tonemapping_pass::settings)
{
    try_save(ar, ser20::make_nvp("exposure", obj.exposure));
    try_save(ar, ser20::make_nvp("method", obj.method));
}
SAVE_INSTANTIATE(tonemapping_pass::settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(tonemapping_pass::settings, ser20::oarchive_binary_t);

LOAD_INLINE(tonemapping_pass::settings)
{
    try_load(ar, ser20::make_nvp("exposure", obj.exposure));
    try_load(ar, ser20::make_nvp("method", obj.method));
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
