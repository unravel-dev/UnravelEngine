#pragma once

#include "deploy_settings.h"
#include "variant_cook.h"

#include <asset_compiler/texture_target.h>
#include <engine/assets/asset_storage.h>
#include <engine/settings/settings.h>
#include <filesystem/filesystem.h>

#include <string>
#include <unordered_set>
#include <vector>

/// A deploy's texture variants: textures a profile compiles differently from the editor (its platform's
/// formats, its defaults, a texture's own quality for that platform) ship from the profile's cache,
/// cache/profiles/<profile>/<protocol>, one file per texture and profile.
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
    /// The editor's compiled folder of the protocol's assets.
    fs::path compiled;
    /// cache/profiles/<profile>/<protocol>: the variants, laid out as the compiled folder.
    fs::path variants;
    /// The keys ("app:/data/...", "engine:/data/...") of the protocol's textures the deploy ships.
    std::vector<std::string> shipped;
    /// The keys of the protocol's textures the content selection leaves out: their variants are kept for a
    /// later deploy, which checks them again before shipping them.
    std::vector<std::string> left_out;
};

/**
 * @param protocol The protocol whose textures are cooked: the project's (app) or the engine's.
 * @param database The protocol's asset index.
 * @param left_out The path keys (fs::make_path_key) of the assets the content selection leaves out.
 * @param editor_target What the editor compiles the protocol's files for in its cache
 * (asset_compiler::get_editor_texture_target_for).
 */
auto make_texture_cook_plan(const fs::path& project_root,
                            const deploy_settings& params,
                            const settings& runtime_settings,
                            const std::string& protocol,
                            const asset_database::database_t& database,
                            const std::unordered_set<std::string>& left_out,
                            const asset_compiler::texture_target& editor_target) -> texture_cook_plan;

/**
 * @brief Brings the profile's variants in step with the project: every shipped texture the profile
 * compiles differently from the editor gets a variant whose manifest proves it current (compiled again
 * when it is not), and every other file in the profile's cache is deleted, so the cache never holds a
 * variant that could ship stale.
 */
auto cook_texture_variants(const texture_cook_plan& plan) -> cook_report;

/// cook_texture_variants for a deploy step: logs what it did and throws with the textures that did not
/// compile.
void cook_textures(const texture_cook_plan& plan);

} // namespace unravel::deployment
