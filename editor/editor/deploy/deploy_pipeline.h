#pragma once

#include <context/context.hpp>
#include <deploy/deploy_settings.h>
#include <engine/threading/threader.h>
#include <filesystem/filesystem.h>

#include <string>
#include <vector>

/// Building the game into the deploy folder.
namespace unravel::deployment
{

/// One step of a deploy: its name and its job, done or failed (a failed job's future rethrows its error).
struct deploy_job
{
    std::string name;
    tpp::shared_future<void> job;
};

/// A deploy's steps, in the order they run.
using deploy_jobs = std::vector<deploy_job>;

/**
 * @brief Starts a deploy with these settings and returns its steps at once.
 *
 * Everything the steps read is resolved on the main thread first (paths, asset indices, what ships, the
 * cooked prefabs), so the steps touch no protocol and no asset manager while they run; only the script
 * library's build lists the script sources itself, as the editor's own builds do. A check that
 * fails before the folder is touched (uncompiled assets, a prefab cycle) is reported as a failed step.
 * When every step succeeded the game is started (Run After Deploy) or its folder shown. Call only when
 * can_deploy says it can; main thread.
 */
auto deploy_project(rtti::context& ctx, const deploy_settings& params) -> deploy_jobs;

/// Whether a deploy started here is still running: the project must not close or change under it.
auto is_deploy_running() -> bool;

/// Stops the running deploy: steps that have not started are skipped, the cooks stop between two assets and the
/// Android package build is ended. The folder keeps an unfinished build. Nothing without a running deploy.
void cancel_deploy();

/// Why a finished step failed; empty while it runs and once it succeeded.
auto get_step_error(const tpp::shared_future<void>& step) -> std::string;

/// Starts the built game with the command line options `arguments` and returns at once; it keeps running
/// when the editor closes.
void run_game(const fs::path& executable, const std::vector<std::string>& arguments);

} // namespace unravel::deployment
