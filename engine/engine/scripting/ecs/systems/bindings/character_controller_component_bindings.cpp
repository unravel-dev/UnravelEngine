#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/physics/ecs/components/character_controller_component.h>

namespace unravel
{
namespace
{


//------------------------------
// Character Controller Component
//------------------------------
void internal_m2n_cc_move(entt::entity id, const math::vec3& displacement)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->move(displacement);
    }
}

void internal_m2n_cc_jump(entt::entity id, const math::vec3& velocity)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->jump(velocity);
    }
}

void internal_m2n_cc_apply_impulse(entt::entity id, const math::vec3& impulse)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->apply_impulse(impulse);
    }
}

void internal_m2n_cc_warp(entt::entity id, const math::vec3& position)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->warp(position);
    }
}

auto internal_m2n_cc_get_is_grounded(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->is_grounded();
    }
    return false;
}

auto internal_m2n_cc_get_can_jump(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->can_jump();
    }
    return false;
}

auto internal_m2n_cc_get_velocity(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_velocity();
    }
    return {};
}

void internal_m2n_cc_set_linear_velocity(entt::entity id, const math::vec3& velocity)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_linear_velocity(velocity);
    }
}

auto internal_m2n_cc_get_linear_velocity(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_linear_velocity();
    }
    return {};
}

auto internal_m2n_cc_get_radius(entt::entity id) -> float
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_radius();
    }
    return 0.5f;
}

void internal_m2n_cc_set_radius(entt::entity id, float radius)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_radius(radius);
    }
}

auto internal_m2n_cc_get_height(entt::entity id) -> float
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_height();
    }
    return 1.0f;
}

void internal_m2n_cc_set_height(entt::entity id, float height)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_height(height);
    }
}

auto internal_m2n_cc_get_center(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_center();
    }
    return {};
}

void internal_m2n_cc_set_center(entt::entity id, const math::vec3& center)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_center(center);
    }
}

auto internal_m2n_cc_get_step_height(entt::entity id) -> float
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_step_height();
    }
    return 0.3f;
}

void internal_m2n_cc_set_step_height(entt::entity id, float step_height)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_step_height(step_height);
    }
}

auto internal_m2n_cc_get_slope_limit(entt::entity id) -> float
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_slope_limit();
    }
    return 45.0f;
}

void internal_m2n_cc_set_slope_limit(entt::entity id, float slope_limit)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_slope_limit(slope_limit);
    }
}

auto internal_m2n_cc_get_skin_width(entt::entity id) -> float
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_skin_width();
    }
    return 0.08f;
}

void internal_m2n_cc_set_skin_width(entt::entity id, float skin_width)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_skin_width(skin_width);
    }
}

auto internal_m2n_cc_get_gravity_scale(entt::entity id) -> float
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_gravity_scale();
    }
    return 1.0f;
}

void internal_m2n_cc_set_gravity_scale(entt::entity id, float scale)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_gravity_scale(scale);
    }
}

auto internal_m2n_cc_get_terminal_velocity(entt::entity id) -> float
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_terminal_velocity();
    }
    return 55.0f;
}

void internal_m2n_cc_set_terminal_velocity(entt::entity id, float speed)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_terminal_velocity(speed);
    }
}

auto internal_m2n_cc_get_linear_damping(entt::entity id) -> float
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_linear_damping();
    }
    return 0.0f;
}

void internal_m2n_cc_set_linear_damping(entt::entity id, float damping)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_linear_damping(damping);
    }
}

auto internal_m2n_cc_get_include_layers(entt::entity id) -> layer_mask
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_collision_include_mask();
    }
    return {};
}

void internal_m2n_cc_set_include_layers(entt::entity id, layer_mask mask)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_collision_include_mask(mask);
    }
}

auto internal_m2n_cc_get_exclude_layers(entt::entity id) -> layer_mask
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_collision_exclude_mask();
    }
    return {};
}

void internal_m2n_cc_set_exclude_layers(entt::entity id, layer_mask mask)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_collision_exclude_mask(mask);
    }
}

auto internal_m2n_cc_get_collision_layers(entt::entity id) -> layer_mask
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_collision_mask();
    }
    return {};
}

} // namespace

