#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/animation/ecs/components/animation_component.h>
#include <engine/assets/asset_manager.h>

#include <cmath>

namespace unravel
{
namespace
{

/// Layer indices scripts pass: the player grows its layer list to reach an index, so an unchecked
/// one could crash (negative) or allocate thousands of layers.
auto is_layer_index(int layer) -> bool
{
    return layer >= 0 && size_t(layer) < MAX_ANIMATION_LAYERS;
}

auto get_controller_instance(entt::entity id) -> animation_controller_instance*
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        return comp->get_controller_instance();
    }
    return nullptr;
}

void internal_m2n_animation_blend(entt::entity id, int layer, hpp::uuid guid, float seconds, bool loop, bool phase_sync)
{
    if(!is_layer_index(layer))
    {
        return;
    }
    if(auto comp = safe_get_component<animation_component>(id))
    {
        auto& ctx = engine::context();
        auto& am = ctx.get_cached<asset_manager>();
        auto asset = am.get_asset<animation_clip>(guid);
        // Stopping a layer the controller drives stops it there too, or it would resume the state.
        auto* controller = comp->try_get_controller_instance();
        if(!asset && controller != nullptr && size_t(layer) < controller->get_layer_count())
        {
            controller->stop_layer(size_t(layer));
        }
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

auto internal_m2n_animation_get_autoplay(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        return comp->get_autoplay();
    }
    return false;
}

void internal_m2n_animation_set_autoplay(entt::entity id, bool autoplay)
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        comp->set_autoplay(autoplay);
    }
}

auto internal_m2n_animation_get_apply_root_motion(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        return comp->get_apply_root_motion();
    }
    return false;
}

void internal_m2n_animation_set_apply_root_motion(entt::entity id, bool apply)
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        comp->set_apply_root_motion(apply);
    }
}

auto internal_m2n_animation_get_clip(entt::entity id) -> hpp::uuid
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        return comp->get_animation().uid();
    }
    return {};
}

void internal_m2n_animation_set_clip(entt::entity id, const hpp::uuid& uid)
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        auto& ctx = engine::context();
        auto& am = ctx.get_cached<asset_manager>();
        comp->set_animation(am.get_asset<animation_clip>(uid));
    }
}

auto internal_m2n_animation_is_playing(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        return comp->get_player().is_playing();
    }
    return false;
}

auto internal_m2n_animation_is_paused(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        return comp->get_player().is_paused();
    }
    return false;
}

auto internal_m2n_animation_get_curve(entt::entity id, const std::string& name) -> float
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        return comp->get_player().get_curve_value(get_animation_curve_id(name));
    }
    return 0.0f;
}

auto internal_m2n_animation_has_curve(entt::entity id, const std::string& name) -> bool
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        return comp->get_player().has_curve(get_animation_curve_id(name));
    }
    return false;
}

void internal_m2n_animation_set_layer_weight(entt::entity id, int layer, float weight)
{
    if(!is_layer_index(layer) || !std::isfinite(weight))
    {
        return;
    }
    auto comp = safe_get_component<animation_component>(id);
    if(comp == nullptr)
    {
        return;
    }
    // A controller's layer keeps a script weight when the layer starts over.
    auto* controller = comp->get_controller_instance();
    if(controller != nullptr && size_t(layer) < controller->get_layer_count())
    {
        controller->set_layer_weight(comp->get_player(), size_t(layer), weight);
        return;
    }
    comp->get_player().set_layer_weight(size_t(layer), weight);
}

auto internal_m2n_animation_get_layer_weight(entt::entity id, int layer) -> float
{
    if(!is_layer_index(layer))
    {
        return 0.0f;
    }
    if(auto comp = safe_get_component<animation_component>(id))
    {
        return comp->get_player().get_layer_weight(size_t(layer));
    }
    return 0.0f;
}

void internal_m2n_animation_set_layer_additive(entt::entity id, int layer, bool additive)
{
    // The base layer has nothing below it to add onto.
    if(layer <= 0 || !is_layer_index(layer))
    {
        return;
    }
    auto comp = safe_get_component<animation_component>(id);
    if(comp == nullptr)
    {
        return;
    }
    const auto mode = additive ? animation_layer_blend_mode::additive : animation_layer_blend_mode::override_pose;
    auto* controller = comp->get_controller_instance();
    if(controller != nullptr && size_t(layer) < controller->get_layer_count())
    {
        controller->set_layer_blend_mode(comp->get_player(), size_t(layer), mode);
        return;
    }
    comp->get_player().set_layer_blend_mode(size_t(layer), mode);
}

auto internal_m2n_animation_get_layer_additive(entt::entity id, int layer) -> bool
{
    if(layer <= 0 || !is_layer_index(layer))
    {
        return false;
    }
    if(auto comp = safe_get_component<animation_component>(id))
    {
        return comp->get_player().get_layer_blend_mode(size_t(layer)) == animation_layer_blend_mode::additive;
    }
    return false;
}

