#include "camera_component.hpp"

#include <engine/meta/rendering/camera.hpp>
#include <engine/meta/layers/layer_mask.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{
REFLECT(camera_component)
{
    auto is_ortho_predicate = entt::property_predicate(
        [](const entt::meta_any& i)    
        {
            return i.try_cast<camera_component>()->get_projection_mode() == projection_mode::orthographic;
        });

    auto is_perspective_predicate = entt::property_predicate(
        [](const entt::meta_any& i)
        {
            return i.try_cast<camera_component>()->get_projection_mode() == projection_mode::perspective;
        });

        entt::meta_factory<camera_component>{}
        .type("camera_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "camera_component"},
            entt::attribute{"category", "RENDERING"},
            entt::attribute{"pretty_name", "Camera"},
        })
        .func<&component_exists<camera_component>>("component_exists"_hs)
        .func<&component_add<camera_component>>("component_add"_hs)
        .func<&component_remove<camera_component>>("component_remove"_hs)
        .data<&camera_component::set_projection_mode, &camera_component::get_projection_mode>("projection_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "projection_mode"},
            entt::attribute{"pretty_name", "Projection Mode"},
        })
        .data<&camera_component::set_fov, &camera_component::get_fov>("field_of_view"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "field_of_view"},
            entt::attribute{"pretty_name", "Field Of View"},
            entt::attribute{"min", 5.0f},
            entt::attribute{"max", 150.0f},
            entt::attribute{"predicate", is_perspective_predicate}, 
        })
        .data<&camera_component::set_ortho_size, &camera_component::get_ortho_size>("orthographic_size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "orthographic_size"},
            entt::attribute{"pretty_name", "Orthographic Size"},
            entt::attribute{"min", 0.1f},
            entt::attribute{"tooltip", "This is half of the vertical size of the viewing volume.\nHorizontal viewing size varies depending on viewport's aspect ratio.\nOrthographic size is ignored when camera is not orthographic."},
            entt::attribute{"predicate", is_ortho_predicate}, 
        })
        .data<nullptr, &camera_component::get_ppu>("pixels_per_unit"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "pixels_per_unit"},
            entt::attribute{"pretty_name", "Pixels Per Unit"},
            entt::attribute{"tooltip", "Pixels per unit only usable in orthographic mode."},
        })
        .data<nullptr, &camera_component::get_viewport_size>("viewport_size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "viewport_size"},
            entt::attribute{"pretty_name", "Viewport Size"},
        })
        .data<&camera_component::set_near_clip, &camera_component::get_near_clip>("near_clip_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "near_clip_distance"},
            entt::attribute{"pretty_name", "Near Clip"},
            entt::attribute{"min", 0.1f},
        })
        .data<&camera_component::set_far_clip, &camera_component::get_far_clip>("far_clip_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "far_clip_distance"},
            entt::attribute{"pretty_name", "Far Clip"},
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

SAVE(camera_component)
{
    try_save(ar, ser20::make_nvp("camera", obj.get_camera()));
    try_save(ar, ser20::make_nvp("render_include_layers", obj.get_render_include_mask()));
    try_save(ar, ser20::make_nvp("render_exclude_layers", obj.get_render_exclude_mask()));
}
SAVE_INSTANTIATE(camera_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(camera_component, ser20::oarchive_binary_t);

LOAD(camera_component)
{
    try_load(ar, ser20::make_nvp("camera", obj.get_camera()));
    
    layer_mask render_include_layers{layer_reserved::everything_layer};
    if(try_load(ar, ser20::make_nvp("render_include_layers", render_include_layers)))
    {
        obj.set_render_include_mask(render_include_layers);
    }
    
    layer_mask render_exclude_layers{layer_reserved::nothing_layer};
    if(try_load(ar, ser20::make_nvp("render_exclude_layers", render_exclude_layers)))
    {
        obj.set_render_exclude_mask(render_exclude_layers);
    }
}
LOAD_INSTANTIATE(camera_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(camera_component, ser20::iarchive_binary_t);
} // namespace unravel
