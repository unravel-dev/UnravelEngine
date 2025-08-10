#include "animation_component.hpp"
#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT(animation_component)
{
    rttr::registration::enumeration<animation_component::culling_mode>("animation_component::culling_mode")(
        rttr::value("Always Animate", animation_component::culling_mode::always_animate),
        rttr::value("Renderer Based", animation_component::culling_mode::renderer_based));

    rttr::registration::class_<animation_component>("animation_component")(rttr::metadata("category", "ANIMATION"),
                                                                           rttr::metadata("pretty_name", "Animation"))
        .constructor<>()()
        .method("component_exists", &component_exists<animation_component>)

        .property("animation", &animation_component::get_animation, &animation_component::set_animation)(
            rttr::metadata("pretty_name", "Animation"))
        .property("auto_play", &animation_component::get_autoplay, &animation_component::set_autoplay)(
            rttr::metadata("pretty_name", "Auto Play"),
            rttr::metadata("tooltip", "Controls whether the animation should auto start."))
        .property("culling_mode", &animation_component::get_culling_mode, &animation_component::set_culling_mode)(
            rttr::metadata("pretty_name", "Culling Mode"),
            rttr::metadata("tooltip", "Controls how the animation logic should be culled."))
        .property("apply_root_motion",
                  &animation_component::get_apply_root_motion,
                  &animation_component::set_apply_root_motion)(rttr::metadata("pretty_name", "Apply Root Motion"))
        .property("speed",
                  &animation_component::get_speed,
                  &animation_component::set_speed)(rttr::metadata("pretty_name", "Speed"),
                                                   rttr::metadata("tooltip", "Controls the playback speed of the animation. 1.0 = normal speed, 2.0 = double speed, 0.5 = half speed."),
                                                   rttr::metadata("min", 0.0f),
                                                   rttr::metadata("max", 10.0f));

    // Register animation_component::culling_mode enum with entt
    entt::meta_factory<animation_component::culling_mode>{}
        .type("culling_mode"_hs)
        .data<animation_component::culling_mode::always_animate>("always_animate"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Always Animate"},
        })
        .data<animation_component::culling_mode::renderer_based>("renderer_based"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Renderer Based"},
        });

    // Register animation_component class with entt
    entt::meta_factory<animation_component>{}
        .type("animation_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"category", "ANIMATION"},
            entt::attribute{"pretty_name", "Animation"},
        })
        .func<&component_exists<animation_component>>("component_exists"_hs)
        .data<&animation_component::set_animation, &animation_component::get_animation>("animation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Animation"},
        })
        .data<&animation_component::set_autoplay, &animation_component::get_autoplay>("auto_play"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Auto Play"},
            entt::attribute{"tooltip", "Controls whether the animation should auto start."},
        })
        .data<&animation_component::set_culling_mode, &animation_component::get_culling_mode>("culling_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Culling Mode"},
            entt::attribute{"tooltip", "Controls how the animation logic should be culled."},
        })
        .data<&animation_component::set_apply_root_motion, &animation_component::get_apply_root_motion>("apply_root_motion"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Apply Root Motion"},
        })
        .data<&animation_component::set_speed, &animation_component::get_speed>("speed"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Speed"},
            entt::attribute{"tooltip", "Controls the playback speed of the animation. 1.0 = normal speed, 2.0 = double speed, 0.5 = half speed."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 10.0f},
        });
}

SAVE(animation_component)
{
    try_save(ar, ser20::make_nvp("animation", obj.get_animation()));
    try_save(ar, ser20::make_nvp("auto_play", obj.get_autoplay()));
    try_save(ar, ser20::make_nvp("culling_mode", obj.get_culling_mode()));
    try_save(ar, ser20::make_nvp("apply_root_motion", obj.get_apply_root_motion()));
    try_save(ar, ser20::make_nvp("speed", obj.get_speed()));
}
SAVE_INSTANTIATE(animation_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(animation_component, ser20::oarchive_binary_t);

LOAD(animation_component)
{
    asset_handle<animation_clip> animation;
    if(try_load(ar, ser20::make_nvp("animation", animation)))
    {
        obj.set_animation(animation);
    }

    bool auto_play{};
    if(try_load(ar, ser20::make_nvp("auto_play", auto_play)))
    {
        obj.set_autoplay(auto_play);
    }

    animation_component::culling_mode culling_mode{};
    if(try_load(ar, ser20::make_nvp("culling_mode", culling_mode)))
    {
        obj.set_culling_mode(culling_mode);
    }

    bool apply_root_motion{};
    if(try_load(ar, ser20::make_nvp("apply_root_motion", apply_root_motion)))
    {
        obj.set_apply_root_motion(apply_root_motion);
    }

    float speed{1.0f};
    if(try_load(ar, ser20::make_nvp("speed", speed)))
    {
        obj.set_speed(speed);
    }
}
LOAD_INSTANTIATE(animation_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(animation_component, ser20::iarchive_binary_t);

} // namespace unravel
