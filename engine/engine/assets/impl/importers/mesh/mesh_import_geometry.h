#pragma once

#include <engine/rendering/mesh.h>

#include <math/math.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct aiScene;

namespace unravel
{
namespace importer
{
namespace mesh_import
{

/// Node name -> depth-first pre-order index (later duplicates of a name win).
using node_index_lut_t = std::unordered_map<std::string, unsigned int>;

auto assign_node_indices(const aiScene* scene) -> node_index_lut_t;

/**
 * @brief Per submesh, the transform from the node holding it to the space its vertices were
 * re-expressed in to share one bind pose per bone; nullopt for vertices left in the node's space.
 */
using submesh_placements_t = std::vector<std::optional<math::transform>>;

/**
 * @brief Append every scene mesh as a submesh: vertices, triangles, bones and a stable id.
 *
 * A skinned mesh whose offset matrices place it in another space than the meshes that registered its
 * bones first is re-expressed in theirs (the engine keeps one bind pose per bone).
 */
auto process_meshes(const aiScene* scene, mesh::load_data& load_data) -> submesh_placements_t;

/// Build the armature tree and the bind-pose bounds of the submeshes it places.
void process_nodes(const aiScene* scene,
                   mesh::load_data& load_data,
                   const submesh_placements_t& placements,
                   node_index_lut_t& node_to_index_lut);

/// Accumulate the submesh bounds placed by the armature into @p out.
void accumulate_bounds_from_armature(const mesh::load_data& load_data, math::bbox& out);

/// Rotate the root 180 degrees about Y (and the bounds with it) to face the engine's forward.
void apply_import_facing_correction_to_load_data(mesh::load_data& load_data);

} // namespace mesh_import
} // namespace importer
} // namespace unravel
