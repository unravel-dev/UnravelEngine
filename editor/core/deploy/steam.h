#pragma once

#include "deploy_settings.h"

#include <filesystem/filesystem.h>

#include <cstdint>
#include <string>
#include <vector>

/// Uploading a desktop build to Steam with steamcmd, Valve's command line client: the app build script
/// it runs, its command line and what its output says.
namespace unravel::deployment
{

/// The steamcmd an editor runs when its settings name none: the one on PATH.
constexpr const char* DEFAULT_STEAMCMD = "steamcmd";
/// The branch every player gets. Steam sets a build live on it only on the Steamworks website.
constexpr const char* STEAM_DEFAULT_BRANCH = "default";

/// One upload, as the app build script states it.
struct steam_build
{
    std::uint32_t app_id{};
    std::uint32_t depot_id{};
    /// The build's description in Steamworks.
    std::string description;
    /// The folder whose files the depot gets: the profile's build.
    fs::path content_root;
    /// Where steamcmd writes its logs and the build's manifests.
    fs::path build_output;
    /// The branch the build is set live on; empty: none.
    std::string branch;
};

/// Why the settings cannot upload, as one sentence; empty when they can.
auto check_steam_settings(const steam_deploy_settings& steam) -> std::string;

/**
 * @brief The app build script (VDF) of an upload: every file of the content root goes to the root of the
 *        depot, but the logs a game run with --log-here leaves beside its executable.
 */
auto make_steam_app_build_script(const steam_build& build) -> std::string;

/**
 * @brief The command line that logs `account` in, runs the app build script and quits. It logs in with the
 *        login steamcmd keeps from an earlier one in a terminal, and fails instead of asking for a password,
 *        which nothing could type.
 * @param steamcmd The steamcmd executable; empty for the one on PATH.
 */
auto make_steamcmd_arguments(const fs::path& steamcmd, const std::string& account, const fs::path& script)
    -> std::vector<std::string>;

/// The build id steamcmd reports for a finished build; empty when its output reports no finished build.
auto find_steam_build_id(const std::string& output) -> std::string;

/// Whether steamcmd's output says the account could not log in, such as without a kept login.
auto is_steam_login_failure(const std::string& output) -> bool;

} // namespace unravel::deployment
