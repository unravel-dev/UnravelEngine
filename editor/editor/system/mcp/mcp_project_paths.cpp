#include "mcp_project_paths.h"

#include "mcp_tools_common.h"

#include <engine/assets/impl/asset_extensions.h>
#include <filesystem/filesystem.h>
#include <uuid/uuid.h>

#include <algorithm>
#include <cstring>

namespace unravel::mcp
{
namespace
{

constexpr const char* k_app_prefix = "app:/";
constexpr const char* k_data_folder = "data";
/// Characters no file name may hold on any platform the editor runs on.
constexpr const char* k_file_name_forbidden = "/\\:*?\"<>|";

auto outside_project_error(const std::string& input) -> std::string
{
    return "'" + input +
           "' is outside the project's data folder: MCP writes only there (an app:/data/... key, or a path "
           "relative to app:/data)";
}

/// `relative` split at '/', with empty and "." segments dropped. False when a ".." appears.
auto split_segments(const std::string& relative, std::vector<std::string>& segments) -> bool
{
    std::string segment;
    const auto flush = [&]() -> bool
    {
        if(segment == "..")
        {
            return false;
        }
        if(!segment.empty() && segment != ".")
        {
            segments.push_back(segment);
        }
        segment.clear();
        return true;
    };
    for(const char c : relative)
    {
        if(c != '/')
        {
            segment.push_back(c);
        }
        else if(!flush())
        {
            return false;
        }
    }
    return flush();
}

/// The input as a path relative to the project folder ("data/Props/Crate.mat").
auto to_project_relative(const std::string& input, std::string& relative, std::string& error) -> bool
{
    std::string text = input;
    std::replace(text.begin(), text.end(), '\\', '/');
    if(fs::path(text).is_absolute())
    {
        const auto converted = fs::convert_to_protocol(fs::path(text)).generic_string();
        if(!starts_with(converted, k_app_prefix))
        {
            error = outside_project_error(input);
            return false;
        }
        relative = converted.substr(std::strlen(k_app_prefix));
        return true;
    }
    const auto protocol = fs::extract_protocol(fs::path(text)).generic_string();
    if(protocol.empty())
    {
        relative = std::string(k_data_folder) + "/" + text;
        return true;
    }
    if(protocol != "app")
    {
        error = outside_project_error(input);
        return false;
    }
    relative = text.substr(protocol.size() + 2);
    return true;
}

/// True when `key` resolves to the data folder or inside it once links are followed.
auto resolves_inside_data_root(const std::string& key) -> bool
{
    fs::error_code ec;
    const auto root = fs::weakly_canonical(fs::resolve_protocol(k_project_data_root), ec);
    const auto target = fs::weakly_canonical(fs::resolve_protocol(key), ec);
    return !root.empty() && (target == root || fs::is_any_parent_path(root, target));
}

auto to_lower_extension(const std::string& key) -> std::string
{
    return to_lower_ascii(fs::path(key).extension().generic_string());
}

} // namespace

auto to_project_data_key(rtti::context& ctx, const std::string& input, std::string& key, std::string& error) -> bool
{
    if(!require_open_project(ctx, error))
    {
        return false;
    }
    if(input.empty())
    {
        error = "Empty path: name a location in the project's data folder (app:/data/...)";
        return false;
    }
    std::string relative;
    if(!to_project_relative(input, relative, error))
    {
        return false;
    }
    std::vector<std::string> segments;
    if(!split_segments(relative, segments))
    {
        error = "'" + input + "' contains '..'; name the location inside app:/data directly";
        return false;
    }
    if(segments.empty() || segments.front() != k_data_folder)
    {
        error = outside_project_error(input);
        return false;
    }
    std::string normalized = k_app_prefix;
    for(size_t i = 0; i < segments.size(); ++i)
    {
        normalized += (i == 0 ? "" : "/") + segments[i];
    }
    if(!resolves_inside_data_root(normalized))
    {
        error = outside_project_error(input);
        return false;
    }
    key = std::move(normalized);
    return true;
}

auto to_project_data_file_key(rtti::context& ctx,
                              const std::string& input,
                              const std::vector<std::string>& extensions,
                              std::string& key,
                              std::string& error) -> bool
{
    if(!to_project_data_key(ctx, input, key, error))
    {
        return false;
    }
    if(key == k_project_data_root)
    {
        error = "Name a file inside app:/data, not the folder itself";
        return false;
    }
    const auto extension = to_lower_extension(key);
    if(std::find(extensions.begin(), extensions.end(), extension) != extensions.end())
    {
        return true;
    }
    // A dot inside a name ("Crate.v2") is not an extension; another asset type's one is a mistake.
    if(!extension.empty() && ex::is_known_format(extension))
    {
        error = "'" + input + "' has extension " + extension + "; expected " +
                fmt::format("{}", fmt::join(extensions, " or "));
        return false;
    }
    key += extensions.front();
    return true;
}

auto to_asset_key(const std::string& input) -> std::string
{
    std::string key = input;
    std::replace(key.begin(), key.end(), '\\', '/');
    if(fs::path(key).is_absolute())
    {
        return fs::convert_to_protocol(fs::path(key)).generic_string();
    }
    if(!key.empty() && fs::extract_protocol(fs::path(key)).empty() && !hpp::uuid::from_string(key))
    {
        return std::string(k_project_data_root) + "/" + key;
    }
    return key;
}

auto to_readable_file_key(const std::string& input,
                          const std::vector<std::string>& extensions,
                          std::string& key,
                          std::string& error) -> bool
{
    std::string candidate = to_asset_key(input);
    const auto protocol = fs::extract_protocol(fs::path(candidate)).generic_string();
    std::vector<std::string> segments;
    if(!fs::has_known_protocol(fs::path(candidate)) ||
       !split_segments(candidate.substr(protocol.size() + 2), segments))
    {
        error = "'" + input + "' is not a file in the project or engine folders (use an app:/ or engine:/ key)";
        return false;
    }
    const auto extension = to_lower_extension(candidate);
    if(std::find(extensions.begin(), extensions.end(), extension) == extensions.end())
    {
        error = "'" + input + "' is not a " + fmt::format("{}", fmt::join(extensions, " or ")) + " file";
        return false;
    }
    key = std::move(candidate);
    return true;
}

auto require_project_data_asset(const std::string& key, std::string& error) -> bool
{
    std::vector<std::string> segments;
    if(!starts_with(key, k_app_prefix) || !split_segments(key.substr(std::strlen(k_app_prefix)), segments) ||
       segments.empty() || segments.front() != k_data_folder)
    {
        error = key + " is not a project asset: MCP edits only assets in the project's data folder (app:/data/...)";
        return false;
    }
    return true;
}

auto is_plain_file_name(const std::string& name) -> bool
{
    if(name.empty() || name == "." || name == "..")
    {
        return false;
    }
    return std::none_of(name.begin(),
                        name.end(),
                        [](char c)
                        {
                            return static_cast<unsigned char>(c) < 0x20 ||
                                   std::strchr(k_file_name_forbidden, c) != nullptr;
                        });
}

} // namespace unravel::mcp
