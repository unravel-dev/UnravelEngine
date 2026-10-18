#pragma once

#include <filesystem/filesystem.h>

namespace unravel
{
namespace importer
{
namespace mesh_import
{

/**
 * @brief Wait until a mesh source and, for multi-file formats (.gltf buffers/images, .obj mtllib),
 * every external sidecar it names exist and stopped growing. False when the source or a glTF buffer
 * is still missing or incomplete at the timeout - the import must not run, or Assimp reads truncated
 * buffers. Missing images and material libraries only cost their maps and are reported instead.
 */
auto wait_for_mesh_source_dependencies(const fs::path& path) -> bool;

} // namespace mesh_import
} // namespace importer
} // namespace unravel
