#pragma once

#include "assembly_references.h"
#include "web_chunks.h"

#include <engine/assets/impl/asset_extensions.h>
#include <filesystem/filesystem.h>

#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace unravel
{
struct web_deploy_settings;
} // namespace unravel

/// The browser player and what a web deploy writes beside it.
namespace unravel::deployment
{

/// The player template's folder name under the editor's players/ folder.
constexpr const char* WEB_TEMPLATE_NAME = "web";
/// The list of the game's files the page loads before the engine starts, beside the page.
constexpr const char* WEB_DATA_MANIFEST_NAME = "data_manifest.json";
/// The manifest's format, which the page reads one of (game/web/wwwroot/data.js, MANIFEST_FORMAT).
constexpr int WEB_MANIFEST_FORMAT = 2;

/**
 * @brief The browser player template beside the editor (cmake/web_player.cmake).
 *
 * The .NET browser runtime with the engine linked into its wasm module, the page that starts it and a stamp
 * naming the engine version that built it. It holds no game: web deploys copy it beside the game's data.
 */
struct web_template
{
    /// <editor folder>/players/web.
    fs::path root;
    std::string engine_version;

    /// The page and the runtime, which a deploy copies into its folder.
    auto get_site_dir() const -> fs::path;
};

/// A template found, or why there is none.
struct web_template_lookup
{
    web_template found;
    /// Sentences for the Deploy window; empty when the template was found.
    std::string error;

