#pragma once

#include <filesystem/filesystem.h>

#include <string>
#include <vector>

/// The browser player and what a web deploy writes beside it.
namespace unravel::deployment
{

/// The player template's folder name under the editor's players/ folder.
constexpr const char* WEB_TEMPLATE_NAME = "web";
/// The list of the game's files the page loads before the engine starts, beside the page.
constexpr const char* WEB_DATA_MANIFEST_NAME = "data_manifest.json";

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

/// What a web build's page shows and keeps, written into its data_manifest.json.
struct web_page_info
{
    /// The game's name: the page's title, shown while the game loads.
    std::string title;
    /// Where the player keeps the game's own files (get_web_user_folder).
    std::string user_folder;
};

/**
 * @brief The data_manifest.json of a web build in `site`: the page's `title` and `user_folder`, the size of the
 * runtime it downloads first (runtime_size: its _framework/ folder, uncompressed, which weighs the runtime's part of
 * the loading bar against the data's), and every file under its data/ folder, as the page fetches it
 * ("data/engine/content.pak"), with a fingerprint of its content and its size, in name order. The page writes each
 * one to that path in the player's file system, which the engine reads as its own folder, and keeps it in the
 * browser's cache under its fingerprint, so a later visit fetches only the files a new deploy changed. Throws when a
 * file cannot be listed or read.
 */
auto make_web_data_manifest(const fs::path& site, const web_page_info& page) -> std::string;

/**
 * @brief Copies the template's page and runtime into `site`, the profile's folder, beside the data/ the other steps
 * deploy. Unchanged files are skipped, and runtime files an older template had are removed. Throws on failure.
 */
void copy_web_player(const web_template& player, const fs::path& site);

/// Writes make_web_data_manifest's list into `site` once its data/ and the player are deployed. Throws on failure.
void write_web_data_manifest(const fs::path& site, const web_page_info& page);

/// `text` for a part of a URL: every byte but the unreserved characters of RFC 3986 as %XX.
auto encode_url_part(const std::string& text) -> std::string;

/// The path a local server serves a project's web build under, "/<project>/<profile>/": each build has a page path
/// of its own, and with it caches of its own in the browser.
auto make_web_site_path(const std::string& project, const std::string& profile) -> std::string;

/**
 * @brief The address of a web build's page: `origin` ("http://127.0.0.1:8742"), the site path with each part
 * encoded, and the run arguments, one arg= each, which the page hands the game (game/web/wwwroot/main.js).
 */
auto make_web_page_address(const std::string& origin, const std::string& site_path, const std::vector<std::string>& arguments)
    -> std::string;

} // namespace unravel::deployment
