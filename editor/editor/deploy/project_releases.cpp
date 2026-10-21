#include "project_releases.h"
#include "deploy_preflight.h"

#include <deploy/releases.h>
#include <editor/system/project_manager.h>
#include <logging/logging.h>

namespace unravel::deployment
{

auto get_default_release_name(rtti::context& ctx, const std::string& profile) -> std::string
{
    return ctx.get_cached<project_manager>().get_profile_settings(profile).app.version;
}

auto list_project_releases(const std::string& profile) -> std::vector<std::string>
{
    return list_releases(get_profile_releases_folder(get_project_root(), profile));
}

auto get_project_release_folder(const std::string& profile, const std::string& name) -> fs::path
{
    return get_release_folder(get_profile_releases_folder(get_project_root(), profile), name);
}

auto keep_project_release(const deploy_settings& params, const std::string& name) -> result
{
    if(params.deploy_target != deploy_platform::desktop)
    {
        return result::failure("Releases are kept of desktop profiles: an Android package always carries every pack whole.");
    }
    // Releases are desktop builds: their packs ship beside the player, and patches update them.
    return keep_release(get_profile_releases_folder(get_project_root(), params.profile), get_output_location(params), name);
}

auto remove_project_release(rtti::context& ctx, const std::string& profile, const std::string& name) -> result
{
    auto removed = remove_release(get_profile_releases_folder(get_project_root(), profile), name);
    if(!removed.is_ok())
    {
        return removed;
    }
    // Releases are a profile's own: the profile stops patching the removed one, and other profiles keep theirs
    // even where they share its name.
    auto& pm = ctx.get_cached<project_manager>();
    if(auto resolved = pm.get_profile_deploy_settings(profile); resolved && resolved->deploy_patch_release == name)
    {
        resolved->deploy_patch_release.clear();
        if(auto changed = pm.override_profile_deploy(ctx, profile, *resolved); !changed.is_ok())
        {
            APPLOG_WARNING("{}", changed.error);
        }
    }
    return removed;
}

} // namespace unravel::deployment
