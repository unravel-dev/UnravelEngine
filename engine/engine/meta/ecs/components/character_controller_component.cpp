#include "character_controller_component.hpp"
#include <engine/meta/core/math/vector.hpp>
#include <engine/meta/layers/layer_mask.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT(character_controller_component)
{
    entt::meta_factory<character_controller_component>{}
        .type("character_controller_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "character_controller_component"},
            entt::attribute{"category", "PHYSICS"},
            entt::attribute{"pretty_name", "Character Controller"},
        })
        .func<&component_meta<character_controller_component>::exists>("component_exists"_hs)
        .func<&component_meta<character_controller_component>::add>("component_add"_hs)
        .func<&component_meta<character_controller_component>::remove>("component_remove"_hs)
        .func<&component_meta<character_controller_component>::save>("component_save"_hs)
        .func<&component_meta<character_controller_component>::load>("component_load"_hs)
        .data<&character_controller_component::set_radius, &character_controller_component::get_radius>("radius"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "radius"},
            entt::attribute{"pretty_name", "Radius"},
            entt::attribute{"tooltip", "The radius of the character capsule."},
            entt::attribute{"min", 0.01f},
            entt::attribute{"step", 0.1f},
        })
        .data<&character_controller_component::set_height, &character_controller_component::get_height>("height"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "height"},
            entt::attribute{"pretty_name", "Height"},
            entt::attribute{"tooltip", "The total height of the character capsule."},
            entt::attribute{"min", 0.01f},
            entt::attribute{"step", 0.1f},
        })
        .data<&character_controller_component::set_center, &character_controller_component::get_center>("center"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "center"},
            entt::attribute{"pretty_name", "Center"},
            entt::attribute{"tooltip", "The center offset of the capsule relative to the entity transform."},
        })
        .data<&character_controller_component::set_step_height, &character_controller_component::get_step_height>("step_height"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "step_height"},
            entt::attribute{"pretty_name", "Step Offset"},
            entt::attribute{"tooltip", "Maximum height of obstacles the character can step over."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.05f},
        })
        .data<&character_controller_component::set_slope_limit, &character_controller_component::get_slope_limit>("slope_limit"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "slope_limit"},
            entt::attribute{"pretty_name", "Slope Limit"},
            entt::attribute{"tooltip", "Maximum slope angle in degrees the character can walk up."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 90.0f},
            entt::attribute{"step", 1.0f},
        })
        .data<&character_controller_component::set_skin_width, &character_controller_component::get_skin_width>("skin_width"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "skin_width"},
            entt::attribute{"pretty_name", "Skin Width"},
            entt::attribute{"tooltip", "Collision skin width around the character capsule."},
            entt::attribute{"min", 0.001f},
            entt::attribute{"step", 0.01f},
        })
        .data<&character_controller_component::set_gravity_scale, &character_controller_component::get_gravity_scale>("gravity_scale"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gravity_scale"},
            entt::attribute{"pretty_name", "Gravity Scale"},
            entt::attribute{"tooltip", "Multiplier for world gravity applied to this controller."},
            entt::attribute{"step", 0.1f},
        })
        .data<&character_controller_component::set_collision_include_mask, &character_controller_component::get_collision_include_mask>("include_layers"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "include_layers"},
            entt::attribute{"pretty_name", "Include Layers"},
            entt::attribute{"tooltip", "Layers to include when producing collisions."},
        })
        .data<&character_controller_component::set_collision_exclude_mask, &character_controller_component::get_collision_exclude_mask>("exclude_layers"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "exclude_layers"},
            entt::attribute{"pretty_name", "Exclude Layers"},
            entt::attribute{"tooltip", "Layers to exclude when producing collisions."},
        })
        .data<nullptr, &character_controller_component::get_collision_mask>("collision_layers"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "collision_layers"},
            entt::attribute{"pretty_name", "Collision Layers"},
            entt::attribute{"tooltip", "Layers (Include - Exclude) used when producing collisions."},
        })
        .data<&character_controller_component::set_terminal_velocity, &character_controller_component::get_terminal_velocity>("terminal_velocity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "terminal_velocity"},
            entt::attribute{"pretty_name", "Terminal Velocity"},
            entt::attribute{"tooltip", "Maximum downward fall speed in m/s. Default 55 (skydiver terminal velocity)."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 1.0f},
        })
        .data<&character_controller_component::set_linear_damping, &character_controller_component::get_linear_damping>("linear_damping"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "linear_damping"},
            entt::attribute{"pretty_name", "Linear Damping"},
            entt::attribute{"tooltip", "Damping applied to linear velocity each step. 0 = no damping, 1 = full damping."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.05f},
        })
        .data<nullptr, &character_controller_component::is_grounded>("is_grounded"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "is_grounded"},
            entt::attribute{"pretty_name", "Is Grounded"},
        })
        .data<nullptr, &character_controller_component::can_jump>("can_jump"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "can_jump"},
            entt::attribute{"pretty_name", "Can Jump"},
        })
        .data<nullptr, &character_controller_component::get_velocity>("velocity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "velocity"},
            entt::attribute{"pretty_name", "Velocity"},
        });
}

