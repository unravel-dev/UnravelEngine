#include "camera_component.hpp"

#include <engine/meta/rendering/camera.hpp>
#include <engine/meta/layers/layer_mask.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{
REFLECT(camera_component)
{
    entt::meta_factory<camera_component>{}
        .type("camera_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "camera_component"},
            entt::attribute{"category", "Rendering"},
            entt::attribute{"pretty_name", "Camera"},
        })
        .func<&component_meta<camera_component>::exists>("component_exists"_hs)
        .func<&component_meta<camera_component>::add>("component_add"_hs)
        .func<&component_meta<camera_component>::save>("component_save"_hs)
        .func<&component_meta<camera_component>::load>("component_load"_hs)
        .func<&component_meta<camera_component>::remove>("component_remove"_hs)
        .data<&camera_component::set_camera,
              static_cast<const camera& (camera_component::*)() const>(&camera_component::get_camera)>("camera"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "camera"},
            entt::attribute{"pretty_name", "Camera"},
            // The projection settings are the component's own rows, not a "Camera" fold inside the Camera component.
            entt::attribute{"flattable", true},
        })
        .data<&camera_component::set_render_include_mask, &camera_component::get_render_include_mask>("include_layers"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "include_layers"},
            entt::attribute{"pretty_name", "Include Layers"},
            entt::attribute{"tooltip", "Layers to include when rendering."},
        })
        .data<&camera_component::set_render_exclude_mask, &camera_component::get_render_exclude_mask>("exclude_layers"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "exclude_layers"},
            entt::attribute{"pretty_name", "Exclude Layers"},
            entt::attribute{"tooltip", "Layers to exclude when rendering."},
        })
        .data<nullptr, &camera_component::get_render_mask>("render_layers"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "render_layers"},
            entt::attribute{"pretty_name", "Render Layers"},
            entt::attribute{"tooltip", "Layers (Include - Exclude) used when rendering."},
        });
}

// The camera saves its own settings in its node; the component saves the layers, which are its own.
SAVE(camera_component)
{
    try_save(ar, ser20::make_nvp("camera", obj.get_camera()));
    try_save(ar, ser20::make_nvp("include_layers", obj.get_render_include_mask()));
    try_save(ar, ser20::make_nvp("exclude_layers", obj.get_render_exclude_mask()));
}
SAVE_INSTANTIATE(camera_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(camera_component, ser20::oarchive_binary_t);

namespace
{
/// Loads one layer row of the camera: the mask a document holds as @p key is read over the component's own, as the
/// row @p name, and set when the document has it.
template<typename Archive>
void load_camera_layer_row(Archive& ar,
                           camera_component& obj,
                           const char* name,
                           const char* key,
                           layer_mask (camera_component::*get)() const,
                           void (camera_component::*set)(layer_mask))
{
    layer_mask mask = (obj.*get)();
    if(try_load_former(ar, name, ser20::make_nvp(key, mask)))
    {
        (obj.*set)(mask);
    }
}

/// Loads the layer rows, from the keys they had before and then from the ones they are saved under.
template<typename Archive>
void load_camera_layer_rows(Archive& ar, camera_component& obj)
{
    using component = camera_component;
    const auto get_include = &component::get_render_include_mask;
    const auto set_include = &component::set_render_include_mask;
    const auto get_exclude = &component::get_render_exclude_mask;
    const auto set_exclude = &component::set_render_exclude_mask;
    load_camera_layer_row(ar, obj, "include_layers", "render_include_layers", get_include, set_include);
    load_camera_layer_row(ar, obj, "exclude_layers", "render_exclude_layers", get_exclude, set_exclude);
    load_camera_layer_row(ar, obj, "include_layers", "include_layers", get_include, set_include);
    load_camera_layer_row(ar, obj, "exclude_layers", "exclude_layers", get_exclude, set_exclude);
}
} // namespace

LOAD(camera_component)
{
    try_load(ar, ser20::make_nvp("camera", obj.get_camera()));
    load_camera_layer_rows(ar, obj);
}
LOAD_INSTANTIATE(camera_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(camera_component, ser20::iarchive_binary_t);
} // namespace unravel
