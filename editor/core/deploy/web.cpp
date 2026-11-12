#include "web.h"
#include "deploy_settings.h"
#include "files.h"
#include "folders.h"

#include <engine/assets/impl/asset_extensions.h>
#include <filesystem/file_fingerprint.h>
#include <logging/logging.h>
#include <string_utils/utils.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <set>
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
/// The letters of a page build's name: enough of a fingerprint to tell every build's page code from another's.
constexpr std::size_t PAGE_BUILD_LENGTH = 16;
/// The precompressed copies a web server may send instead of a file: not downloaded besides it.
constexpr std::array<const char*, 2> PRECOMPRESSED_EXTENSIONS = {".br", ".gz"};
constexpr player_wording WEB_PLAYER_WORDING{"The browser player beside this editor", "Rebuild the editor."};
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

/// The name of the file a server sends `file` for: its own, or for a precompressed copy the file's it is a copy of.
auto get_served_name(const fs::path& file) -> std::string
{
    return fs::to_utf8_string(is_precompressed(file) ? file.stem() : file.filename());
}

/// The first file of the runtime folder `source` that `site_runtime` lacks, by name; precompressed copies and the
/// files `trimmed` names do not count. Empty when it has them all.
auto find_missing_runtime_file(const fs::path& source, const fs::path& site_runtime, const std::set<std::string>& trimmed) -> std::string
{
    fs::error_code ec;
    for(fs::directory_iterator it(source, ec), end; !ec && it != end; it.increment(ec))
    {
        const auto name = fs::to_utf8_string(it->path().filename());
        const bool is_downloaded = it->is_regular_file(ec) && !is_precompressed(it->path()) && !trimmed.contains(name);
        if(is_downloaded && !is_present(site_runtime / it->path().filename()))
        {
            return name;
        }
    }
    return {};
}

/// A file of a web build's data, where the deploy's data steps wrote it.
struct data_file
{
    fs::path file;
    /// The file's path in the player's file system: '/' separated, relative to the application's folder, UTF-8.
    std::string path;
};

/// The files under `data_root`/data, in path order.
auto list_data_files(const fs::path& data_root) -> std::vector<data_file>
{
    std::vector<data_file> files;
    const auto data = data_root / DATA_FOLDER;
    fs::error_code ec;
    for(fs::recursive_directory_iterator it(data, ec), end; !ec && it != end; it.increment(ec))
    {
        if(it->is_regular_file(ec))
        {
            files.push_back({it->path(), fs::to_utf8_string(it->path().lexically_relative(data_root))});
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
                  return lhs.path < rhs.path;
              });
    return files;
}

/// Whether the engine maps `file`: a pack.
auto is_mapped_file(const fs::path& file) -> bool
{
    return file.extension() == fs::path(ex::get_pack_file_name()).extension();
}

/// Cuts every file under `data_root`/data into chunks in `chunks_folder`; the files in path order.
auto write_data_chunks(const fs::path& data_root, const fs::path& chunks_folder) -> std::vector<web_data_file>
{
    std::vector<web_data_file> files;
    for(const auto& data : list_data_files(data_root))
    {
        web_data_file file;
        file.path = data.path;
        file.chunks = write_web_chunks(data.file, chunks_folder);
        file.is_mapped = is_mapped_file(data.file);
        for(const auto& chunk : file.chunks)
        {
            file.size += chunk.size;
        }
        files.push_back(std::move(file));
    }
    return files;
}

/// Removes the chunks in `chunks_folder` that none of `files` has: what earlier builds were cut into.
void remove_stale_chunks(const fs::path& chunks_folder, const std::vector<web_data_file>& files)
{
    std::set<std::string> listed;
    for(const auto& file : files)
    {
        for(const auto& chunk : file.chunks)
        {
            listed.insert(chunk.name);
        }
    }
    prune_folder_or_throw(chunks_folder,
                          [&listed](const fs::path& name)
                          {
                              return listed.contains(name.generic_string());
                          });
}

