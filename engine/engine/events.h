#pragma once
#include <engine/engine_export.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <hpp/event.hpp>
#include <ospp/event.h>

namespace unravel
{

struct events
{
    /// engine loop events
    hpp::event<void(rtti::context&, delta_t)> on_frame_begin;
    hpp::event<void(rtti::context&, delta_t)> on_frame_update;
    hpp::event<void(rtti::context&, delta_t)> on_frame_fixed_update;
    hpp::event<void(rtti::context&, delta_t)> on_frame_before_render;
    hpp::event<void(rtti::context&, delta_t)> on_frame_render;
    hpp::event<void(rtti::context&, delta_t)> on_frame_end;

    /// engine play events
    hpp::event<void(rtti::context&)> on_play_before_begin;
    hpp::event<void(rtti::context&)> on_play_begin;
    hpp::event<void(rtti::context&)> on_play_end;
    hpp::event<void(rtti::context&)> on_play_after_end;


    hpp::event<void(rtti::context&)> on_pause;
    hpp::event<void(rtti::context&)> on_resume;
    hpp::event<void(rtti::context&)> on_skip_next_frame;

    /// os events
    hpp::event<void(rtti::context&, os::event& e)> on_os_event;

    hpp::event<void(rtti::context&, const std::string& protocol, uint64_t version)> on_script_recompile;

    void toggle_play_mode(rtti::context& ctx);
    void set_play_mode(rtti::context& ctx, bool play);
    void toggle_pause(rtti::context& ctx);
    void set_paused(rtti::context& ctx, bool paused);
    void skip_next_frame(rtti::context& ctx);

    bool is_playing{};
    bool is_paused{};
    uint64_t frames_playing{};
};

struct deploy
{

};

} // namespace unravel
