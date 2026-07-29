#include "audio_system.h"
#include <engine/events.h>

#include <engine/audio/ecs/components/audio_listener_component.h>
#include <engine/audio/ecs/components/audio_source_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/engine.h>
#include <engine/profiler/profiler.h>
#include <audiopp/logger.h>
#include <logging/logging.h>

#include <climits>
#include <cmath>

namespace unravel
{

namespace
{

void on_create_component(entt::registry& r, entt::entity e)
{
    auto& comp = r.get<audio_source_component>(e);

    if(r.all_of<active_component>(e))
    {
        comp.on_play_begin();
    }
}
void on_destroy_component(entt::registry& r, entt::entity e)
{
    auto& comp = r.get<audio_source_component>(e);
    comp.on_play_end();
}

void on_create_active_component(entt::registry& r, entt::entity e)
{
    if(auto comp = r.try_get<audio_source_component>(e))
    {
        comp->on_play_begin();
    }
}
void on_destroy_active_component(entt::registry& r, entt::entity e)
{
    if(auto comp = r.try_get<audio_source_component>(e))
    {
        comp->on_play_end();
    }
}

auto clamp_gain(float volume) -> float
{
    return std::fmin(1.0f, std::fmax(0.0f, volume));
}

} // namespace

auto audio_system::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    auto& ev = ctx.get_cached<events>();
    ev.on_frame_update.connect(sentinel_, this, &audio_system::on_frame_update);

    ev.on_play_begin.connect(sentinel_, 10, this, &audio_system::on_play_begin);
    ev.on_play_end.connect(sentinel_, -10, this, &audio_system::on_play_end);
    ev.on_pause.connect(sentinel_, 10, this, &audio_system::on_pause);
    ev.on_resume.connect(sentinel_, -10, this, &audio_system::on_resume);
    ev.on_skip_next_frame.connect(sentinel_, -10, this, &audio_system::on_skip_next_frame);

    audio::set_info_logger(
        [](const std::string& s)
        {
            APPLOG_TRACE(s);
        });
    audio::set_error_logger(
        [](const std::string& s)
        {
            APPLOG_ERROR(s);
        });

    audio::set_trace_logger(
        [](const std::string& s)
        {
            APPLOG_TRACE(s);
        });

    audio::device::print_devices();
    device_ = std::make_unique<audio::device>();

    return true;
}

auto audio_system::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
}

void audio_system::set_master_volume(float volume)
{
    master_volume_ = clamp_gain(volume);
    refresh_source_volumes(engine::context());
}

auto audio_system::get_master_volume() const -> float
{
    return master_volume_;
}

void audio_system::set_bus_volume(audio_bus bus, float volume)
{
    const auto index = static_cast<std::uint8_t>(bus);
    if(index >= audio_bus_count)
    {
        return;
    }
    bus_volumes_[index] = clamp_gain(volume);
    refresh_source_volumes(engine::context());
}

auto audio_system::get_bus_volume(audio_bus bus) const -> float
{
    const auto index = static_cast<std::uint8_t>(bus);
    if(index >= audio_bus_count)
    {
        return 0.0f;
    }
    return bus_volumes_[index];
}

auto audio_system::get_effective_bus_gain(audio_bus bus) const -> float
{
    return master_volume_ * get_bus_volume(bus);
}

void audio_system::ensure_voice_capacity(audio_source_component& requesting)
{
    auto& ctx = engine::context();
    if(!ctx.has<ecs>())
    {
        return;
    }
    auto& ec = ctx.get_cached<ecs>();
    auto& registry = *ec.get_scene().registry;

    while(true)
    {
        std::size_t live_count = 0;
        audio_source_component* steal_candidate = nullptr;
        int steal_priority = INT_MAX;
        bool steal_is_looping = true;

        registry.view<audio_source_component>().each(
            [&](auto /*e*/, audio_source_component& comp)
            {
                if(!comp.has_source() || &comp == &requesting)
                {
                    return;
                }
                ++live_count;
                const bool is_looping = comp.is_looping();
                const int priority = comp.get_priority();
                // Prefer stealing non-looping, then lower priority.
                const bool better =
                    (steal_candidate == nullptr) || (!is_looping && steal_is_looping) ||
                    (is_looping == steal_is_looping && priority < steal_priority);
                if(better)
                {
                    steal_candidate = &comp;
                    steal_priority = priority;
                    steal_is_looping = is_looping;
                }
            });

        if(live_count < max_concurrent_voices || steal_candidate == nullptr)
        {
            return;
        }
        steal_candidate->release_source();
    }
}

void audio_system::refresh_source_volumes(rtti::context& ctx)
{
    if(!ctx.has<ecs>())
    {
        return;
    }
    auto& ec = ctx.get_cached<ecs>();
    auto& registry = *ec.get_scene().registry;
    registry.view<audio_source_component>().each(
        [&](auto /*e*/, audio_source_component& comp)
        {
            comp.apply_mixer_volume();
        });
}

void audio_system::on_play_begin(rtti::context& ctx)
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();
    auto& registry = *scn.registry;

    registry.on_construct<audio_source_component>().connect<&on_create_component>();
    registry.on_destroy<audio_source_component>().connect<&on_destroy_component>();


    registry.on_construct<active_component>().connect<&on_create_active_component>();
    registry.on_destroy<active_component>().connect<&on_destroy_active_component>();


    registry.view<audio_source_component, active_component>().each(
        [&](auto e, auto&& comp, auto&& active)
        {
            comp.on_play_begin();
        });
}

void audio_system::on_play_end(rtti::context& ctx)
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();
    auto& registry = *scn.registry;

    registry.view<audio_source_component, active_component>().each(
        [&](auto e, auto&& comp, auto&& active)
        {
            comp.on_play_end();
        });

    registry.on_construct<active_component>().disconnect<&on_create_active_component>();
    registry.on_destroy<active_component>().disconnect<&on_destroy_active_component>();

    registry.on_construct<audio_source_component>().disconnect<&on_create_component>();
    registry.on_destroy<audio_source_component>().disconnect<&on_destroy_component>();
}

void audio_system::on_pause(rtti::context& ctx)
{
    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();
    auto& registry = *scn.registry;

    registry.view<audio_source_component>().each(
        [&](auto e, auto&& comp)
        {
            comp.pause();
        });
}

void audio_system::on_resume(rtti::context& ctx)
{
    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();
    auto& registry = *scn.registry;

    registry.view<audio_source_component>().each(
        [&](auto e, auto&& comp)
        {
            comp.resume();
        });
}

void audio_system::on_skip_next_frame(rtti::context& ctx)
{
    delta_t step(1.0f / 60.0f);
    on_frame_update(ctx, step);
}

void audio_system::on_frame_update(rtti::context& ctx, delta_t dt)
{
    APP_SCOPE_PERF("Audio/System Update");
    auto& ev = ctx.get_cached<events>();

    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();
    auto& registry = *scn.registry;

    // update auidio spatial properties from transform
    registry.view<transform_component, audio_listener_component, active_component>().each(
        [&](auto e, auto&& transform, auto&& comp, auto&& active)
        {
            comp.update(transform.get_transform_global(), dt);
        });

    registry.view<transform_component, audio_source_component, active_component>().each(
        [&](auto e, auto&& transform, auto&& comp, auto&& active)
        {
            comp.update(transform.get_transform_global(), dt);
        });
}

} // namespace unravel
