#include "asset_actions.h"

#include <engine/assets/impl/asset_extensions.h>

#include <filesystem/watcher.h>
#include <logging/logging.h>

namespace unravel::asset_actions
{

auto resolve_asset_source_path(const std::string& asset_key) -> fs::path
{
    return fs::absolute(fs::resolve_protocol(asset_key).string());
}

auto resolve_asset_source_path(const fs::path& path) -> fs::path
{
    if(fs::has_known_protocol(path))
    {
        return resolve_asset_source_path(path.generic_string());
    }
    return fs::absolute(path);
}

auto can_reimport(const fs::path& absolute_path) -> bool
{
    fs::error_code ec;
    if(!fs::exists(absolute_path, ec) || !fs::is_regular_file(absolute_path, ec))
    {
        return false;
    }

    const auto extension = absolute_path.extension().string();
    for(const auto& formats : ex::get_all_formats())
    {
        for(const auto& format : formats)
        {
            if(format == extension)
            {
                return true;
            }
        }
    }

    return false;
}

void reimport_path(const fs::path& absolute_path)
{
    const fs::path resolved = resolve_asset_source_path(absolute_path);
    fs::error_code ec;
    if(!fs::exists(resolved, ec) || !fs::is_regular_file(resolved, ec))
    {
        APPLOG_WARNING("Reimport skipped: '{}' is not a regular file", resolved.generic_string());
        return;
    }

    fs::watcher::touch(resolved, false);
}

void reimport_key(const std::string& asset_key)
{
    reimport_path(resolve_asset_source_path(asset_key));
}

} // namespace unravel::asset_actions
