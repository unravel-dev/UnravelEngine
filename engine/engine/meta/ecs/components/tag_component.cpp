#include "tag_component.hpp"
#include <engine/ecs/components/basic_component.h>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT(tag_component)
{
    entt::meta_factory<tag_component>{}
        .type("tag_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "tag_component"},
            entt::attribute{"category", "BASIC"},
            entt::attribute{"pretty_name", "Tag"},
        })
        .func<&component_meta<tag_component>::exists>("component_exists"_hs)
        .func<&component_meta<tag_component>::add>("component_add"_hs)
        .func<&component_meta<tag_component>::remove>("component_remove"_hs)
        .func<&component_meta<tag_component>::save>("component_save"_hs)
        .func<&component_meta<tag_component>::load>("component_load"_hs)
        .data<&tag_component::name>("name"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "name"},
            entt::attribute{"pretty_name", "Name"},
            entt::attribute{"tooltip", "This is the name of the entity."},
        })
        .data<&tag_component::tag>("tag"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "tag"},
            entt::attribute{"pretty_name", "Tag"},
            entt::attribute{"tooltip", "This is the tag(group) of the entity."},
        });
}

SAVE(tag_component)
{
    try_save(ar, ser20::make_nvp("name", obj.name));
    try_save(ar, ser20::make_nvp("tag", obj.tag));
}
SAVE_INSTANTIATE(tag_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(tag_component, ser20::oarchive_binary_t);

LOAD(tag_component)
{
    try_load(ar, ser20::make_nvp("name", obj.name));
    try_load(ar, ser20::make_nvp("tag", obj.tag));
}

LOAD_INSTANTIATE(tag_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(tag_component, ser20::iarchive_binary_t);
} // namespace unravel
