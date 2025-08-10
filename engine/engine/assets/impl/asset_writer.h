#pragma once
#include "../asset_handle.h"

#include <filesystem/filesystem.h>
#include <filesystem>
#include <string_utils/utils.h>

#include <engine/assets/impl/asset_extensions.h>

namespace unravel
{
namespace asset_writer
{
//------------------------------------------------------------------------------
// Atomically rename src -> dst, overwriting dst if it exists.
//------------------------------------------------------------------------------
auto atomic_rename_file(const fs::path& src, const fs::path& dst, fs::error_code& ec) noexcept -> bool;
auto make_temp_path(const fs::path& dir, fs::path& out, fs::error_code& ec) noexcept -> bool;

//------------------------------------------------------------------------------
// Atomically copy src -> dst, overwriting dst if it exists.
//------------------------------------------------------------------------------
auto atomic_copy_file(const fs::path& src, const fs::path& dst, fs::error_code& ec) noexcept -> bool;
void atomic_write_file(const fs::path& dst,
                       const std::function<void(const fs::path&)>& callback,
                       fs::error_code& ec) noexcept;

template<typename T>
auto resolve_meta_file(const asset_handle<T>& asset) -> fs::path
{
    const fs::path& key = asset.id();
    fs::path absolute_path = fs::convert_to_protocol(key);
    absolute_path =
        fs::resolve_protocol(fs::replace(absolute_path, ex::get_data_directory(), ex::get_meta_directory()));
    if(absolute_path.extension() != ".meta")
    {
        absolute_path += ".meta";
    }
    return absolute_path;
}

template<typename T>
auto atomic_save_to_file(const fs::path& key, const asset_handle<T>& obj) -> bool
{
    try
    {
        fs::path absolute_key = fs::absolute(fs::resolve_protocol(key));

        fs::error_code err;
        atomic_write_file(
            absolute_key,
            [&](const fs::path& temp)
            {
                save_to_file(temp.string(), obj.get());
            },
            err);

        return !err;
    }
    catch(const std::exception& e)
    {
        APPLOG_ERROR("Failed to save object to file: {0}", e.what());
        return false;
    }
}

template<typename T>
auto atomic_save_to_file(const fs::path& key, const T& obj) -> bool
{
    try
    {
        fs::path absolute_key = fs::absolute(fs::resolve_protocol(key));

        fs::error_code err;
        atomic_write_file(
            absolute_key,
            [&](const fs::path& temp)
            {
                save_to_file(temp.string(), obj);
            },
            err);

        return !err;
    }
    catch(const std::exception& e)
    {
        APPLOG_ERROR("Failed to save object to file: {0}", e.what());
        return false;
    }
}

} // namespace asset_writer
} // namespace unravel
