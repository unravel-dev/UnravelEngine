#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <ospp/event.h>

#include "panels/panel.h"
#include "start_page/start_page.h"

namespace unravel
{

class hub
{
public:
    hub(rtti::context& ctx);
    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    void open_project_settings(rtti::context& ctx, const std::string& hint);

    auto get_panels() -> imgui_panels& { return panels_; }
    auto get_panels() const -> const imgui_panels& { return panels_; }

private:

    void on_project_opened(rtti::context& ctx);
    void on_frame_update(rtti::context& ctx, delta_t dt);
    void on_frame_before_render(rtti::context& ctx, delta_t dt);
    void on_frame_render(rtti::context& ctx, delta_t dt);
    void on_frame_ui_render(rtti::context& ctx, delta_t dt);
    void on_play_before_begin(rtti::context& ctx);
    void on_play_begin(rtti::context& ctx);
    void on_play_after_end(rtti::context& ctx);
    void on_script_recompile(rtti::context& ctx, const std::string& protocol, uint64_t version);
    void on_os_event(rtti::context& ctx, os::event& e);

    void on_start_page_render(rtti::context& ctx);
    void on_opened_project_render(rtti::context& ctx);

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);

    imgui_panels panels_{};

    /// Shown while no project is open.
    start_page start_page_{};
};
} // namespace unravel
