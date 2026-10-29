#pragma once

#include <engine/engine_export.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/frame_times.h>

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

    /// Game time one frame step advances while paused, before the time scale.
    static constexpr float FRAME_STEP_SECONDS = 1.0f / 60.0f;

    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    void toggle(rtti::context& ctx, bool allow_splash = true);
    void set_active(rtti::context& ctx, bool active, bool allow_splash = true);
    void set_paused(rtti::context& ctx, bool paused);
    void toggle_pause(rtti::context& ctx);

    /**
     * @brief Requests a frame step while paused: the next frame runs like an unpaused one.
     *
     * Requests made within one frame queue up and run on consecutive frames.
     */
    void skip_next_frame();

    /**
     * @brief Starts a requested frame step for the current frame. Called by the engine loop
     * before it dispatches the frame.
     * @return True when this paused frame runs as a step.
     */
    auto begin_frame_step() -> bool;

    /**
     * @brief Ends the frame step begin_frame_step started. Called by the engine loop at frame end.
     */
    void end_frame_step();

    void on_frame_update(rtti::context& ctx, const frame_times& times);

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

    /// True while a frame step runs: the frame advances although play mode stays paused.
    [[nodiscard]] auto is_stepping() const -> bool
    {
        return is_stepping_;
    }

    /// True when the simulation must hold still: paused and not running a frame step.
    [[nodiscard]] auto is_simulation_paused() const -> bool
    {
        return is_paused_ && !is_stepping_;
    }

    [[nodiscard]] auto frames_running() const -> uint64_t
    {
        return frames_running_;
    }

    /// Frames the simulation advanced since play began running: unpaused frames and frame steps.
    /// frames_running also counts the frames spent paused.
    [[nodiscard]] auto frames_simulated() const -> uint64_t
    {
        return frames_simulated_;
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
    bool is_stepping_ = false;
    /// Frame steps requested and not yet run.
    uint32_t pending_steps_ = 0;
    uint64_t frames_running_ = 0;
    uint64_t frames_simulated_ = 0;
    splash_scene_state splash_state_{};

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
};

} // namespace unravel
