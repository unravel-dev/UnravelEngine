#include "deploy_preflight.h"
#include "deploy_android.h"
#include "deploy_runtime.h"

#include <deploy/folders.h>
#include <deploy/keys.h>
#include <deploy/releases.h>
#include <editor/assets/asset_watcher.h>
#include <editor/system/project_manager.h>
#include <engine/play_mode.h>
#include <engine/scripting/ecs/systems/script_system.h>

namespace unravel::deployment
{
namespace
{
/// How long a problems_cache answer is kept while the settings stay the same.
constexpr auto PROBLEMS_REFRESH_INTERVAL = std::chrono::milliseconds(500);

/// The deploy folder's problem, if any. In the project it stays out of the folders the editor manages;
/// it never holds the project. It is created when missing, in a folder that exists. The platform builds
/// into a folder of its own in it, created when missing, which must be empty or an earlier build of
/// that platform, since a deploy replaces and removes files in it. An Android folder holds packages only.
auto get_folder_problem(const fs::path& location, const fs::path& output, bool is_android) -> std::string
{
    fs::error_code ec;
    if(location.empty())
    {
        return fmt::format("Choose a folder to deploy to, such as {} in the project.", DEFAULT_DEPLOY_LOCATION);
    }
    if(is_inside(get_project_root(), location))
    {
        return fmt::format("The deploy folder contains the project. Choose another one, such as {} in the project.",
                           DEFAULT_DEPLOY_LOCATION);
    }
    const auto managed = get_managed_project_folder(location, get_project_root());
    if(!managed.empty())
    {
        return fmt::format("The deploy folder is in the project's {} folder, which the editor manages. Choose "
                           "another one, such as {} in the project.",
                           managed,
                           DEFAULT_DEPLOY_LOCATION);
    }
    if(fs::exists(location, ec) && !fs::is_directory(location, ec))
    {
        return fmt::format("The deploy location {} is a file, not a folder.", location.generic_string());
    }
    if(!fs::exists(location, ec) && !fs::is_directory(location.parent_path(), ec))
    {
        return fmt::format("The deploy folder's parent {} does not exist.", location.parent_path().generic_string());
    }
    if(!fs::exists(output, ec))
    {
        return {};
    }
    const auto folder = output.filename().generic_string();
    if(is_android)
    {
        return is_android_package_folder(output)
                   ? std::string{}
                   : fmt::format("The {} folder in the deploy folder holds other files. A deploy replaces the package "
                                 "in it. Empty it, or choose another deploy folder.",
                                 folder);
    }
    if(!fs::is_empty(output, ec) && !is_previous_deploy(output))
    {
        return fmt::format("The {} folder in the deploy folder holds files of no earlier build. A deploy replaces "
                           "and removes files in it. Empty it, or choose another deploy folder.",
                           folder);
    }
    return {};
}

auto get_patch_problem(const deploy_settings& params) -> std::string
{
    if(params.deploy_patch_release.empty())
    {
        return {};
    }
    if(!params.deploy_packed_assets)
    {
        return "A patch against a release needs Pack Assets on.";
    }
    if(!is_kept_release(get_project_root(), params.deploy_patch_release))
    {
        return fmt::format("Release '{}' is not kept. Keep one with Keep as Release, or set Patch Against Release to "
                           "None.",
                           params.deploy_patch_release);
    }
    return {};
}

auto get_compile_problem(rtti::context& ctx) -> std::string
{
    auto pending = get_pending_compile_reason(ctx);
    if(!pending.empty())
    {
        return pending;
    }
    return ctx.get_cached<script_system>().has_compilation_errors()
               ? "Scripts have compile errors. Fix them so the game does not ship an older build."
               : std::string{};
}

void add_problem(std::vector<std::string>& problems, std::string problem)
{
    if(!problem.empty())
    {
        problems.push_back(std::move(problem));
    }
}
} // namespace

auto get_project_root() -> fs::path
{
    return fs::resolve_protocol(PROJECT_ROOT_KEY);
}

auto is_inside_project(const fs::path& path) -> bool
{
    return is_inside(path, get_project_root());
}

auto get_deployed_executable(rtti::context& ctx, const deploy_settings& params) -> fs::path
{
    return get_platform_location(params, deploy_platform::desktop) / (ctx.get_cached<project_manager>().get_name() + fs::executable_extension());
}

auto get_pending_compile_reason(rtti::context& ctx) -> std::string
{
    // The deploy reads the compiled cache as it is: until the compiler is idle that is a mix of old and
    // new files. A script library build lands only when its domain reloads.
    const size_t compile_jobs = ctx.get_cached<asset_watcher>().get_pending_compile_jobs_count(ctx);
    if(compile_jobs > 0)
    {
        return fmt::format("Assets are still compiling ({} job(s) left). The deploy can start once they finish.", compile_jobs);
    }
    auto& scripts = ctx.get_cached<script_system>();
    if(scripts.is_build_pending(ENGINE_PROTOCOL) || scripts.is_build_pending(CONTENT_PROTOCOL))
    {
        return "Scripts are still compiling. The deploy can start once they finish.";
    }
    return {};
}

auto get_problems(rtti::context& ctx, const deploy_settings& params) -> std::vector<std::string>
{
    std::vector<std::string> problems;
    const bool is_android = params.deploy_target == deploy_platform::android;
    const auto folder_problem = get_folder_problem(get_deploy_folder(params), get_output_location(params), is_android);
    add_problem(problems, folder_problem);
    if(folder_problem.empty() && !is_android && is_executable_in_use(get_deployed_executable(ctx, params)))
    {
        add_problem(problems, "The game in the deploy folder is running. Close it first.");
    }
    if(!ctx.get_cached<project_manager>().get_settings().standalone.startup_scene.is_valid())
    {
        add_problem(problems, "Choose the scene the game starts with.");
    }
    if(params.deploy_cooked_assets && ctx.get_cached<play_mode>().is_active())
    {
        add_problem(problems, "Stop play mode first: prefabs cannot be cooked while the game runs.");
    }
    // An Android game carries the runtime of the editor's Android player, not the editor's own.
    if(is_android)
    {
        for(auto& problem : get_android_problems(ctx, params))
        {
            add_problem(problems, std::move(problem));
        }
    }
    else
    {
        add_problem(problems, get_runtime_problem());
        // An Android package carries every pack whole, so only desktop deploys patch.
        add_problem(problems, get_patch_problem(params));
    }
    add_problem(problems, get_compile_problem(ctx));
    return problems;
}

auto can_deploy(rtti::context& ctx, const deploy_settings& params) -> bool
{
    return get_problems(ctx, params).empty();
}

auto problems_cache::get(rtti::context& ctx, const deploy_settings& params) -> const std::vector<std::string>&
{
    const auto signature = fmt::format("{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}",
                                       params.deploy_location.generic_string(),
                                       params.deploy_patch_release,
                                       params.deploy_packed_assets,
                                       params.deploy_cooked_assets,
                                       static_cast<int>(params.deploy_target),
                                       params.deploy_and_run,
                                       params.android_device,
                                       params.android_application_id,
                                       params.android_release,
                                       params.android_keystore.generic_string(),
                                       params.android_key_alias,
                                       params.android_icon.id());
    const auto now = std::chrono::steady_clock::now();
    if(signature != signature_ || now - checked_at_ >= PROBLEMS_REFRESH_INTERVAL)
    {
        problems_ = get_problems(ctx, params);
        signature_ = signature;
        checked_at_ = now;
    }
    return problems_;
}

void problems_cache::invalidate()
{
    signature_.clear();
}

} // namespace unravel::deployment
