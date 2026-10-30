#include "deploy_steam.h"
#include "deploy_preflight.h"

#include <deploy/steam.h>
#include <editor/system/project_manager.h>
#include <engine/threading/threader.h>
#include <logging/logging.h>
#include <subprocess/subprocess.hpp>

#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>

namespace unravel::deployment
{
namespace
{
constexpr const char* STEP_STEAM_UPLOAD = "Steam Upload";
/// The editor's folder of upload scripts and steamcmd's build output, one folder per project and profile.
constexpr const char* STEAM_STAGING_FOLDER = "unravel-steam";
constexpr const char* BUILD_OUTPUT_FOLDER = "output";
/// The lines of steamcmd's output a failed upload quotes.
constexpr std::size_t STEAMCMD_ERROR_LINES = 15;

/// Everything the upload step reads, resolved on the main thread.
struct steam_upload_plan
{
    steam_build build;
    std::string account;
    /// Empty: the steamcmd on PATH.
    fs::path steamcmd;
    fs::path script;
};

auto get_upload_dir(const std::string& profile) -> fs::path
{
    const auto key = std::hash<std::string>{}(get_project_root().generic_string() + "|" + profile);
    return fs::temp_directory_path() / STEAM_STAGING_FOLDER / fmt::format("{:016x}", key);
}

/// "<product> <version> (<profile>)", which Steamworks lists the build by.
auto make_description(rtti::context& ctx, const deploy_settings& params) -> std::string
{
    auto& pm = ctx.get_cached<project_manager>();
    const auto app = pm.get_profile_settings(params.profile).app;
    const auto product = app.product.empty() ? pm.get_name() : app.product;
    return app.version.empty() ? fmt::format("{} ({})", product, params.profile)
                               : fmt::format("{} {} ({})", product, app.version, params.profile);
}

void write_script(const steam_upload_plan& plan)
{
    fs::error_code ec;
    fs::create_directories(plan.build.build_output, ec);
    std::ofstream stream(plan.script, std::ios::binary | std::ios::trunc);
    stream << make_steam_app_build_script(plan.build);
    if(!stream.good())
    {
        throw std::runtime_error(fmt::format("Writing {} failed.", plan.script.generic_string()));
    }
}

auto describe_failure(const steam_upload_plan& plan, const subprocess::call_result& result) -> std::string
{
    auto output = get_last_lines(result.out_output, STEAMCMD_ERROR_LINES);
    while(!output.empty() && (output.back() == '\n' || output.back() == '\r'))
    {
        output.pop_back();
    }
    auto message = fmt::format("steamcmd did not finish the build (exit code {}). Its last output:\n{}", result.retcode, output);
    if(is_steam_login_failure(result.out_output))
    {
        message += fmt::format("\nIf {} has not logged in on this computer yet, log it in once in a terminal: "
                               "steamcmd +login {}",
                               plan.account,
                               plan.account);
    }
    return message;
}

void run_upload(const steam_upload_plan& plan)
{
    write_script(plan);
    APPLOG_INFO("Steam: uploading {} to app {}, depot {}, as {}.",
                plan.build.content_root.generic_string(),
                plan.build.app_id,
                plan.build.depot_id,
                plan.account);
    const auto arguments = make_steamcmd_arguments(plan.steamcmd, plan.account, plan.script);
    const auto result = subprocess::call(arguments,
                                         {},
                                         [](const std::string& line)
                                         {
                                             if(!line.empty())
                                             {
                                                 APPLOG_TRACE("[steamcmd] {}", line);
                                             }
                                         });
    if(!result.started)
    {
        throw std::runtime_error(fmt::format("steamcmd did not start ({}). Install it, then set its path in Editor "
                                             "Settings > External Tools, or put it on PATH.",
                                             arguments.front()));
    }
    // steamcmd's exit code does not say whether the build was committed; its report of the build does.
    const auto build_id = find_steam_build_id(result.out_output);
    if(build_id.empty())
    {
        throw std::runtime_error(describe_failure(plan, result));
    }
    APPLOG_INFO("Steam: uploaded build {} of app {}{}.",
                build_id,
                plan.build.app_id,
                plan.build.branch.empty() ? std::string(". Set it live on the Steamworks website")
                                          : fmt::format(", live on branch {}", plan.build.branch));
}
} // namespace

auto has_steam_upload(const deploy_settings& params) -> bool
{
    return params.deploy_target == deploy_platform::desktop && params.desktop.steam.app_id != 0;
}

auto get_steam_upload_problem(rtti::context& ctx, const deploy_settings& params) -> std::string
{
    if(params.deploy_target != deploy_platform::desktop)
    {
        return "Steam uploads are for desktop profiles.";
    }
    if(auto problem = check_steam_settings(params.desktop.steam); !problem.empty())
    {
        return problem;
    }
    fs::error_code ec;
    if(!fs::exists(get_deployed_executable(ctx, params), ec))
    {
        return fmt::format("Deploy {} first: {} holds no build of it.", params.profile, get_output_location(params).generic_string());
    }
    return {};
}

auto upload_to_steam(rtti::context& ctx, const deploy_settings& params) -> deploy_jobs
{
    const auto& steam = params.desktop.steam;
    const auto folder = get_upload_dir(params.profile);
    auto plan = std::make_shared<steam_upload_plan>();
    plan->build.app_id = steam.app_id;
    plan->build.depot_id = steam.depot_id;
    plan->build.description = make_description(ctx, params);
    plan->build.content_root = get_output_location(params);
    plan->build.build_output = folder / BUILD_OUTPUT_FOLDER;
    plan->build.branch = steam.branch;
    plan->account = steam.account;
    plan->steamcmd = ctx.get_cached<project_manager>().get_editor_settings().external_tools.steamcmd_executable;
    plan->script = folder / ("app_build_" + std::to_string(steam.app_id) + ".vdf");
    auto& th = ctx.get_cached<threader>();
    auto job = th.pool
                   ->schedule(STEP_STEAM_UPLOAD,
                              [plan]()
                              {
                                  run_upload(*plan);
                              })
                   .share();
    return {{STEP_STEAM_UPLOAD, job}};
}

} // namespace unravel::deployment
