#include "audio_listener_component.hpp"

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{
REFLECT(audio_listener_component)
{
    rttr::registration::class_<audio_listener_component>("audio_listener_component")(
        rttr::metadata("category", "AUDIO"),
        rttr::metadata("pretty_name", "Audio Listener"))
        .constructor<>()()
        .method("component_exists", &component_exists<audio_listener_component>);

    // Register audio_listener_component class with entt
    entt::meta_factory<audio_listener_component>{}
        .type("audio_listener_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "audio_listener_component"},
            entt::attribute{"category", "AUDIO"},
            entt::attribute{"pretty_name", "Audio Listener"},
        })
        .func<&component_exists<audio_listener_component>>("component_exists"_hs);
}

SAVE(audio_listener_component)
{
}
SAVE_INSTANTIATE(audio_listener_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(audio_listener_component, ser20::oarchive_binary_t);

LOAD(audio_listener_component)
{
}
LOAD_INSTANTIATE(audio_listener_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(audio_listener_component, ser20::iarchive_binary_t);
} // namespace unravel
