#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/animation/ecs/components/animation_component.h>
#include <engine/assets/asset_manager.h>

namespace unravel
{
namespace
{


void internal_m2n_animation_blend(entt::entity id, int layer, hpp::uuid guid, float seconds, bool loop, bool phase_sync)
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        auto& ctx = engine::context();
        auto& am = ctx.get_cached<asset_manager>();

        auto asset = am.get_asset<animation_clip>(guid);
        comp->get_player().blend_to(layer, asset, animation_player::seconds_t(seconds), loop, phase_sync);
    }
}

void internal_m2n_animation_play(entt::entity id)
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        comp->get_player().play();
    }
}

void internal_m2n_animation_pause(entt::entity id)
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        comp->get_player().pause();
    }
}

void internal_m2n_animation_resume(entt::entity id)
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        comp->get_player().resume();
    }
}

void internal_m2n_animation_stop(entt::entity id)
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        comp->get_player().stop();
    }
}

void internal_m2n_animation_set_speed(entt::entity id, float speed)
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        comp->set_speed(speed);
    }
}

auto internal_m2n_animation_get_speed(entt::entity id) -> float
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        return comp->get_speed();
    }
    return 1.0f;
}

} // namespace

void register_animation_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    auto reg = dotnet::internal_call_registry("Unravel.Core.AnimationComponent");
    reg.add_internal_call("internal_m2n_animation_blend", dotnet_internal_call(internal_m2n_animation_blend));
    reg.add_internal_call("internal_m2n_animation_play", dotnet_internal_call(internal_m2n_animation_play));
    reg.add_internal_call("internal_m2n_animation_pause", dotnet_internal_call(internal_m2n_animation_pause));
    reg.add_internal_call("internal_m2n_animation_resume", dotnet_internal_call(internal_m2n_animation_resume));
    reg.add_internal_call("internal_m2n_animation_stop", dotnet_internal_call(internal_m2n_animation_stop));
    reg.add_internal_call("internal_m2n_animation_set_speed", dotnet_internal_call(internal_m2n_animation_set_speed));
    reg.add_internal_call("internal_m2n_animation_get_speed", dotnet_internal_call(internal_m2n_animation_get_speed));
}

} // namespace unravel
