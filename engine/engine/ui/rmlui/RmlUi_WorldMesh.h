/*
 * RmlUi meshes prepared for drawing straight into a 3D view with anti-aliased outlines
 */

#pragma once

#include <RmlUi/Core/Types.h>
#include <RmlUi/Core/Vertex.h>

#include <array>
#include <cstdint>
#include <vector>

namespace unravel
{

/**
 * @brief What vs_rmlui_world reads per vertex besides the RmlUi vertex.
 *
 * A vertex is a corner of an axis-aligned rectangle (kind 0), of a lone triangle (kind 1), of a fringe
 * (kind 2): the strip a lone triangle's edge reaches out across, or of a strip triangle (kind 3): one of
 * two triangles that make a thin quad between two edges it fades across, such as a piece of a border.
 * Outline sides and edges are those no other shape of the mesh continues across: a rectangle grows across
 * its own by about a pixel, a triangle keeps its corners and leaves that to its fringes, and all of them
 * fade by their coverage. Where shapes of different colours meet, the later one fades across the shared
 * edge the same way and the earlier one reaches under it without fading, so the fade blends the two
 * colours; a later draw's outline does the same to a mesh drawn before it, or covers it, which pulls the
 * mesh in. An edge is a line "inward normal x, y, offset": dot(normal, position) + offset is the distance
 * inside it, with a fourth component of 1 when the shape fades across it. A triangle's interior edge gives
 * its slot to a fading edge of the mesh that meets the triangle at a convex corner, so that a triangle
 * running along a fading edge it does not own fades by it too.
 */
struct RmlUi_WorldShapeVertex
{
    /// A rectangle's bounds in mesh pixels (left, top, right, bottom), a triangle's first edge (a strip
    /// triangle's own long edge), or the edge a fringe lies across.
    std::array<float, 4> bounds_or_edge0{};
    /// A rectangle's sides (left, top, right, bottom): 1 where it fades across the side, 2 where it
    /// reaches under a later shape without fading, 3 where a later draw covers it and it pulls in, 0
    /// elsewhere; a triangle's second edge (a strip triangle's other long edge, which its partner owns);
    /// or, for a fringe across a strip's long edge, the strip's other long edge.
    std::array<float, 4> outline_sides_or_edge1{};
    /// A rectangle's or a fringe's texture coordinates per mesh pixel (d(u, v)/dx, d(u, v)/dy), or a
    /// triangle's third edge.
    std::array<float, 4> tex_coord_gradient_or_edge2{};
    /// The texture coordinates the shape may sample: min u, min v, max u, max v.
    std::array<float, 4> tex_coord_bounds{};
    /// A fringe's outer corner: the inward normals of the two edges of the mesh of its kind (fading or
    /// reaching under) that meet at it, so that the fringes on either side move it alike, and zero at the
    /// fringe's corners on the edge itself. A triangle's corner: the inward normals of the covered edges
    /// that meet at it, which it pulls in by. Or how far, in mesh pixels, each side of a rectangle (left,
    /// top, right, bottom) may reach under a later shape: no further than that shape is deep.
    std::array<float, 4> outline_normals_or_underlay_limits{};
    /// x = the kind: 0 for a rectangle, 1 for a triangle, 2 for a fringe, 3 for a strip triangle; y = 1 for
    /// a fringe that reaches under a later shape without fading, and z = how far it may reach, in mesh
    /// pixels.
    std::array<float, 4> kind{};
};
// Read as six tightly packed float4 attributes (TexCoord1 to TexCoord6).
static_assert(sizeof(RmlUi_WorldShapeVertex) == 6 * 4 * sizeof(float));

/// An edge of a mesh's outline that the mesh fades across, in mesh pixels.
struct RmlUi_WorldOutlineEdge
{
    Rml::Vector2f from;
    Rml::Vector2f to;
    /// Points into the shape the edge belongs to.
    Rml::Vector2f inward_normal;
    /// How far, in mesh pixels, that shape reaches inward from the edge.
    float depth = 0.0f;
};

auto operator==(const RmlUi_WorldOutlineEdge& a, const RmlUi_WorldOutlineEdge& b) -> bool;

/// An edge of an outline by its key: the line an axis-aligned edge lies on, or the two ends of another.
struct RmlUi_WorldOutlineKey
{
    int64_t key = 0;
    uint32_t edge = 0;
};

/// The outline of a mesh, for a draw before it to compare its own outline with.
struct RmlUi_WorldOutline
{
    std::vector<RmlUi_WorldOutlineEdge> edges;
    /// Indices into edges sorted by key: axis-aligned ones by the line they lie on, the others by their ends.
    std::vector<RmlUi_WorldOutlineKey> axis_aligned_edges;
    std::vector<RmlUi_WorldOutlineKey> slanted_edges;
    /// The box around the edges, in mesh pixels.
    Rml::Vector2f min;
    Rml::Vector2f max;
};

/**
 * @brief An RmlUi mesh rebuilt for a world draw: each rectangle, lone triangle and fringe with corners of
 * its own and the per-vertex shape data, plus its outline.
 */
struct RmlUi_WorldMesh
{
    std::vector<Rml::Vertex> vertices;
    /// Parallel to vertices.
    std::vector<RmlUi_WorldShapeVertex> shapes;
    std::vector<int> indices;
    RmlUi_WorldOutline outline;
};

/**
 * @brief Finds the axis-aligned rectangles of a mesh (two consecutive triangles over four corners, with
 * texture coordinates that follow the axes, as RmlUi builds glyphs, images, nine-patches and plain
 * backgrounds) and treats every other triangle alone (rounded backgrounds and borders), marking the
 * outline sides and edges of both and the edges between shapes of different colours, and adding a fringe
 * across each such edge of a lone triangle.
 * @param later_edges The outline edges of the draws after this mesh's draw, in this mesh's pixels: where
 * one runs along an edge of the mesh all the way, the mesh reaches under it from the other side, or pulls
 * in under it from the same side, instead of fading.
 */
auto build_world_mesh(Rml::Span<const Rml::Vertex> vertices,
                      Rml::Span<const int> indices,
                      Rml::Span<const RmlUi_WorldOutlineEdge> later_edges = {}) -> RmlUi_WorldMesh;

/// Appends to @p touching the edges of @p later, moved by @p offset into the pixels of @p outline, that run
/// along @p outline: on the line of one of its axis-aligned edges and overlapping it, or between the same
/// two ends as another of its edges.
void append_touching_edges(const RmlUi_WorldOutline& outline,
                           const RmlUi_WorldOutline& later,
                           Rml::Vector2f offset,
                           std::vector<RmlUi_WorldOutlineEdge>& touching);

} // namespace unravel
