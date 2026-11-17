#include "decal_component.hpp"

#include <engine/rendering/material_graph/material_graph.h>
#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/core/math/vector.hpp>
#include <engine/meta/layers/layer_mask.hpp>
#include <engine/meta/rendering/material.hpp>
#include <engine/meta/rendering/material_instance.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT(decal_component)
{
    entt::meta_factory<decal_component>{}
        .type("decal_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "decal_component"},
            entt::attribute{"category", "Rendering"},
            entt::attribute{"pretty_name", "Decal"},
        })
        .func<&component_meta<decal_component>::exists>("component_exists"_hs)
        .func<&component_meta<decal_component>::add>("component_add"_hs)
        .func<&component_meta<decal_component>::remove>("component_remove"_hs)
        .func<&component_meta<decal_component>::save>("component_save"_hs)
        .func<&component_meta<decal_component>::load>("component_load"_hs)
        .data<&decal_component::set_material, &decal_component::get_material>("material"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "material"},
            entt::attribute{"pretty_name", "Material"},
            entt::attribute{"material_domain", get_material_domain_key(material_domain::decal)},
            entt::attribute{"tooltip", "The Decal material the box lays onto the surfaces inside it. Create one in the Content "
                                       "Browser (Create > Decal Material); a material of another domain draws nothing."},
        })
        .data<&decal_component::set_material_instance,
              static_cast<const material_instance& (decal_component::*)() const>(&decal_component::get_material_instance)>(
            "material_overrides"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "material_overrides"},
            entt::attribute{"pretty_name", "Material Overrides"},
            entt::attribute{"tooltip", "This decal's own values of its material's parameters and textures, such as a tint or an "
                                       "atlas frame. A value it does not override follows the material."},
        })
        .data<&decal_component::size>("size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size"},
            entt::attribute{"pretty_name", "Size"},
            entt::attribute{"min", 0.001f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "Width and height of the projected face, in metres before the entity's scale."},
        })
        .data<&decal_component::depth>("depth"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "depth"},
            entt::attribute{"pretty_name", "Depth"},
            entt::attribute{"min", 0.001f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "How far the box reaches along the direction it projects in, in metres before the "
                                       "entity's scale. Keep it shallow so the decal stays on the surface it is placed on."},
        })
        .data<&decal_component::opacity>("opacity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "opacity"},
            entt::attribute{"pretty_name", "Opacity"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Scales the material's Opacity: 0 hides the decal. Animate it from a script to fade a "
                                       "decal in or out."},
        })
        .data<&decal_component::sort_order>("sort_order"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sort_order"},
            entt::attribute{"pretty_name", "Sort Order"},
            entt::attribute{"tooltip", "Where decals overlap, a higher Sort Order draws over a lower one. Equal orders draw by "
                                       "material, then in a fixed order, so they never flicker."},
        })
        .data<&decal_component::receiver_layers>("receiver_layers"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "receiver_layers"},
            entt::attribute{"pretty_name", "Receiver Layers"},
            entt::attribute{"tooltip", "The layers of the models the decal lays onto: a model whose entity is on none of "
                                       "them keeps none of it. Everything, the default, leaves the choice to each model's "
                                       "Receive Decals."},
        })
        .data<&decal_component::angle_fade>("angle_fade"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "angle_fade"},
            entt::attribute{"pretty_name", "Angle Fade"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 180.0f},
            entt::attribute{"step", 1.0f},
            entt::attribute{"tooltip", "Degrees between a surface and the direction back toward the projector where the decal "
                                       "starts to fade (X) and is gone (Y). Surfaces turned further away receive nothing, so "
                                       "the decal never stretches down a wall or shows through a thin object."},
        })
        .data<&decal_component::fade_screen_size>("fade_screen_size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fade_screen_size"},
            entt::attribute{"pretty_name", "Fade Screen Size"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.001f},
            entt::attribute{"tooltip", "Below this share of the view's height the decal fades out and is not drawn: small decals "
                                       "far away cost nothing, large ones stay."},
        })
        .data<&decal_component::max_distance>("max_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_distance"},
            entt::attribute{"pretty_name", "Max Distance"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 1.0f},
            entt::attribute{"tooltip", "Beyond this distance from the camera, in metres, the decal is not drawn. 0 sets no "
                                       "limit."},
        })
        .data<&decal_component::lifetime>("lifetime"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lifetime"},
            entt::attribute{"pretty_name", "Lifetime"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"tooltip", "In play, the seconds the decal lasts after it has faded in, before it fades out. 0 "
                                       "keeps it."},
        })
        .data<&decal_component::fade_in>("fade_in"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fade_in"},
            entt::attribute{"pretty_name", "Fade In"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "In play, the seconds the decal takes to appear."},
        })
        .data<&decal_component::fade_out>("fade_out"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fade_out"},
            entt::attribute{"pretty_name", "Fade Out"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "In play, the seconds the decal takes to disappear once its Lifetime is over."},
        })
        .data<&decal_component::remove_after_fade>("remove_after_fade"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "remove_after_fade"},
            entt::attribute{"pretty_name", "Remove After Fade"},
            entt::attribute{"tooltip", "In play, the entity is destroyed once the decal has faded out."},
        });
}

