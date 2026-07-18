#include "asset_actions.h"

#include <engine/assets/impl/asset_extensions.h>
#include <engine/assets/impl/asset_writer.h>

#include <filesystem/watcher.h>
#include <logging/logging.h>
#include <string_utils/utils.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

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

auto is_valid_csharp_identifier(const std::string& name) -> bool
{
    if(name.empty() || (std::isdigit(static_cast<unsigned char>(name.front())) != 0))
    {
        return false;
    }
    return std::all_of(name.begin(),
                       name.end(),
                       [](unsigned char c)
                       {
                           return std::isalnum(c) != 0 || c == '_';
                       });
}

auto create_script_from_template(const fs::path& template_path, const fs::path& dst, std::string* error) -> bool
{
    std::ifstream input(template_path, std::ios::binary);
    if(!input.is_open())
    {
        const auto msg = "Script template not found: " + template_path.generic_string();
        APPLOG_ERROR("{}", msg);
        if(error)
        {
            *error = msg;
        }
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    auto content = buffer.str();
    string_utils::alterable::replace(content, "#SCRIPTNAME#", dst.stem().string());

    fs::error_code err;
    const auto absolute = fs::absolute(dst);
    fs::create_directories(absolute.parent_path(), err);
    err.clear();

    try
    {
        asset_writer::atomic_write_file(
            absolute,
            [&](const fs::path& temp)
            {
                std::ofstream output(temp, std::ios::binary | std::ios::trunc);
                if(!output.is_open())
                {
                    throw std::runtime_error("Failed to open temp file for write: " + temp.generic_string());
                }
                output.write(content.data(), static_cast<std::streamsize>(content.size()));
                if(!output.good())
                {
                    throw std::runtime_error("Failed writing temp file: " + temp.generic_string());
                }
            },
            err);
    }
    catch(const std::exception& ex)
    {
        const auto msg = std::string(ex.what());
        APPLOG_ERROR("{}", msg);
        if(error)
        {
            *error = msg;
        }
        return false;
    }

    if(err)
    {
        const auto msg = "Atomic write failed: " + absolute.generic_string() + " (" + err.message() + ")";
        APPLOG_ERROR("{}", msg);
        if(error)
        {
            *error = msg;
        }
        return false;
    }

    return true;
}

} // namespace unravel::asset_actions
