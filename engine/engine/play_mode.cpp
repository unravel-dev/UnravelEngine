#include "play_mode.h"

#include "events.h"
#include "splash_scene.h"

#include <engine/ecs/ecs.h>
#include <engine/settings/settings.h>
#include <logging/logging.h>
#include <seq/seq.h>

namespace unravel
{

auto play_mode::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);
    auto& ev = ctx.get_cached<events>();
    ev.on_frame_update.connect(sentinel_, 10000, this, &play_mode::on_frame_update);
    return true;
}

auto play_mode::deinit(rtti::context& ctx) -> bool
{
    (void)ctx;
    if(is_active())
    {
        end_play(ctx);
    }
    return true;
}

void play_mode::toggle(rtti::context& ctx, bool allow_splash)
{
    auto action = seq::delay(0ms);
    action.on_end.connect([this, &ctx, allow_splash]()
    {
        set_active(ctx, !is_active(), allow_splash);
    });
    seq::queue(action, "play_mode");
}

void play_mode::set_active(rtti::context& ctx, bool active, bool allow_splash)
{
    if(is_active() == active)
    {
        return;
    }
    if(active)
    {
        begin_play(ctx, allow_splash);
    }
    else
    {
        end_play(ctx);
    }
}

void play_mode::set_paused(rtti::context& ctx, bool paused)
{
    if(paused && !is_active())
    {
        return;
    }
    if(is_paused_ == paused)
    {
        return;
    }
    is_paused_ = paused;
    auto& ev = ctx.get_cached<events>();
    is_paused_ ? ev.on_pause(ctx) : ev.on_resume(ctx);
}

void play_mode::toggle_pause(rtti::context& ctx)
{
    auto action = seq::delay(0ms);
    action.on_begin.connect([this, &ctx]()
    {
        set_paused(ctx, !is_paused_);
    });
    seq::queue(action, "play_mode");
}

void play_mode::skip_next_frame(rtti::context& ctx)
{
    if(!is_active())
    {
        return;
    }
    if(!is_paused_)
    {
        return;
    }
    ctx.get_cached<events>().on_skip_next_frame(ctx);
}

void play_mode::on_frame_update(rtti::context& ctx, delta_t dt)
{
    (void)dt;
    if(current_phase_ != phase::splash)
    {
        return;
    }
    splash_scene::update(ctx, splash_state_);
    if(splash_scene::is_finished(splash_state_))
    {
        enter_running(ctx);
    }
}

void play_mode::begin_play(rtti::context& ctx, bool allow_splash)
{
    auto& ev = ctx.get_cached<events>();
    ev.on_play_before_begin(ctx);

    const bool show_splash = allow_splash && should_show_splash(ctx);
    if(show_splash)
    {
        current_phase_ = phase::splash;
        is_paused_ = false;
        frames_running_ = 0;
        auto& ec = ctx.get_cached<ecs>();
        splash_scene::setup(ctx, ec.get_scene(), splash_state_);
    }
    else
    {
        is_paused_ = false;
        frames_running_ = 0;
        enter_running(ctx);
    }
}

void play_mode::enter_running(rtti::context& ctx)
{
    current_phase_ = phase::running;
    frames_running_ = 0;
    splash_scene::teardown(splash_state_);
    ctx.get_cached<events>().on_play_begin(ctx);
}

void play_mode::end_play(rtti::context& ctx)
{
    auto& ev = ctx.get_cached<events>();
    if(is_paused_)
    {
        set_paused(ctx, false);
    }
    ev.on_play_end(ctx);
    current_phase_ = phase::inactive;
    is_paused_ = false;
    frames_running_ = 0;
    splash_scene::teardown(splash_state_);
    ev.on_play_after_end(ctx);
}

auto play_mode::should_show_splash(rtti::context& ctx) const -> bool
{
    if(!ctx.has<settings>())
    {
        return splash_scene::has_content(ctx);
    }
    const auto& splash = ctx.get<settings>().splash;
    return splash.enabled && splash_scene::has_content(ctx);
}

void play_mode::on_simulation_frame()
{
    ++frames_running_;
}

} // namespace unravel
