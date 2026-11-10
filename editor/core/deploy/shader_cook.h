#pragma once

#include "deploy_settings.h"
#include "protocol_data.h"
#include "variant_cook.h"

#include <engine/assets/asset_storage.h>
#include <filesystem/filesystem.h>

#include <set>
#include <string>
#include <vector>

/// A deploy's cooked shaders: the compiled shaders a profile's renderer reads and the editor does not compile
/// for itself (WGSL and GLSL ES for the web) ship from the profile's cache, cache/profiles/<profile>/shaders/<protocol>,
/// laid out as the protocol's compiled folder.
namespace unravel::deployment
{

/// The renderer extensions of the compiled shaders a deploy ships: Android's player renders with Vulkan, the browser
/// player with the Web profile's graphics APIs (WebGPU, WebGL 2 or both). Empty for the desktop, which ships every
/// variant its platform compiles, since the game picks its renderer when it starts.
auto get_shader_variants(const deploy_settings& params) -> std::set<std::string>;

/**
 * @brief The shader sets of a packed web deploy for WebGPU and WebGL 2: each API's shaders in a pack of its own, of
 * which the page downloads the one it starts. Empty for every other deploy, whose shaders ship in the protocols' packs.
 */
auto get_web_shader_sets(const deploy_settings& params) -> std::vector<shader_set_job>;

/// The ones of `shipped` (get_shader_variants) the editor does not compile for itself: the deploy cooks them.
auto get_cooked_shader_variants(const std::set<std::string>& shipped) -> std::set<std::string>;

/// One protocol's shaders, as cooking reads them.
struct shader_cook_protocol
{
    /// cache/profiles/<profile>/shaders/<protocol>: the cooked shaders, laid out as the compiled folder.
    fs::path variants;
    /// The protocol's shaders, by whether the deploy ships them.
    asset_split shaders;
};

/// What cooking a profile's shaders reads, resolved before it runs.
struct shader_cook_plan
{
    std::string profile;
    /// The renderer extensions cooked (".wgsl", ".essl"); none: the deploy cooks no shader.
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
 * @param engine_database The engine's asset index. Every engine shader ships: the content selection decides only
 * the engine's fonts and UI files.
 * @param app_database The project's asset index.
 * @param left_out The project assets the content selection leaves out.
 */
auto make_shader_cook_plan(const fs::path& project_root,
                           const deploy_settings& params,
                           const asset_database::database_t& engine_database,
                           const asset_database::database_t& app_database,
                           const left_out_assets& left_out) -> shader_cook_plan;

/**
 * @brief Brings the profile's cooked shaders in step with the project: every shipped shader gets a file per
 * renderer whose manifest proves it current (compiled again when it is not), and every other file in the
 * profile's shader cache is deleted, so it never holds a shader that could ship stale.
 */
auto cook_shader_variants(const shader_cook_plan& plan) -> cook_report;

/// cook_shader_variants for a deploy step: logs what it did and throws with the shaders that did not compile.
void cook_shaders(const shader_cook_plan& plan);

} // namespace unravel::deployment
