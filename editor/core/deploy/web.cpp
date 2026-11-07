#include "web.h"
#include "files.h"

#include <filesystem/file_fingerprint.h>
#include <logging/logging.h>

#include <algorithm>
#include <cctype>
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
/// SDL's preferences root in the browser (SDL_GetPrefPath on Emscripten).
constexpr const char* WEB_PREFERENCES_ROOT = "/libsdl";
/// The program name the browser player starts the engine as (game/web/Program.cs ProgramPath), which names a game's
/// folder when its project names no product.
constexpr const char* WEB_PROGRAM_NAME = "app";
constexpr const char* URL_HEX_DIGITS = "0123456789ABCDEF";
constexpr unsigned char URL_HIGH_NIBBLE_SHIFT = 4;
constexpr unsigned char URL_LOW_NIBBLE_MASK = 0xF;

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

/// A file of a web build's data, as the page fetches it.
struct data_file
{
    fs::path file;
    /// The page's path of the file: '/' separated, relative to the site, UTF-8.
    std::string site_path;
};

auto list_data_files(const fs::path& site) -> std::vector<data_file>
{
    std::vector<data_file> files;
    const auto data = site / DATA_FOLDER;
    fs::error_code ec;
    for(fs::recursive_directory_iterator it(data, ec), end; !ec && it != end; it.increment(ec))
    {
        if(it->is_regular_file(ec))
        {
            files.push_back({it->path(), fs::to_utf8_string(it->path().lexically_relative(site))});
        }
    }
    if(ec)
    {
        throw std::runtime_error(fmt::format("Listing {} failed: {}", data.generic_string(), ec.message()));
    }
    std::sort(files.begin(),
              files.end(),
              [](const data_file& lhs, const data_file& rhs)
              {
                  return lhs.site_path < rhs.site_path;
              });
    return files;
}

auto read_fingerprint(const fs::path& file) -> std::string
{
    auto fingerprint = fs::hash_file_fingerprint(file, false);
    if(fingerprint.empty())
    {
        throw std::runtime_error(fmt::format("{} could not be read for the web build's data manifest.", file.generic_string()));
    }
    return fingerprint;
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

auto get_web_user_folder(const std::string& company, const std::string& product) -> std::string
{
    const auto& name = product.empty() ? std::string(WEB_PROGRAM_NAME) : product;
    return company.empty() ? fmt::format("{}/{}", WEB_PREFERENCES_ROOT, name)
                           : fmt::format("{}/{}/{}", WEB_PREFERENCES_ROOT, company, name);
}

auto make_web_data_manifest(const fs::path& site, const std::string& user_folder) -> std::string
{
    std::string files_json;
    for(const auto& data : list_data_files(site))
    {
        files_json += fmt::format("{}{{\"path\": {}, \"hash\": {}}}",
                                  files_json.empty() ? "\n    " : ",\n    ",
                                  to_json_string(data.site_path),
                                  to_json_string(read_fingerprint(data.file)));
    }
    return fmt::format("{{\n  \"user_folder\": {},\n  \"files\": [{}\n  ]\n}}\n", to_json_string(user_folder), files_json);
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

void write_web_data_manifest(const fs::path& site, const std::string& user_folder)
{
    write_text_or_throw(site / WEB_DATA_MANIFEST_NAME, make_web_data_manifest(site, user_folder));
}

auto encode_url_part(const std::string& text) -> std::string
{
    std::string encoded;
    for(const unsigned char c : text)
    {
        if(std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            encoded += char(c);
            continue;
        }
        encoded += '%';
        encoded += URL_HEX_DIGITS[c >> URL_HIGH_NIBBLE_SHIFT];
        encoded += URL_HEX_DIGITS[c & URL_LOW_NIBBLE_MASK];
    }
    return encoded;
}

auto make_web_site_path(const std::string& project, const std::string& profile) -> std::string
{
    return "/" + project + "/" + profile + "/";
}

auto make_web_page_address(const std::string& origin, const std::string& site_path, const std::vector<std::string>& arguments)
    -> std::string
{
    std::string address = origin;
    std::size_t start = 0;
    for(std::size_t slash = site_path.find('/'); slash != std::string::npos; slash = site_path.find('/', start))
    {
        address += encode_url_part(site_path.substr(start, slash - start)) + "/";
        start = slash + 1;
    }
    address += encode_url_part(site_path.substr(start));
    std::string query;
    for(const auto& argument : arguments)
    {
        query += (query.empty() ? "?arg=" : "&arg=") + encode_url_part(argument);
    }
    return address + query;
}

} // namespace unravel::deployment
