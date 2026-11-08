#include "keys.h"

#include <engine/assets/impl/asset_extensions.h>
#include <filesystem/filesystem.h>

#include <algorithm>

namespace unravel::deployment
{

left_out_assets::left_out_assets(std::initializer_list<std::string> keys)
{
    for(const auto& key : keys)
    {
        insert(key);
    }
}

void left_out_assets::insert(const std::string& key)
{
    path_keys_.insert(fs::make_path_key(key));
}

auto left_out_assets::contains(const std::string& key) const -> bool
{
    return path_keys_.contains(fs::make_path_key(key));
}

auto left_out_assets::size() const -> std::size_t
{
    return path_keys_.size();
}

auto left_out_assets::empty() const -> bool
{
    return path_keys_.empty();
}

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

auto list_keys(const std::vector<std::string>& keys, std::size_t shown_max) -> std::string
{
    std::string listed;
    for(std::size_t i = 0; i < keys.size() && i < shown_max; ++i)
    {
        listed += (i == 0 ? "" : ", ") + keys[i];
    }
    if(keys.size() > shown_max)
    {
        listed += " and " + std::to_string(keys.size() - shown_max) + " more";
    }
    return listed;
}

} // namespace unravel::deployment
