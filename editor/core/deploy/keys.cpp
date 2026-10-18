#include "keys.h"

#include <engine/assets/impl/asset_extensions.h>
#include <filesystem/filesystem.h>

#include <algorithm>

namespace unravel::deployment
{

auto normalize_key(std::string key) -> std::string
{
    std::replace(key.begin(), key.end(), '\\', '/');
    // A protocol's root ("app:/") keeps its slash.
    const size_t separator = key.find(":/");
    const size_t root_end = separator == std::string::npos ? 0 : separator + 2;
    while(key.size() > root_end && key.back() == '/')
    {
        key.pop_back();
    }
    return key;
}

auto is_same_key(const std::string& lhs, const std::string& rhs) -> bool
{
    return fs::make_path_key(normalize_key(lhs)) == fs::make_path_key(normalize_key(rhs));
}

auto is_key_under(const std::string& key, const std::string& root) -> bool
{
    const auto key_text = fs::make_path_key(normalize_key(key));
    const auto root_text = fs::make_path_key(normalize_key(root));
    if(root_text.empty() || key_text.size() < root_text.size() || key_text.compare(0, root_text.size(), root_text) != 0)
    {
        return false;
    }
    return key_text.size() == root_text.size() || key_text[root_text.size()] == '/' || root_text.back() == '/';
}

auto get_key_without_extension(const std::string& key) -> std::string
{
    const size_t slash = key.rfind('/');
    const size_t dot = key.rfind('.');
    const bool has_extension = dot != std::string::npos && (slash == std::string::npos || dot > slash);
    return has_extension ? key.substr(0, dot) : key;
}

auto get_key_name(const std::string& key) -> std::string
{
    const auto normal = normalize_key(key);
    const size_t slash = normal.rfind('/');
    return slash == std::string::npos ? normal : normal.substr(slash + 1);
}

auto get_display_path(const std::string& key) -> std::string
{
    const std::string root = ex::get_data_directory(CONTENT_PROTOCOL) + "/";
    return is_key_under(key, root) && key.size() > root.size() ? key.substr(root.size()) : key;
}

} // namespace unravel::deployment
