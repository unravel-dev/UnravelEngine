#include "layer_component.hpp"

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

#include <engine/meta/layers/layer_mask.hpp>
#include <engine/ecs/components/basic_component.h>
namespace unravel
{
REFLECT(layer_component)
{
    entt::meta_factory<layer_component>{}
        .type("layer_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "layer_component"},
            entt::attribute{"category", "BASIC"},
            entt::attribute{"pretty_name", "Layer"},
        })
        .func<&component_exists<layer_component>>("component_exists"_hs)
        .data<&layer_component::layers>("layers"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "layers"},
            entt::attribute{"pretty_name", "Layers"},
            entt::attribute{"tooltip", "This is the layers of the entity."},
        });
}

SAVE(layer_component)
{
    try_save(ar, ser20::make_nvp("layers", obj.layers));
}
SAVE_INSTANTIATE(layer_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(layer_component, ser20::oarchive_binary_t);

LOAD(layer_component)
{
    try_load(ar, ser20::make_nvp("layers", obj.layers));
}

LOAD_INSTANTIATE(layer_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(layer_component, ser20::iarchive_binary_t);
} // namespace unravel
