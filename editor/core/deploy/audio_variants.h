#pragma once

#include "deploy_settings.h"
#include "variant_cook.h"

#include <engine/assets/asset_storage.h>
#include <engine/settings/settings.h>
#include <filesystem/filesystem.h>

#include <string>
#include <unordered_set>
#include <vector>

/// A deploy's audio variants: clips a profile compiles differently from the editor (its audio default, a clip's own
/// compression for the profile's platform) ship from the profile's cache, cache/profiles/<profile>/audio/app, one file
/// per clip and profile.
namespace unravel::deployment
{

/// What cooking a profile's audio reads, resolved before it runs.
struct audio_cook_plan
{
    std::string profile;
    /// The platform whose tab of each clip's import settings the profile compiles with.
    target_platform platform{target_platform::desktop};
    /// The profile's audio defaults (its resolved settings').
    settings::asset_settings::audio_importer_settings defaults;
    /// The project's audio defaults, which the editor's own compiled files are compiled with (on the Desktop tab):
    /// they ship where the profile resolves the same and they are current.
    settings::asset_settings::audio_importer_settings editor_defaults;
    /// The editor's compiled folder of the project's assets.
    fs::path compiled;
    /// cache/profiles/<profile>/audio/app: the variants, laid out as the compiled folder.
    fs::path variants;
    /// The keys ("app:/data/...") of the clips the deploy ships.
    std::vector<std::string> shipped;
    /// The keys of the clips the content selection leaves out: their variants are kept for a later deploy, which
    /// checks them again before shipping them.
    std::vector<std::string> left_out;
};

/**
 * @param database The project's asset index.
 * @param left_out The path keys (fs::make_path_key) of the assets the content selection leaves out.
 * @param editor_defaults The project's audio defaults the editor compiles with
 * (asset_compiler::get_editor_audio_defaults).
 */
auto make_audio_cook_plan(const fs::path& project_root,
                          const deploy_settings& params,
                          const settings& runtime_settings,
                          const asset_database::database_t& database,
                          const std::unordered_set<std::string>& left_out,
                          const settings::asset_settings::audio_importer_settings& editor_defaults) -> audio_cook_plan;

/**
 * @brief Brings the profile's audio variants in step with the project: every shipped clip the profile compiles
 * differently from the editor gets a variant whose manifest proves it current (compiled again when it is not), and
 * every other file in the profile's audio cache is deleted, so the cache never holds a variant that could ship stale.
 */
auto cook_audio_variants(const audio_cook_plan& plan) -> cook_report;

/// cook_audio_variants for a deploy step: logs what it did and throws with the clips that did not compile.
void cook_audio(const audio_cook_plan& plan);

} // namespace unravel::deployment
