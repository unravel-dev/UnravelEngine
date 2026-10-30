#pragma once

#include <context/context.hpp>
#include <deploy/deploy_settings.h>
#include <filesystem/filesystem.h>

#include <chrono>
#include <string>
#include <vector>

/// What keeps the open project from deploying.
namespace unravel::deployment
{

/// The open project's folder (the app protocol's root).
auto get_project_root() -> fs::path;

/// Whether `path` is the open project's folder or inside it (existing or not).
auto is_inside_project(const fs::path& path) -> bool;

/// Where the deployed game's executable is.
auto get_deployed_executable(rtti::context& ctx, const deploy_settings& params) -> fs::path;

/// Why the compiled cache is not ready to deploy yet - assets or scripts still compiling - in one
/// sentence, or empty when the compiler is idle. Main thread.
auto get_pending_compile_reason(rtti::context& ctx) -> std::string;

/// What keeps the project from deploying with these settings, one sentence each; empty when it can.
/// Main thread.
auto get_problems(rtti::context& ctx, const deploy_settings& params) -> std::vector<std::string>;

auto can_deploy(rtti::context& ctx, const deploy_settings& params) -> bool;

/// get_problems for a window drawn every frame: the checks touch the disk, so their answer is kept for a
/// moment while the settings stay the same.
class problems_cache
{
public:
    auto get(rtti::context& ctx, const deploy_settings& params) -> const std::vector<std::string>&;
    /// get_steam_upload_problem, kept like get's answer; empty for a profile without a Steam upload.
    auto get_steam_upload_problem(rtti::context& ctx, const deploy_settings& params) -> const std::string&;
    /// Checks again on the next get: something the checks read just changed.
    void invalidate();

private:
    void refresh(rtti::context& ctx, const deploy_settings& params);

    std::vector<std::string> problems_;
    std::string steam_upload_problem_;
    std::string signature_;
    std::chrono::steady_clock::time_point checked_at_{};
};

} // namespace unravel::deployment
