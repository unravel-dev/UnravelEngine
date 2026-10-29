#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/frame_times.h>
#include <ospp/event.h>

#include "integration/imgui.h"
#include "loading_page.h"

#include <chrono>

namespace unravel
{
struct loading_screen;

class imgui_interface
{
public:
    imgui_interface(rtti::context& ctx);
    ~imgui_interface();

    /// Phase 1: creates ImGui context, embedded shaders, fonts. No asset dependencies.
    auto init_basic(rtti::context& ctx) -> bool;
    /// Phase 2: creates the cubemap shader program from compiled editor:/ assets.
    auto init_finalize(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    /// Renders a single loading frame outside the normal event loop.
    /// Pumps OS events, draws the loading page, and presents the frame.
    void render_loading_frame(rtti::context& ctx, const loading_screen& screen);

private:
    void on_frame_ui_render(rtti::context& ctx, const frame_times& times);
    void on_os_event(rtti::context& ctx, os::event& e);

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
    bool inited_{};

    std::chrono::steady_clock::time_point last_frame_time_;
    loading_page loading_page_{};
};
} // namespace unravel
