#include "camera_component.hpp"

#include <engine/meta/rendering/camera.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{
REFLECT(camera_component)
{
    // predicates for conditional GUI
    auto is_ortho = rttr::property_predicate(
        [](rttr::instance& i)
        {
            return i.try_convert<camera_component>()->get_projection_mode() == projection_mode::orthographic;;
        });

    auto is_perspective = rttr::property_predicate(
        [](rttr::instance& i)
        {
            return i.try_convert<camera_component>()->get_projection_mode() == projection_mode::perspective;;
        });

    rttr::registration::class_<camera_component>("camera_component")(rttr::metadata("category", "RENDERING"),
                                                                     rttr::metadata("pretty_name", "Camera"))
        .constructor<>()
        .method("component_exists", &component_exists<camera_component>)

        .property("projection_mode", &camera_component::get_projection_mode, &camera_component::set_projection_mode)(
            rttr::metadata("pretty_name", "Projection Mode"))
        .property("field_of_view", &camera_component::get_fov, &camera_component::set_fov)(
            rttr::metadata("pretty_name", "Field Of View"),
            rttr::metadata("min", 5.0f),
            rttr::metadata("max", 150.0f),
            rttr::metadata("predicate", is_perspective))
        .property("orthographic_size", &camera_component::get_ortho_size, &camera_component::set_ortho_size)(
            rttr::metadata("pretty_name", "Orthographic Size"),
            rttr::metadata("min", 0.1f),
            rttr::metadata("predicate", is_ortho),
            rttr::metadata("tooltip",
                           "This is half of the vertical size of the viewing volume.\n"
                           "Horizontal viewing size varies depending on viewport's aspect ratio.\n"
                           "Orthographic size is ignored when camera is not orthographic."))
        .property_readonly("pixels_per_unit", &camera_component::get_ppu)(
            rttr::metadata("pretty_name", "Pixels Per Unit"),
            rttr::metadata("tooltip", "Pixels per unit only usable in orthographic mode."))
        .property_readonly("viewport_size",
                           &camera_component::get_viewport_size)(rttr::metadata("pretty_name", "Viewport Size"))
        .property("near_clip_distance", &camera_component::get_near_clip, &camera_component::set_near_clip)(
            rttr::metadata("pretty_name", "Near Clip"),
            rttr::metadata("min", 0.1f))
        .property("far_clip_distance", &camera_component::get_far_clip, &camera_component::set_far_clip)(
            rttr::metadata("pretty_name", "Far Clip"));

    // Register camera_component class with entt
    entt::meta_factory<camera_component>{}
        .type("camera_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"category", "RENDERING"},
            entt::attribute{"pretty_name", "Camera"},
        })
        .func<&component_exists<camera_component>>("component_exists"_hs)
        .data<&camera_component::set_projection_mode, &camera_component::get_projection_mode>("projection_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Projection Mode"},
        })
        .data<&camera_component::set_fov, &camera_component::get_fov>("field_of_view"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Field Of View"},
            entt::attribute{"min", 5.0f},
            entt::attribute{"max", 150.0f},
            entt::attribute{"predicate", is_perspective}, 
        })
        .data<&camera_component::set_ortho_size, &camera_component::get_ortho_size>("orthographic_size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Orthographic Size"},
            entt::attribute{"min", 0.1f},
            entt::attribute{"tooltip", "This is half of the vertical size of the viewing volume.\nHorizontal viewing size varies depending on viewport's aspect ratio.\nOrthographic size is ignored when camera is not orthographic."},
            entt::attribute{"predicate", is_ortho}, 
        })
        .data<nullptr, &camera_component::get_ppu>("pixels_per_unit"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Pixels Per Unit"},
            entt::attribute{"tooltip", "Pixels per unit only usable in orthographic mode."},
        })
        .data<nullptr, &camera_component::get_viewport_size>("viewport_size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Viewport Size"},
        })
        .data<&camera_component::set_near_clip, &camera_component::get_near_clip>("near_clip_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Near Clip"},
            entt::attribute{"min", 0.1f},
        })
        .data<&camera_component::set_far_clip, &camera_component::get_far_clip>("far_clip_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Far Clip"},
        });
}

SAVE(camera_component)
{
    try_save(ar, ser20::make_nvp("camera", obj.get_camera()));
}
SAVE_INSTANTIATE(camera_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(camera_component, ser20::oarchive_binary_t);

LOAD(camera_component)
{
    try_load(ar, ser20::make_nvp("camera", obj.get_camera()));
}
LOAD_INSTANTIATE(camera_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(camera_component, ser20::iarchive_binary_t);
} // namespace unravel
