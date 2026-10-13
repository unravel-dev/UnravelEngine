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

auto find_audio_system() -> audio_system*
{
    auto& ctx = engine::context();
    return ctx.has<audio_system>() ? &ctx.get_cached<audio_system>() : nullptr;
}

auto internal_m2n_audio_play_one_shot(const hpp::uuid& clip,
                                      float volume,
                                      float pitch,
                                      std::uint8_t bus,
                                      int priority,
                                      bool spatial,
                                      const math::vec3& position) -> uint64_t
{
    auto system = find_audio_system();
    if(!system || clip.is_nil())
    {
        return 0;
    }
    auto& am = engine::context().get_cached<asset_manager>();
    one_shot_params params;
    params.volume = volume;
    params.pitch = pitch;
    params.bus = bus < audio_bus_count ? static_cast<audio_bus>(bus) : audio_bus::sfx;
    params.priority = priority;
    params.spatial = spatial;
    params.position = position;
    return system->play_one_shot(am.get_asset<audio_clip>(clip), params);
}

void internal_m2n_audio_voice_stop(uint64_t voice)
{
    if(auto system = find_audio_system())
    {
        system->get_voices().stop(voice);
    }
}

auto internal_m2n_audio_voice_is_playing(uint64_t voice) -> bool
{
    auto system = find_audio_system();
    return system && system->get_voices().is_playing(voice);
}

void internal_m2n_audio_voice_set_volume(uint64_t voice, float volume)
{
    if(auto system = find_audio_system())
    {
        system->get_voices().set_volume(voice, volume);
    }
}

void internal_m2n_audio_voice_set_pitch(uint64_t voice, float pitch)
{
    if(auto system = find_audio_system())
    {
        system->get_voices().set_pitch(voice, pitch);
    }
}

void internal_m2n_audio_voice_fade_to(uint64_t voice, float volume, float seconds, bool stop_at_end)
{
    if(auto system = find_audio_system())
    {
        system->get_voices().fade_to(voice, volume, seconds, stop_at_end);
    }
}

void internal_m2n_audio_set_clip_limit(const hpp::uuid& clip, int max_voices, float min_interval)
{
    if(auto system = find_audio_system())
    {
        system->get_voices().set_clip_limit(clip, max_voices, min_interval);
    }
}

void internal_m2n_audio_fade_bus_volume(std::uint8_t bus, float target, float seconds)
{
    auto system = find_audio_system();
    if(system && bus < audio_bus_count)
    {
        system->fade_bus_volume(static_cast<audio_bus>(bus), target, seconds);
    }
}

void internal_m2n_audio_source_fade_to(entt::entity id, float volume, float seconds, bool stop_at_end)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        comp->fade_to(volume, seconds, stop_at_end);
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
    reg.add_internal_call("internal_m2n_audio_source_fade_to",
                            dotnet_internal_call(internal_m2n_audio_source_fade_to));

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
    audio_reg.add_internal_call("internal_m2n_audio_play_one_shot",
                                dotnet_internal_call(internal_m2n_audio_play_one_shot));
    audio_reg.add_internal_call("internal_m2n_audio_voice_stop", dotnet_internal_call(internal_m2n_audio_voice_stop));
    audio_reg.add_internal_call("internal_m2n_audio_voice_is_playing",
                                dotnet_internal_call(internal_m2n_audio_voice_is_playing));
    audio_reg.add_internal_call("internal_m2n_audio_voice_set_volume",
                                dotnet_internal_call(internal_m2n_audio_voice_set_volume));
    audio_reg.add_internal_call("internal_m2n_audio_voice_set_pitch",
                                dotnet_internal_call(internal_m2n_audio_voice_set_pitch));
    audio_reg.add_internal_call("internal_m2n_audio_voice_fade_to",
                                dotnet_internal_call(internal_m2n_audio_voice_fade_to));
    audio_reg.add_internal_call("internal_m2n_audio_set_clip_limit",
                                dotnet_internal_call(internal_m2n_audio_set_clip_limit));
    audio_reg.add_internal_call("internal_m2n_audio_fade_bus_volume",
                                dotnet_internal_call(internal_m2n_audio_fade_bus_volume));
}

} // namespace unravel
