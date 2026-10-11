#include "asset_actions.h"

#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/assets/impl/asset_writer.h>
#include <engine/meta/physics/physics_material.hpp>
#include <engine/physics/physics_material.h>

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

auto create_physics_material(rtti::context& ctx, const fs::path& absolute_path) -> asset_handle<physics_material>
{
    auto& am = ctx.get_cached<asset_manager>();
    const auto key = fs::convert_to_protocol(absolute_path).generic_string();
    auto handle = am.get_asset_from_instance<physics_material>(key, std::make_shared<physics_material>());
    if(!handle || !asset_writer::atomic_save_to_file(handle.id(), handle))
    {
        return {};
    }
    return handle;
}

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

namespace
{
auto report_error(std::string* error, const std::string& message) -> bool
{
    APPLOG_ERROR("{}", message);
    if(error)
    {
        *error = message;
    }
    return false;
}

auto copy_engine_template(const char* template_key, const fs::path& dst, std::string* error) -> bool
{
    fs::error_code ec;
    const auto source = fs::resolve_protocol(template_key);
    if(!fs::exists(source, ec))
    {
        return report_error(error, std::string("Template not found: ") + template_key);
    }
    const auto absolute = fs::absolute(dst);
    fs::create_directories(absolute.parent_path(), ec);
    ec.clear();
    if(!asset_writer::atomic_copy_file(source, absolute, ec) || ec)
    {
        return report_error(error,
                            "Copy failed: " + absolute.generic_string() + (ec ? " (" + ec.message() + ")" : ""));
    }
    return true;
}
} // namespace

auto read_text_file(const fs::path& path, std::string& out, std::string* error) -> bool
{
    std::ifstream input(path, std::ios::binary);
    if(!input.is_open())
    {
        if(error)
        {
            *error = "Failed to open file: " + path.generic_string();
        }
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    out = buffer.str();
    return true;
}

auto write_text_file(const fs::path& dst, const std::string& content, std::string* error) -> bool
{
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
        return report_error(error, ex.what());
    }

    if(err)
    {
        return report_error(error, "Atomic write failed: " + absolute.generic_string() + " (" + err.message() + ")");
    }
    return true;
}

auto create_ui_document(const fs::path& dst, std::string* error) -> bool
{
    return copy_engine_template("engine:/data/ui/template.rhtml", dst, error);
}

auto create_style_sheet(const fs::path& dst, std::string* error) -> bool
{
    return copy_engine_template("engine:/data/ui/template.rcss", dst, error);
}

auto create_script_from_template(const fs::path& template_path, const fs::path& dst, std::string* error) -> bool
{
    std::string content;
    if(!read_text_file(template_path, content))
    {
        return report_error(error, "Script template not found: " + template_path.generic_string());
    }
    string_utils::alterable::replace(content, "#SCRIPTNAME#", dst.stem().string());
    return write_text_file(dst, content, error);
}

} // namespace unravel::asset_actions
