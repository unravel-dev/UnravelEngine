#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/assets/asset_manager.h>
#include <engine/audio/audio_bus.h>
#include <engine/audio/ecs/components/audio_source_component.h>
#include <engine/audio/ecs/systems/audio_system.h>
#include <engine/engine.h>

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

auto internal_m2n_audio_source_get_spatial(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return comp->is_spatial();
    }
    return true;
}

void internal_m2n_audio_source_set_spatial(entt::entity id, bool spatial)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        comp->set_spatial(spatial);
    }
}

auto internal_m2n_audio_source_get_bus(entt::entity id) -> std::uint8_t
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return static_cast<std::uint8_t>(comp->get_bus());
    }
    return static_cast<std::uint8_t>(audio_bus::sfx);
}

void internal_m2n_audio_source_set_bus(entt::entity id, std::uint8_t bus)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        if(bus < audio_bus_count)
        {
            comp->set_bus(static_cast<audio_bus>(bus));
        }
    }
}

auto internal_m2n_audio_source_get_priority(entt::entity id) -> int
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return comp->get_priority();
    }
    return 0;
}

void internal_m2n_audio_source_set_priority(entt::entity id, int priority)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        comp->set_priority(priority);
    }
}

auto internal_m2n_audio_get_master_volume() -> float
{
    auto& ctx = engine::context();
    if(!ctx.has<audio_system>())
    {
        return 1.0f;
    }
    return ctx.get_cached<audio_system>().get_master_volume();
}

void internal_m2n_audio_set_master_volume(float volume)
{
    auto& ctx = engine::context();
    if(!ctx.has<audio_system>())
    {
        return;
    }
    ctx.get_cached<audio_system>().set_master_volume(volume);
}

auto internal_m2n_audio_get_bus_volume(std::uint8_t bus) -> float
{
    auto& ctx = engine::context();
    if(!ctx.has<audio_system>() || bus >= audio_bus_count)
    {
        return 1.0f;
    }
    return ctx.get_cached<audio_system>().get_bus_volume(static_cast<audio_bus>(bus));
}

void internal_m2n_audio_set_bus_volume(std::uint8_t bus, float volume)
{
    auto& ctx = engine::context();
    if(!ctx.has<audio_system>() || bus >= audio_bus_count)
    {
        return;
    }
    ctx.get_cached<audio_system>().set_bus_volume(static_cast<audio_bus>(bus), volume);
}

auto internal_m2n_audio_get_max_voices() -> int
{
    return static_cast<int>(audio_system::max_concurrent_voices);
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
    reg.add_internal_call("internal_m2n_audio_source_get_spatial",
                            dotnet_internal_call(internal_m2n_audio_source_get_spatial));
    reg.add_internal_call("internal_m2n_audio_source_set_spatial",
                            dotnet_internal_call(internal_m2n_audio_source_set_spatial));
    reg.add_internal_call("internal_m2n_audio_source_get_bus",
                            dotnet_internal_call(internal_m2n_audio_source_get_bus));
    reg.add_internal_call("internal_m2n_audio_source_set_bus",
                            dotnet_internal_call(internal_m2n_audio_source_set_bus));
    reg.add_internal_call("internal_m2n_audio_source_get_priority",
                            dotnet_internal_call(internal_m2n_audio_source_get_priority));
    reg.add_internal_call("internal_m2n_audio_source_set_priority",
                            dotnet_internal_call(internal_m2n_audio_source_set_priority));

    auto audio_reg = dotnet::internal_call_registry("Unravel.Core.Audio");
    audio_reg.add_internal_call("internal_m2n_audio_get_master_volume",
                                dotnet_internal_call(internal_m2n_audio_get_master_volume));
    audio_reg.add_internal_call("internal_m2n_audio_set_master_volume",
                                dotnet_internal_call(internal_m2n_audio_set_master_volume));
    audio_reg.add_internal_call("internal_m2n_audio_get_bus_volume",
                                dotnet_internal_call(internal_m2n_audio_get_bus_volume));
    audio_reg.add_internal_call("internal_m2n_audio_set_bus_volume",
                                dotnet_internal_call(internal_m2n_audio_set_bus_volume));
    audio_reg.add_internal_call("internal_m2n_audio_get_max_voices",
                                dotnet_internal_call(internal_m2n_audio_get_max_voices));
}

} // namespace unravel
