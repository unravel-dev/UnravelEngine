#pragma once

#include "mesh_import_geometry.h"

#include <engine/animation/animation.h>

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
 * @brief One clip per scene animation, named "<file>_<animation>". Channels of nodes that move
 * neither a bone nor a mesh are dropped (a file without meshes keeps every animated node); the
 * root-motion nodes are the shallowest animated nodes on the skeleton's trunk.
 */
void process_animations(const aiScene* scene,
                        const fs::path& filename,
                        node_index_lut_t& node_to_index_lut,
                        std::vector<animation_clip>& animations);

} // namespace mesh_import
} // namespace importer
} // namespace unravel