/// The page's own files in `site`, and the runtime's loader, in name order: what get_web_page_build fingerprints.
auto list_page_code_files(const fs::path& site) -> std::vector<fs::path>
{
    std::vector<fs::path> files;
    fs::error_code ec;
    for(fs::directory_iterator it(site, ec), end; !ec && it != end; it.increment(ec))
    {
        const auto name = it->path().filename();
        if(it->is_regular_file(ec) && name != WEB_DATA_MANIFEST_NAME && name != BUILD_STAMP_NAME && !is_precompressed(it->path()))
        {
            files.push_back(it->path());
        }
    }
    if(ec)
    {
        throw std::runtime_error(fmt::format("Listing {} failed: {}", site.generic_string(), ec.message()));
    }
    std::sort(files.begin(), files.end());
    files.push_back(site / RUNTIME_LOADER_FILE);
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

/// The set among `sets` whose pack `file` is (ex::get_set_pack_file_name); empty for any other file.
auto get_set_of(const fs::path& file, const std::vector<std::string>& sets) -> std::string
{
    const auto name = fs::to_utf8_string(file.filename());
    const auto found = std::find_if(sets.begin(),
                                    sets.end(),
                                    [&name](const std::string& set)
                                    {
                                        return name == ex::get_set_pack_file_name(set);
                                    });
    return found != sets.end() ? *found : std::string();
}

/// `names` as a JSON array.
auto make_json_array(const std::vector<std::string>& names) -> std::string
{
    std::string listed;
    for(const auto& name : names)
    {
        listed += (listed.empty() ? "" : ", ") + string_utils::make_json_string(name);
    }
    return "[" + listed + "]";
}

/// The manifest's line listing `sets` under `key`; empty without sets.
auto make_sets_json(const char* key, const std::vector<std::string>& sets) -> std::string
{
    return sets.empty() ? std::string() : fmt::format("  \"{}\": {},\n", key, make_json_array(sets));
}

/// The manifest's graphics block.
auto make_graphics_json(const web_graphics_policy& graphics) -> std::string
{
    return fmt::format("  \"graphics\": {{\"backends\": {}, \"fallback_notice\": {}, \"help_link\": {}}},\n",
                       make_json_array(graphics.backends),
                       string_utils::make_json_string(graphics.fallback_notice),
                       string_utils::make_json_string(graphics.help_link));
}

/// The manifest's tag of a set pack ({"texture_set": "astc"}); empty for any other file.
auto make_set_tag_json(const fs::path& file, const web_page_info& page) -> std::string
{
    if(const auto set = get_set_of(file, page.texture_sets); !set.empty())
    {
        return ", \"texture_set\": " + string_utils::make_json_string(set);
    }
    if(const auto set = get_set_of(file, page.shader_sets); !set.empty())
    {
        return ", \"shader_set\": " + string_utils::make_json_string(set);
    }
    return {};
}

/// A file's chunks as the manifest lists them: [name, size, check] each.
auto make_chunks_json(const std::vector<web_chunk>& chunks) -> std::string
{
    std::string listed;
    for(const auto& chunk : chunks)
    {
        listed += fmt::format("{}[{}, {}, {}]", listed.empty() ? "" : ", ", string_utils::make_json_string(chunk.name), chunk.size, chunk.check);
    }
    return "[" + listed + "]";
}

/// One data file's line of the manifest.
auto make_file_json(const web_data_file& file, const web_page_info& page) -> std::string
{
    return fmt::format("{{\"path\": {}, \"size\": {}{}{}, \"chunks\": {}}}",
                       string_utils::make_json_string(file.path),
                       file.size,
                       file.is_mapped ? ", \"mapped\": true" : "",
                       make_set_tag_json(fs::from_utf8_string(file.path), page),
                       make_chunks_json(file.chunks));
}

auto get_fallback_notice_name(web_fallback_notice notice) -> const char*
{
    switch(notice)
    {
        case web_fallback_notice::ask:
            return "ask";
        case web_fallback_notice::silent:
            return "silent";
        case web_fallback_notice::tell:
        default:
            return "tell";
    }
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
    lookup.error = get_version_problem(found.engine_version, engine_version, WEB_PLAYER_WORDING);
    if(!lookup.error.empty())
    {
        return lookup;
    }
    if(!is_present(found.get_site_dir() / PAGE_FILE) || !is_present(found.get_site_dir() / RUNTIME_LOADER_FILE))
    {
        lookup.error = fmt::format("{} is incomplete. {}", WEB_PLAYER_WORDING.player, WEB_PLAYER_WORDING.advice);
    }
    return lookup;
}

auto get_web_user_folder(const std::string& company, const std::string& product) -> std::string
{
    const auto& name = product.empty() ? std::string(WEB_PROGRAM_NAME) : product;
    return company.empty() ? fmt::format("{}/{}", WEB_PREFERENCES_ROOT, name)
                           : fmt::format("{}/{}/{}", WEB_PREFERENCES_ROOT, company, name);
}

auto make_web_graphics_policy(const web_deploy_settings& web) -> web_graphics_policy
{
    web_graphics_policy policy;
    switch(web.graphics)
    {
        case web_graphics::webgpu_then_webgl2:
            policy.backends = {ex::WEBGPU_GRAPHICS_NAME, ex::WEBGL2_GRAPHICS_NAME};
            break;
        case web_graphics::webgl2:
            policy.backends = {ex::WEBGL2_GRAPHICS_NAME};
            break;
        case web_graphics::webgpu:
        default:
            policy.backends = {ex::WEBGPU_GRAPHICS_NAME};
            break;
    }
    policy.fallback_notice = get_fallback_notice_name(web.fallback_notice);
    policy.help_link = web.help_link;
    return policy;
}

auto get_web_run_graphics(const web_deploy_settings& web) -> std::string
{
    // A build for one API starts that one: there is nothing to choose.
    if(web.graphics != web_graphics::webgpu_then_webgl2)
    {
        return {};
    }
    switch(web.run_graphics)
    {
        case web_run_graphics::webgpu:
            return ex::WEBGPU_GRAPHICS_NAME;
        case web_run_graphics::webgl2:
            return ex::WEBGL2_GRAPHICS_NAME;
        case web_run_graphics::automatic:
        default:
            return {};
    }
}

auto make_web_data_manifest(const web_manifest& manifest) -> std::string
{
    const auto& page = manifest.page;
    std::string files_json;
    for(const auto& file : manifest.files)
    {
        files_json += (files_json.empty() ? "\n    " : ",\n    ") + make_file_json(file, page);
    }
    return fmt::format("{{\n  \"format\": {},\n  \"build\": {},\n  \"title\": {},\n  \"user_folder\": {},\n  \"runtime_size\": {},\n"
                       "{}{}{}  \"chunks\": {},\n  \"files\": [{}\n  ]\n}}\n",
                       WEB_MANIFEST_FORMAT,
                       string_utils::make_json_string(manifest.build),
                       string_utils::make_json_string(page.title),
                       string_utils::make_json_string(page.user_folder),
                       manifest.runtime_size,
                       make_graphics_json(page.graphics),
                       make_sets_json("texture_sets", page.texture_sets),
                       make_sets_json("shader_sets", page.shader_sets) + make_sets_json("trimmed", page.trimmed_runtime_files),
                       string_utils::make_json_string(WEB_CHUNKS_FOLDER),
                       files_json);
}

auto get_web_page_build(const fs::path& site) -> std::string
{
    std::string text;
    for(const auto& file : list_page_code_files(site))
    {
        text += fs::to_utf8_string(file.filename()) + ":" + read_fingerprint(file) + "\n";
    }
    return fs::hash_bytes_fingerprint(text.data(), text.size()).substr(0, PAGE_BUILD_LENGTH);
}

auto find_web_trimmed_files(const web_template& player, framework_trim trim) -> std::set<std::string>
{
    trim.kept.insert(trim.kept.end(), WEB_PLAYER_ASSEMBLIES.begin(), WEB_PLAYER_ASSEMBLIES.end());
    return find_trimmed_files(player.get_site_dir() / RUNTIME_FOLDER, trim);
}

void copy_web_player(const web_template& player, const fs::path& site, const std::set<std::string>& trimmed)
{
    const auto source = player.get_site_dir();
    const fs::mirror_filter is_shipped = [&trimmed](const fs::path& file)
    {
        return !trimmed.contains(get_served_name(file));
    };
    fs::error_code ec;
    for(fs::directory_iterator it(source, ec), end; !ec && it != end; it.increment(ec))
    {
        const auto destination = site / it->path().filename();
        if(it->is_directory(ec))
        {
            mirror_or_throw(it->path(), destination, is_shipped);
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

void remove_precompressed_copies(const fs::path& site, const std::vector<std::string>& names)
{
    for(const auto& name : names)
    {
        for(const auto* extension : PRECOMPRESSED_EXTENSIONS)
        {
            auto copy = site / fs::from_utf8_string(name);
            copy += extension;
            remove_file_or_throw(copy);
        }
    }
}

auto get_web_player_problem(const web_template& player,
                            const fs::path& site,
                            const std::vector<std::string>& replaced,
                            const std::set<std::string>& trimmed) -> std::string
{
    const auto source = player.get_site_dir();
    for(const auto* name : {PAGE_FILE, RUNTIME_LOADER_FILE})
    {
        fs::error_code ec;
        const bool is_projects_own = std::find(replaced.begin(), replaced.end(), name) != replaced.end();
        if(!is_projects_own && !fs::have_same_content(source / name, site / name, ec))
        {
            return fmt::format("The browser player in {} is not this editor's: its {} differs. Deploy with the "
                               "dependencies, which copies the player.",
                               site.generic_string(),
                               name);
        }
    }
    const auto missing = find_missing_runtime_file(source / RUNTIME_FOLDER, site / RUNTIME_FOLDER, trimmed);
    if(!missing.empty())
    {
        return fmt::format("The browser player in {} lacks {}, which this build's page downloads. Deploy with the "
                           "dependencies, which copies the player.",
                           site.generic_string(),
                           missing);
    }
    return {};
}

void remove_legacy_web_data(const fs::path& site)
{
    prune_folder_or_throw(site,
                          [](const fs::path& name)
                          {
                              return name != DATA_FOLDER;
                          });
}

void write_web_data(const fs::path& data_root, const fs::path& site, const web_page_info& page)
{
    const auto chunks_folder = site / WEB_CHUNKS_FOLDER;
    web_manifest manifest;
    manifest.page = page;
    manifest.files = write_data_chunks(data_root, chunks_folder);
    remove_stale_chunks(chunks_folder, manifest.files);
    manifest.build = get_web_page_build(site);
    manifest.runtime_size = get_runtime_size(site);
    write_text_or_throw(site / WEB_DATA_MANIFEST_NAME, make_web_data_manifest(manifest));
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

auto make_web_page_address(const std::string& origin,
                           const std::string& site_path,
                           const std::vector<std::string>& arguments,
                           const std::string& graphics) -> std::string
{
    std::string address = origin;
    std::size_t start = 0;
    for(std::size_t slash = site_path.find('/'); slash != std::string::npos; slash = site_path.find('/', start))
    {
        address += encode_url_part(site_path.substr(start, slash - start)) + "/";
        start = slash + 1;
    }
    address += encode_url_part(site_path.substr(start));
    std::string query = graphics.empty() ? std::string() : "?graphics=" + encode_url_part(graphics);
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
