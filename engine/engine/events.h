#pragma once
#include <engine/engine_export.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <hpp/event.hpp>
#include <ospp/event.h>

namespace unravel
{

/**
 * @brief Slot priorities for the frame events.
 *
 * hpp::event stores slots in a multimap ordered by descending priority, so a
 * higher value runs earlier. Slots that share a priority fall back to
 * registration order, which depends on whether a system connects from its
 * constructor (ctx.add<>) or from init() - a distinction no caller should have
 * to reason about. Anything with a real ordering contract states it here.
 */
struct frame_update_priority
{
    /// Play-mode state transitions. Must observe the frame before anyone else.
    static constexpr int64_t play_mode = 10000;

    /// Editor selection / gizmo bookkeeping.
    static constexpr int64_t editing = 1000;

    /// Default band: scene systems (transforms, cameras, models, animation,
    /// probes), physics and audio. Ordering within the band is registration
    /// order and is deliberately not contractual.
    static constexpr int64_t scene_systems = 0;

    /// Gameplay scripts. Runs AFTER the scene-system band so script code sees a
    /// fully evaluated animation pose: animation_system rewrites every animated
    /// bone's local transform, so bone writes made before it (IK, procedural
    /// offsets) would be discarded in the same frame. Writes made here still
    /// land before model_system's skinning pass, which runs in
    /// on_frame_before_render.
    static constexpr int64_t scripts = -1000;

    /// Script LateUpdate - after every other frame-update consumer.
    static constexpr int64_t scripts_late = -100000;
};

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
    hpp::event<void(rtti::context&)> on_project_opened;

    /// os events
    hpp::event<void(rtti::context&, os::event& e)> on_os_event;

    hpp::event<void(rtti::context&, const std::string& protocol, uint64_t version)> on_script_recompile;

    void toggle_play_mode(rtti::context& ctx);
    void set_play_mode(rtti::context& ctx, bool play);
    void toggle_pause(rtti::context& ctx);
    void set_paused(rtti::context& ctx, bool paused);
    void skip_next_frame(rtti::context& ctx);
};

struct deploy
{

};

} // namespace unravel