SAVE(character_controller_component)
{
    try_save(ar, ser20::make_nvp("radius", obj.get_radius()));
    try_save(ar, ser20::make_nvp("height", obj.get_height()));
    try_save(ar, ser20::make_nvp("center", obj.get_center()));
    try_save(ar, ser20::make_nvp("step_height", obj.get_step_height()));
    try_save(ar, ser20::make_nvp("slope_limit", obj.get_slope_limit()));
    try_save(ar, ser20::make_nvp("skin_width", obj.get_skin_width()));
    try_save(ar, ser20::make_nvp("gravity_scale", obj.get_gravity_scale()));
    try_save(ar, ser20::make_nvp("include_layers", obj.get_collision_include_mask()));
    try_save(ar, ser20::make_nvp("exclude_layers", obj.get_collision_exclude_mask()));
    try_save(ar, ser20::make_nvp("terminal_velocity", obj.get_terminal_velocity()));
    try_save(ar, ser20::make_nvp("linear_damping", obj.get_linear_damping()));
}
SAVE_INSTANTIATE(character_controller_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(character_controller_component, ser20::oarchive_binary_t);

LOAD(character_controller_component)
{
    float radius{0.5f};
    if(try_load(ar, ser20::make_nvp("radius", radius)))
    {
        obj.set_radius(radius);
    }
    float height{2.0f};
    if(try_load(ar, ser20::make_nvp("height", height)))
    {
        obj.set_height(height);
    }
    math::vec3 center{};
    if(try_load(ar, ser20::make_nvp("center", center)))
    {
        obj.set_center(center);
    }
    float step_height{0.3f};
    if(try_load(ar, ser20::make_nvp("step_height", step_height)))
    {
        obj.set_step_height(step_height);
    }
    float slope_limit{45.0f};
    if(try_load(ar, ser20::make_nvp("slope_limit", slope_limit)))
    {
        obj.set_slope_limit(slope_limit);
    }
    float skin_width{0.08f};
    if(try_load(ar, ser20::make_nvp("skin_width", skin_width)))
    {
        obj.set_skin_width(skin_width);
    }
    float gravity_scale{1.0f};
    if(try_load(ar, ser20::make_nvp("gravity_scale", gravity_scale)))
    {
        obj.set_gravity_scale(gravity_scale);
    }
    layer_mask include_layers;
    if(try_load(ar, ser20::make_nvp("include_layers", include_layers)))
    {
        obj.set_collision_include_mask(include_layers);
    }
    layer_mask exclude_layers;
    if(try_load(ar, ser20::make_nvp("exclude_layers", exclude_layers)))
    {
        obj.set_collision_exclude_mask(exclude_layers);
    }
    float terminal_velocity{55.0f};
    if(try_load(ar, ser20::make_nvp("terminal_velocity", terminal_velocity)) ||
       try_load(ar, ser20::make_nvp("fall_speed", terminal_velocity)))
    {
        obj.set_terminal_velocity(terminal_velocity);
    }
    float linear_damping{0.0f};
    if(try_load(ar, ser20::make_nvp("linear_damping", linear_damping)))
    {
        obj.set_linear_damping(linear_damping);
    }
}

LOAD_INSTANTIATE(character_controller_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(character_controller_component, ser20::iarchive_binary_t);

} // namespace unravel
