#pragma once

#include "deploy_pipeline.h"

#include <context/context.hpp>
#include <deploy/deploy_settings.h>

#include <string>

/// Uploading a desktop profile's build to Steam with steamcmd.
namespace unravel::deployment
{

/// Whether the profile goes to Steam: a desktop profile with a Steam app id.
auto has_steam_upload(const deploy_settings& params) -> bool;

/// Why the profile's build cannot go to Steam now, as one sentence; empty when it can.
auto get_steam_upload_problem(rtti::context& ctx, const deploy_settings& params) -> std::string;

/**
 * @brief Uploads the build in the profile's folder to its Steam depot with steamcmd, and sets it live on the
 *        profile's branch when it names one. One step, which fails with steamcmd's last output. Call only when
 *        get_steam_upload_problem finds nothing; main thread.
 */
auto upload_to_steam(rtti::context& ctx, const deploy_settings& params) -> deploy_jobs;

} // namespace unravel::deployment
