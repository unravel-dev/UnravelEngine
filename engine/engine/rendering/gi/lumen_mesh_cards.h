#pragma once

#include <engine/engine_export.h>

#include <math/math.h>

#include <cstdint>
#include <vector>

namespace unravel
{

struct sdf_source_geometry;

/**
 * @brief One Lumen mesh card: an oriented box, axis-aligned in mesh space, that the surface cache
 *        captures orthographically looking along -axis_z (into the mesh).
 *
 * The box spans the in-plane extent of the surface it covers and a depth range starting half a
 * cluster voxel in front of the nearest surface it was built for, so a capture depth-tested inside
 * the box sees exactly the layer of the mesh the card represents (Lumen's FLumenCardBuildData).
 */
struct lumen_card
{
    ///< Box centre in mesh local space.
    math::vec3 origin{0.0f};
    ///< Half size along axis_x, axis_y and axis_z.
    math::vec3 extent{0.0f};
    math::vec3 axis_x{1.0f, 0.0f, 0.0f};
    math::vec3 axis_y{0.0f, 1.0f, 0.0f};
    ///< The direction the card faces (outward); the capture looks along -axis_z.
    math::vec3 axis_z{0.0f, 0.0f, 1.0f};
    ///< Axis-aligned direction: 0 -X, 1 +X, 2 -Y, 3 +Y, 4 -Z, 5 +Z (the side of the mesh it faces).
    uint32_t direction = 0;
};

/**
 * @brief The card set of one submesh (Lumen's FMeshCardsBuildData).
 */
struct lumen_mesh_cards
{
    ///< Local-space bounds the cards were built in (the mesh bounds grown by the build margin).
    math::bbox bounds{};
    std::vector<lumen_card> cards;
    ///< Two-sided submeshes keep only their outer cards and take a larger sampling bias.
    bool is_mostly_two_sided = false;
};

/**
 * @brief Builds a submesh's cards by surfel clustering, a port of UE 5.8's
 *        MeshCardRepresentationUtilities (GenerateCardRepresentationData).
 *
 * Surfels are found by casting 32 rays per cell into the mesh from each of the six axis-aligned
 * sides of a voxel grid (at most 64 cells per axis, 10 cm target cells); every hit facing the
 * side is a candidate, and hits deeper than a gap of one cell start a new layer. Each surfel is
 * weighted by its coverage and by how much of its hemisphere escapes the mesh. Clusters are depth
 * layers per side: the outer layer, then (one-sided meshes only) the deeper near plane covering
 * the most remaining weight, repeatedly. The @p max_cards heaviest clusters become cards.
 *
 * Mesh local units are metres; Lumen's centimetre constants are converted.
 *
 * @param geometry  The submesh's triangles (the same soup the SDF bake reads).
 * @param two_sided True when the submesh's material is two-sided: back faces count as front faces.
 * @param max_cards Card budget (Lumen's default MaxLumenMeshCards is 12).
 * @return false when the geometry has no triangles.
 */
auto build_lumen_mesh_cards(const sdf_source_geometry& geometry,
                            bool two_sided,
                            uint32_t max_cards,
                            lumen_mesh_cards& out) -> bool;

} // namespace unravel
