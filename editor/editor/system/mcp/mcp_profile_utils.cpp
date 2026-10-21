#include "mcp_profile_utils.h"
#include "mcp_tools_common.h"

#include <editor/system/project_manager.h>

#include <algorithm>

namespace unravel::mcp
{
namespace
{
/// A deploy setting: the name the deploy tools take, and the name it is stored and overridden under.
struct deploy_argument
{
    const char* argument;
    const char* path;
};

constexpr deploy_argument DEPLOY_ARGUMENTS[] = {
    {"location", "deploy_location"},
    {"deploy_dependencies", "deploy_dependencies"},
    {"run_after_deploy", "deploy_and_run"},
    {"run_arguments", "deploy_run_arguments"},
    {"cook_assets", "deploy_cooked_assets"},
    {"include_debug_symbols", "deploy_debug_symbols"},
    {"pack_assets", "deploy_packed_assets"},
    {"patch_release", "deploy_patch_release"},
    {"only_used_assets", "deploy_only_used_assets"},
    {"scenes", "deploy_scenes"},
    {"include", "deploy_included"},
    {"exclude", "deploy_excluded"},
};
} // namespace

auto get_platform_name(target_platform platform) -> const char*
{
    switch(platform)
    {
        case target_platform::android:
            return "android";
        case target_platform::desktop:
        default:
            return "desktop";
    }
}

auto parse_platform_name(hpp::string_view name, target_platform& platform) -> bool
{
    for(const auto candidate : {target_platform::desktop, target_platform::android})
    {
        if(name == get_platform_name(candidate))
        {
            platform = candidate;
            return true;
        }
    }
    return false;
}

auto to_profile_path(hpp::string_view dotted) -> std::string
{
    std::string path(dotted);
    std::replace(path.begin(), path.end(), '.', '/');
    return path;
}

auto to_dotted_paths(const serialization::path_set& paths) -> std::vector<std::string>
{
    std::vector<std::string> dotted;
    dotted.reserve(paths.size());
    for(const auto& path : paths)
    {
        auto entry = path;
        std::replace(entry.begin(), entry.end(), '/', '.');
        dotted.push_back(std::move(entry));
    }
    return dotted;
}

auto get_editable_project(rtti::context& ctx, std::string& error) -> project_manager*
{
    if(!require_not_play_mode(ctx, error) || !require_not_deploying(ctx, error))
    {
        return nullptr;
    }
    return get_open_project(ctx, error);
}

auto read_profile_argument(const project_manager& pm,
                           const simdjson::dom::object& args,
                           const char* key,
                           std::string& profile,
                           std::string& error) -> bool
{
    std::string named;
    if(!read_string(args, key, named) || named.empty())
    {
        return true;
    }
    if(pm.get_profiles().find(named) == nullptr)
    {
        error = fmt::format("No profile named '{}'. settings_profiles_list lists them.", named);
        return false;
    }
    profile = std::move(named);
    return true;
}

auto get_deploy_argument_names() -> std::vector<std::string>
{
    std::vector<std::string> names;
    for(const auto& entry : DEPLOY_ARGUMENTS)
    {
        names.emplace_back(entry.argument);
    }
    return names;
}

auto find_deploy_setting_path(hpp::string_view argument) -> std::string
{
    const auto* found = std::find_if(std::begin(DEPLOY_ARGUMENTS),
                                     std::end(DEPLOY_ARGUMENTS),
                                     [argument](const deploy_argument& entry)
                                     {
                                         return argument == entry.argument;
                                     });
    return found != std::end(DEPLOY_ARGUMENTS) ? std::string(found->path) : std::string{};
}

auto to_deploy_argument_names(const serialization::path_set& paths) -> std::vector<std::string>
{
    std::vector<std::string> names;
    names.reserve(paths.size());
    for(const auto& path : paths)
    {
        const auto* found = std::find_if(std::begin(DEPLOY_ARGUMENTS),
                                         std::end(DEPLOY_ARGUMENTS),
                                         [&path](const deploy_argument& entry)
                                         {
                                             return serialization::is_path_within(path, entry.path);
                                         });
        names.emplace_back(found != std::end(DEPLOY_ARGUMENTS) ? found->argument : path);
    }
    return names;
}

} // namespace unravel::mcp