auto internal_m2n_animation_get_controller(entt::entity id) -> hpp::uuid
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        return comp->get_controller().uid();
    }
    return {};
}

void internal_m2n_animation_set_controller(entt::entity id, const hpp::uuid& uid)
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        auto& ctx = engine::context();
        auto& am = ctx.get_cached<asset_manager>();
        comp->set_controller(am.get_asset<animation_controller>(uid));
    }
}

auto internal_m2n_animation_set_float(entt::entity id, const std::string& name, float value) -> bool
{
    auto controller = get_controller_instance(id);
    return controller && controller->set_float(name, value);
}

auto internal_m2n_animation_get_float(entt::entity id, const std::string& name) -> float
{
    auto controller = get_controller_instance(id);
    return controller ? controller->get_float(name) : 0.0f;
}

auto internal_m2n_animation_set_int(entt::entity id, const std::string& name, int value) -> bool
{
    auto controller = get_controller_instance(id);
    return controller && controller->set_int(name, value);
}

auto internal_m2n_animation_get_int(entt::entity id, const std::string& name) -> int
{
    auto controller = get_controller_instance(id);
    return controller ? controller->get_int(name) : 0;
}

auto internal_m2n_animation_set_bool(entt::entity id, const std::string& name, bool value) -> bool
{
    auto controller = get_controller_instance(id);
    return controller && controller->set_bool(name, value);
}

auto internal_m2n_animation_get_bool(entt::entity id, const std::string& name) -> bool
{
    auto controller = get_controller_instance(id);
    return controller && controller->get_bool(name);
}

auto internal_m2n_animation_set_trigger(entt::entity id, const std::string& name) -> bool
{
    auto controller = get_controller_instance(id);
    return controller && controller->set_trigger(name);
}

auto internal_m2n_animation_reset_trigger(entt::entity id, const std::string& name) -> bool
{
    auto controller = get_controller_instance(id);
    return controller && controller->reset_trigger(name);
}

auto internal_m2n_animation_get_state_name(entt::entity id, int layer, bool next) -> std::string
{
    auto controller = get_controller_instance(id);
    if(!controller || !is_layer_index(layer))
    {
        return {};
    }
    const auto status = controller->get_layer_status(size_t(layer));
    return controller->get_state_name(size_t(layer), next ? status.next_state : status.current_state);
}

auto internal_m2n_animation_is_in_state(entt::entity id, int layer, const std::string& name) -> bool
{
    auto controller = get_controller_instance(id);
    return controller && is_layer_index(layer) && controller->is_in_state(size_t(layer), name);
}

auto internal_m2n_animation_is_in_tag(entt::entity id, int layer, const std::string& tag) -> bool
{
    auto controller = get_controller_instance(id);
    return controller && is_layer_index(layer) && controller->is_in_tag(size_t(layer), tag);
}

auto internal_m2n_animation_is_in_transition(entt::entity id, int layer) -> bool
{
    auto controller = get_controller_instance(id);
    return controller && is_layer_index(layer) && controller->get_layer_status(size_t(layer)).next_state >= 0;
}

auto internal_m2n_animation_get_state_time(entt::entity id, int layer, bool next) -> float
{
    auto comp = safe_get_component<animation_component>(id);
    auto controller = comp ? comp->get_controller_instance() : nullptr;
    if(!controller || !is_layer_index(layer))
    {
        return 0.0f;
    }
    const auto status = controller->get_layer_status(size_t(layer), comp->get_player());
    return next ? status.next_state_time : status.state_time;
}

auto internal_m2n_animation_get_transition_progress(entt::entity id, int layer) -> float
{
    auto comp = safe_get_component<animation_component>(id);
    auto controller = comp ? comp->get_controller_instance() : nullptr;
    if(!controller || !is_layer_index(layer))
    {
        return 0.0f;
    }
    return controller->get_layer_status(size_t(layer), comp->get_player()).transition_progress;
}

auto internal_m2n_animation_cross_fade(entt::entity id,
                                       int layer,
                                       const std::string& state,
                                       float duration,
                                       float normalized_time) -> bool
{
    auto controller = get_controller_instance(id);
    return controller && is_layer_index(layer) && controller->cross_fade(size_t(layer), state, duration, normalized_time);
}

auto internal_m2n_animation_get_layer_index(entt::entity id, const std::string& name) -> int
{
    auto controller = get_controller_instance(id);
    return controller ? controller->get_layer_index(name) : -1;
}

auto internal_m2n_animation_get_layer_count(entt::entity id) -> int
{
    auto controller = get_controller_instance(id);
    return controller ? int(controller->get_layer_count()) : 0;
}

} // namespace

