#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <deploy/deploy_settings.h>
#include <editor/deploy/deploy_pipeline.h>
#include <editor/deploy/deploy_preflight.h>

#include "deploy_content_view.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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
    /// Run Last Build: installs the package of an earlier Android deploy on the chosen device and starts
    /// it, its steps shown as a deploy's. @return False while a deploy runs.
    auto run_last_android_build(rtti::context& ctx, const deploy_settings& params) -> bool;
    /// The steps of the running deploy, or of the last one until the window is closed.
    auto get_jobs() const -> const deployment::deploy_jobs&;
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

    /// Closes the window. A finished deploy's steps go with it; a running deploy keeps them, since the
    /// toolbar shows its progress.
    void close_window();
    void draw_ui(rtti::context& ctx);
    void draw_tabs(rtti::context& ctx);
    /// A tab's body: as tall as its content between a floor and a share of the screen, scrolling past it.
    void draw_tab_body(rtti::context& ctx, tab_drawer draw);
    void draw_build_tab(rtti::context& ctx);
    /// The Build tab's left column: the product, the deploy folder and running the game, packaging.
    void draw_common_settings(rtti::context& ctx);
    /// No deploy folder is set: says so, with a button that sets the project's own (.deploy).
    void draw_missing_location_notice(rtti::context& ctx);
    void draw_content_tab(rtti::context& ctx);
    void draw_releases_tab(rtti::context& ctx);
    /// Patch Against Release as a pick of the kept releases. @return True when the pick changed.
    auto draw_patch_release_picker(rtti::context& ctx) -> bool;
    void draw_kept_releases(rtti::context& ctx);
    /// A tab per platform: the selected one is what the deploy builds for, and shows its settings.
    void draw_platform_tabs(rtti::context& ctx);
    /// The desktop settings: the player and its libraries.
    void draw_desktop_section(rtti::context& ctx);
    /// The Android settings: the device, the game's id and version, its orientation and signing.
    void draw_android_section(rtti::context& ctx);
    /// The device Deploy and Run installs on, as a pick of the devices adb lists. @return True when the pick changed.
    auto draw_android_device_picker(rtti::context& ctx) -> bool;
    /// Run Last Build, Follow Log, Clear Data and Uninstall for the game on the chosen device.
    void draw_android_device_actions(rtti::context& ctx);
    /// The game on the device was signed with another key: Uninstall and Install, after a confirmation.
    void draw_install_conflict();
    void draw_status(rtti::context& ctx);
    void draw_actions(rtti::context& ctx);
    void keep_release(rtti::context& ctx);
    void remove_release(rtti::context& ctx, const std::string& name);
    void refresh_releases();

    imgui_panels* parent_{};
    bool show_request_{};

    deployment::deploy_jobs deploy_jobs_;
    /// The folder the steps in deploy_jobs_ build into.
    fs::path deployed_location_;
    /// What keeps the project from deploying, checked again a moment after the settings change.
    deployment::problems_cache problems_;
    deploy_content_view content_view_;
    /// The kept releases, read when the Releases tab comes into view, a release is kept or the picker opens.
    std::vector<kept_release> releases_;
    /// The frame the Releases tab was last drawn in: a tab drawn again after a gap reads the releases anew.
    int releases_tab_frame_{-1};
    /// What the last Keep as Release did, shown under the releases.
    std::string release_message_;
    bool is_release_message_error_{};
    /// Whether the Android devices were listed since the window opened.
    bool has_listed_android_devices_{};
    /// The platform whose tab the Build tab showed last; empty before it was drawn.
    std::optional<deploy_platform> shown_platform_;
    /// The Android install conflicts already shown: a new one opens the window.
    std::uint32_t seen_install_conflicts_{};
};
} // namespace unravel
