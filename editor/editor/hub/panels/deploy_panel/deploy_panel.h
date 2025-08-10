#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <editor/editing/editor_actions.h>

namespace unravel
{

class imgui_panels;

class deploy_panel
{
public:
    deploy_panel(imgui_panels* parent);

    void on_frame_ui_render(rtti::context& ctx, const char* name);

    void show(bool s);

private:
    void draw_ui(rtti::context& ctx);
    auto get_progress() const -> float;

    imgui_panels* parent_{};
    bool show_request_{};

    std::map<std::string, tpp::shared_future<void>> deploy_jobs_;
};
} // namespace unravel