void register_animation_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    auto reg = dotnet::internal_call_registry("Unravel.Core.AnimationComponent");
    reg.add_internal_call("internal_m2n_animation_blend", script_internal_call(internal_m2n_animation_blend));
    reg.add_internal_call("internal_m2n_animation_play", script_internal_call(internal_m2n_animation_play));
    reg.add_internal_call("internal_m2n_animation_pause", script_internal_call(internal_m2n_animation_pause));
    reg.add_internal_call("internal_m2n_animation_resume", script_internal_call(internal_m2n_animation_resume));
    reg.add_internal_call("internal_m2n_animation_stop", script_internal_call(internal_m2n_animation_stop));
    reg.add_internal_call("internal_m2n_animation_set_speed", script_internal_call(internal_m2n_animation_set_speed));
    reg.add_internal_call("internal_m2n_animation_get_speed", script_internal_call(internal_m2n_animation_get_speed));
    reg.add_internal_call("internal_m2n_animation_get_autoplay",
                          script_internal_call(internal_m2n_animation_get_autoplay));
    reg.add_internal_call("internal_m2n_animation_set_autoplay",
                          script_internal_call(internal_m2n_animation_set_autoplay));
    reg.add_internal_call("internal_m2n_animation_get_apply_root_motion",
                          script_internal_call(internal_m2n_animation_get_apply_root_motion));
    reg.add_internal_call("internal_m2n_animation_set_apply_root_motion",
                          script_internal_call(internal_m2n_animation_set_apply_root_motion));
    reg.add_internal_call("internal_m2n_animation_get_clip", script_internal_call(internal_m2n_animation_get_clip));
    reg.add_internal_call("internal_m2n_animation_set_clip", script_internal_call(internal_m2n_animation_set_clip));
    reg.add_internal_call("internal_m2n_animation_is_playing",
                          script_internal_call(internal_m2n_animation_is_playing));
    reg.add_internal_call("internal_m2n_animation_is_paused",
                          script_internal_call(internal_m2n_animation_is_paused));
    reg.add_internal_call("internal_m2n_animation_get_curve", script_internal_call(internal_m2n_animation_get_curve));
    reg.add_internal_call("internal_m2n_animation_get_controller",
                          script_internal_call(internal_m2n_animation_get_controller));
    reg.add_internal_call("internal_m2n_animation_set_controller",
                          script_internal_call(internal_m2n_animation_set_controller));
    reg.add_internal_call("internal_m2n_animation_set_float", script_internal_call(internal_m2n_animation_set_float));
    reg.add_internal_call("internal_m2n_animation_get_float", script_internal_call(internal_m2n_animation_get_float));
    reg.add_internal_call("internal_m2n_animation_set_int", script_internal_call(internal_m2n_animation_set_int));
    reg.add_internal_call("internal_m2n_animation_get_int", script_internal_call(internal_m2n_animation_get_int));
    reg.add_internal_call("internal_m2n_animation_set_bool", script_internal_call(internal_m2n_animation_set_bool));
    reg.add_internal_call("internal_m2n_animation_get_bool", script_internal_call(internal_m2n_animation_get_bool));
    reg.add_internal_call("internal_m2n_animation_set_trigger",
                          script_internal_call(internal_m2n_animation_set_trigger));
    reg.add_internal_call("internal_m2n_animation_reset_trigger",
                          script_internal_call(internal_m2n_animation_reset_trigger));
    reg.add_internal_call("internal_m2n_animation_get_state_name",
                          script_internal_call(internal_m2n_animation_get_state_name));
    reg.add_internal_call("internal_m2n_animation_is_in_state",
                          script_internal_call(internal_m2n_animation_is_in_state));
    reg.add_internal_call("internal_m2n_animation_is_in_tag", script_internal_call(internal_m2n_animation_is_in_tag));
    reg.add_internal_call("internal_m2n_animation_is_in_transition",
                          script_internal_call(internal_m2n_animation_is_in_transition));
    reg.add_internal_call("internal_m2n_animation_get_state_time",
                          script_internal_call(internal_m2n_animation_get_state_time));
    reg.add_internal_call("internal_m2n_animation_get_transition_progress",
                          script_internal_call(internal_m2n_animation_get_transition_progress));
    reg.add_internal_call("internal_m2n_animation_cross_fade",
                          script_internal_call(internal_m2n_animation_cross_fade));
    reg.add_internal_call("internal_m2n_animation_get_layer_index",
                          script_internal_call(internal_m2n_animation_get_layer_index));
    reg.add_internal_call("internal_m2n_animation_get_layer_count",
                          script_internal_call(internal_m2n_animation_get_layer_count));
    reg.add_internal_call("internal_m2n_animation_has_curve", script_internal_call(internal_m2n_animation_has_curve));
    reg.add_internal_call("internal_m2n_animation_set_layer_weight",
                          script_internal_call(internal_m2n_animation_set_layer_weight));
    reg.add_internal_call("internal_m2n_animation_get_layer_weight",
                          script_internal_call(internal_m2n_animation_get_layer_weight));
    reg.add_internal_call("internal_m2n_animation_set_layer_additive",
                          script_internal_call(internal_m2n_animation_set_layer_additive));
    reg.add_internal_call("internal_m2n_animation_get_layer_additive",
                          script_internal_call(internal_m2n_animation_get_layer_additive));
}

} // namespace unravel
