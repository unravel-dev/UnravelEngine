#include "asset_importer_meta.hpp"

#include <engine/meta/core/common/basetypes.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/map.hpp>

namespace unravel
{

REFLECT(animation_importer_meta)
{
    rttr::registration::class_<animation_importer_meta::root_motion_meta>("root_motion_meta")
        .property("keep_position_y", &animation_importer_meta::root_motion_meta::keep_position_y)(
            rttr::metadata("pretty_name", "Keep Position Y"))
        .property("keep_position_xz", &animation_importer_meta::root_motion_meta::keep_position_xz)(
            rttr::metadata("pretty_name", "Keep Position XZ"))
        .property("keep_rotation", &animation_importer_meta::root_motion_meta::keep_rotation)(
            rttr::metadata("pretty_name", "Keep Rotation"))
        .property("keep_in_place", &animation_importer_meta::root_motion_meta::keep_in_place)(
        rttr::metadata("pretty_name", "Keep In Place"));

    rttr::registration::class_<animation_importer_meta>("animation_importer_meta")
        .property("root_motion", &animation_importer_meta::root_motion)(rttr::metadata("pretty_name", "Root Motion"));

    // Register root_motion_meta with entt
    entt::meta_factory<animation_importer_meta::root_motion_meta>{}
        .type("root_motion_meta"_hs)
        .data<&animation_importer_meta::root_motion_meta::keep_position_y>("keep_position_y"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Keep Position Y"},
        })
        .data<&animation_importer_meta::root_motion_meta::keep_position_xz>("keep_position_xz"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Keep Position XZ"},
        })
        .data<&animation_importer_meta::root_motion_meta::keep_rotation>("keep_rotation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Keep Rotation"},
        })
        .data<&animation_importer_meta::root_motion_meta::keep_in_place>("keep_in_place"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Keep In Place"},
        });

    // Register animation_importer_meta with entt
    entt::meta_factory<animation_importer_meta>{}
        .type("animation_importer_meta"_hs)
        .data<&animation_importer_meta::root_motion>("root_motion"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Root Motion"},
        });
}

SAVE(animation_importer_meta::root_motion_meta)
{
    try_save(ar, ser20::make_nvp("keep_position_y", obj.keep_position_y));
    try_save(ar, ser20::make_nvp("keep_position_xz", obj.keep_position_xz));
    try_save(ar, ser20::make_nvp("keep_rotation", obj.keep_rotation));

    try_save(ar, ser20::make_nvp("keep_in_place", obj.keep_in_place));
}
SAVE_INSTANTIATE(animation_importer_meta::root_motion_meta, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(animation_importer_meta::root_motion_meta, ser20::oarchive_binary_t);

LOAD(animation_importer_meta::root_motion_meta)
{
    try_load(ar, ser20::make_nvp("keep_position_y", obj.keep_position_y));
    try_load(ar, ser20::make_nvp("keep_position_xz", obj.keep_position_xz));
    try_load(ar, ser20::make_nvp("keep_rotation", obj.keep_rotation));

    try_load(ar, ser20::make_nvp("keep_in_place", obj.keep_in_place));
}
LOAD_INSTANTIATE(animation_importer_meta::root_motion_meta, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(animation_importer_meta::root_motion_meta, ser20::iarchive_binary_t);

SAVE(animation_importer_meta)
{
    try_save(ar, ser20::make_nvp("base_type", ser20::base_class<asset_importer_meta>(&obj)));
    try_save(ar, ser20::make_nvp("root_motion", obj.root_motion));
}
SAVE_INSTANTIATE(animation_importer_meta, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(animation_importer_meta, ser20::oarchive_binary_t);

LOAD(animation_importer_meta)
{
    try_load(ar, ser20::make_nvp("base_type", ser20::base_class<asset_importer_meta>(&obj)));
    try_load(ar, ser20::make_nvp("root_motion", obj.root_motion));
}
LOAD_INSTANTIATE(animation_importer_meta, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(animation_importer_meta, ser20::iarchive_binary_t);

} // namespace unravel
