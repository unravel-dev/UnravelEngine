#include "web.h"
#include "files.h"

#include <logging/logging.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace unravel::deployment
{
namespace
{
constexpr const char* TEMPLATE_STAMP_NAME = "template.stamp";
/// The template's folder of what a web server serves, as the .NET browser build names it.
constexpr const char* SITE_FOLDER = "wwwroot";
/// The page, and the .NET runtime's loader it imports, without which the template is incomplete.
constexpr const char* PAGE_FILE = "index.html";
constexpr const char* RUNTIME_LOADER_FILE = "_framework/dotnet.js";
/// The first character JSON escapes as a code point: the control characters are below it.
constexpr unsigned char JSON_FIRST_PLAIN_CHARACTER = 0x20;
constexpr const char* REBUILD_ADVICE = "Rebuild the editor.";

auto is_present(const fs::path& path) -> bool
{
    fs::error_code ec;
    return fs::exists(path, ec);
}

auto to_json_string(const std::string& text) -> std::string
{
    std::string quoted = "\"";
    for(const char character : text)
    {
        if(character == '"' || character == '\\')
        {
            quoted += '\\';
            quoted += character;
        }
        else if(static_cast<unsigned char>(character) < JSON_FIRST_PLAIN_CHARACTER)
        {
            quoted += fmt::format("\\u{:04x}", static_cast<unsigned char>(character));
        }
        else
        {
            quoted += character;
        }
    }
    return quoted + "\"";
}

/// The page's path of a file under `site`: '/' separated, relative to it.
auto get_site_path(const fs::path& file, const fs::path& site) -> std::string
{
    auto path = fs::to_utf8_string(file.lexically_relative(site));
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

auto list_data_files(const fs::path& site) -> std::vector<std::string>
{
    std::vector<std::string> files;
    const auto data = site / DATA_FOLDER;
    fs::error_code ec;
    for(fs::recursive_directory_iterator it(data, ec), end; !ec && it != end; it.increment(ec))
    {
        if(it->is_regular_file(ec))
        {
            files.push_back(get_site_path(it->path(), site));
        }
    }
    if(ec)
    {
        throw std::runtime_error(fmt::format("Listing {} failed: {}", data.generic_string(), ec.message()));
    }
    std::sort(files.begin(), files.end());
    return files;
}
} // namespace

auto web_template::get_site_dir() const -> fs::path
{
    return root / SITE_FOLDER;
}

auto find_web_template(const fs::path& root, const std::string& engine_version) -> web_template_lookup
{
    web_template_lookup lookup;
    if(!is_present(root))
    {
        lookup.error = "This editor was built without the browser player. A source build makes it when its .NET SDK "
                       "has the wasm-tools workload (dotnet workload install wasm-tools).";
        return lookup;
    }
    auto& found = lookup.found;
    found.root = root;
    found.engine_version = read_stamp(root / TEMPLATE_STAMP_NAME)["engine_version"];
    const bool is_complete = !found.engine_version.empty() && is_present(found.get_site_dir() / PAGE_FILE) &&
                             is_present(found.get_site_dir() / RUNTIME_LOADER_FILE);
    if(!is_complete)
    {
        lookup.error = fmt::format("The browser player beside this editor is incomplete. {}", REBUILD_ADVICE);
        return lookup;
    }
    if(found.engine_version != engine_version)
    {
        lookup.error = fmt::format("The browser player beside this editor comes from engine version {}, and the editor "
                                   "is {}. {}",
                                   found.engine_version,
                                   engine_version,
                                   REBUILD_ADVICE);
    }
    return lookup;
}

auto make_web_data_manifest(const fs::path& site) -> std::string
{
    std::string manifest = "[";
    const auto files = list_data_files(site);
    for(std::size_t i = 0; i < files.size(); ++i)
    {
        manifest += (i == 0 ? "\n  " : ",\n  ") + to_json_string(files[i]);
    }
    return manifest + "\n]\n";
}

void copy_web_player(const web_template& player, const fs::path& site)
{
    const auto source = player.get_site_dir();
    fs::error_code ec;
    for(fs::directory_iterator it(source, ec), end; !ec && it != end; it.increment(ec))
    {
        const auto destination = site / it->path().filename();
        if(it->is_directory(ec))
        {
            mirror_or_throw(it->path(), destination);
        }
        else
        {
            copy_file_or_throw(it->path(), destination);
        }
    }
    if(ec)
    {
        throw std::runtime_error(fmt::format("Listing {} failed: {}", source.generic_string(), ec.message()));
    }
}

void write_web_data_manifest(const fs::path& site)
{
    write_text_or_throw(site / WEB_DATA_MANIFEST_NAME, make_web_data_manifest(site));
}

} // namespace unravel::deployment
