#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <editor/editing/editor_actions.h>

namespace unravel
{

class imgui_panels;

/**
 * @brief The Deploy Project window: tabs for what the game is and where it goes (Build), what it
 * ships (Content) and the releases it patches (Releases); under them, always in view, what still
 * keeps it from deploying, the steps of the deploy while it runs, and the buttons.
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
    /// A kept release as the Releases tab lists it.
    struct kept_release
    {
        std::string name;
        /// Bytes of its packs, every protocol.
        std::uint64_t size{};
    };

    using tab_drawer = void (deploy_panel::*)(rtti::context& ctx);

    void draw_ui(rtti::context& ctx);
    void draw_tabs(rtti::context& ctx);
    /// A tab's body: as tall as its content between a floor and a share of the screen, scrolling past it.
    void draw_tab_body(rtti::context& ctx, tab_drawer draw);
    void draw_build_tab(rtti::context& ctx);
    void draw_content_tab(rtti::context& ctx);
    void draw_releases_tab(rtti::context& ctx);
    /// Patch Against Release as a pick of the kept releases. @return True when the pick changed.
    auto draw_patch_release_picker(rtti::context& ctx) -> bool;
    void draw_kept_releases(rtti::context& ctx);
    void draw_status(rtti::context& ctx);
    void draw_actions(rtti::context& ctx);
    void keep_release(rtti::context& ctx);
    void refresh_releases();

    imgui_panels* parent_{};
    bool show_request_{};

    std::map<std::string, tpp::shared_future<void>> deploy_jobs_;
    /// The kept releases, read when the window opens, a release is kept or the picker opens.
    std::vector<kept_release> releases_;
    /// What the last Keep as Release did, shown under the releases.
    std::string release_message_;
    bool is_release_message_error_{};
};
} // namespace unravel
