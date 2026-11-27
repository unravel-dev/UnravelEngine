#include "camera.hpp"
#include <engine/meta/core/common/basetypes.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{
REFLECT(camera)
{
    auto is_ortho_predicate = entt::property_predicate<bool>(
        [](const entt::meta_any& i)
        {
            return i.try_cast<camera>()->get_projection_mode() == projection_mode::orthographic;
        });

    auto is_perspective_predicate = entt::property_predicate<bool>(
        [](const entt::meta_any& i)
        {
            return i.try_cast<camera>()->get_projection_mode() == projection_mode::perspective;
        });

    entt::meta_factory<projection_mode>{}
        .type("projection_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "projection_mode"},
            entt::attribute{"pretty_name", "Projection Mode"},
        })
        .data<projection_mode::perspective>("perspective"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "perspective"},
            entt::attribute{"pretty_name", "Perspective"},
        })
        .data<projection_mode::orthographic>("orthographic"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "orthographic"},
            entt::attribute{"pretty_name", "Orthographic"},
        });

    // Register camera with entt
    entt::meta_factory<camera>{}
        .type("camera"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "camera"},
            entt::attribute{"pretty_name", "Camera"},
        })
        .data<&camera::set_projection_mode, &camera::get_projection_mode>("projection_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "projection_mode"},
            entt::attribute{"pretty_name", "Projection Mode"},
        })
        .data<&camera::set_fov, &camera::get_fov>("field_of_view"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "field_of_view"},
            entt::attribute{"pretty_name", "Field Of View"},
            entt::attribute{"min", 5.0f},
            entt::attribute{"max", 150.0f},
            entt::attribute{"predicate", is_perspective_predicate},
        })
        .data<&camera::set_orthographic_size, &camera::get_ortho_size>("orthographic_size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "orthographic_size"},
            entt::attribute{"pretty_name", "Orthographic Size"},
            entt::attribute{"min", 0.1f},
            entt::attribute{"tooltip", "This is half of the vertical size of the viewing volume.\nHorizontal viewing size varies depending on viewport's aspect ratio.\nOrthographic size is ignored when camera is not orthographic."},
            entt::attribute{"predicate", is_ortho_predicate},
        })
        .data<nullptr, &camera::get_ppu>("pixels_per_unit"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "pixels_per_unit"},
            entt::attribute{"pretty_name", "Pixels Per Unit"},
            entt::attribute{"tooltip", "Pixels per unit only usable in orthographic mode."},
        })
        .data<nullptr, &camera::get_viewport_size>("viewport_size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "viewport_size"},
            entt::attribute{"pretty_name", "Viewport Size"},
        })
        .data<&camera::set_near_clip, &camera::get_near_clip>("near_clip"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "near_clip"},
            entt::attribute{"former_name", "near_clip_distance"},
            entt::attribute{"pretty_name", "Near Clip"},
            entt::attribute{"min", 0.001f},
        })
        .data<&camera::set_far_clip, &camera::get_far_clip>("far_clip"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "far_clip"},
            entt::attribute{"former_name", "far_clip_distance"},
            entt::attribute{"pretty_name", "Far Clip"},
        });
}

SAVE(camera)
{
    try_save(ar, ser20::make_nvp("projection_mode", obj.projection_mode_));
    try_save(ar, ser20::make_nvp("field_of_view", obj.fov_));
    try_save(ar, ser20::make_nvp("near_clip", obj.near_clip_));
    try_save(ar, ser20::make_nvp("far_clip", obj.far_clip_));
    try_save(ar, ser20::make_nvp("viewport_position", obj.viewport_pos_));
    try_save(ar, ser20::make_nvp("viewport_size", obj.viewport_size_));
    try_save(ar, ser20::make_nvp("orthographic_size", obj.ortho_size_));
    try_save(ar, ser20::make_nvp("aspect_ratio", obj.aspect_ratio_));
    try_save(ar, ser20::make_nvp("aspect_locked", obj.aspect_locked_));
    try_save(ar, ser20::make_nvp("frustum_locked", obj.frustum_locked_));
}
SAVE_INSTANTIATE(camera, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(camera, ser20::oarchive_binary_t);

LOAD(camera)
{
    try_load(ar, ser20::make_nvp("projection_mode", obj.projection_mode_));
    try_load(ar, ser20::make_nvp("field_of_view", obj.fov_));
    try_load(ar, ser20::make_nvp("near_clip", obj.near_clip_));
    try_load(ar, ser20::make_nvp("far_clip", obj.far_clip_));
    try_load(ar, ser20::make_nvp("viewport_position", obj.viewport_pos_));
    try_load(ar, ser20::make_nvp("viewport_size", obj.viewport_size_));
    try_load(ar, ser20::make_nvp("orthographic_size", obj.ortho_size_));
    try_load(ar, ser20::make_nvp("aspect_ratio", obj.aspect_ratio_));
    try_load(ar, ser20::make_nvp("aspect_locked", obj.aspect_locked_));
    try_load(ar, ser20::make_nvp("frustum_locked", obj.frustum_locked_));

    obj.projection_dirty_ = true;
    obj.frustum_dirty_ = true;
}
LOAD_INSTANTIATE(camera, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(camera, ser20::iarchive_binary_t);
} // namespace unravel
