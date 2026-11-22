#pragma once

#include <engine/animation/animation.h>
#include <engine/assets/asset_storage.h>
#include <engine/rendering/morph_targets.h>

#include <filesystem/filesystem.h>

#include <cstdint>
#include <vector>

struct aiAnimation;
struct aiMesh;
struct aiScene;

namespace unravel
{
namespace importer
{
namespace mesh_import
{

/**
 * @brief What the importer does with a file's morph targets (Assimp's anim meshes).
 */
struct morph_import_options
{
    /// Read the morph targets and their curves; off, prepare_morph_targets drops them from the scene.
    bool import_morph_targets{true};
    /// Where a target's normals come from.
    mesh_importer_meta::model_meta::morph_normals normals{mesh_importer_meta::model_meta::morph_normals::import};
    /// A vertex a target moves less than this, in the mesh's units, without turning its normal, is left out of it.
    float position_threshold{0.00001f};
    /// The file stores default weights (glTF). FBX keeps a channel's current percentage there instead.
    bool file_default_weights{false};
    /// FBX: animation keys index blend shape channels, and a channel with in-between shapes keeps its full shape.
    bool fbx_channels{false};
};

/// The morph options of an import of @p path with @p import_meta.
auto get_morph_import_options(const mesh_importer_meta& import_meta, const fs::path& path) -> morph_import_options;

/// A normal delta under this turns nothing worth keeping.
constexpr float k_morph_normal_threshold = 0.001f;

/**
 * @brief Prepares a scene read without post-processing. Without Import Morph Targets, drops every morph target and
 *        morph curve. Otherwise gives each vertex of a mesh with targets its morph signature in a spare texture
 *        coordinate set, so welding keeps apart the vertices only a target separates (closed lips), and turns off
 *        Optimize Meshes and Split Large Meshes, which drop the targets.
 * @return The post-processing flags to apply.
 */
auto prepare_morph_targets(aiScene& scene, const morph_import_options& options, uint32_t flags) -> uint32_t;

/// After post-processing: removes the signatures prepare_morph_targets wrote.
void remove_morph_signatures(aiScene& scene);

/**
 * @brief For each blend shape channel of @p mesh, the anim mesh holding its full shape. FBX gives a channel with
 *        in-between shapes one anim mesh per shape, all with the channel's current percentage as their weight, where a
 *        channel with one shape has weight 1: such a run keeps only its last (full) shape. Every anim mesh is its own
 *        channel otherwise.
 */
auto get_morph_channel_shapes(const aiMesh* mesh, bool fbx_channels) -> std::vector<unsigned int>;

/// The name of @p mesh's anim mesh @p shape as a target: its own, or "Target <channel>" when it has none.
auto get_morph_target_name(const aiMesh* mesh, unsigned int shape, unsigned int channel) -> std::string;

/**
 * @brief Adds @p mesh's morph targets to @p builder, its vertices numbered from @p vertex_offset: each channel's full
 *        shape as position and normal deltas from the rest shape.
 */
void read_morph_targets(const aiMesh* mesh,
                        uint32_t vertex_offset,
                        const morph_import_options& options,
                        morph_target_builder& builder);

/**
 * @brief Adds the morph target curves of @p animation to @p clip: one curve per target its keys set, named after the
 *        target, over the clip's normalized time. A target a key leaves out holds its value.
 */
void read_morph_curves(const aiScene* scene,
                       const aiAnimation* animation,
                       double ticks_per_second,
                       const morph_import_options& options,
                       animation_clip& clip);

} // namespace mesh_import
} // namespace importer
} // namespace unravel
