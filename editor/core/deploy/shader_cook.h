#pragma once

#include "deploy_settings.h"

#include <engine/assets/asset_storage.h>
#include <filesystem/filesystem.h>

#include <cstddef>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

/// A deploy's cooked shaders: the compiled shaders a profile's renderer reads and the editor does not compile
/// for itself (WGSL for the web) ship from the profile's cache, cache/profiles/<profile>/shaders/<protocol>,
/// laid out as the protocol's compiled folder.
namespace unravel::deployment
{

/// The renderer extensions of the compiled shaders a deploy ships: Android's player renders with Vulkan and the
/// browser player with WebGPU only. Empty for the desktop, which ships every variant its platform compiles, since
/// the game picks its renderer when it starts.
auto get_shader_variants(const deploy_settings& params) -> std::set<std::string>;

/// The ones of `shipped` (get_shader_variants) the editor does not compile for itself: the deploy cooks them.
auto get_cooked_shader_variants(const std::set<std::string>& shipped) -> std::set<std::string>;

/// One protocol's shaders, as cooking reads them.
struct shader_cook_protocol
{
    /// cache/profiles/<profile>/shaders/<protocol>: the cooked shaders, laid out as the compiled folder.
    fs::path variants;
    /// The keys ("engine:/data/shaders/...") of the protocol's shaders the deploy ships.
    std::vector<std::string> shipped;
    /// The keys of the shaders the content selection leaves out: their cooked files are kept for a later
    /// deploy, which checks them again before shipping them.
    std::vector<std::string> left_out;
};

/// What cooking a profile's shaders reads, resolved before it runs.
struct shader_cook_plan
{
    std::string profile;
    /// The renderer extensions cooked (".wgsl"); none: the deploy cooks no shader.
    std::set<std::string> renderers;
    std::vector<shader_cook_protocol> protocols;

    /// Whether the deploy cooks any shader.
    auto has_work() const -> bool
    {
        return !renderers.empty();
    }
};

/**
 * @brief What a deploy with `params` cooks: the engine's shaders and the project's, for the renderers the profile
 * ships and the editor does not compile for itself; nothing for a profile whose renderers it compiles.
 * @param engine_database The engine's asset index, which ships whole.
 * @param app_database The project's asset index.
 * @param left_out The path keys (fs::make_path_key) of the project assets the content selection leaves out.
 */
auto make_shader_cook_plan(const fs::path& project_root,
                           const deploy_settings& params,
                           const asset_database::database_t& engine_database,
                           const asset_database::database_t& app_database,
                           const std::unordered_set<std::string>& left_out) -> shader_cook_plan;

/// What cooking did, for the log and the tests.
struct shader_cook_report
{
    /// Cooked files the deploy ships: one per shipped shader and renderer.
    std::size_t variants{};
    /// Cooked files compiled because none was current.
    std::size_t compiled{};
    /// Files of the profile's shader cache deleted because nothing needed them.
    std::size_t removed{};
    /// Files of the profile's shader cache nothing needs that could not be deleted: a deploy would ship them.
    std::vector<fs::path> undeletable;
    /// The keys of the shaders that did not compile.
    std::vector<std::string> failed;
};

/**
 * @brief Brings the profile's cooked shaders in step with the project: every shipped shader gets a file per
 * renderer whose manifest proves it current (compiled again when it is not), and every other file in the
 * profile's shader cache is deleted, so it never holds a shader that could ship stale.
 */
auto cook_shader_variants(const shader_cook_plan& plan) -> shader_cook_report;

/// cook_shader_variants for a deploy step: logs what it did and throws with the shaders that did not compile.
void cook_shaders(const shader_cook_plan& plan);

} // namespace unravel::deployment
