#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <ospp/event.h>

#include "integration/imgui.h"

#include <string>

namespace unravel
{

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
    /// Pumps OS events, draws a loading overlay, and presents the frame.
    void render_loading_frame(rtti::context& ctx,
                              const std::string& stage,
                              size_t completed,
                              size_t total,
                              const std::string& current_job = {});

private:
    void draw_loading_overlay(const std::string& stage,
                              size_t completed,
                              size_t total,
                              const std::string& current_job);
    void on_frame_ui_render(rtti::context& ctx, delta_t dt);
    void on_os_event(rtti::context& ctx, os::event& e);

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
    bool inited_{};
};
} // namespace unravel