    auto is_found() const -> bool
    {
        return error.empty();
    }
};

/**
 * @brief The template under `root` when it is complete and an editor of `engine_version` built it: a player of
 * another version may not read the data the editor deploys, nor bind its scripts.
 */
auto find_web_template(const fs::path& root, const std::string& engine_version) -> web_template_lookup;

/**
 * @brief The folder the browser player keeps its own files in (user:/ and cache:/): SDL's preferences folder in the
 * browser, /libsdl/<company>/<product>, with the product the deployed game uses - the project's, else the player's
 * program name (game/web/Program.cs). The page keeps that folder in the browser's IndexedDB, in a database of its
 * own, so games on one site never mix their saves.
 */
auto get_web_user_folder(const std::string& company, const std::string& product) -> std::string;

/**
 * @brief The graphics APIs a web build's page may start the game with and what it tells the player when WebGL 2
 * stands in for WebGPU: the manifest's graphics block (game/web/wwwroot/graphics.js reads it).
 */
struct web_graphics_policy
{
    /// "webgpu" and "webgl2", in the order the page prefers them.
    std::vector<std::string> backends{ex::WEBGPU_GRAPHICS_NAME};
    /// "tell", "ask" or "silent" (web_fallback_notice).
    std::string fallback_notice{"tell"};
    /// The page the notices link to on turning WebGPU on; empty: the page's own advice.
    std::string help_link;
};

/// The policy of a web profile's settings.
auto make_web_graphics_policy(const web_deploy_settings& web) -> web_graphics_policy;

/**
 * @brief The graphics API Deploy and Run and the public link ask the page for ("webgpu" or "webgl2"): the profile's
 * run_graphics where it renders with both; empty to let the page choose.
 */
auto get_web_run_graphics(const web_deploy_settings& web) -> std::string;

/// What a web build's page shows and keeps, written into its data_manifest.json.
struct web_page_info
{
    /// The game's name: the page's title, shown while the game loads.
    std::string title;
    /// Where the player keeps the game's own files (get_web_user_folder).
    std::string user_folder;
    /// The texture sets the build ships in packs of their own (ex::get_set_pack_file_name), the one computers read
    /// first; empty when its textures ship in the protocols' packs.
    std::vector<std::string> texture_sets;
    /// The shader sets the build ships in packs of their own (ex::get_set_pack_file_name), one per graphics API;
    /// empty when its shaders ship in the protocols' packs.
    std::vector<std::string> shader_sets;
    web_graphics_policy graphics;
    /// The runtime's library files the build leaves out (find_web_trimmed_files), by file name: the page takes
    /// them off the list of what the runtime downloads.
    std::vector<std::string> trimmed_runtime_files;
};

/// A data file of a web build as the page gets it.
struct web_data_file
{
    /// Its path in the player's file system, which the engine reads as its own folder: '/' separated, relative to
    /// the application's folder, UTF-8 ("data/engine/content.pak").
    std::string path;
    std::uint64_t size{};
    /// The file's bytes in order, as chunks in the site's chunks folder (web_chunks.h).
    std::vector<web_chunk> chunks;
    /// The engine maps the file (a pack): the page downloads it straight into the player's memory.
    bool is_mapped{};
};

/// What a web build's data_manifest.json says.
struct web_manifest
{
    web_page_info page;
    /// Names the build's page code (get_web_page_build).
    std::string build;
    /// The bytes of the runtime the page downloads first, which weigh its part of the loading bar against the data's.
    std::uint64_t runtime_size{};
    /// In path order.
    std::vector<web_data_file> files;
};

/**
 * @brief The text of a web build's data_manifest.json, which the page reads before anything else
 * (game/web/wwwroot/main.js): the format the page must know (format), the build's page code (build), the page's
 * title and the folder of the player's own files (title, user_folder), the runtime's size (runtime_size), the
 * graphics policy (graphics), the folder of the chunks (chunks), and every data file with its size and its chunks
 * in order, each as [name, size, check]. A pack carries "mapped". With texture sets, the manifest lists them
 * (texture_sets) and names the set of each set's pack (texture_set): the page downloads the packs of the first set
 * the browser's GPU reads, the first set's when it reads none. Shader sets are listed the same way (shader_sets,
 * shader_set): the page downloads the set of the graphics API it chose. A trimmed build lists the runtime's library
 * files it left out (trimmed).
 */
auto make_web_data_manifest(const web_manifest& manifest) -> std::string;

/**
 * @brief A name for the page code in `site`: a fingerprint of the page's own files (everything at the top of the
 * site but the manifest and the build's stamp) and of the runtime's loader. The page loads its code under this
 * name, so a browser or a host never serves the code of another build beside this build's files.
 */
auto get_web_page_build(const fs::path& site) -> std::string;

/// The assemblies the browser player starts with, which a trimmed build keeps with everything they reference: the
/// player's own (game/web/UnravelWebPlayer.csproj), which references the script bridge, and the library the
/// runtime's JavaScript side calls into.
constexpr std::array<const char*, 2> WEB_PLAYER_ASSEMBLIES = {"UnravelWebPlayer", "System.Runtime.InteropServices.JavaScript"};

/// The names of the template's runtime files a web build with `trim` leaves out (find_trimmed_files, with
/// WEB_PLAYER_ASSEMBLIES kept); none for a trim that is off.
auto find_web_trimmed_files(const web_template& player, framework_trim trim) -> std::set<std::string>;

/**
 * @brief Copies the template's page and runtime into `site`, the profile's folder. Unchanged files are skipped, and
 * runtime files an older template had are removed. Throws on failure.
 * @param trimmed Runtime files the build leaves out, by name: neither they nor their precompressed copies are
 * copied, and the ones an earlier build copied are removed.
 */
void copy_web_player(const web_template& player, const fs::path& site, const std::set<std::string>& trimmed = {});

/// Removes the precompressed copies (.br, .gz) beside the files `names` of `site`: copies of what the files held
/// before something else took their names, which a server would send in their place.
void remove_precompressed_copies(const fs::path& site, const std::vector<std::string>& names);

/**
 * @brief Why the player in `site` cannot start a build this editor deploys, in one sentence, or empty: the page and
 * the runtime's loader are the template's, byte for byte, and the site has every runtime file the build's page
 * downloads. A deploy that leaves the player as it is (without dependencies) writes its files for this editor's page.
 * @param replaced Files the project's extra files put in the player's place, '/' separated from the site's top: the
 * project's own, whatever they hold.
 * @param trimmed Runtime files the build leaves out, by name, which the site need not have.
 */
auto get_web_player_problem(const web_template& player,
                            const fs::path& site,
                            const std::vector<std::string>& replaced = {},
                            const std::set<std::string>& trimmed = {}) -> std::string;

/// Removes the data/ folder a build of an older editor left in `site`, which kept the data's files whole there. A
/// deploy calls it before the project's extra files are copied, whose own data/ folder then stays. Throws on failure.
void remove_legacy_web_data(const fs::path& site);

/**
 * @brief Writes a web build's data into `site` once the player is there: every file under `data_root`/data cut into
 * chunks in the site's chunks folder (web_chunks.h), the chunks no file of this build has removed, and
 * data_manifest.json. Throws on failure.
 */
void write_web_data(const fs::path& data_root, const fs::path& site, const web_page_info& page);

/// `text` for a part of a URL: every byte but the unreserved characters of RFC 3986 as %XX.
auto encode_url_part(const std::string& text) -> std::string;

/// The path a local server serves a project's web build under, "/<project>/<profile>/": each build has a page path
/// of its own, and with it caches of its own in the browser.
auto make_web_site_path(const std::string& project, const std::string& profile) -> std::string;

/**
 * @brief The address of a web build's page: `origin` ("http://127.0.0.1:8742"), the site path with each part
 * encoded, the graphics API the page starts the game with when `graphics` names one (graphics=webgl2, see
 * get_web_run_graphics), and the run arguments, one arg= each, which the page hands the game
 * (game/web/wwwroot/main.js).
 */
auto make_web_page_address(const std::string& origin,
                           const std::string& site_path,
                           const std::vector<std::string>& arguments,
                           const std::string& graphics = {}) -> std::string;

/// The tunnel tool a public link runs when Editor Settings names none: Cloudflare's cloudflared on PATH.
constexpr const char* DEFAULT_CLOUDFLARED = "cloudflared";

/**
 * @brief The command that opens a Cloudflare quick tunnel to `local_origin` ("http://127.0.0.1:8742"): a new
 * https://<name>.trycloudflare.com address forwards to it, without a Cloudflare account, until the process ends.
 * `cloudflared` empty: the one on PATH.
 */
auto make_web_tunnel_arguments(const fs::path& cloudflared, const std::string& local_origin) -> std::vector<std::string>;

/// The quick tunnel's public origin ("https://<name>.trycloudflare.com") a line of cloudflared's output announces;
/// empty when it announces none. Addresses with a path, such as the API's it reports failures of, or under a longer
/// domain are not one.
auto find_web_tunnel_origin(const std::string& line) -> std::string;

/**
 * @brief Whether a request with these Host and Origin values (empty when absent) came through the tunnel whose public
 * origin is `tunnel_origin`: its Host names the tunnel and its Origin, when it has one, is the tunnel's page. A page
 * elsewhere sends its own Origin, and a name rebound to this computer is not the tunnel's.
 */
auto is_web_tunnel_request(const std::string& host, const std::string& origin, const std::string& tunnel_origin) -> bool;

} // namespace unravel::deployment
