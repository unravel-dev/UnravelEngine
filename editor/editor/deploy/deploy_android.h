#pragma once

#include "deploy_pipeline.h"

#include <context/context.hpp>
#include <deploy/android.h>
#include <deploy/deploy_settings.h>
#include <filesystem/filesystem.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

/// Android deploys from the editor: the player template beside it, this machine's tools, the devices, and
/// the game running on one.
namespace unravel::deployment
{

/// The Android player beside this editor, checked against the editor's build; looked up once.
auto get_android_template() -> const android_template_lookup&;

/// This machine's Android tools; searched once, and again after refresh_android_devices.
auto get_android_toolchain() -> android_toolchain;

/// The folder an Android deploy writes the game's data/ into: the open project's build folder, beside
/// the Gradle project. It stays between deploys, so the packs update in place.
auto get_android_content_dir() -> fs::path;

/// The application id a deploy with these settings uses: the set one, or com.<company>.<product>.
auto get_android_application_id(rtti::context& ctx, const deploy_settings& params) -> std::string;

/// What keeps these settings from an Android deploy, one sentence each; empty when nothing does.
auto get_android_problems(rtti::context& ctx, const deploy_settings& params) -> std::vector<std::string>;

/**
 * @brief A deploy step the game's session finishes on the device: Install on Device, Start on Device.
 *
 * Done, or failed with a sentence; finishing it again changes nothing. One dropped unfinished fails as
 * stopped, so a deploy waiting on it always ends.
 */
class device_step
{
public:
    device_step();
    ~device_step();
    device_step(const device_step&) = delete;
    auto operator=(const device_step&) -> device_step& = delete;

    auto get_job() const -> const tpp::shared_future<void>&;
    void complete();
    void fail(const std::string& reason);

private:
    std::mutex mutex_;
    bool is_finished_{};
    tpp::promise<void> promise_;
    tpp::shared_future<void> job_;
};

/// What the package step and Deploy and Run read, resolved on the main thread.
struct android_package_plan
{
    android_toolchain tools;
    android_package_params package;
    /// The finished package in the deploy folder.
    fs::path output;
    /// The device Deploy and Run installs on.
    std::string device;
    /// The command line options Deploy and Run starts the game with.
    std::vector<std::string> run_arguments;
    /// Uninstall a copy signed with another key before installing (its saved data goes with it).
    bool replace_installed{};
    /// Uninstall the game before installing: the copy on the device is known to be signed with another key.
    bool uninstall_first{};
    /// Deploy and Run's steps on the device, which the game's session finishes; empty without Deploy and Run.
    std::shared_ptr<device_step> install_step;
    std::shared_ptr<device_step> start_step;
};

/// The device steps of a plan, in their order, for the deploy's step list.
auto get_device_jobs(const android_package_plan& plan) -> deploy_jobs;
/// Fails the plan's device steps that did not finish, with `reason`.
void fail_device_steps(const android_package_plan& plan, const std::string& reason);

auto make_android_package_plan(rtti::context& ctx, const deploy_settings& params) -> android_package_plan;

/// Builds the package with Gradle and puts it into the deploy folder. Throws on failure.
void build_android_package(const android_package_plan& plan);

/**
 * @brief Installs the package on the plan's device and starts it; returns at once.
 *
 * The game's log then shows in the Console until the game ends or stop_android_game. A native crash is
 * symbolized when this editor was built from source with the NDK.
 */
void run_android_game(const android_package_plan& plan);
/// Asks the game's session to stop the game; returns at once.
void stop_android_game();
auto is_android_game_running() -> bool;
/// Stops following the game, which keeps running on the device, and waits for its session to end; for the
/// editor's shutdown, before logging ends.
void end_android_game_session();

/// The device whose installed game was signed with another key (debug and release builds differ), so the
/// last Deploy and Run could not replace it; empty when there is no such conflict.
auto get_android_install_conflict() -> std::string;
/// Counts the conflicts so far, so a window can notice a new one.
auto get_android_install_conflict_count() -> std::uint32_t;
/// Uninstalls the conflicting game, which removes its saved data, then installs and starts the build
/// the conflict stopped. Returns the steps of that, for the Deploy window.
auto replace_android_game() -> deploy_jobs;

/// The devices adb listed at the last refresh.
auto get_android_devices() -> std::vector<android_device>;
/// Whether the devices were listed at least once since the editor started.
auto has_listed_android_devices() -> bool;
/// Lists the devices again in the background, searching the tools again too.
void refresh_android_devices(rtti::context& ctx);
/// Lists the devices again on this thread; adb can take seconds when its server has to start.
void update_android_devices();
auto is_refreshing_android_devices() -> bool;

} // namespace unravel::deployment
