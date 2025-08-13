#include "fxaa_component.hpp"
#include <engine/ecs/components/basic_component.h>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{
REFLECT(fxaa_component)
{
    rttr::registration::class_<fxaa_component>("fxaa_component")(rttr::metadata("category", "RENDERING"),
                                                                   rttr::metadata("pretty_name", "FXAA"))
        .constructor<>()
        .method("component_exists", &component_exists<fxaa_component>)

        .property("enabled", &fxaa_component::enabled)(
            rttr::metadata("pretty_name", "Enabled"),
            rttr::metadata("tooltip", "Enable/disable FXAA anti-aliasing"))
        ;

    // Register fxaa_component class with entt
    entt::meta_factory<fxaa_component>{}
        .type("fxaa_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fxaa_component"},
            entt::attribute{"category", "RENDERING"},
            entt::attribute{"pretty_name", "FXAA"},
        })
        .func<&component_exists<fxaa_component>>("component_exists"_hs)
        .data<&fxaa_component::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Enable/disable FXAA anti-aliasing"},
        });
}

SAVE(fxaa_component)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
}
SAVE_INSTANTIATE(fxaa_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(fxaa_component, ser20::oarchive_binary_t);

LOAD(fxaa_component)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
}
LOAD_INSTANTIATE(fxaa_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(fxaa_component, ser20::iarchive_binary_t);
} // namespace unravel
