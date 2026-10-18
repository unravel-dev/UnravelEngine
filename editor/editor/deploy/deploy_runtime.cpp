#include "deploy_runtime.h"

#include <deploy/files.h>
#include <deploy/keys.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <logging/logging.h>

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace unravel::deployment
{
namespace
{
auto get_engine_root(const deploy_settings& params) -> fs::path
{
    return params.deploy_location / DATA_FOLDER / ENGINE_PROTOCOL;
}

#if DOTNETPP_BACKEND_MONO

constexpr const char* MONO_FOLDER = "mono";

#else

constexpr const char* DOTNET_FOLDER = "dotnet";
constexpr const char* HOST_FXR_FOLDER = "host/fxr";
constexpr const char* SHARED_FRAMEWORK_FOLDER = "shared/Microsoft.NETCore.App";

/// A leading dotted version from a directory name ("8.0.4" -> {8, 0, 4}).
auto parse_version_name(const std::string& name) -> std::vector<int>
{
    std::vector<int> parts;
    std::string current;
    for(const char c : name)
    {
        if(c >= '0' && c <= '9')
        {
            current += c;
            continue;
        }
        if(c != '.')
        {
            break;
        }
        parts.push_back(current.empty() ? 0 : std::atoi(current.c_str()));
        current.clear();
    }
    if(!current.empty())
    {
        parts.push_back(std::atoi(current.c_str()));
    }
    return parts;
}

/// The subfolder of `base` with the highest dotted version name. Versions of `preferred_major` win over
/// others, which are only a fallback; -1 prefers none. Empty when none is found.
auto pick_highest_version_folder(const fs::path& base, int preferred_major) -> fs::path
{
    fs::path best;
    std::vector<int> best_version;
    bool best_matches_major = false;
    fs::error_code ec;
    for(const auto& entry : fs::directory_iterator(base, ec))
    {
        const auto version = entry.is_directory(ec) ? parse_version_name(entry.path().filename().string()) : std::vector<int>{};
        if(version.empty())
        {
            continue;
        }
        const bool matches_major = preferred_major >= 0 && version.front() == preferred_major;
        const bool is_better = best.empty() || (matches_major != best_matches_major
                                                    ? matches_major
                                                    : std::lexicographical_compare(best_version.begin(),
                                                                                   best_version.end(),
                                                                                   version.begin(),
                                                                                   version.end()));
        if(is_better)
        {
            best = entry.path();
            best_version = version;
            best_matches_major = matches_major;
        }
    }
    return best;
}

/// The major of a "major.minor" version string; -1 when it does not parse.
auto get_version_major(const std::string& version) -> int
{
    const int major = std::atoi(version.c_str());
    return major > 0 ? major : -1;
}

#endif
} // namespace

#if DOTNETPP_BACKEND_MONO

auto make_runtime_plan(rtti::context& ctx, const deploy_settings& params) -> runtime_plan
{
    const auto mono_root = get_engine_root(params) / MONO_FOLDER;
    const fs::path assembly_folder = fs::path(dotnet::get_core_assembly_path()).parent_path();
    runtime_plan plan;
    plan.step_name = "Mono";
    plan.with_debug_symbols = params.deploy_debug_symbols;
    // lib/mono/<version>: the class libraries; lib/mono/gac: the global assembly cache.
    plan.managed_source = assembly_folder;
    plan.managed_destination = mono_root / "lib" / MONO_FOLDER / assembly_folder.filename();
    plan.framework_source = assembly_folder.parent_path() / "gac";
    plan.framework_destination = mono_root / "lib" / MONO_FOLDER / "gac";
    // lib: the native libraries, by name; etc/mono: the runtime's configuration.
    plan.host_source = assembly_folder.parent_path().parent_path();
    plan.host_destination = mono_root / "lib";
    plan.library_names = dotnet::get_common_library_names_for_deploy();
    plan.config_source = fs::path(script_system::find_dotnet_paths(ctx).config_dir) / MONO_FOLDER;
    plan.config_destination = mono_root / "etc" / MONO_FOLDER;
    return plan;
}

void deploy_runtime(const runtime_plan& plan)
{
    for(const auto& name : plan.library_names)
    {
        fs::error_code ec;
        if(fs::exists(plan.host_source / name, ec))
        {
            copy_file_or_throw(plan.host_source / name, plan.host_destination / name);
        }
    }
    mirror_or_throw(plan.managed_source, plan.managed_destination);
    mirror_or_throw(plan.framework_source, plan.framework_destination);
    // An older class library version beside the current one goes, the cache next to it stays.
    const auto libraries_root = plan.managed_destination.parent_path();
    fs::error_code ec;
    for(const auto& entry : fs::directory_iterator(libraries_root, ec))
    {
        const auto name = entry.path().filename();
        if(name != plan.managed_destination.filename() && name != plan.framework_destination.filename())
        {
            fs::error_code remove_ec;
            fs::remove_all(entry.path(), remove_ec);
        }
    }
    mirror_or_throw(plan.config_source, plan.config_destination);
}

#else

auto make_runtime_plan(rtti::context&, const deploy_settings& params) -> runtime_plan
{
    const auto engine_root = get_engine_root(params);
    runtime_plan plan;
    plan.step_name = ".NET";
    plan.with_debug_symbols = params.deploy_debug_symbols;
    // The managed bridge payload (Clrpp.Managed.dll, runtimeconfig, NuGet deps) is one folder; the game
    // passes its deployed location to the runtime at init.
    const std::string managed_folder = dotnet::managed_runtime_dir();
    plan.managed_source = fs::resolve_protocol("binary:/" + managed_folder);
    plan.managed_destination = engine_root / managed_folder;
    // A pruned dotnet root (hostfxr and the shared framework) lets the game run without an installed
    // .NET; the runtime major the scripts target is preferred, the newest installed one is the fallback.
    const fs::path dotnet_root = dotnet::get_core_assembly_path();
    const int preferred_major = get_version_major(dotnet::get_dotnet_version());
    plan.host_source = pick_highest_version_folder(dotnet_root / HOST_FXR_FOLDER, preferred_major);
    plan.framework_source = pick_highest_version_folder(dotnet_root / SHARED_FRAMEWORK_FOLDER, preferred_major);
    plan.host_destination = engine_root / DOTNET_FOLDER / HOST_FXR_FOLDER / plan.host_source.filename();
    plan.framework_destination = engine_root / DOTNET_FOLDER / SHARED_FRAMEWORK_FOLDER / plan.framework_source.filename();
    if(plan.host_source.empty() || plan.framework_source.empty())
    {
        APPLOG_WARNING("Deploy: no hostfxr or shared framework under {}; the deployed game will need an installed .NET "
                       "runtime",
                       dotnet_root.generic_string());
        plan.host_source.clear();
        plan.framework_source.clear();
    }
    return plan;
}

void deploy_runtime(const runtime_plan& plan)
{
    fs::mirror_filter include;
    if(!plan.with_debug_symbols)
    {
        include = [](const fs::path& file)
        {
            return !is_debug_companion_file(file);
        };
    }
    mirror_or_throw(plan.managed_source, plan.managed_destination, include);
    if(plan.host_source.empty())
    {
        return;
    }
    // The runtime is the largest part of a build and rarely changes: only an updated version is copied,
    // and the one it replaces removed.
    mirror_or_throw(plan.host_source, plan.host_destination);
    remove_siblings(plan.host_destination);
    mirror_or_throw(plan.framework_source, plan.framework_destination);
    remove_siblings(plan.framework_destination);
}

#endif

} // namespace unravel::deployment
