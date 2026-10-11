#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <editor/editing/editor_actions.h>

namespace unravel
{

class imgui_panels;

/**
 * @brief The Deploy Project window: what the game is and where it goes, what still keeps it from
 * deploying, and the steps of the deploy while it runs.
 */
class deploy_panel
{
public:
    deploy_panel(imgui_panels* parent);

    void on_frame_ui_render(rtti::context& ctx, const char* name);

    void show(bool s);

    /// Starts a deploy, or opens the window to show what keeps it from deploying.
    void deploy_and_run(rtti::context& ctx, const deploy_settings& params);
    /// Starts a deploy unless one is already running or the project cannot deploy. No UI.
    /// @return True when it started.
    auto start_deploy(rtti::context& ctx, const deploy_settings& params) -> bool;
    /// The steps of the running or most recent deploy; empty after the window is closed.
    auto get_jobs() const -> const std::map<std::string, tpp::shared_future<void>>&;
    auto is_deploying() const -> bool;
    /// Share of the deploy steps that are done, 0 to 1; 1 while nothing deploys.
    auto get_progress() const -> float;

private:
    void draw_ui(rtti::context& ctx);
    void draw_settings(rtti::context& ctx);
    void draw_status(rtti::context& ctx);
    void draw_actions(rtti::context& ctx);

    imgui_panels* parent_{};
    bool show_request_{};

    std::map<std::string, tpp::shared_future<void>> deploy_jobs_;
};
} // namespace unravel
