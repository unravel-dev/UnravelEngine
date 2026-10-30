#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <deploy/deploy_settings.h>
#include <deploy/result.h>
#include <editor/deploy/deploy_pipeline.h>
#include <editor/deploy/deploy_preflight.h>

#include "deploy_content_view.h"
#include "deploy_settings_editor.h"

#include <editor/hub/panels/inspector_panel/inspectors/property_override_view.h>

#include <array>
#include <initializer_list>
#include <cstdint>
#include <string>
#include <vector>

namespace unravel
{
namespace deployment
{
class project_profiles;
} // namespace deployment

class imgui_panels;

/**
 * @brief The Deploy Project window: tabs for what the game is and where it goes (Build), what it
 * ships (Content) and the releases it patches (Releases); under them, always in view, what still
 * keeps it from deploying, the steps of the deploy while it runs, and the buttons.
 *
 * It deploys one settings profile at a time, the deploy profile, chosen in the Build tab. The settings
 * every profile shares are edited for all of them, or as overrides of the deploy profile.
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
    /// Upload to Steam: sends the build in the profile's folder to its Steam depot, its step shown as a deploy's.
    /// @return False while a deploy runs or when deployment::get_steam_upload_problem finds a problem.
    auto start_steam_upload(rtti::context& ctx, const deploy_settings& params) -> bool;
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
    /// The Build tab's left column: the product and the deploy folder, then the deploy settings.
    void draw_common_settings(rtti::context& ctx);
    /// The product settings every profile builds from, marked where the deploy profile overrides them.
    void draw_product_settings(rtti::context& ctx);
    /// What project settings drawn here are marked with: the deploy profile's overrides of them.
    auto make_product_marks(rtti::context& ctx) -> property_override_marks;
    /// The switch between the deploy settings every profile shares and the deploy profile's overrides of them.
    void draw_edit_target(rtti::context& ctx);
    /// What deploy settings are marked with: the deploy profile's overrides, owned while they are edited.
    auto make_deploy_marks(rtti::context& ctx) -> property_override_marks;
    /// The deploy settings' properties `names`, in rows as the inspector draws them, marked where the deploy
    /// profile overrides them; committed once an edit is finished.
    void inspect_deploy_properties(rtti::context& ctx, std::initializer_list<entt::hashed_string> names);
    /// No deploy folder is set: says so, with a button that sets the project's own (.deploy).
    void draw_missing_location_notice(rtti::context& ctx);
    void draw_content_tab(rtti::context& ctx);
    void draw_releases_tab(rtti::context& ctx);
    /// Patch Against Release as a pick of the kept releases. @return True when the pick changed.
    auto draw_patch_release_picker(rtti::context& ctx) -> bool;
    void draw_kept_releases(rtti::context& ctx);
    /// The Build tab's right column: the deploy profile, with its platform's settings.
    void draw_profile_section(rtti::context& ctx);
    /// The deploy profile as a pick of the project's profiles, with New, Rename and Delete.
    void draw_profile_picker(rtti::context& ctx);
    void draw_new_profile_popup(rtti::context& ctx);
    void draw_rename_profile_popup(rtti::context& ctx);
    /// What a desktop profile builds: the player, its icon and where it goes on Steam, saved with the profile once
    /// an edit is finished.
    void draw_desktop_section(rtti::context& ctx);
    /// The Android settings: the device, the game's id and version, its orientation, signing and textures.
    void draw_android_section(rtti::context& ctx);
    /// The deploy profile's Android settings `names`, in rows; saved with the profile once an edit is finished.
    void inspect_android_properties(rtti::context& ctx, std::initializer_list<entt::hashed_string> names);
    /// The properties `props` of `object`, a part of the deploy profile's Android settings, in rows; the
    /// profile is saved once an edit is finished.
    void inspect_android_data(rtti::context& ctx, entt::meta_any& object, const std::vector<entt::meta_data>& props);
    /// Saves the deploy profile's Android settings as edited.
    void save_android_settings(rtti::context& ctx);
    /// Application Id: the id made from Company and Product shows in the empty field and follows them; a typed
    /// id overrides it, with a button back to the generated one.
    void draw_application_id(rtti::context& ctx);
    /// The permissions the game asks for: the named ones, then Other Permissions.
    void draw_android_permissions(rtti::context& ctx);
    /// The device Deploy and Run installs on, as a pick of the devices adb lists. @return True when the pick changed.
    auto draw_android_device_picker(rtti::context& ctx) -> bool;
    /// Run Last Build, Follow Log, Clear Data and Uninstall for the game on the chosen device.
    void draw_android_device_actions(rtti::context& ctx);
    /// The game on the device was signed with another key: Uninstall and Install, after a confirmation.
    void draw_install_conflict();
    void draw_status(rtti::context& ctx);
    void draw_actions(rtti::context& ctx);
    /// Upload to Steam, after a confirmation; disabled with the reason while the build cannot go.
    void draw_steam_upload_button(rtti::context& ctx, const deploy_settings& params);
    void keep_release(rtti::context& ctx);
    void remove_release(rtti::context& ctx, const std::string& name);
    void refresh_releases();
    /// Shows the outcome of a profile action under the profile picker.
    void set_profile_message(const deployment::result& outcome, const std::string& success);

    imgui_panels* parent_{};
    bool show_request_{};

    deployment::deploy_jobs deploy_jobs_;
    /// The folder the steps in deploy_jobs_ build into.
    fs::path deployed_location_;
    /// Whether deploy_jobs_ upload that folder to Steam rather than build it.
    bool is_steam_upload_{};
    /// What keeps the project from deploying, checked again a moment after the settings change.
    deployment::problems_cache problems_;
    deploy_content_view content_view_;
    deploy_settings_editor settings_editor_;
    /// The kept releases of the deploy profile, read when the Releases tab comes into view, a release is
    /// kept or the picker opens.
    std::vector<kept_release> releases_;
    /// The frame the Releases tab was last drawn in: a tab drawn again after a gap reads the releases anew.
    int releases_tab_frame_{-1};
    /// What the last Keep as Release did, shown under the releases.
    std::string release_message_;
    bool is_release_message_error_{};
    /// Whether the Android devices were listed since the window opened.
    bool has_listed_android_devices_{};
    /// The Android install conflicts already shown: a new one opens the window.
    std::uint32_t seen_install_conflicts_{};
    /// The longest profile name the popups take, with its terminating zero.
    static constexpr std::size_t PROFILE_NAME_CAPACITY = 128;
    /// A profile name typed in a popup, and why the profiles do not take it: an error shown under the field,
    /// checked again when the name changes.
    struct profile_name_field
    {
        std::array<char, PROFILE_NAME_CAPACITY> text{};
        std::string checked;
        std::string error;

        /// Checks the name once it changes: whether `profiles` take it, for a rename of `renamed` when set.
        void check(const deployment::project_profiles& profiles, const std::string& renamed = {});
    };
    /// The New Profile popup's fields.
    profile_name_field new_profile_name_;
    target_platform new_profile_platform_{target_platform::desktop};
    /// The profile the new one copies; empty: the platform's starting values.
    std::string new_profile_source_;
    /// The Rename Profile popup's field.
    profile_name_field rename_profile_name_;
    /// What the last profile action did, shown under the profile picker.
    std::string profile_message_;
    bool is_profile_message_error_{};
    /// Why the last revert of a product setting failed, shown under them.
    std::string product_error_;
};
} // namespace unravel
