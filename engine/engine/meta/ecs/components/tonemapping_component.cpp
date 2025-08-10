#include "tonemapping_component.hpp"

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT_INLINE(tonemapping_pass::settings)
{
    rttr::registration::enumeration<tonemapping_method>("tonemapping_method")(
        rttr::value("None", tonemapping_method::none),
        rttr::value("Exponential", tonemapping_method::exponential),
        rttr::value("Reinhard", tonemapping_method::reinhard),
        rttr::value("Reinhard Lum", tonemapping_method::reinhard_lum),
        rttr::value("Dukier", tonemapping_method::duiker),
        rttr::value("Aces", tonemapping_method::aces),
        rttr::value("Aces Lum", tonemapping_method::aces_lum),
        rttr::value("Filmic", tonemapping_method::filmic));

    rttr::registration::class_<tonemapping_pass::settings>("tonemapping_pass::settings")(
        rttr::metadata("pretty_name", "Tonemapping Settings"))
        .constructor<>()
        
        .property("exposure", &tonemapping_pass::settings::exposure)(rttr::metadata("pretty_name", "Exposure"),
                                                                     rttr::metadata("min", 0.0f),
                                                                     rttr::metadata("step", 0.1f))
        .property("method", &tonemapping_pass::settings::method)(rttr::metadata("pretty_name", "Method"));

    // Register tonemapping_method enum with entt
    entt::meta_factory<tonemapping_method>{}
        .type("tonemapping_method"_hs)
        .data<tonemapping_method::none>("none"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "None"},
        })
        .data<tonemapping_method::exponential>("exponential"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Exponential"},
        })
        .data<tonemapping_method::reinhard>("reinhard"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Reinhard"},
        })
        .data<tonemapping_method::reinhard_lum>("reinhard_lum"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Reinhard Lum"},
        })
        .data<tonemapping_method::duiker>("duiker"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Dukier"},
        })
        .data<tonemapping_method::aces>("aces"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Aces"},
        })
        .data<tonemapping_method::aces_lum>("aces_lum"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Aces Lum"},
        })
        .data<tonemapping_method::filmic>("filmic"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Filmic"},
        });

    // Register tonemapping_pass::settings class with entt
    entt::meta_factory<tonemapping_pass::settings>{}
        .type("tonemapping_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Tonemapping Settings"},
        })
        .data<&tonemapping_pass::settings::exposure>("exposure"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Exposure"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.1f},
        })
        .data<&tonemapping_pass::settings::method>("method"_hs)
        .custom<entt::attributes>(entt::attributes{
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
    rttr::registration::class_<tonemapping_component>(
        "tonemapping_component")(rttr::metadata("category", "RENDERING"), rttr::metadata("pretty_name", "Tonemapping"))
        .constructor<>()
        .method("component_exists", &component_exists<tonemapping_component>)
        .property("enabled", &tonemapping_component::enabled)(
            rttr::metadata("pretty_name", "Enabled"),
            rttr::metadata("tooltip", "Enable/disable tonemapping"))
        .property("settings", &tonemapping_component::settings)(rttr::metadata("pretty_name", "Settings"),
                                                                rttr::metadata("flattable", true));

    // Register tonemapping_component class with entt
    entt::meta_factory<tonemapping_component>{}
        .type("tonemapping_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"category", "RENDERING"},
            entt::attribute{"pretty_name", "Tonemapping"},
        })
        .func<&component_exists<tonemapping_component>>("component_exists"_hs)
        .data<&tonemapping_component::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Enable/disable tonemapping"},
        })
        .data<&tonemapping_component::settings>("settings"_hs)
        .custom<entt::attributes>(entt::attributes{
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
