#pragma once

#include <engine/engine_export.h>
#include <engine/rendering/gi/mesh_sdf_baker.h>
// mesh::load_data is a nested type, so a forward declaration of `mesh` cannot name it.
#include <engine/rendering/mesh.h>

namespace unravel
{

/**
 * @brief Extracts a triangle soup for the distance-field bake from raw vertex and index data.
 *
 * The single place vertex positions are decoded for a bake, shared by both sources of mesh
 * data: compiled assets (via the overload below) and meshes built procedurally at runtime.
 * Primitives never pass through the asset compiler, so without this they would carry no field
 * at all and would be invisible to global illumination.
 *
 * Kept out of the baker translation unit on purpose: the bake itself depends only on math,
 * which lets it be built and validated in isolation from the asset and graphics layers.
 *
 * @param vertex_data    Packed vertex buffer, laid out per @p format.
 * @param vertex_count   Vertices in @p vertex_data.
 * @param format         Vertex layout; must carry a position attribute.
 * @param indices        Triangle corner indices, three per triangle.
 * @param triangle_count Triangles in @p indices.
 * @return false when the inputs carry no usable position data.
 */
auto extract_sdf_source_geometry(const uint8_t* vertex_data,
                                 uint32_t vertex_count,
                                 const gfx::vertex_layout& format,
                                 const uint32_t* indices,
                                 uint32_t triangle_count,
                                 sdf_source_geometry& out) -> bool;

/**
 * @brief Extracts the geometry of ONE submesh.
 *
 * A model's submeshes are drawn at their own node transforms, which importers routinely differ
 * between, so a single field baked over the whole mesh cannot be placed correctly for all of
 * them -- it would have to be at several transforms at once. Each submesh therefore gets its own
 * field, and this selects the triangles belonging to one.
 *
 * Selection is by the submesh's own face range. A submesh's `data_group_id` is its MATERIAL
 * index, which several submeshes routinely share, so selecting by it would pull every sibling
 * using the same material into this submesh's field -- baking their geometry a second time at
 * the wrong transform, and costing one whole material group per submesh instead of one submesh.
 *
 * Both the positions and the indices are compacted to the submesh, because every per-vertex pass
 * in the baker is linear in the vertex count it is handed.
 */
auto extract_sdf_source_geometry(const mesh::load_data& data,
                                 const mesh::submesh& submesh,
                                 sdf_source_geometry& out) -> bool;

/**
 * @brief Extracts one submesh from a chosen LOD, for a cheaper bake.
 *
 * A closest-point query costs roughly the square root of the triangle count, not its logarithm,
 * so simplified geometry is a real saving rather than a rounding error: measured at ~3.8x for 16x
 * the triangles (`test_bake_cost_is_dominated_by_voxels_not_triangles`). It does NOT change the
 * voxel count, which is the other and larger term -- see @ref mesh_sdf_bake_settings::max_total_voxels.
 *
 * @param lod_index 0 for the base topology. The valid range is [0, number of GENERATED levels];
 *                  a higher request is clamped to the coarsest level available rather than
 *                  dropped to the base, since asking for a higher level means "cheaper" and
 *                  full detail is the most expensive possible answer to that. A mesh with no
 *                  generated LODs therefore always bakes from the base.
 */
auto extract_sdf_source_geometry(const mesh::load_data& data,
                                 uint32_t lod_index,
                                 size_t submesh_index,
                                 sdf_source_geometry& out) -> bool;

} // namespace unravel
