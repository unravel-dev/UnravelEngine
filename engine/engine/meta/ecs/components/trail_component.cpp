#include "trail_component.hpp"

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

REFLECT(trail_component)
{
    // A setting shows only where it applies.
    const auto is_edge = entt::property_predicate<bool>(
        [](const entt::meta_any& object)
        {
            const auto* trail = object.try_cast<trail_component>();
            return trail != nullptr && trail->edge;
        });

    const dynamic_renderer_texts texts{"trail", "strip material, which shows the strip's colour", "A strip that faces the camera casts the shadow of the side the camera sees."};
    auto factory = entt::meta_factory<trail_component>{}
        .type("trail_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "trail_component"},
            entt::attribute{"category", "Rendering"},
            entt::attribute{"pretty_name", "Trail"},
        })
        .func<&component_meta<trail_component>::exists>("component_exists"_hs)
        .func<&component_meta<trail_component>::add>("component_add"_hs)
        .func<&component_meta<trail_component>::remove>("component_remove"_hs)
        .func<&component_meta<trail_component>::save>("component_save"_hs)
        .func<&component_meta<trail_component>::load>("component_load"_hs);
    factory = reflect_dynamic_renderer_material(factory, texts)
        .data<&trail_component::time>("time"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "time"},
            entt::attribute{"pretty_name", "Time"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "Seconds each point of the trail lives: how long the trail is in time."},
        })
        .data<&trail_component::min_distance>("min_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "min_distance"},
            entt::attribute{"pretty_name", "Min Distance"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "How far the entity moves, in metres, before the trail takes another point. Lower follows a curve more closely and builds more triangles."},
        })
        .data<&trail_component::emitting>("emitting"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "emitting"},
            entt::attribute{"pretty_name", "Emitting"},
            entt::attribute{"tooltip", "Whether the trail takes new points. Off, the points it has age out and the trail ends. Turned on again, it starts a stretch of its own that no strip joins to the one still fading."},
        })
        .data<&trail_component::edge>("edge"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "edge"},
            entt::attribute{"pretty_name", "Edge"},
            entt::attribute{"tooltip", "The trail is the sheet that Edge Base and Edge Tip sweep through space, such as a blade's swing. The strip's Subdivisions round it into an arc between frames. The edge's own length is the sheet's width, so the strip's Shape, Alignment, Width, Corner Vertices and Cap Vertices do not apply."},
        })
        .data<&trail_component::edge_base>("edge_base"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "edge_base"},
            entt::attribute{"pretty_name", "Edge Base"},
            entt::attribute{"predicate", is_edge},
            entt::attribute{"tooltip", "One end of the edge, in the entity's space: a blade's base."},
        })
        .data<&trail_component::edge_tip>("edge_tip"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "edge_tip"},
            entt::attribute{"pretty_name", "Edge Tip"},
            entt::attribute{"predicate", is_edge},
            entt::attribute{"tooltip", "The other end of the edge, in the entity's space: a blade's tip."},
        })
        .data<&trail_component::strip>("strip"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "strip"},
            entt::attribute{"pretty_name", "Strip"},
            entt::attribute{"tooltip", "How the strip is shaped, sized, coloured and textured along its length."},
        });
    reflect_dynamic_renderer_drawing(factory, texts);
}

SAVE(trail_component)
{
    save_dynamic_renderer_material(ar, obj);
    try_save(ar, ser20::make_nvp("time", obj.time));
    try_save(ar, ser20::make_nvp("min_distance", obj.min_distance));
    try_save(ar, ser20::make_nvp("emitting", obj.emitting));
    try_save(ar, ser20::make_nvp("edge", obj.edge));
    try_save(ar, ser20::make_nvp("edge_base", obj.edge_base));
    try_save(ar, ser20::make_nvp("edge_tip", obj.edge_tip));
    try_save(ar, ser20::make_nvp("strip", obj.strip));
    save_dynamic_renderer_drawing(ar, obj);
}
SAVE_INSTANTIATE(trail_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(trail_component, ser20::oarchive_binary_t);

LOAD(trail_component)
{
    load_dynamic_renderer_material(ar, obj);
    try_load(ar, ser20::make_nvp("time", obj.time));
    try_load(ar, ser20::make_nvp("min_distance", obj.min_distance));
    try_load(ar, ser20::make_nvp("emitting", obj.emitting));
    try_load(ar, ser20::make_nvp("edge", obj.edge));
    try_load(ar, ser20::make_nvp("edge_base", obj.edge_base));
    try_load(ar, ser20::make_nvp("edge_tip", obj.edge_tip));
    try_load(ar, ser20::make_nvp("strip", obj.strip));
    load_dynamic_renderer_drawing(ar, obj);
}
LOAD_INSTANTIATE(trail_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(trail_component, ser20::iarchive_binary_t);

} // namespace unravel
