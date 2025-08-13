#include "layer_mask.hpp"

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

#include <serialization/types/bitset.hpp>

namespace unravel
{
REFLECT(layer_mask)
{
    rttr::registration::class_<layer_mask>("layer_mask")(rttr::metadata("pretty_name", "Layer Mask"))
        .constructor<>()()
        .property_readonly("mask",
                           &layer_mask::mask)(rttr::metadata("pretty_name", "Mask"));

    // Register layer_mask with entt
    entt::meta_factory<layer_mask>{}
        .type("layer_mask"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "layer_mask"},
            entt::attribute{"pretty_name", "Layer Mask"},
        })
        .data<nullptr, &layer_mask::mask>("mask"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mask"},
            entt::attribute{"pretty_name", "Mask"},
        });
}

SAVE(layer_mask)
{
    try_save(ar, ser20::make_nvp("mask", obj.mask));
}
SAVE_INSTANTIATE(layer_mask, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(layer_mask, ser20::oarchive_binary_t);

LOAD(layer_mask)
{
    try_load(ar, ser20::make_nvp("mask", obj.mask));
}

LOAD_INSTANTIATE(layer_mask, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(layer_mask, ser20::iarchive_binary_t);
} // namespace unravel
