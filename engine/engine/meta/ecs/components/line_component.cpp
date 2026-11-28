#include "line_component.hpp"

#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/core/common/basetypes.hpp>
#include <engine/meta/core/math/vector.hpp>
#include <engine/meta/rendering/dynamic_renderer.hpp>
#include <engine/meta/rendering/strip_style.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/vector.hpp>

namespace unravel
{

REFLECT(line_component)
{
    const dynamic_renderer_texts texts{"line", "strip material, which shows the strip's colour", "A strip that faces the camera casts the shadow of the side the camera sees."};
    auto factory = entt::meta_factory<line_component>{}
        .type("line_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "line_component"},
            entt::attribute{"category", "Rendering"},
            entt::attribute{"pretty_name", "Line"},
        })
        .func<&component_meta<line_component>::exists>("component_exists"_hs)
        .func<&component_meta<line_component>::add>("component_add"_hs)
        .func<&component_meta<line_component>::remove>("component_remove"_hs)
        .func<&component_meta<line_component>::save>("component_save"_hs)
        .func<&component_meta<line_component>::load>("component_load"_hs);
    factory = reflect_dynamic_renderer_material(factory, texts)
        .data<&line_component::points>("points"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "points"},
            entt::attribute{"pretty_name", "Points"},
            entt::attribute{"tooltip", "The points the line runs through, in order. A script moves them for a laser, a rope or an aim arc."},
        })
        .data<&line_component::world_space>("world_space"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "world_space"},
            entt::attribute{"pretty_name", "World Space"},
            entt::attribute{"tooltip", "The points are in the world's space, so the line stays where it is when the entity moves. Off, they are in the entity's space and move with it."},
        })
        .data<&line_component::loop>("loop"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "loop"},
            entt::attribute{"pretty_name", "Loop"},
            entt::attribute{"tooltip", "The last point joins the first."},
        })
        .data<&line_component::strip>("strip"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "strip"},
            entt::attribute{"pretty_name", "Strip"},
            entt::attribute{"tooltip", "How the strip is shaped, sized, coloured and textured along its length."},
        });
    reflect_dynamic_renderer_drawing(factory, texts);
}

SAVE(line_component)
{
    save_dynamic_renderer_material(ar, obj);
    try_save(ar, ser20::make_nvp("points", obj.points));
    try_save(ar, ser20::make_nvp("world_space", obj.world_space));
    try_save(ar, ser20::make_nvp("loop", obj.loop));
    try_save(ar, ser20::make_nvp("strip", obj.strip));
    save_dynamic_renderer_drawing(ar, obj);
}
SAVE_INSTANTIATE(line_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(line_component, ser20::oarchive_binary_t);

LOAD(line_component)
{
    load_dynamic_renderer_material(ar, obj);
    try_load(ar, ser20::make_nvp("points", obj.points));
    try_load(ar, ser20::make_nvp("world_space", obj.world_space));
    try_load(ar, ser20::make_nvp("loop", obj.loop));
    try_load(ar, ser20::make_nvp("strip", obj.strip));
    load_dynamic_renderer_drawing(ar, obj);
}
LOAD_INSTANTIATE(line_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(line_component, ser20::iarchive_binary_t);

} // namespace unravel
