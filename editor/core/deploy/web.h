#pragma once

#include <filesystem/filesystem.h>

#include <string>

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
 * @brief The data_manifest.json of a web build in `site`: every file under its data/ folder, as the page fetches
 * it ("data/engine/content.pak"), in name order. The page writes each one to that path in the player's file
 * system, which the engine reads as its own folder.
 */
auto make_web_data_manifest(const fs::path& site) -> std::string;

/**
 * @brief Copies the template's page and runtime into `site`, the profile's folder, beside the data/ the other steps
 * deploy. Unchanged files are skipped, and runtime files an older template had are removed. Throws on failure.
 */
void copy_web_player(const web_template& player, const fs::path& site);

/// Writes make_web_data_manifest's list into `site` once its data/ is deployed. Throws on failure.
void write_web_data_manifest(const fs::path& site);

} // namespace unravel::deployment
