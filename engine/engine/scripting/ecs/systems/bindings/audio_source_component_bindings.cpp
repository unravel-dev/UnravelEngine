#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/assets/asset_manager.h>
#include <engine/audio/ecs/components/audio_source_component.h>

namespace unravel
{
namespace
{

auto internal_m2n_audio_source_get_loop(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return comp->is_looping();
    }

    return {};
}

//-------------------------------------------------

void internal_m2n_audio_source_set_loop(entt::entity id, bool loop)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        comp->set_loop(loop);
    }
}

auto internal_m2n_audio_source_get_volume(entt::entity id) -> float
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return comp->get_volume();
    }

    return {};
}

void internal_m2n_audio_source_set_volume(entt::entity id, float volume)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        comp->set_volume(volume);
    }
}

auto internal_m2n_audio_source_get_pitch(entt::entity id) -> float
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return comp->get_pitch();
    }

    return {};
}

void internal_m2n_audio_source_set_pitch(entt::entity id, float pitch)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        comp->set_pitch(pitch);
    }
}

auto internal_m2n_audio_source_get_volume_rolloff(entt::entity id) -> float
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return comp->get_volume_rolloff();
    }

    return {};
}

void internal_m2n_audio_source_set_volume_rolloff(entt::entity id, float rolloff)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        comp->set_volume_rolloff(rolloff);
    }
}

auto internal_m2n_audio_source_get_min_distance(entt::entity id) -> float
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return comp->get_range().min;
    }

    return {};
}

void internal_m2n_audio_source_set_min_distance(entt::entity id, float distance)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        auto range = comp->get_range();
        range.min = distance;
        comp->set_range(range);
    }
}

auto internal_m2n_audio_source_get_max_distance(entt::entity id) -> float
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return comp->get_range().max;
    }

    return {};
}

void internal_m2n_audio_source_set_max_distance(entt::entity id, float distance)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        auto range = comp->get_range();
        range.max = distance;
        comp->set_range(range);
    }
}

auto internal_m2n_audio_source_get_mute(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return comp->is_muted();
    }

    return {};
}

void internal_m2n_audio_source_set_mute(entt::entity id, bool mute)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        comp->set_mute(mute);
    }
}

auto internal_m2n_audio_source_get_time(entt::entity id) -> float
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return float(comp->get_playback_position().count());
    }

    return {};
}

void internal_m2n_audio_source_set_time(entt::entity id, float seconds)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        comp->set_playback_position(audio::duration_t(seconds));
    }
}

auto internal_m2n_audio_source_is_playing(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return comp->is_playing();
    }

    return {};
}

auto internal_m2n_audio_source_is_paused(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return comp->is_paused();
    }

    return {};
}

void internal_m2n_audio_source_play(entt::entity id)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        comp->play();
    }
}

void internal_m2n_audio_source_stop(entt::entity id)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        comp->stop();
    }
}

void internal_m2n_audio_source_pause(entt::entity id)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        comp->pause();
    }
}

void internal_m2n_audio_source_resume(entt::entity id)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        comp->resume();
    }
}

auto internal_m2n_audio_source_get_audio_clip(entt::entity id) -> hpp::uuid
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return comp->get_clip().uid();
    }

    return {};
}

void internal_m2n_audio_source_set_audio_clip(entt::entity id, hpp::uuid uid)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        auto& ctx = engine::context();
        auto& am = ctx.get_cached<asset_manager>();

        auto asset = am.get_asset<audio_clip>(uid);
        comp->set_clip(asset);
    }
}

} // namespace

void register_audio_source_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);

    auto reg = dotnet::internal_call_registry("Unravel.Core.AudioSourceComponent");
    reg.add_internal_call("internal_m2n_audio_source_get_loop", dotnet_internal_call(internal_m2n_audio_source_get_loop));
    reg.add_internal_call("internal_m2n_audio_source_set_loop", dotnet_internal_call(internal_m2n_audio_source_set_loop));
    reg.add_internal_call("internal_m2n_audio_source_get_volume",
                            dotnet_internal_call(internal_m2n_audio_source_get_volume));
    reg.add_internal_call("internal_m2n_audio_source_set_volume",
                            dotnet_internal_call(internal_m2n_audio_source_set_volume));
    reg.add_internal_call("internal_m2n_audio_source_get_pitch",
                            dotnet_internal_call(internal_m2n_audio_source_get_pitch));
    reg.add_internal_call("internal_m2n_audio_source_set_pitch",
                            dotnet_internal_call(internal_m2n_audio_source_set_pitch));
    reg.add_internal_call("internal_m2n_audio_source_get_volume_rolloff",
                            dotnet_internal_call(internal_m2n_audio_source_get_volume_rolloff));
    reg.add_internal_call("internal_m2n_audio_source_set_volume_rolloff",
                            dotnet_internal_call(internal_m2n_audio_source_set_volume_rolloff));
    reg.add_internal_call("internal_m2n_audio_source_get_min_distance",
                            dotnet_internal_call(internal_m2n_audio_source_get_min_distance));
    reg.add_internal_call("internal_m2n_audio_source_set_min_distance",
                            dotnet_internal_call(internal_m2n_audio_source_set_min_distance));
    reg.add_internal_call("internal_m2n_audio_source_get_max_distance",
                            dotnet_internal_call(internal_m2n_audio_source_get_max_distance));
    reg.add_internal_call("internal_m2n_audio_source_set_max_distance",
                            dotnet_internal_call(internal_m2n_audio_source_set_max_distance));
    reg.add_internal_call("internal_m2n_audio_source_get_mute", dotnet_internal_call(internal_m2n_audio_source_get_mute));

    reg.add_internal_call("internal_m2n_audio_source_set_mute", dotnet_internal_call(internal_m2n_audio_source_set_mute));

    reg.add_internal_call("internal_m2n_audio_source_is_playing",
                            dotnet_internal_call(internal_m2n_audio_source_is_playing));
    reg.add_internal_call("internal_m2n_audio_source_is_paused",
                            dotnet_internal_call(internal_m2n_audio_source_is_paused));
    reg.add_internal_call("internal_m2n_audio_source_play", dotnet_internal_call(internal_m2n_audio_source_play));
    reg.add_internal_call("internal_m2n_audio_source_stop", dotnet_internal_call(internal_m2n_audio_source_stop));

    reg.add_internal_call("internal_m2n_audio_source_pause", dotnet_internal_call(internal_m2n_audio_source_pause));
    reg.add_internal_call("internal_m2n_audio_source_resume", dotnet_internal_call(internal_m2n_audio_source_resume));
    reg.add_internal_call("internal_m2n_audio_source_get_audio_clip",
                            dotnet_internal_call(internal_m2n_audio_source_get_audio_clip));
    reg.add_internal_call("internal_m2n_audio_source_set_audio_clip",
                            dotnet_internal_call(internal_m2n_audio_source_set_audio_clip));
}

} // namespace unravel
