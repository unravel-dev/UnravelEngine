#include "asset_dependencies.h"
#include "asset_extensions.h"

#include <engine/ui/ui_tree.h>
#include <graphics/shader.h>
#include <string_utils/utils.h>

#include <algorithm>
#include <fstream>
#include <unordered_set>

namespace unravel
{
namespace asset_compiler
{

namespace
{
/// Shader/DCC exports may use Windows separators; normalize before fs::path parsing on POSIX.
auto normalize_dependency_path_string(std::string path) -> std::string
{
    string_utils::replace(path, "\\", "/");
    return path;
}

auto extract_href_from_link_tag(hpp::string_view link_tag) -> hpp::string_view
{
    size_t href_pos = link_tag.find("href=");
    if(href_pos == std::string::npos)
    {
        return {};
    }
    href_pos += 5;
    char quote_char = link_tag[href_pos];
    if(quote_char != '"' && quote_char != '\'')
    {
        return {};
    }
    href_pos++;
    size_t href_end = link_tag.find(quote_char, href_pos);
    if(href_end == std::string::npos)
    {
        return {};
    }
    return link_tag.substr(href_pos, href_end - href_pos);
}

auto resolve_ui_tree_dependency_path(const fs::path& href_value, const fs::path& base_file_path) -> fs::path
{
    if(fs::has_known_protocol(href_value))
    {
        return fs::resolve_protocol(href_value);
    }
    return fs::absolute(base_file_path.parent_path() / href_value);
}

auto visit_key_for_path(const fs::path& file_path) -> std::string
{
    return fs::absolute(file_path).string();
}

void append_shader_varying_dependency(const fs::path& file_path, std::vector<fs::path>& processed_files)
{
    const std::string file_name = file_path.stem().string();
    const fs::path dir = file_path.parent_path();
    fs::path varying = dir / (file_name + ".io");
    fs::error_code err;
    if(!fs::exists(varying, err))
    {
        varying = dir / "varying.def.io";
    }
    if(!fs::exists(varying, err))
    {
        varying = dir / "varying.def.sc";
    }
    if(fs::exists(varying, err))
    {
        processed_files.push_back(varying);
    }
}

void resolve_shader_dependencies(const fs::path& file_path,
                                 std::vector<fs::path>& processed_files,
                                 std::unordered_set<std::string>& visited,
                                 bool is_root)
{
    if(!visited.insert(visit_key_for_path(file_path)).second)
    {
        return;
    }

    // Root source is watched separately; only emit external deps here.
    if(is_root)
    {
        append_shader_varying_dependency(file_path, processed_files);
    }
    else
    {
        processed_files.push_back(file_path);
    }

    std::ifstream file(file_path);
    if(!file.is_open())
    {
        return;
    }

    const std::string include_keyword = "#include";
    std::string line;
    while(std::getline(file, line))
    {
        line.erase(0, line.find_first_not_of(" \t"));
        if(line.compare(0, include_keyword.length(), include_keyword) != 0)
        {
            continue;
        }
        size_t start = line.find_first_of("\"<") + 1;
        size_t end = line.find_last_of("\">");
        if(start == std::string::npos || end == std::string::npos || start >= end)
        {
            continue;
        }
        if(line[start - 1] == '<' && line[end] == '>')
        {
            continue;
        }
        const std::string include_path = normalize_dependency_path_string(line.substr(start, end - start));
        const fs::path resolved_path = fs::absolute(file_path.parent_path() / fs::path(include_path));
        resolve_shader_dependencies(resolved_path, processed_files, visited, false);
    }
}

void resolve_ui_tree_dependencies(const fs::path& file_path,
                                  std::vector<fs::path>& processed_files,
                                  std::unordered_set<std::string>& visited,
                                  bool is_root)
{
    if(!visited.insert(visit_key_for_path(file_path)).second)
    {
        return;
    }

    // Root source is watched separately; only emit linked deps here.
    if(!is_root)
    {
        processed_files.push_back(file_path);
    }

    std::ifstream file(file_path);
    if(!file.is_open())
    {
        return;
    }

    std::string content_str((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    hpp::string_view content(content_str);
    size_t pos = 0;
    while((pos = content.find("<link", pos)) != std::string::npos)
    {
        size_t tag_end = content.find('>', pos);
        if(tag_end == std::string::npos)
        {
            break;
        }
        hpp::string_view link_tag = content.substr(pos, tag_end - pos + 1);
        hpp::string_view href_value = extract_href_from_link_tag(link_tag);
        if(!href_value.empty())
        {
            fs::path resolved_path = resolve_ui_tree_dependency_path(fs::path(href_value), file_path);
            const auto& supported_deps = ex::get_suported_dependencies_formats<ui_tree>();
            auto extension = resolved_path.extension().string();
            if(std::find(supported_deps.begin(), supported_deps.end(), extension) != supported_deps.end())
            {
                resolve_ui_tree_dependencies(resolved_path, processed_files, visited, false);
            }
        }
        pos = tag_end + 1;
    }
}

} // namespace

template<>
void resolve_dependencies<gfx::shader>(const fs::path& file_path, std::vector<fs::path>& processed_files)
{
    std::unordered_set<std::string> visited;
    resolve_shader_dependencies(file_path, processed_files, visited, true);
}

template<>
void resolve_dependencies<ui_tree>(const fs::path& file_path, std::vector<fs::path>& processed_files)
{
    std::unordered_set<std::string> visited;
    resolve_ui_tree_dependencies(file_path, processed_files, visited, true);
}

} // namespace asset_compiler
} // namespace unravel
