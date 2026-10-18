#include "project_releases.h"
#include "deploy_preflight.h"

#include <deploy/releases.h>
#include <editor/system/project_manager.h>

namespace unravel::deployment
{

auto get_default_release_name(rtti::context& ctx) -> std::string
{
    return ctx.get_cached<project_manager>().get_settings().app.version;
}

auto list_project_releases() -> std::vector<std::string>
{
    return list_releases(get_project_root());
}

auto get_project_release_folder(const std::string& name) -> fs::path
{
    return get_release_folder(get_project_root(), name);
}

auto keep_project_release(const deploy_settings& params, const std::string& name) -> result
{
    // Releases are desktop builds: their packs ship beside the player, and patches update them.
    return keep_release(get_project_root(), get_platform_location(params, deploy_platform::desktop), name);
}

auto remove_project_release(rtti::context& ctx, const std::string& name) -> result
{
    auto removed = remove_release(get_project_root(), name);
    auto& pm = ctx.get_cached<project_manager>();
    if(removed.is_ok() && pm.get_deploy_settings().deploy_patch_release == name)
    {
        pm.get_deploy_settings().deploy_patch_release.clear();
        pm.save_deploy_settings();
    }
    return removed;
}

} // namespace unravel::deployment
