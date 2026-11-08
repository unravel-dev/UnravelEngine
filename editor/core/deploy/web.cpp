#include "web.h"
#include "files.h"

#include <engine/assets/impl/asset_extensions.h>
#include <filesystem/file_fingerprint.h>
#include <logging/logging.h>
#include <string_utils/utils.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
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
/// The .NET runtime's folder in the site.
constexpr const char* RUNTIME_FOLDER = "_framework";
/// The precompressed copies a web server may send instead of a file: not downloaded besides it.
constexpr std::array<const char*, 2> PRECOMPRESSED_EXTENSIONS = {".br", ".gz"};
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
/// The domain cloudflared names a quick tunnel's address under.
constexpr const char* QUICK_TUNNEL_DOMAIN = ".trycloudflare.com";
constexpr const char* HTTPS_SCHEME = "https://";

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

auto read_size(const fs::path& file) -> std::uintmax_t
{
    fs::error_code ec;
    const auto size = fs::file_size(file, ec);
    if(ec)
    {
        throw std::runtime_error(fmt::format("{} could not be read for the web build's data manifest.", file.generic_string()));
    }
    return size;
}

auto is_precompressed(const fs::path& file) -> bool
{
    const auto extension = file.extension().string();
    return std::any_of(PRECOMPRESSED_EXTENSIONS.begin(),
                       PRECOMPRESSED_EXTENSIONS.end(),
                       [&extension](const char* precompressed)
                       {
                           return extension == precompressed;
                       });
}

/// The bytes of the runtime's files in `site`, without their precompressed copies; 0 without a runtime.
auto get_runtime_size(const fs::path& site) -> std::uintmax_t
{
    std::uintmax_t size = 0;
    fs::error_code ec;
    for(fs::recursive_directory_iterator it(site / RUNTIME_FOLDER, ec), end; !ec && it != end; it.increment(ec))
    {
        if(it->is_regular_file(ec) && !is_precompressed(it->path()))
        {
            size += read_size(it->path());
        }
    }
    return size;
}

/// The texture set among `sets` whose pack `file` is (ex::get_texture_pack_file_name); empty for any other file.
auto get_texture_set_of(const fs::path& file, const std::vector<std::string>& sets) -> std::string
{
    const auto name = fs::to_utf8_string(file.filename());
    const auto found = std::find_if(sets.begin(),
                                    sets.end(),
                                    [&name](const std::string& set)
                                    {
                                        return name == ex::get_texture_pack_file_name(set);
                                    });
    return found != sets.end() ? *found : std::string();
}

/// The manifest's line listing `sets`, the one computers read first; empty without sets.
auto make_texture_sets_json(const std::vector<std::string>& sets) -> std::string
{
    if(sets.empty())
    {
        return {};
    }
    std::string listed;
    for(const auto& set : sets)
    {
        listed += (listed.empty() ? "" : ", ") + to_json_string(set);
    }
    return fmt::format("  \"texture_sets\": [{}],\n", listed);
}

/// A letter, digit or hyphen: what the labels of a tunnel's host name are made of.
auto is_host_character(char character) -> bool
{
    return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '-';
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

auto make_web_data_manifest(const fs::path& site, const web_page_info& page) -> std::string
{
    std::string files_json;
    for(const auto& data : list_data_files(site))
    {
        const auto set = get_texture_set_of(data.file, page.texture_sets);
        files_json += fmt::format("{}{{\"path\": {}, \"hash\": {}, \"size\": {}{}}}",
                                  files_json.empty() ? "\n    " : ",\n    ",
                                  to_json_string(data.site_path),
                                  to_json_string(read_fingerprint(data.file)),
                                  read_size(data.file),
                                  set.empty() ? std::string() : ", \"texture_set\": " + to_json_string(set));
    }
    return fmt::format("{{\n  \"title\": {},\n  \"user_folder\": {},\n  \"runtime_size\": {},\n{}  \"files\": [{}\n  ]\n}}\n",
                       to_json_string(page.title),
                       to_json_string(page.user_folder),
                       get_runtime_size(site),
                       make_texture_sets_json(page.texture_sets),
                       files_json);
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

void write_web_data_manifest(const fs::path& site, const web_page_info& page)
{
    write_text_or_throw(site / WEB_DATA_MANIFEST_NAME, make_web_data_manifest(site, page));
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

auto make_web_tunnel_arguments(const fs::path& cloudflared, const std::string& local_origin) -> std::vector<std::string>
{
    return {cloudflared.empty() ? std::string(DEFAULT_CLOUDFLARED) : fs::to_utf8_string(cloudflared),
            "tunnel",
            "--no-autoupdate",
            "--url",
            local_origin};
}

auto find_web_tunnel_origin(const std::string& line) -> std::string
{
    const auto domain = line.find(QUICK_TUNNEL_DOMAIN);
    if(domain == std::string::npos)
    {
        return {};
    }
    const auto end = domain + std::char_traits<char>::length(QUICK_TUNNEL_DOMAIN);
    if(end < line.size() && (is_host_character(line[end]) || line[end] == '.' || line[end] == '/'))
    {
        return {};
    }
    auto name = domain;
    while(name > 0 && is_host_character(line[name - 1]))
    {
        --name;
    }
    const auto scheme_length = std::char_traits<char>::length(HTTPS_SCHEME);
    if(name == domain || name < scheme_length || line.compare(name - scheme_length, scheme_length, HTTPS_SCHEME) != 0)
    {
        return {};
    }
    return line.substr(name - scheme_length, end - (name - scheme_length));
}

auto is_web_tunnel_request(const std::string& host, const std::string& origin, const std::string& tunnel_origin) -> bool
{
    const auto scheme_length = std::char_traits<char>::length(HTTPS_SCHEME);
    if(tunnel_origin.compare(0, scheme_length, HTTPS_SCHEME) != 0 || tunnel_origin.size() == scheme_length)
    {
        return false;
    }
    const auto tunnel_host = string_utils::to_lower(tunnel_origin.substr(scheme_length));
    const auto request_host = string_utils::to_lower(host.substr(0, host.find(':')));
    return request_host == tunnel_host &&
           (origin.empty() || string_utils::to_lower(origin) == string_utils::to_lower(tunnel_origin));
}

} // namespace unravel::deployment
