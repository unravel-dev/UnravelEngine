#pragma once

#include "audit_types.h"

#include <asset_compiler/texture_target.h>
#include <deploy/content.h>
#include <deploy/deploy_report.h>
#include <deploy/deploy_settings.h>
#include <engine/assets/asset_storage.h>
#include <engine/rendering/feature_limits.h>
#include <engine/settings/settings.h>
#include <filesystem/filesystem.h>
#include <hpp/uuid.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/// What the audit's rules read: a snapshot of the project taken on the main thread, and what the run works out from
/// it on its own thread. Nothing in it refers to live editor state.
namespace unravel::audit
{

/// A compiled texture as its container's header describes it.
struct texture_facts
{
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t mips{1};
    /// The format's name ("BC7", "RGBA8").
    std::string format;
    bool is_compressed{false};
    /// Bits a texel takes; for a block format, the average.
    std::uint32_t bits_per_pixel{};
};

/// A compiled audio clip as it decodes.
struct audio_facts
{
    double seconds{};
    std::uint32_t sample_rate{};
    std::uint32_t channels{};
    /// The decoded sound's bytes: what the clip takes in memory while loaded.
    std::uint64_t decoded_bytes{};
};

/// A compiled mesh's counts.
struct mesh_facts
{
    std::uint32_t vertices{};
    std::uint32_t triangles{};
    std::uint32_t submeshes{};
    /// Generated LODs beside the full mesh.
    std::uint32_t lods{};
};

/// What the run read from an asset's compiled file.
struct asset_facts
{
    hpp::optional<texture_facts> texture;
    hpp::optional<audio_facts> audio;
    hpp::optional<mesh_facts> mesh;
};

/// A project asset as the asset database describes it.
struct asset_record
{
    /// "app:/data/textures/wall.png".
    std::string key;
    hpp::uuid uid{};
    /// The asset's file on disk.
    fs::path source_file;
    /// Its compiled files in the editor's cache; empty before its first compile.
    std::vector<fs::path> compiled_files;
    /// Bytes of its compiled files.
    std::uint64_t compiled_size{};
    /// A copy of its import settings; null for an asset type without any.
    std::shared_ptr<const asset_importer_meta> importer;
    /// Whether an import writes it as a by-product of another asset.
    bool is_generated{false};
    /// Filled by the run from the compiled file.
    asset_facts facts;
};

/// A settings profile with everything resolved.
struct profile_record
{
    std::string name;
    target_platform platform{target_platform::desktop};
    /// The key of the scene its game starts with; empty when none is chosen.
    std::string startup_scene;
    /// Its resolved settings as a document: the handles the content walk follows.
    std::string settings_document;
    /// The settings its builds run with: the project's, with the profile's overrides.
    settings resolved;
    /// The settings a new profile of its platform starts with, made from the project's settings now.
    settings start;
    deploy_settings deploy;
    /// What keeps it from deploying, as the Deploy window reports it.
    std::vector<std::string> deploy_problems;
    /// Filled by the run: what its deploy ships.
    deployment::content content;
    /// Filled by the run: its last finished deploy's report and the one before, when the cache keeps them.
    hpp::optional<deployment::deploy_report> last_deploy;
    hpp::optional<deployment::deploy_report> previous_deploy;
};

/// A rendering feature a profile's feature limits can turn off, as a component asks for it.
enum class limited_feature : std::uint8_t
{
    global_illumination,
    screen_space_reflections,
    screen_space_indirect_lighting,
    ambient_occlusion,
    bloom,
    decals,
    post_process_materials,
    temporal_anti_aliasing,
    fxaa,
};

/// Whether `limits` let the feature work.
auto is_feature_allowed(const feature_limits& limits, limited_feature feature) -> bool;
/// The settings path of the limit that decides the feature ("graphics/limits/decals").
auto get_feature_setting_path(limited_feature feature) -> const char*;
/// What components of the feature do while the limit turns it off, and why, as the end of a sentence about them
/// ("draw nothing, because ... is off").
auto get_feature_off_effect(limited_feature feature, bool is_plural) -> std::string;

/// An asset reference that resolves to nothing.
struct missing_reference
{
    /// The document holding it.
    std::string document;
    /// The entity's uid in a scene or prefab; empty for a material, a controller, or a field outside entities.
    std::string entity_id;
    /// The entity's path, as names from its root.
    std::string entity;
    /// The kind of asset the field holds ("Texture", "Material").
    std::string asset_type;
    hpp::uuid uid{};
};

/// A problem of one entity of a document.
struct entity_problem
{
    std::string document;
    /// The entity's uid in the document.
    std::string entity_id;
    /// The entity's path, as names from its root.
    std::string entity;
    std::string message;
};

/// Entities of one document whose component asks for a feature that limits can turn off.
struct feature_use
{
    std::string document;
    /// The component's name as the inspector titles it.
    std::string component;
    limited_feature feature{limited_feature::decals};
    /// The entities' paths, in document order.
    std::vector<std::string> entities;
};

/// An audio source of a scene or prefab and the clip it plays.
struct audio_source_use
{
    std::string document;
    std::string clip;
    bool is_spatial{false};
};

/// A document that cannot be read, and why.
struct document_problem
{
    std::string document;
    std::string message;
};

/// What loading the project's scenes, prefabs, materials and controllers found. Taken on the main thread.
struct document_scan
{
    /// Whether the scan ran; the scene rules report nothing without it.
    bool ran{false};
    std::size_t documents{};
    std::vector<missing_reference> missing;
    std::vector<entity_problem> joints;
    std::vector<feature_use> features;
    std::vector<audio_source_use> audio_sources;
    std::vector<document_problem> unreadable;
    /// Each invalid controller's key and its problems.
    std::vector<std::pair<std::string, std::vector<std::string>>> controllers;
    /// The keys of the materials whose surface reads the lit scene behind it (Refraction, or Scene Texture's Scene
    /// Color), in document order.
    std::vector<std::string> scene_color_materials;
};

/// A compiler message of the project's scripts.
struct script_message
{
    std::string file;
    std::uint32_t line{};
    std::string message;
    bool is_error{false};
};

/// Where the code analysis finds what it needs.
struct code_tool_paths
{
    /// The dotnet host the script compiler runs on.
    std::string dotnet;
    /// The analysis tool's assembly.
    fs::path tool;
    /// The project's compiled script library, with its .pdb beside it.
    fs::path assembly;
    /// Folders holding the assemblies it references: the engine's script library, the framework's reference set.
    std::vector<fs::path> reference_folders;
    /// The project's folder, to name script files by key.
    fs::path project_root;
};

/// One thing the code analysis found.
struct code_finding
{
    std::string rule;
    /// The script's key ("app:/data/scripts/player.cs"), or its path when it is outside the project.
    std::string file;
    std::uint32_t line{};
    /// "Player.OnUpdate".
    std::string method;
    /// What it calls or allocates ("Scene.FindEntityByName", "Int32[]").
    std::string target;
    /// More of the finding, when the rule has more: what an engine call allocates ("a new string"), the call a
    /// params array is passed to ("String.Format"), "by key" for an instantiation from an asset key.
    std::string detail;
    /// From the update method to `method`, each a "Type.Method".
    std::vector<std::string> path;
};

/// An update method the analysis starts from.
struct code_entry_point
{
    std::string method;
    std::string file;
    std::uint32_t line{};
    /// How many methods of the project's code it reaches.
    std::size_t reachable{};
};

/// What the code analysis found.
struct code_analysis
{
    bool ran{false};
    /// Why it did not run or failed, in a sentence.
    std::string error;
    std::size_t methods{};
    std::vector<code_finding> findings;
    std::vector<code_entry_point> entry_points;
};

/// Everything a run reads.
struct input
{
    fs::path project_root;
    std::string project_name;
    std::string engine_version;
    /// The project's settings, without a profile.
    settings project_settings;
    /// The asset databases the content walk reads.
    deployment::content_source source;
    /// The project's assets.
    std::vector<asset_record> assets;
    /// What the editor compiles the project's textures for: the compiled files' sizes and formats follow it.
    asset_compiler::texture_target editor_texture_target;
    std::vector<profile_record> profiles;
    /// The profile the editor deploys (the Deploy window's choice).
    std::string deploy_profile;
    /// Whether the project's scripts compiled, and the compiler's messages.
    bool scripts_compiled{true};
    std::vector<script_message> script_messages;
    document_scan scan;
    /// Empty `tool` skips the code analysis, with a note.
    code_tool_paths code_tool;
    /// Filled by the run.
    std::vector<deployment::content_reference> references;
    code_analysis code;
};

} // namespace unravel::audit
