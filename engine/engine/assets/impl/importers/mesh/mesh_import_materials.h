#pragma once

#include <engine/assets/impl/importers/mesh_importer.h>

#include <filesystem/filesystem.h>

#include <vector>

struct aiScene;

namespace unravel
{
namespace importer
{
namespace mesh_import
{

/**
 * @brief Import every scene material as a pbr_material, producing the textures they bind.
 *
 * Two passes over the materials: the first collects the texture jobs (embedded extraction,
 * conversions, spec-gloss pair bakes), which then run in parallel; the second binds the
 * produced files and the scalar properties. Embedded textures no material referenced are
 * extracted last. @p textures receives the sorted manifest of everything produced.
 */
void process_materials(asset_manager& am,
                       const fs::path& filename,
                       const fs::path& output_dir,
                       const aiScene* scene,
                       std::vector<imported_material>& materials,
                       std::vector<imported_texture>& textures);

} // namespace mesh_import
} // namespace importer
} // namespace unravel
