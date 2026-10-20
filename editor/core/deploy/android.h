#pragma once

#include "deploy_settings.h"

#include <filesystem/filesystem.h>

#include <cstdint>
#include <string>
#include <vector>

/// Android deploys: the player template beside the editor, the tools that package and install a game,
/// and the devices they install it on.
namespace unravel::deployment
{

/// The player template's folder name under the editor's players/ folder.
constexpr const char* ANDROID_TEMPLATE_NAME = "android-arm64";

/**
 * @brief The Android player template beside the editor (cmake/android_player.cmake).
 *
 * The engine build assembles it: the stripped player library, the Gradle project, SDL's Java sources and
 * the .NET runtime, plus a stamp naming the build that made it. Android deploys package it with the game.
 */
struct android_template
{
    /// <editor folder>/players/android-arm64.
    fs::path root;
    std::string engine_version;
    std::string configuration;
    std::string abi;
    std::string dotnet_runtime;
    /// The folder of the unstripped player on the machine that built the template; crashes are symbolized
    /// with it when it exists.
    fs::path symbols_dir;

    /// The Gradle project, which a deploy copies before building it.
    auto get_project_dir() const -> fs::path;
    /// The folder holding libmain.so.
    auto get_player_dir() const -> fs::path;
    auto get_sdl_java_dir() const -> fs::path;
    /// The .NET runtime: native/ and lib/<framework>/.
    auto get_dotnet_dir() const -> fs::path;
};

/// A template found, or why there is none.
struct android_template_lookup
{
    android_template found;
    /// Sentences for the Deploy window; empty when the template was found.
    std::string error;

    auto is_found() const -> bool
    {
        return error.empty();
    }
};

/**
 * @brief The template under `root` when it is complete and this editor's build made it.
 *
 * A template from another engine version or configuration is refused: its player would not match the
 * data and scripts the editor deploys.
 */
auto find_android_template(const fs::path& root, const std::string& engine_version, const std::string& configuration)
    -> android_template_lookup;

/**
 * @brief The application id a project gets when it names none: com.<company>.<product>.
 *
 * Lowercase; other characters become '_' (without runs or edges), a part starting with a digit gets "app"
 * in front, and an empty company or product is "unravel" or "game".
 */
auto make_default_application_id(const std::string& company, const std::string& product) -> std::string;

/// Why `id` is no valid application id, as a sentence; empty when it is one. Valid: two or more parts
/// separated by '.', each starting with a letter and holding only letters, digits and '_'.
auto check_application_id(const std::string& id) -> std::string;

/// The manifest's android:screenOrientation value for `orientation`.
auto get_screen_orientation(android_orientation orientation) -> std::string;

/// A device adb lists.
struct android_device
{
    /// What adb -s takes: a USB serial, ip:port, or a wireless debugging service name.
    std::string serial;
    /// "device" when it accepts commands; "unauthorized" until the phone allows this computer, "offline".
    std::string state;
    /// The model adb reports, '_' read as ' '; empty when it reports none.
    std::string model;

    auto is_ready() const -> bool
    {
        return state == "device";
    }

    /// The model and serial, for a device list.
    auto get_label() const -> std::string;
};

/// The devices in the output of `adb devices -l`.
auto parse_android_devices(const std::string& adb_output) -> std::vector<android_device>;

/// One line of `adb logcat -v threadtime`.
struct logcat_line
{
    std::string pid;
    /// V, D, I, W, E or F.
    char level{'I'};
    std::string tag;
    std::string message;
};

/// Reads a logcat line into `line`; false for text of another format (the "--------- beginning of"
/// markers).
auto parse_logcat_line(const std::string& text, logcat_line& line) -> bool;

/// Where this machine's Android tools are looked for.
struct android_toolchain_search
{
    /// ANDROID_HOME, ANDROID_SDK_ROOT, then the folder Android Studio installs the SDK to.
    std::vector<fs::path> sdk_candidates;
    /// JAVA_HOME, then the JDK bundled with Android Studio.
    std::vector<fs::path> jdk_candidates;
};

/// The places of this machine: environment variables first, then Android Studio's default folders.
auto get_android_toolchain_search() -> android_toolchain_search;

/// The Android tools a deploy runs.
struct android_toolchain
{
    fs::path sdk;
    /// sdk/platform-tools/adb.
    fs::path adb;
    /// The JDK's java executable, which runs Gradle.
    fs::path java;
    /// What is missing, as sentences for the Deploy window; empty when every tool was found.
    std::string error;

    auto is_complete() const -> bool
    {
        return error.empty();
    }
};

/// The first SDK with platform-tools and the first JDK among the candidates.
auto find_android_toolchain(const android_toolchain_search& search) -> android_toolchain;

/// What a deploy packages and how.
struct android_package_params
{
    android_template player;
    /// The writable copy of the template's Gradle project that Gradle builds in.
    fs::path project_dir;
    /// The deploy folder: the data/ Gradle puts into the package.
    fs::path content_dir;
    std::string application_id;
    std::string name;
    std::string version_name;
    std::uint32_t version_code{1};
    android_orientation orientation{android_orientation::landscape};
    /// A release build, signed with `keystore`; otherwise a debug build, signed with the SDK's debug keystore.
    bool is_release{};
    fs::path keystore;
    std::string key_alias;
};

/// The command line that builds the package: the JDK's java running the project's Gradle wrapper.
auto make_gradle_arguments(const android_toolchain& tools, const android_package_params& params)
    -> std::vector<std::string>;

/// The local.properties text that points Gradle at the SDK.
auto make_gradle_local_properties(const fs::path& sdk) -> std::string;

/// The package Gradle writes for a debug or release build of the project in `project_dir`.
auto get_built_package(const fs::path& project_dir, bool is_release) -> fs::path;

/// The command line that starts the game's activity on `device`. The game's `run_arguments` go in an
/// extra of the start intent, which the player's activity hands to the game as its command line.
auto make_start_arguments(const android_toolchain& tools,
                          const std::string& device,
                          const std::string& application_id,
                          const std::vector<std::string>& run_arguments) -> std::vector<std::string>;

} // namespace unravel::deployment