SAVE(decal_component)
{
    try_save(ar, ser20::make_nvp("material", obj.get_material()));
    try_save(ar, ser20::make_nvp("material_overrides", obj.get_material_instance()));
    try_save(ar, ser20::make_nvp("size", obj.size));
    try_save(ar, ser20::make_nvp("depth", obj.depth));
    try_save(ar, ser20::make_nvp("opacity", obj.opacity));
    try_save(ar, ser20::make_nvp("sort_order", obj.sort_order));
    try_save(ar, ser20::make_nvp("receiver_layers", obj.receiver_layers));
    try_save(ar, ser20::make_nvp("angle_fade", obj.angle_fade));
    try_save(ar, ser20::make_nvp("fade_screen_size", obj.fade_screen_size));
    try_save(ar, ser20::make_nvp("max_distance", obj.max_distance));
    try_save(ar, ser20::make_nvp("lifetime", obj.lifetime));
    try_save(ar, ser20::make_nvp("fade_in", obj.fade_in));
    try_save(ar, ser20::make_nvp("fade_out", obj.fade_out));
    try_save(ar, ser20::make_nvp("remove_after_fade", obj.remove_after_fade));
}
SAVE_INSTANTIATE(decal_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(decal_component, ser20::oarchive_binary_t);

LOAD(decal_component)
{
    asset_handle<material> material = obj.get_material();
    if(try_load(ar, ser20::make_nvp("material", material)))
    {
        obj.set_material(material);
    }
    // The overrides load onto an instance of the material just read, which tells their types.
    material_instance overrides = obj.get_material_instance();
    if(try_load(ar, ser20::make_nvp("material_overrides", overrides)))
    {
        obj.set_material_instance(overrides);
    }
    try_load(ar, ser20::make_nvp("size", obj.size));
    try_load(ar, ser20::make_nvp("depth", obj.depth));
    try_load(ar, ser20::make_nvp("opacity", obj.opacity));
    try_load(ar, ser20::make_nvp("sort_order", obj.sort_order));
    try_load(ar, ser20::make_nvp("receiver_layers", obj.receiver_layers));
    try_load(ar, ser20::make_nvp("angle_fade", obj.angle_fade));
    try_load(ar, ser20::make_nvp("fade_screen_size", obj.fade_screen_size));
    try_load(ar, ser20::make_nvp("max_distance", obj.max_distance));
    try_load(ar, ser20::make_nvp("lifetime", obj.lifetime));
    try_load(ar, ser20::make_nvp("fade_in", obj.fade_in));
    try_load(ar, ser20::make_nvp("fade_out", obj.fade_out));
    try_load(ar, ser20::make_nvp("remove_after_fade", obj.remove_after_fade));
}
LOAD_INSTANTIATE(decal_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(decal_component, ser20::iarchive_binary_t);

} // namespace unravel
