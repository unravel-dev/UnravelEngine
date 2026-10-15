#pragma once

#include "mcp_tools_common.h"

#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>

#include <context/context.hpp>
#include <filesystem/filesystem.h>
#include <logging/logging.h>
#include <uuid/uuid.h>

#include <string>

namespace unravel::mcp
{

/// True when extension @p ext is one of T's formats, in its own case or in lower case.
template<typename T>
auto matches_asset_extension(const std::string& ext) -> bool
{
    if(ext.empty())
    {
        return false;
    }
    if(ex::is_format<T>(ext))
    {
        return true;
    }
    const auto lower = to_lower_ascii(ext);
    return lower != ext && ex::is_format<T>(lower);
}

/**
 * @brief The asset a key or uid names, when it exists and is a T.
 *
 * asset_manager::get_asset registers whatever key it is given: asked for a typo it leaves a
 * phantom database entry behind, and asked for another type's file it loads that file as a T
 * (a physics material saved over a render material, for one). This checks first that the key
 * or uid is known - to the database, the loaded handles or the disk - and that its extension
 * is one of T's.
 *
 * @param key_or_uid An asset key (an extension may be left out) or a uid.
 * @param error Receives one sentence when no such asset exists.
 * @return The handle, or an empty one.
 */
template<typename T>
auto find_existing_asset(rtti::context& ctx, const std::string& key_or_uid, std::string& error) -> asset_handle<T>
{
    auto& am = ctx.get_cached<asset_manager>();
    std::string key = key_or_uid;
    if(const auto uid = hpp::uuid::from_string(key_or_uid))
    {
        const auto meta = am.get_metadata(*uid);
        if(meta.location.empty())
        {
            error = "No asset has uid " + key_or_uid;
            return {};
        }
        key = meta.location;
    }
    if(const auto& loaded = am.find_asset<T>(key))
    {
        return loaded;
    }
    if(ex::is_embedded_key(key))
    {
        error = "No embedded asset " + key;
        return {};
    }
    if(!ex::should_keep_key_as_is(key))
    {
        key = ex::resolve_key_missing_extension<T>(key);
    }
    const auto extension = fs::path(key).extension().generic_string();
    if(!ex::is_format<T>(extension))
    {
        error = fmt::format("{} is not the right asset type here (expected {})",
                            key,
                            fmt::join(ex::get_suported_formats<T>(), ", "));
        return {};
    }
    fs::error_code ec;
    if(am.get_metadata_for_key(key).location.empty() && !fs::exists(fs::resolve_protocol(key), ec))
    {
        error = "Asset not found: " + key;
        return {};
    }
    auto handle = am.get_asset<T>(key);
    if(!handle)
    {
        error = "Asset failed to load: " + key;
    }
    return handle;
}

} // namespace unravel::mcp
