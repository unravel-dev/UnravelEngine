#include "id_component.hpp"
#include "hpp/uuid.hpp"
#include <engine/ecs/components/basic_component.h>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{
REFLECT(id_component)
{
    entt::meta_factory<id_component>{}
        .type("id_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "id_component"},
            entt::attribute{"category", "BASIC"},
            entt::attribute{"pretty_name", "Id"},
        })
        .func<&component_meta<id_component>::exists>("component_exists"_hs)
        .func<&component_meta<id_component>::add>("component_add"_hs)
        .func<&component_meta<id_component>::remove>("component_remove"_hs)
        .func<&component_meta<id_component>::save>("component_save"_hs)
        .func<&component_meta<id_component>::load>("component_load"_hs)
        .data<nullptr, &id_component::id>("id"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "id"},
            entt::attribute{"pretty_name", "Id"},
            entt::attribute{"tooltip", "This is the unique id of the entity."},
        });
}


SAVE(id_component)
{
    try_save(ar, ser20::make_nvp("id", hpp::to_string(obj.id)));
}
SAVE_INSTANTIATE(id_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(id_component, ser20::oarchive_binary_t);

LOAD(id_component)
{
    std::string suuid;

    if(try_load(ar, ser20::make_nvp("id", suuid)))
    {
        auto id = hpp::uuid::from_string(suuid);
        obj.id = id.value_or(hpp::uuid{});
    }
}

LOAD_INSTANTIATE(id_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(id_component, ser20::iarchive_binary_t);

} // namespace unravel
