#include "audio_listener_component.hpp"

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{
REFLECT(audio_listener_component)
{
    entt::meta_factory<audio_listener_component>{}
        .type("audio_listener_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "audio_listener_component"},
            entt::attribute{"category", "AUDIO"},
            entt::attribute{"pretty_name", "Audio Listener"},
        })
        .func<&component_exists<audio_listener_component>>("component_exists"_hs)
        .func<&component_add<audio_listener_component>>("component_add"_hs)
        .func<&component_remove<audio_listener_component>>("component_remove"_hs);
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
