#pragma once

#include <engine/engine_export.h>

#include <math/math.h>

#include <cstdint>
#include <vector>

namespace unravel
{

struct sdf_source_geometry;

/**
 * @brief One mesh card: an oriented box, axis-aligned in mesh space, that the surface cache
 *        captures orthographically looking along -axis_z (into the mesh).
 *
 * The box spans the in-plane extent of the surface it covers and a depth range starting half a
 * cluster voxel in front of the nearest surface it was built for, so a capture depth-tested inside
 * the box sees exactly the layer of the mesh the card represents.
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
 * @brief The card set of one submesh.
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
 * @brief What a card build saw, recorded when the build runs in debug mode, for the card generation surfel and
 *        cluster views (lumen_visualize_pass). Mesh local space.
 */
struct lumen_card_build_debug
{
    enum class surfel_type : uint8_t
    {
        ///< A surfel candidate the build kept, and one it dropped as inside geometry.
        valid,
        invalid,
        ///< In a card's cluster: the card's own surfels, the side's surfels an earlier card took, the rest.
        cluster,
        used,
        idle,
    };

    struct surfel
    {
        math::vec3 position{0.0f};
        math::vec3 normal{0.0f};
        surfel_type type = surfel_type::valid;
    };

    struct ray
    {
        math::vec3 start{0.0f};
        math::vec3 end{0.0f};
        bool is_hit = false;
    };

    struct cluster
    {
        std::vector<surfel> surfels;
        ///< From each of the card's surfels seen from a deeper near plane to that plane.
        std::vector<ray> rays;
    };

    ///< Every side's surfel candidates, side by side.
    std::vector<surfel> surfels;
    ///< One per card, in card order.
    std::vector<cluster> clusters;
};

/**
 * @brief Builds a submesh's cards by surfel clustering.
 *
 * Surfels are found by casting 32 rays per cell into the mesh from each of the six axis-aligned
 * sides of a voxel grid (at most 64 cells per axis, 10 cm target cells); every hit facing the
 * side is a candidate, and hits deeper than a gap of one cell start a new layer. Each surfel is
 * weighted by its coverage and by how much of its hemisphere escapes the mesh. Clusters are depth
 * layers per side: the outer layer, then (one-sided meshes only) the deeper near plane covering
 * the most remaining weight, repeatedly. The @p max_cards heaviest clusters become cards.
 *
 * Mesh local units are metres, and so are the build's length constants.
 *
 * @param geometry  The submesh's triangles (the same soup the SDF bake reads).
 * @param two_sided True when the submesh's material is two-sided: back faces count as front faces.
 * @param max_cards Card budget (the import settings' max_cards, 12 by default).
 * @param debug     When set, receives what the build saw (debug mode); the cards are the same either way.
 * @return false when the geometry has no triangles.
 */
auto build_lumen_mesh_cards(const sdf_source_geometry& geometry,
                            bool two_sided,
                            uint32_t max_cards,
                            lumen_mesh_cards& out,
                            lumen_card_build_debug* debug = nullptr) -> bool;

} // namespace unravel
