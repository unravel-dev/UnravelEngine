#pragma once

#include "deploy_settings.h"

#include <asset_compiler/texture_target.h>
#include <engine/assets/asset_storage.h>
#include <engine/settings/settings.h>
#include <filesystem/filesystem.h>

#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

/// A deploy's texture variants: textures a profile compiles differently from the editor (its platform's
/// formats, its defaults, a texture's own quality for that platform) ship from the profile's cache,
/// cache/profiles/<profile>/app, one file per texture and profile.
namespace unravel::deployment
{

/// The texture target a profile deploys for: its platform, the formats its GPUs read and its texture
/// defaults.
auto get_profile_texture_target(const deploy_settings& params, const settings& runtime_settings) -> asset_compiler::texture_target;

/// What cooking a profile's textures reads, resolved before it runs.
struct texture_cook_plan
{
    std::string profile;
    asset_compiler::texture_target target;
    /// The target of the editor's own compiled files, which ship where the profile resolves the same and they
    /// are current.
    asset_compiler::texture_target editor_target;
    /// The editor's compiled folder of the project's assets.
    fs::path compiled;
    /// cache/profiles/<profile>/app: the variants, laid out as the compiled folder.
    fs::path variants;
    /// The keys ("app:/data/...") of the project's textures the deploy ships.
    std::vector<std::string> shipped;
    /// The keys of the project's textures the content selection leaves out: their variants are kept for a
    /// later deploy, which checks them again before shipping them.
    std::vector<std::string> left_out;
};

/**
 * @param app_database The project's asset index; its textures are cooked.
 * @param left_out The path keys (fs::make_path_key) of the assets the content selection leaves out.
 * @param editor_target What the editor compiles its own cache for (asset_compiler::get_editor_texture_target).
 */
auto make_texture_cook_plan(const fs::path& project_root,
                            const deploy_settings& params,
                            const settings& runtime_settings,
                            const asset_database::database_t& app_database,
                            const std::unordered_set<std::string>& left_out,
                            const asset_compiler::texture_target& editor_target) -> texture_cook_plan;

/// What cooking did, for the log and the tests.
struct texture_cook_report
{
    /// Shipped textures that ship a variant.
    std::size_t variants{};
    /// Variants compiled because none was current.
    std::size_t compiled{};
    /// Files of the profile's cache deleted because nothing needed them.
    std::size_t removed{};
    /// Files of the profile's cache nothing needs that could not be deleted: a deploy would ship them.
    std::vector<fs::path> undeletable;
    /// The keys of the textures that did not compile.
    std::vector<std::string> failed;
};

/**
 * @brief Brings the profile's variants in step with the project: every shipped texture the profile
 * compiles differently from the editor gets a variant whose manifest proves it current (compiled again
 * when it is not), and every other file in the profile's cache is deleted, so the cache never holds a
 * variant that could ship stale.
 */
auto cook_texture_variants(const texture_cook_plan& plan) -> texture_cook_report;

/// cook_texture_variants for a deploy step: logs what it did and throws with the textures that did not
/// compile.
void cook_textures(const texture_cook_plan& plan);

} // namespace unravel::deployment
