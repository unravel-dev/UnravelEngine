#include "deploy_pipeline.h"
#include "deploy_preflight.h"
#include "deploy_runtime.h"

#include <deploy/content.h>
#include <deploy/dependencies.h>
#include <deploy/files.h>
#include <deploy/keys.h>
#include <deploy/protocol_data.h>
#include <deploy/releases.h>
#include <editor/editing/prefab_bake.h>
#include <editor/system/project_manager.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <logging/logging.h>
#include <process/process.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>

namespace unravel::deployment
{
namespace
{
// Step names, as the Deploy window and the deploy tool list them.
constexpr const char* STEP_CHECK_ASSETS = "Checking Assets";
constexpr const char* STEP_COOK = "Cooking Prefabs";
constexpr const char* STEP_DEPENDENCIES = "Dependencies";
constexpr const char* STEP_SETTINGS = "Project Settings";
constexpr const char* STEP_PROJECT_DATA = "Project Data";
constexpr const char* STEP_ENGINE_DATA = "Engine Data";
/// Uncompiled assets named in the failed step before "and N more".
constexpr std::size_t LISTED_UNCOMPILED_MAX = 10;
constexpr const char* COOK_STAGING_PREFIX = "unravel-deploy-cook-";
constexpr const char* DEPENDENCY_SCANNER = "editor:/tools/dependencies/Dependencies.exe";
constexpr const char* SETTINGS_FOLDER = "settings";

std::atomic<int> running_deploys{0};

/// Everything the steps of one deploy read, resolved on the main thread before they start.
struct deploy_plan
{
    deploy_settings params;
    fs::path executable;
    fs::path player;
    fs::path dependency_scanner;
    fs::path settings_source;
    fs::path settings_destination;
    protocol_job app;
    protocol_job engine;
    runtime_plan runtime;
    fs::path cook_staging;
};

struct deploy_step
{
    std::string name;
    std::function<void()> run;
};

/// A deploy refused before the folder is touched: one step that fails with the reason.
auto make_failed_step(threader& th, const std::string& name, const std::string& reason) -> deploy_jobs
{
    APPLOG_ERROR("Deploy aborted: {}", reason);
    deploy_jobs jobs;
    jobs[name] = th.pool
                     ->schedule(name,
                                [reason]()
                                {
                                    throw std::runtime_error(reason);
                                })
                     .share();
    return jobs;
}

/// Shipped assets of a protocol's index without their compiled files. With the compiler idle that is a
/// source it failed to compile, and a build without it silently lacks the asset.
void find_uncompiled_assets(const asset_database::database_t& database,
                            const fs::path& protocol_root,
                            const std::unordered_set<std::string>& left_out,
                            std::vector<std::string>& uncompiled)
{
    for(const auto& [uid, meta] : database)
    {
        if(left_out.contains(fs::make_path_key(meta.location)))
        {
            continue;
        }
        const auto paths = get_compiled_paths(meta.location, protocol_root);
        const bool is_missing = std::any_of(paths.begin(),
                                            paths.end(),
                                            [](const fs::path& path)
                                            {
                                                fs::error_code ec;
                                                return !fs::exists(path, ec);
                                            });
        if(is_missing)
        {
            uncompiled.push_back(meta.location);
        }
    }
}

auto describe_uncompiled_assets(const std::vector<std::string>& uncompiled) -> std::string
{
    std::string listed;
    for(std::size_t i = 0; i < uncompiled.size() && i < LISTED_UNCOMPILED_MAX; ++i)
    {
        listed += (i == 0 ? "" : ", ") + uncompiled[i];
    }
    if(uncompiled.size() > LISTED_UNCOMPILED_MAX)
    {
        listed += fmt::format(" and {} more", uncompiled.size() - LISTED_UNCOMPILED_MAX);
    }
    return fmt::format("{} asset(s) have no compiled file (the log says why they failed to compile). Fix or remove "
                       "them and deploy again: {}",
                       uncompiled.size(),
                       listed);
}

/// A folder no other deploy uses: two editors cooking at once must not share one.
auto make_cook_staging() -> fs::path
{
    std::random_device device;
    return fs::temp_directory_path() /
           fmt::format("{}{}-{:08x}", COOK_STAGING_PREFIX, process::get_current_process_id(), device());
}

/// The path keys of the project assets the content selection leaves out; empty when everything ships.
auto find_left_out_assets(asset_manager& am, const deploy_settings& params, const std::string& startup_scene)
    -> std::unordered_set<std::string>
{
    if(!is_filtered(params))
    {
        return {};
    }
    const auto source = make_project_content_source(am);
    const auto found = needs_walk(params) ? detect_content(source, params, startup_scene) : list_content(source);
    auto left_out = get_left_out_keys(params, found);
    APPLOG_INFO("Deploy: shipping {} of {} project assets; {} left out (see the Deploy window's Content tab).",
                found.entries.size() - left_out.size(),
                found.entries.size(),
                left_out.size());
    return left_out;
}

/// Cooks the prefabs into `staging`; why it could not, or empty.
auto cook_prefabs(rtti::context& ctx, const deploy_settings& params, const fs::path& staging) -> std::string
{
    if(!params.deploy_cooked_assets)
    {
        clear_prefab_nesting_marker_once(ctx);
        return {};
    }
    size_t baked = 0;
    const auto report = bake_prefab_nesting(ctx, staging, &baked);
    if(!report.is_valid())
    {
        return report.describe_problem();
    }
    APPLOG_INFO("Deploy: cooked {} asset(s) into staging.", baked);
    return {};
}

auto make_protocol_job(asset_manager& am,
                       const deploy_settings& params,
                       const std::string& protocol,
                       const fs::path& staging,
                       std::unordered_set<std::string> left_out) -> protocol_job
{
    protocol_job job;
    job.protocol = protocol;
    job.compiled_root = fs::resolve_protocol(ex::get_compiled_directory(protocol));
    job.cooked_root = staging / protocol;
    job.protocol_root = params.deploy_location / DATA_FOLDER / protocol;
    job.database = am.get_database_snapshot(protocol + ":/");
    job.left_out = std::move(left_out);
    job.is_packed = params.deploy_packed_assets;
    job.with_debug_symbols = params.deploy_debug_symbols;
    if(!params.deploy_patch_release.empty())
    {
        job.patch_release_pack = get_release_pack(get_project_root(), params.deploy_patch_release, protocol);
    }
    return job;
}

auto make_plan(rtti::context& ctx, const deploy_settings& params, const fs::path& staging, std::unordered_set<std::string> left_out)
    -> deploy_plan
{
    auto& am = ctx.get_cached<asset_manager>();
    deploy_plan plan;
    plan.params = params;
    plan.executable = get_deployed_executable(ctx, params);
    plan.player = fs::resolve_protocol("binary:/" + std::string(PLAYER_NAME) + fs::executable_extension());
    plan.dependency_scanner = fs::resolve_protocol(DEPENDENCY_SCANNER);
    plan.settings_source = fs::resolve_protocol(std::string(CONTENT_PROTOCOL) + ":/" + SETTINGS_FOLDER);
    plan.settings_destination = params.deploy_location / DATA_FOLDER / CONTENT_PROTOCOL / SETTINGS_FOLDER;
    plan.app = make_protocol_job(am, params, CONTENT_PROTOCOL, staging, std::move(left_out));
    plan.engine = make_protocol_job(am, params, ENGINE_PROTOCOL, staging, {});
    plan.runtime = make_runtime_plan(ctx, params);
    plan.cook_staging = staging;
    return plan;
}

/// The player renamed after the project, and the libraries it loads beside it; binaries an older build
/// left at the top of the folder are removed.
void deploy_dependencies(const deploy_plan& plan)
{
    std::set<std::string> shipped;
    for(const auto& dependency : find_dependencies(plan.player, plan.dependency_scanner))
    {
        const fs::path library(dependency);
        copy_file_or_throw(library, plan.params.deploy_location / library.filename());
        shipped.insert(get_binary_key(library));
    }
    copy_file_or_throw(plan.player, plan.executable);
    shipped.insert(get_binary_key(plan.executable));
    remove_stale_binaries(plan.params.deploy_location, shipped);
}

auto make_steps(const std::shared_ptr<const deploy_plan>& plan) -> std::vector<deploy_step>
{
    std::vector<deploy_step> steps;
    if(plan->params.deploy_dependencies)
    {
        steps.push_back({STEP_DEPENDENCIES, [plan]() { deploy_dependencies(*plan); }});
    }
    steps.push_back({STEP_SETTINGS, [plan]() { mirror_or_throw(plan->settings_source, plan->settings_destination); }});
    steps.push_back({STEP_PROJECT_DATA, [plan]() { deploy_protocol_data(plan->app); }});
    steps.push_back({STEP_ENGINE_DATA, [plan]() { deploy_protocol_data(plan->engine); }});
    steps.push_back({plan->runtime.step_name, [plan]() { deploy_runtime(plan->runtime); }});
    return steps;
}

auto has_failed_step(const std::vector<tpp::shared_future<void>>& steps) -> bool
{
    return std::any_of(steps.begin(),
                       steps.end(),
                       [](const tpp::shared_future<void>& step)
                       {
                           return !get_step_error(step).empty();
                       });
}

/// Runs on the main thread once every step ended. A failed step left a partial build: the Deploy window
/// names it, and running or showing that build would only hide the failure.
void finish_deploy(const deploy_plan& plan, const std::vector<tpp::shared_future<void>>& steps)
{
    fs::error_code ec;
    fs::remove_all(plan.cook_staging, ec);
    --running_deploys;
    if(has_failed_step(steps))
    {
        APPLOG_ERROR("Deploy to {} failed. See the Deploy window for the step.", plan.params.deploy_location.generic_string());
        return;
    }
    if(plan.params.deploy_and_run)
    {
        run_game(plan.executable);
        return;
    }
    fs::show_in_graphical_env(plan.params.deploy_location);
}
} // namespace

auto deploy_project(rtti::context& ctx, const deploy_settings& params) -> deploy_jobs
{
    auto& th = ctx.get_cached<threader>();
    auto& am = ctx.get_cached<asset_manager>();
    const auto startup_scene = ctx.get_cached<project_manager>().get_settings().standalone.startup_scene.id();
    auto left_out = find_left_out_assets(am, params, startup_scene);
    // Before anything touches the destination, so a refused deploy leaves the previous build there.
    std::vector<std::string> uncompiled;
    find_uncompiled_assets(am.get_database_snapshot("engine:/"), fs::resolve_protocol("engine:/"), {}, uncompiled);
    find_uncompiled_assets(am.get_database_snapshot("app:/"), get_project_root(), left_out, uncompiled);
    if(!uncompiled.empty())
    {
        return make_failed_step(th, STEP_CHECK_ASSETS, describe_uncompiled_assets(uncompiled));
    }
    const auto staging = make_cook_staging();
    const auto cook_error = cook_prefabs(ctx, params, staging);
    if(!cook_error.empty())
    {
        fs::error_code ec;
        fs::remove_all(staging, ec);
        return make_failed_step(th, STEP_COOK, cook_error);
    }
    fs::error_code ec;
    fs::create_directories(params.deploy_location, ec);
    const auto plan = std::make_shared<const deploy_plan>(make_plan(ctx, params, staging, std::move(left_out)));
    ++running_deploys;
    deploy_jobs jobs;
    std::vector<tpp::shared_future<void>> futures;
    for(auto& step : make_steps(plan))
    {
        auto job = th.pool
                       ->schedule("Deploying " + step.name,
                                  [step]()
                                  {
                                      APPLOG_INFO("Deploying {}...", step.name);
                                      step.run();
                                      APPLOG_INFO("Deploying {} - Done", step.name);
                                  })
                       .share();
        jobs[step.name] = job;
        futures.push_back(job);
    }
    tpp::when_all(futures.begin(), futures.end())
        .then(tpp::this_thread::get_id(),
              [plan](auto all)
              {
                  finish_deploy(*plan, all.get());
              });
    return jobs;
}

auto is_deploy_running() -> bool
{
    return running_deploys.load() > 0;
}

/// A ready future rethrows its job's exception.
auto get_step_error(const tpp::shared_future<void>& step) -> std::string
{
    if(!step.is_ready())
    {
        return {};
    }
    try
    {
        step.get();
    }
    catch(const std::exception& ex)
    {
        return ex.what();
    }
    catch(...)
    {
        return "unknown error";
    }
    return {};
}

void run_game(const fs::path& executable)
{
    // The game runs on its own: the editor neither waits for it nor reads its output, and the game writes
    // its log beside itself rather than into the editor's folder.
    const auto error = process::spawn_detached({fs::to_utf8_string(executable)}, fs::to_utf8_string(executable.parent_path()));
    if(error)
    {
        APPLOG_ERROR("Running {} failed: {}", executable.generic_string(), error.message());
    }
}

} // namespace unravel::deployment
