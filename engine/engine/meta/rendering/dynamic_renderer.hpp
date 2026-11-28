#pragma once

#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/rendering/material.hpp>
#include <engine/meta/rendering/material_instance.hpp>
#include <engine/rendering/dynamic_geometry/dynamic_renderer.h>
#include <engine/rendering/material_graph/material_graph.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

#include <string>

namespace unravel
{

/// How the rows a Line and a Trail share (dynamic_renderer) are worded for one of them.
struct dynamic_renderer_texts
{
    /// The component in a sentence: "line".
    std::string noun;
    /// The engine's own material for it and what that shows: "strip material, which shows the strip's colour".
    std::string default_material;
    /// What else its Casts Shadow tooltip says, as a sentence.
    std::string shadow_note;
};

/// Adds the rows a renderer shows before its own: Material and Material Overrides.
template<typename T>
auto reflect_dynamic_renderer_material(entt::meta_factory<T> factory, const dynamic_renderer_texts& texts) -> entt::meta_factory<T>
{
    return factory.template data<&T::set_material, &T::get_material>("material"_hs)
        .template custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "material"},
            entt::attribute{"pretty_name", "Material"},
            entt::attribute{"material_domain", get_material_domain_key(material_domain::surface)},
            entt::attribute{"tooltip",
                            "The Surface material the " + texts.noun +
                                " draws with. An Opaque or Dithered one is lit as any surface, and a see-through one sorts with "
                                "the particles and the see-through models. Unset, it draws the engine's " +
                                texts.default_material + "."},
        })
        .template data<&T::set_material_instance, &T::get_material_instance>("material_overrides"_hs)
        .template custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "material_overrides"},
            entt::attribute{"pretty_name", "Material Overrides"},
            entt::attribute{"tooltip",
                            "The " + texts.noun +
                                "'s own values of its material's parameters and textures, such as its texture or a tint. A value "
                                "it does not override follows the material."},
        });
}

/// Adds the rows a renderer shows after its own: Casts Shadow and Sort Priority.
template<typename T>
auto reflect_dynamic_renderer_drawing(entt::meta_factory<T> factory, const dynamic_renderer_texts& texts) -> entt::meta_factory<T>
{
    return factory.template data<&T::casts_shadow>("casts_shadow"_hs)
        .template custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "casts_shadow"},
            entt::attribute{"pretty_name", "Casts Shadow"},
            entt::attribute{"tooltip",
                            "The " + texts.noun +
                                " casts shadows when its material casts them: an Opaque material, or a see-through one whose Cast "
                                "Shadow is Dithered. " +
                                texts.shadow_note},
        })
        .template data<&T::sort_priority>("sort_priority"_hs)
        .template custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sort_priority"},
            entt::attribute{"pretty_name", "Sort Priority"},
            entt::attribute{"tooltip",
                            "Where a see-through material draws the " + texts.noun +
                                " among the particles and the see-through models: lower first, then far to near."},
        });
}

/// Saves the material a renderer draws with and its overrides of it: the first keys of its component.
template<typename Archive>
void save_dynamic_renderer_material(Archive& ar, const dynamic_renderer& renderer)
{
    try_save(ar, ser20::make_nvp("material", renderer.get_material()));
    try_save(ar, ser20::make_nvp("material_overrides", renderer.get_material_instance()));
}

/// Loads what save_dynamic_renderer_material saved.
template<typename Archive>
void load_dynamic_renderer_material(Archive& ar, dynamic_renderer& renderer)
{
    asset_handle<material> shared = renderer.get_material();
    if(try_load(ar, ser20::make_nvp("material", shared)))
    {
        renderer.set_material(shared);
    }
    // The overrides load onto an instance of the material just read, which tells their types.
    material_instance overrides = renderer.get_material_instance();
    if(try_load(ar, ser20::make_nvp("material_overrides", overrides)))
    {
        renderer.set_material_instance(overrides);
    }
}

/// Saves how a renderer draws beside its material: the last keys of its component.
template<typename Archive>
void save_dynamic_renderer_drawing(Archive& ar, const dynamic_renderer& renderer)
{
    try_save(ar, ser20::make_nvp("casts_shadow", renderer.casts_shadow));
    try_save(ar, ser20::make_nvp("sort_priority", renderer.sort_priority));
}

/// Loads what save_dynamic_renderer_drawing saved.
template<typename Archive>
void load_dynamic_renderer_drawing(Archive& ar, dynamic_renderer& renderer)
{
    try_load(ar, ser20::make_nvp("casts_shadow", renderer.casts_shadow));
    try_load(ar, ser20::make_nvp("sort_priority", renderer.sort_priority));
}

} // namespace unravel
