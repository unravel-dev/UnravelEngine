#pragma once

#include <engine/engine_export.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include "splash_scene.h"

namespace unravel
{

struct events;

/// Owns play-mode state and orchestrates the splash -> running lifecycle.
struct play_mode
{
    enum class phase
    {
        inactive,
        splash,
        running,
    };

    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    void toggle(rtti::context& ctx, bool allow_splash = true);
    void set_active(rtti::context& ctx, bool active, bool allow_splash = true);
    void set_paused(rtti::context& ctx, bool paused);
    void toggle_pause(rtti::context& ctx);
    void skip_next_frame(rtti::context& ctx);

    void on_frame_update(rtti::context& ctx, delta_t dt);

    [[nodiscard]] auto is_active() const -> bool
    {
        return current_phase_ != phase::inactive;
    }

    [[nodiscard]] auto is_simulation_running() const -> bool
    {
        return current_phase_ == phase::running;
    }

    [[nodiscard]] auto is_splash() const -> bool
    {
        return current_phase_ == phase::splash;
    }

    [[nodiscard]] auto is_paused() const -> bool
    {
        return is_paused_;
    }

    [[nodiscard]] auto frames_running() const -> uint64_t
    {
        return frames_running_;
    }

    void on_simulation_frame();

    [[nodiscard]] auto current_phase() const -> phase
    {
        return current_phase_;
    }

private:
    void begin_play(rtti::context& ctx, bool allow_splash = true);
    void end_play(rtti::context& ctx);
    void enter_running(rtti::context& ctx);
    [[nodiscard]] auto should_show_splash(rtti::context& ctx) const -> bool;

    phase current_phase_ = phase::inactive;
    bool is_paused_ = false;
    uint64_t frames_running_ = 0;
    splash_scene_state splash_state_{};

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
};

} // namespace unravel