void register_character_controller_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);

    auto reg = dotnet::internal_call_registry("Unravel.Core.CharacterControllerComponent");
    reg.add_internal_call("internal_m2n_cc_move", dotnet_internal_call(internal_m2n_cc_move));
    reg.add_internal_call("internal_m2n_cc_jump", dotnet_internal_call(internal_m2n_cc_jump));
    reg.add_internal_call("internal_m2n_cc_apply_impulse", dotnet_internal_call(internal_m2n_cc_apply_impulse));
    reg.add_internal_call("internal_m2n_cc_warp", dotnet_internal_call(internal_m2n_cc_warp));

    reg.add_internal_call("internal_m2n_cc_get_is_grounded", dotnet_internal_call(internal_m2n_cc_get_is_grounded));
    reg.add_internal_call("internal_m2n_cc_get_can_jump", dotnet_internal_call(internal_m2n_cc_get_can_jump));
    reg.add_internal_call("internal_m2n_cc_get_velocity", dotnet_internal_call(internal_m2n_cc_get_velocity));

    reg.add_internal_call("internal_m2n_cc_get_linear_velocity", dotnet_internal_call(internal_m2n_cc_get_linear_velocity));
    reg.add_internal_call("internal_m2n_cc_set_linear_velocity", dotnet_internal_call(internal_m2n_cc_set_linear_velocity));

    reg.add_internal_call("internal_m2n_cc_get_radius", dotnet_internal_call(internal_m2n_cc_get_radius));
    reg.add_internal_call("internal_m2n_cc_set_radius", dotnet_internal_call(internal_m2n_cc_set_radius));
    reg.add_internal_call("internal_m2n_cc_get_height", dotnet_internal_call(internal_m2n_cc_get_height));
    reg.add_internal_call("internal_m2n_cc_set_height", dotnet_internal_call(internal_m2n_cc_set_height));
    reg.add_internal_call("internal_m2n_cc_get_center", dotnet_internal_call(internal_m2n_cc_get_center));
    reg.add_internal_call("internal_m2n_cc_set_center", dotnet_internal_call(internal_m2n_cc_set_center));
    reg.add_internal_call("internal_m2n_cc_get_step_height", dotnet_internal_call(internal_m2n_cc_get_step_height));
    reg.add_internal_call("internal_m2n_cc_set_step_height", dotnet_internal_call(internal_m2n_cc_set_step_height));
    reg.add_internal_call("internal_m2n_cc_get_slope_limit", dotnet_internal_call(internal_m2n_cc_get_slope_limit));
    reg.add_internal_call("internal_m2n_cc_set_slope_limit", dotnet_internal_call(internal_m2n_cc_set_slope_limit));
    reg.add_internal_call("internal_m2n_cc_get_skin_width", dotnet_internal_call(internal_m2n_cc_get_skin_width));
    reg.add_internal_call("internal_m2n_cc_set_skin_width", dotnet_internal_call(internal_m2n_cc_set_skin_width));
    reg.add_internal_call("internal_m2n_cc_get_gravity_scale", dotnet_internal_call(internal_m2n_cc_get_gravity_scale));
    reg.add_internal_call("internal_m2n_cc_set_gravity_scale", dotnet_internal_call(internal_m2n_cc_set_gravity_scale));
    reg.add_internal_call("internal_m2n_cc_get_terminal_velocity", dotnet_internal_call(internal_m2n_cc_get_terminal_velocity));
    reg.add_internal_call("internal_m2n_cc_set_terminal_velocity", dotnet_internal_call(internal_m2n_cc_set_terminal_velocity));
    reg.add_internal_call("internal_m2n_cc_get_linear_damping", dotnet_internal_call(internal_m2n_cc_get_linear_damping));
    reg.add_internal_call("internal_m2n_cc_set_linear_damping", dotnet_internal_call(internal_m2n_cc_set_linear_damping));

    reg.add_internal_call("internal_m2n_cc_get_include_layers", dotnet_internal_call(internal_m2n_cc_get_include_layers));
    reg.add_internal_call("internal_m2n_cc_set_include_layers", dotnet_internal_call(internal_m2n_cc_set_include_layers));
    reg.add_internal_call("internal_m2n_cc_get_exclude_layers", dotnet_internal_call(internal_m2n_cc_get_exclude_layers));
    reg.add_internal_call("internal_m2n_cc_set_exclude_layers", dotnet_internal_call(internal_m2n_cc_set_exclude_layers));
    reg.add_internal_call("internal_m2n_cc_get_collision_layers", dotnet_internal_call(internal_m2n_cc_get_collision_layers));
}

} // namespace unravel
