// This translation unit depends only on math and the standard library, so the bake can be
// built and validated in isolation from the asset and graphics layers. The mesh-format glue
// lives in mesh_sdf_source.cpp -- keep it that way.
#include "mesh_sdf_baker.h"

#include <engine/profiler/profiler.h>

#include <poolstl/poolstl.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace unravel
{
namespace
{

/// Maximum triangles referenced by a single BVH leaf. Small leaves keep the closest-point
/// query's inner loop short; the traversal overhead of going smaller stops paying off.
constexpr uint32_t bvh_max_leaf_triangles = 4;

/// How far the one-voxel shell floor may exceed the authored two-sided thickness before the
/// bake spends resolution to close the gap (see the thin-geometry escalation in bake_mesh_sdf).
/// Four keeps mildly-coarse shells - awnings, banners - baking exactly as they always have,
/// while catching the order-of-magnitude phantoms (a rope shelled to 10-20x its diameter).
constexpr float k_max_shell_floor_ratio = 4.0f;

/// Which feature of a triangle the closest point landed on. Selects the pseudonormal used
/// for the inside/outside test.
enum class triangle_feature : uint8_t
{
    vertex0 = 0,
    vertex1,
    vertex2,
    edge01,
    edge12,
    edge20,
    face,
};

struct closest_point_result
{
    math::vec3 point{};
    triangle_feature feature{triangle_feature::face};
};

/**
 * @brief Closest point on a triangle to a query point, and which feature it lies on.
 *
 * Voronoi-region formulation (Ericson, Real-Time Collision Detection). The branches are
 * kept explicit rather than derived from barycentric coordinates afterwards, because
 * classifying by comparing barycentrics against an epsilon misclassifies near-degenerate
 * triangles, and a misclassified feature picks the wrong pseudonormal and flips the sign
 * of the voxel.
 */
auto closest_point_on_triangle(const math::vec3& p,
                               const math::vec3& a,
                               const math::vec3& b,
                               const math::vec3& c) -> closest_point_result
{
    const math::vec3 ab = b - a;
    const math::vec3 ac = c - a;
    const math::vec3 ap = p - a;
    const float d1 = math::dot(ab, ap);
    const float d2 = math::dot(ac, ap);
    if(d1 <= 0.0f && d2 <= 0.0f)
    {
        return {a, triangle_feature::vertex0};
    }
    const math::vec3 bp = p - b;
    const float d3 = math::dot(ab, bp);
    const float d4 = math::dot(ac, bp);
    if(d3 >= 0.0f && d4 <= d3)
    {
        return {b, triangle_feature::vertex1};
    }
    const float vc = d1 * d4 - d3 * d2;
    if(vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
    {
        const float v = d1 / (d1 - d3);
        return {a + ab * v, triangle_feature::edge01};
    }
    const math::vec3 cp = p - c;
    const float d5 = math::dot(ab, cp);
    const float d6 = math::dot(ac, cp);
    if(d6 >= 0.0f && d5 <= d6)
    {
        return {c, triangle_feature::vertex2};
    }
    const float vb = d5 * d2 - d1 * d6;
    if(vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
    {
        const float w = d2 / (d2 - d6);
        return {a + ac * w, triangle_feature::edge20};
    }
    const float va = d3 * d6 - d5 * d4;
    if(va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
    {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return {b + (c - b) * w, triangle_feature::edge12};
    }
    const float denom = 1.0f / (va + vb + vc);
    const float v = vb * denom;
    const float w = vc * denom;
    return {a + ab * v + ac * w, triangle_feature::face};
}

/**
 * @brief Squared distance from a point to an axis-aligned box, zero when inside.
 */
auto distance_squared_to_bounds(const math::bbox& bounds, const math::vec3& p) -> float
{
    const math::vec3 clamped = math::clamp(p, bounds.min, bounds.max);
    const math::vec3 delta = p - clamped;
    return math::dot(delta, delta);
}

/**
 * @brief Interior angle of triangle (a, b, c) at corner @p a.
 *
 * Used to weight vertex pseudonormals. Angle weighting is what makes the pseudonormal test
 * exact rather than merely usually right: an unweighted normal sum is biased toward
 * whichever side of the vertex happens to be more finely tessellated.
 */
auto compute_corner_angle(const math::vec3& a, const math::vec3& b, const math::vec3& c) -> float
{
    const math::vec3 e0 = b - a;
    const math::vec3 e1 = c - a;
    const float len0 = math::length(e0);
    const float len1 = math::length(e1);
    if(len0 <= 0.0f || len1 <= 0.0f)
    {
        return 0.0f;
    }
    const float cos_angle = math::clamp(math::dot(e0, e1) / (len0 * len1), -1.0f, 1.0f);
    return std::acos(cos_angle);
}

/// Quantised vertex position, used as an EXACT key when welding coincident vertices.
/// Hashing alone is not enough: a hash collision would weld two unrelated vertices, which
/// corrupts the pseudonormal at both and flips the sign of every voxel near them.
struct quantized_position
{
    int64_t x = 0;
    int64_t y = 0;
    int64_t z = 0;

    auto operator==(const quantized_position& other) const -> bool
    {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct quantized_position_hash
{
    auto operator()(const quantized_position& p) const -> size_t
    {
        // Large odd primes, mixed by XOR. Collisions are fine here -- the map compares keys
        // for equality, so a collision costs a probe, not a wrong weld.
        const uint64_t hash = uint64_t(p.x) * 73856093ull ^ uint64_t(p.y) * 19349663ull ^
                              uint64_t(p.z) * 83492791ull;
        return size_t(hash);
    }
};

/**
 * @brief Packs an undirected vertex pair into a stable key for the edge adjacency map.
 */
auto make_edge_key(uint32_t v0, uint32_t v1) -> uint64_t
{
    const uint64_t lo = math::min(v0, v1);
    const uint64_t hi = math::max(v0, v1);
    return (hi << 32) | lo;
}

/**
 * @brief Triangle BVH plus the pseudonormal tables needed for a signed closest-point query.
 */
class sdf_triangle_accelerator
{
public:
    auto build(const sdf_source_geometry& geometry) -> bool;

    /**
     * @brief Signed distance from @p p to the surface.
     *
     * Positive outside, negative inside. When @p unsigned_only is set the sign is skipped
     * entirely and the unsigned distance is returned, which is the correct answer for
     * geometry that is not a closed surface.
     */
    auto signed_distance(const math::vec3& p, bool unsigned_only) const -> float;

    /**
     * @brief Whether the surface has a meaningful signed interior: closed, manifold, AND
     *        enclosing actual volume.
     *
     * The inside/outside test is only meaningful when all three hold. On an open surface the
     * pseudonormal reports "inside" for regions that are plainly outside. And a surface can be
     * combinatorially a PERFECT closed manifold while enclosing nothing: the engine's plane
     * primitive is two coincident, oppositely wound sheets whose rotations mirror their
     * triangulations, so every welded edge is shared by exactly two faces, each traversed once
     * per direction -- a zero-volume "pillow" that no edge counting can distinguish from a real
     * solid. Its cancelling pseudonormals make every voxel's sign floating-point noise, which
     * bakes as phantom brick-quantised walls. The enclosed-volume test is what catches it: the
     * volume terms of coincident opposite sheets cancel to rounding error, while any genuine
     * solid -- however thin its slab -- keeps a volume orders of magnitude above that.
     */
    auto is_closed() const -> bool
    {
        return boundary_edge_count_ == 0 && encloses_volume_;
    }

    auto get_boundary_edge_count() const -> uint32_t
    {
        return boundary_edge_count_;
    }

private:
    struct node
    {
        math::bbox bounds{};
        ///< Leaves: index of the first entry in @ref order_. Internal nodes: index of the
        ///< RIGHT child. Nodes are allocated pre-order, so an internal node's left child is
        ///< always the very next slot, but its right child sits after the entire left
        ///< subtree and must therefore be stored explicitly.
        uint32_t first = 0;
        ///< Zero for internal nodes, triangle count for leaves.
        uint32_t count = 0;
    };

    auto build_recursive(uint32_t begin, uint32_t end, const std::vector<math::vec3>& centroids) -> uint32_t;
    void query_recursive(uint32_t node_index, const math::vec3& p, float& best_dist_sq, uint32_t& best_tri,
                         closest_point_result& best_hit) const;

    std::vector<math::vec3> positions_;
    std::vector<uint32_t> indices_;
    ///< Maps each vertex to a representative shared by all vertices at the same position.
    ///< Adjacency must be computed on welded topology: exporters split vertices at UV and
    ///< normal seams, so index-keyed adjacency reports a closed mesh as full of boundary
    ///< edges and gives every seam vertex only half of its incident faces.
    std::vector<uint32_t> welded_;
    ///< Edges adjacent to exactly one triangle, counted on the welded topology.
    uint32_t boundary_edge_count_ = 0;
    ///< False when the closed surface encloses no volume beyond rounding error -- a doubled
    ///< sheet, whose sign is meaningless. See @ref is_closed.
    bool encloses_volume_ = false;
    ///< Per triangle geometric normal, normalized. Degenerate triangles are dropped before
    ///< this is built, so every entry is finite.
    std::vector<math::vec3> face_normals_;
    ///< Per vertex angle-weighted normal sum.
    std::vector<math::vec3> vertex_pseudonormals_;
    ///< Per triangle, per edge (01, 12, 20) sum of the adjacent face normals.
    std::vector<math::vec3> edge_pseudonormals_;
    ///< Triangle indices in BVH leaf order.
    std::vector<uint32_t> order_;
    std::vector<node> nodes_;
};

auto sdf_triangle_accelerator::build(const sdf_source_geometry& geometry) -> bool
{
    APP_SCOPE_PERF("GI/Bake/Build SDF Accelerator");
    positions_ = geometry.positions;
    const uint32_t source_triangles = geometry.get_triangle_count();
    if(source_triangles == 0 || positions_.empty())
    {
        return false;
    }
    // Drop degenerate triangles up front. A zero-area triangle has no defined normal, and
    // letting one into the pseudonormal accumulation poisons the sign of every voxel whose
    // closest feature touches it.
    indices_.clear();
    indices_.reserve(geometry.indices.size());
    face_normals_.clear();
    face_normals_.reserve(source_triangles);
    for(uint32_t t = 0; t < source_triangles; ++t)
    {
        const uint32_t i0 = geometry.indices[t * 3 + 0];
        const uint32_t i1 = geometry.indices[t * 3 + 1];
        const uint32_t i2 = geometry.indices[t * 3 + 2];
        if(i0 >= positions_.size() || i1 >= positions_.size() || i2 >= positions_.size())
        {
            continue;
        }
        const math::vec3 normal = math::cross(positions_[i1] - positions_[i0], positions_[i2] - positions_[i0]);
        const float area2 = math::length(normal);
        if(!(area2 > 1e-12f))
        {
            continue;
        }
        indices_.push_back(i0);
        indices_.push_back(i1);
        indices_.push_back(i2);
        face_normals_.push_back(normal / area2);
    }
    const uint32_t triangle_count = uint32_t(face_normals_.size());
    if(triangle_count == 0)
    {
        return false;
    }
    // Orientation safeguard. The sign of the field is derived from triangle winding, so a
    // mesh authored with inverted winding bakes an inside-out field: solid becomes empty and
    // the object silently stops occluding, which reads as light leaking through it. Flipped
    // winding is common enough in real content (mirrored props, negative-scale exports) that
    // detecting it is worth the one extra pass.
    //
    // The divergence theorem gives the enclosed signed volume as a sum over triangles; it is
    // negative exactly when the surface is wound inward. For open meshes the sum is
    // meaningless, but those should be baked two-sided anyway, where the sign is unused.
    double signed_volume = 0.0;
    for(uint32_t t = 0; t < triangle_count; ++t)
    {
        const math::vec3& a = positions_[indices_[t * 3 + 0]];
        const math::vec3& b = positions_[indices_[t * 3 + 1]];
        const math::vec3& c = positions_[indices_[t * 3 + 2]];
        signed_volume += double(math::dot(a, math::cross(b, c)));
    }
    if(signed_volume < 0.0)
    {
        for(auto& normal : face_normals_)
        {
            normal = -normal;
        }
    }
    // Does the surface enclose volume AT ALL? The doubled-sheet case cancels this sum to pure
    // rounding error while a genuine solid, however thin, keeps it orders of magnitude above --
    // so the threshold is a tight relative one against the bounds' largest extent cubed, not a
    // judgement call. `signed_volume` is six times the enclosed volume (the /6 of the divergence
    // formula is omitted throughout since only ratios matter here), which only widens the margin.
    // Meaningless for open surfaces, but those are already routed to the unsigned path by the
    // boundary edge count, so this flag is consulted only when the surface is closed.
    const math::vec3 volume_extent = geometry.bounds.get_dimensions();
    const double volume_span =
        double(math::max(volume_extent.x, math::max(volume_extent.y, volume_extent.z)));
    encloses_volume_ = std::abs(signed_volume) > volume_span * volume_span * volume_span * 1e-9;
    // Weld vertices by position before computing any adjacency. Exporters split vertices at
    // UV and normal seams, so two triangles meeting along a seam reference different indices
    // for the same point. Index-keyed adjacency would then see every seam edge as a boundary
    // edge (reporting a closed mesh as open) and would give each seam vertex only the faces
    // from one side (producing a pseudonormal that points the wrong way, and with it wrongly
    // signed voxels along every seam).
    const math::vec3 extent = geometry.bounds.get_dimensions();
    const float largest_extent = math::max(extent.x, math::max(extent.y, extent.z));
    const float weld_epsilon = math::max(largest_extent * 1e-5f, 1e-7f);
    const float inv_weld_epsilon = 1.0f / weld_epsilon;
    welded_.resize(positions_.size());
    {
        std::unordered_map<quantized_position, uint32_t, quantized_position_hash> position_to_vertex;
        position_to_vertex.reserve(positions_.size());
        for(uint32_t v = 0; v < positions_.size(); ++v)
        {
            const math::vec3 cell = math::round(positions_[v] * inv_weld_epsilon);
            const quantized_position key{int64_t(cell.x), int64_t(cell.y), int64_t(cell.z)};
            const auto inserted = position_to_vertex.emplace(key, v);
            welded_[v] = inserted.first->second;
        }
    }
    // Vertex pseudonormals: angle-weighted sum of incident face normals, accumulated onto the
    // welded representative so both sides of a seam contribute.
    //
    // Triangles whose corners WELD together are excluded from every topology pass below. Such a
    // triangle is thinner than the weld epsilon, so it survived the area test while its topology
    // has collapsed -- a UV sphere is the canonical producer: float sin(pi) is ~1e-7 rather than
    // zero, so the pole ring is a fan of slivers whose two pole corners weld into one vertex.
    // Counting a collapsed triangle's edges double-counts the surviving edge (the pole edges of
    // that sphere read four faces each, which would flunk the manifold test on a perfectly good
    // closed mesh), and its corner angles at the welded pair are the angle between a real edge
    // and pure rounding noise -- a large random weight on an unstable sliver normal, fed straight
    // into the pole vertex's pseudonormal. The triangle itself stays in the BVH: as geometry it
    // is a legitimate (if negligible) part of the surface; it is only as TOPOLOGY that it lies.
    const auto is_welded_degenerate = [&](uint32_t t) -> bool
    {
        const uint32_t w0 = welded_[indices_[t * 3 + 0]];
        const uint32_t w1 = welded_[indices_[t * 3 + 1]];
        const uint32_t w2 = welded_[indices_[t * 3 + 2]];
        return w0 == w1 || w1 == w2 || w2 == w0;
    };
    vertex_pseudonormals_.assign(positions_.size(), math::vec3(0.0f));
    for(uint32_t t = 0; t < triangle_count; ++t)
    {
        if(is_welded_degenerate(t))
        {
            continue;
        }
        const uint32_t i0 = indices_[t * 3 + 0];
        const uint32_t i1 = indices_[t * 3 + 1];
        const uint32_t i2 = indices_[t * 3 + 2];
        const math::vec3& n = face_normals_[t];
        vertex_pseudonormals_[welded_[i0]] +=
            n * compute_corner_angle(positions_[i0], positions_[i1], positions_[i2]);
        vertex_pseudonormals_[welded_[i1]] +=
            n * compute_corner_angle(positions_[i1], positions_[i2], positions_[i0]);
        vertex_pseudonormals_[welded_[i2]] +=
            n * compute_corner_angle(positions_[i2], positions_[i0], positions_[i1]);
    }
    // Edge pseudonormals: sum of the face normals sharing each edge, plus the count of faces
    // per edge, which is what tells us whether the surface is closed.
    struct edge_adjacency
    {
        math::vec3 normal_sum{0.0f};
        uint32_t face_count = 0;
    };
    std::unordered_map<uint64_t, edge_adjacency> edges;
    edges.reserve(triangle_count * 3);
    for(uint32_t t = 0; t < triangle_count; ++t)
    {
        if(is_welded_degenerate(t))
        {
            continue;
        }
        const uint32_t i0 = welded_[indices_[t * 3 + 0]];
        const uint32_t i1 = welded_[indices_[t * 3 + 1]];
        const uint32_t i2 = welded_[indices_[t * 3 + 2]];
        const math::vec3& n = face_normals_[t];
        for(const uint64_t key : {make_edge_key(i0, i1), make_edge_key(i1, i2), make_edge_key(i2, i0)})
        {
            auto& adjacency = edges[key];
            adjacency.normal_sum += n;
            ++adjacency.face_count;
        }
    }
    boundary_edge_count_ = 0;
    for(const auto& entry : edges)
    {
        // EXACTLY two, not at least two. A closed 2-manifold has every edge shared by two faces;
        // anything else makes the pseudonormal sign test undefined, and the failure mode of the
        // ">= 2 counts as closed" reading is not hypothetical: the engine's own plane primitive
        // (mesh::create_plane) is TWO coincident, oppositely wound sheets merged, so after welding
        // every edge carries an even face count and no boundary edge exists -- yet the coincident
        // opposite faces cancel every vertex and edge pseudonormal to numerical zero, and the sign
        // of each voxel degenerates to floating-point noise. That bakes as random inside/outside
        // regions quantised at brick granularity, which renders as phantom walls and staircases
        // around a perfectly flat mesh. Counting non-manifold edges as open sends such geometry
        // down the unsigned-shell path, where the sign is never consulted; a genuinely closed but
        // non-manifold union loses only its interior solidity, which is the safe direction.
        if(entry.second.face_count != 2)
        {
            ++boundary_edge_count_;
        }
    }
    // Flatten per triangle edge so the hot query path is an array index, not a hash lookup.
    edge_pseudonormals_.resize(size_t(triangle_count) * 3);
    for(uint32_t t = 0; t < triangle_count; ++t)
    {
        const uint32_t i0 = welded_[indices_[t * 3 + 0]];
        const uint32_t i1 = welded_[indices_[t * 3 + 1]];
        const uint32_t i2 = welded_[indices_[t * 3 + 2]];
        edge_pseudonormals_[t * 3 + 0] = edges[make_edge_key(i0, i1)].normal_sum;
        edge_pseudonormals_[t * 3 + 1] = edges[make_edge_key(i1, i2)].normal_sum;
        edge_pseudonormals_[t * 3 + 2] = edges[make_edge_key(i2, i0)].normal_sum;
    }
    // BVH over triangle centroids, median split on the longest centroid-bounds axis.
    std::vector<math::vec3> centroids(triangle_count);
    order_.resize(triangle_count);
    for(uint32_t t = 0; t < triangle_count; ++t)
    {
        centroids[t] = (positions_[indices_[t * 3 + 0]] + positions_[indices_[t * 3 + 1]] +
                        positions_[indices_[t * 3 + 2]]) /
                       3.0f;
        order_[t] = t;
    }
    nodes_.clear();
    nodes_.reserve(size_t(triangle_count) * 2);
    build_recursive(0, triangle_count, centroids);
    return true;
}

auto sdf_triangle_accelerator::build_recursive(uint32_t begin,
                                               uint32_t end,
                                               const std::vector<math::vec3>& centroids) -> uint32_t
{
    const uint32_t node_index = uint32_t(nodes_.size());
    nodes_.emplace_back();
    math::bbox bounds;
    bounds.reset();
    for(uint32_t i = begin; i < end; ++i)
    {
        const uint32_t t = order_[i];
        bounds.add_point(positions_[indices_[t * 3 + 0]]);
        bounds.add_point(positions_[indices_[t * 3 + 1]]);
        bounds.add_point(positions_[indices_[t * 3 + 2]]);
    }
    const uint32_t count = end - begin;
    if(count <= bvh_max_leaf_triangles)
    {
        nodes_[node_index].bounds = bounds;
        nodes_[node_index].first = begin;
        nodes_[node_index].count = count;
        return node_index;
    }
    math::bbox centroid_bounds;
    centroid_bounds.reset();
    for(uint32_t i = begin; i < end; ++i)
    {
        centroid_bounds.add_point(centroids[order_[i]]);
    }
    const math::vec3 extents = centroid_bounds.get_dimensions();
    int axis = 0;
    if(extents.y > extents.x)
    {
        axis = 1;
    }
    if(extents.z > extents[axis])
    {
        axis = 2;
    }
    const uint32_t mid = begin + count / 2;
    std::nth_element(order_.begin() + begin,
                     order_.begin() + mid,
                     order_.begin() + end,
                     [&](uint32_t lhs, uint32_t rhs) { return centroids[lhs][axis] < centroids[rhs][axis]; });
    build_recursive(begin, mid, centroids);
    const uint32_t right = build_recursive(mid, end, centroids);
    nodes_[node_index].bounds = bounds;
    nodes_[node_index].first = right;
    nodes_[node_index].count = 0;
    return node_index;
}

void sdf_triangle_accelerator::query_recursive(uint32_t node_index,
                                               const math::vec3& p,
                                               float& best_dist_sq,
                                               uint32_t& best_tri,
                                               closest_point_result& best_hit) const
{
    const node& n = nodes_[node_index];
    if(n.count > 0)
    {
        for(uint32_t i = 0; i < n.count; ++i)
        {
            const uint32_t t = order_[n.first + i];
            const closest_point_result hit = closest_point_on_triangle(p,
                                                                       positions_[indices_[t * 3 + 0]],
                                                                       positions_[indices_[t * 3 + 1]],
                                                                       positions_[indices_[t * 3 + 2]]);
            const math::vec3 delta = p - hit.point;
            const float dist_sq = math::dot(delta, delta);
            if(dist_sq < best_dist_sq)
            {
                best_dist_sq = dist_sq;
                best_tri = t;
                best_hit = hit;
            }
        }
        return;
    }
    // Visit the nearer child first so the farther one is more likely to be pruned.
    const uint32_t left = node_index + 1;
    const uint32_t right = n.first;
    const float left_dist = distance_squared_to_bounds(nodes_[left].bounds, p);
    const float right_dist = distance_squared_to_bounds(nodes_[right].bounds, p);
    const uint32_t first_child = left_dist <= right_dist ? left : right;
    const uint32_t second_child = left_dist <= right_dist ? right : left;
    const float second_dist = left_dist <= right_dist ? right_dist : left_dist;
    query_recursive(first_child, p, best_dist_sq, best_tri, best_hit);
    if(second_dist < best_dist_sq)
    {
        query_recursive(second_child, p, best_dist_sq, best_tri, best_hit);
    }
}

auto sdf_triangle_accelerator::signed_distance(const math::vec3& p, bool unsigned_only) const -> float
{
    float best_dist_sq = std::numeric_limits<float>::max();
    uint32_t best_tri = 0;
    closest_point_result best_hit{};
    query_recursive(0, p, best_dist_sq, best_tri, best_hit);
    const float distance = std::sqrt(math::max(best_dist_sq, 0.0f));
    if(unsigned_only)
    {
        return distance;
    }
    // Pseudonormal of the feature the closest point landed on. Using the face normal for an
    // edge or vertex hit is the classic source of wrongly signed voxels on convex corners.
    math::vec3 pseudonormal;
    switch(best_hit.feature)
    {
        case triangle_feature::vertex0:
            pseudonormal = vertex_pseudonormals_[welded_[indices_[best_tri * 3 + 0]]];
            break;
        case triangle_feature::vertex1:
            pseudonormal = vertex_pseudonormals_[welded_[indices_[best_tri * 3 + 1]]];
            break;
        case triangle_feature::vertex2:
            pseudonormal = vertex_pseudonormals_[welded_[indices_[best_tri * 3 + 2]]];
            break;
        case triangle_feature::edge01:
            pseudonormal = edge_pseudonormals_[best_tri * 3 + 0];
            break;
        case triangle_feature::edge12:
            pseudonormal = edge_pseudonormals_[best_tri * 3 + 1];
            break;
        case triangle_feature::edge20:
            pseudonormal = edge_pseudonormals_[best_tri * 3 + 2];
            break;
        default:
            pseudonormal = face_normals_[best_tri];
            break;
    }
    const float side = math::dot(p - best_hit.point, pseudonormal);
    return side < 0.0f ? -distance : distance;
}

/// Exact-position key for welding. Seams duplicate a position for a different normal or UV, so
/// connectivity has to be judged on the position rather than on the vertex index -- otherwise one
/// solid part reads as several pieces and a sparse submesh looks the same as a normal one.
struct welded_position_key
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    auto operator==(const welded_position_key& other) const -> bool
    {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct welded_position_hash
{
    auto operator()(const welded_position_key& key) const -> size_t
    {
        // Hash the bit patterns, so the key matches exactly what operator== compares.
        size_t seed = std::hash<float>{}(key.x);
        seed ^= std::hash<float>{}(key.y) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        seed ^= std::hash<float>{}(key.z) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        return seed;
    }
};

/// Union-find with path halving. Rank is not tracked: the trees here are built from mesh adjacency
/// and stay shallow, and halving alone keeps this effectively linear.
auto find_root(std::vector<uint32_t>& parent, uint32_t node) -> uint32_t
{
    while(parent[node] != node)
    {
        parent[node] = parent[parent[node]];
        node = parent[node];
    }
    return node;
}

} // namespace

auto bake_mesh_sdf(const sdf_source_geometry& geometry,
                   const mesh_sdf_bake_settings& settings,
                   mesh_sdf& out,
                   sdf_bake_threading threading) -> bool
{
    APP_SCOPE_PERF("GI/Bake/Mesh SDF");
    // Both voxel passes below run under this policy. par_if(false) makes poolstl run the range
    // inline on the calling thread, which is what keeps a caller that is already on the pool
    // from deadlocking against itself -- see sdf_bake_threading.
    const auto policy = poolstl::par.par_if(threading == sdf_bake_threading::parallel);
    out = {};
    if(!geometry.is_valid() || !geometry.bounds.is_populated() || geometry.bounds.is_degenerate())
    {
        return false;
    }
    // Refuse geometry this representation cannot carry, rather than producing a field that lies.
    //
    // The voxel size is the bounds divided by a fixed count, so a soup of small parts scattered far
    // apart gets a voxel sized to the GAPS between them. Past a point each part is a fraction of one
    // voxel and no field can represent it -- what comes out instead is a blob the size of the spread,
    // which traces as solid geometry that is not there and can black out a whole neighbourhood.
    //
    // Not detectable by any test on the triangles themselves: the parts are ordinary geometry, and
    // this same submesh baked alone would be fine. It is the relationship between the geometry and
    // the space it occupies that is unrepresentable, so the check has to be here, where both the
    // bounds and the geometry are in hand.
    if(settings.max_component_spread > 0.0f)
    {
        const auto summary = summarize_connected_components(geometry);
        if(summary.get_sparsity() > settings.max_component_spread)
        {
            return false;
        }
    }
    sdf_triangle_accelerator accelerator;
    if(!accelerator.build(geometry))
    {
        return false;
    }
    // An open surface has no meaningful inside, so bake it unsigned whether or not the asset
    // asked for it. Keeping the signed path on an open mesh does not merely lose the interior:
    // the pseudonormal test reports "inside" for regions that are outside, those bricks store a
    // NEGATIVE distance, and the tracer reads any negative sample as a surface hit -- so the
    // field's whole bounding box renders solid. Scanned props are frequently open, so this is
    // the common case, not an edge case.
    //
    // Erring toward unsigned is deliberate. Treating a nearly-closed mesh as a shell only
    // loses interior solidity (the shell still occludes); treating an open mesh as closed
    // produces phantom geometry.
    const bool use_unsigned = settings.two_sided || !accelerator.is_closed();
    const math::vec3 surface_extent = geometry.bounds.get_dimensions();
    const float longest_axis = math::max(surface_extent.x, math::max(surface_extent.y, surface_extent.z));
    const uint32_t target_resolution = math::max(settings.resolution, 1u);
    float voxel_size = math::clamp(longest_axis / float(target_resolution),
                                   settings.min_voxel_size,
                                   settings.max_voxel_size);
    // Thin-geometry escalation: the shell floor is one voxel, so a voxel derived from LARGE
    // bounds wraps thin geometry in a shell many times fatter than the author intended - at
    // material-merged scales (a 3 cm parapet rope spanning 30 m bakes metre voxels) the result
    // is not a coarse field but a PHANTOM: a metre-thick blob that occludes rays and steals GI
    // attribution over a whole neighbourhood (measured: Sponza's gallery floor attributed
    // rope-red). REQUEST a voxel the authored thickness can justify and let the existing caps
    // arbitrate: the grow loop below reclaims whatever the per-axis and total budgets cannot
    // afford, so volume-filling meshes end up exactly where they always did, while long-thin
    // bounds - the pathological case - fit easily and bake representable shells. This is also
    // what makes Max Total Voxels an effective lever for thin shells: before it, Resolution
    // pinned the voxel and a raised budget could not reach anything finer.
    if(use_unsigned)
    {
        const float shell_target = k_max_shell_floor_ratio *
                                   math::max(settings.two_sided_thickness, settings.min_voxel_size);
        if(voxel_size > shell_target)
        {
            voxel_size = math::max(shell_target, settings.min_voxel_size);
        }
    }
    // Pad by the encode range so the field carries useful distances just outside the surface,
    // which is where sphere tracing spends most of its steps.
    //
    // A shell also expands the effective surface outward by its thickness, so that has to be
    // included. Without it a shell thicker than the encode range reaches past the bounds, the
    // samples where a ray enters read negative, and the tracer draws the bounding box solid.
    const auto compute_shell_thickness = [&](float voxels) -> float
    {
        return use_unsigned ? math::max(settings.two_sided_thickness, voxels) : 0.0f;
    };
    const auto compute_padding = [&](float voxels) -> float
    {
        return mesh_sdf::encode_range * voxels + compute_shell_thickness(voxels);
    };
    // Grid sizing. The voxel size only ever GROWS from here: every cap is satisfied by making
    // voxels coarser, never by cropping the grid, because a cropped field would let rays pass
    // straight through the uncovered part of the geometry.
    const auto compute_brick_dim = [&](float voxel) -> math::uvec3
    {
        const math::vec3 extent = surface_extent + math::vec3(2.0f * compute_padding(voxel));
        const auto axis_bricks = [&](float axis_extent) -> uint32_t
        {
            const uint32_t voxels = uint32_t(std::ceil(axis_extent / voxel));
            return math::max(1u, (voxels + mesh_sdf::brick_size - 1u) / mesh_sdf::brick_size);
        };
        return math::uvec3(axis_bricks(extent.x), axis_bricks(extent.y), axis_bricks(extent.z));
    };
    const auto count_grid_voxels = [](const math::uvec3& bricks) -> uint64_t
    {
        constexpr uint64_t voxels_per_brick = uint64_t(mesh_sdf::brick_size) * mesh_sdf::brick_size *
                                              mesh_sdf::brick_size;
        return uint64_t(bricks.x) * bricks.y * bricks.z * voxels_per_brick;
    };
    const uint32_t max_bricks = math::max(1u, settings.max_resolution / mesh_sdf::brick_size);
    const uint64_t max_total_voxels = math::max<uint64_t>(settings.max_total_voxels, 1ull);
    math::uvec3 brick_dim = compute_brick_dim(voxel_size);
    // Two caps, both enforced by growing the voxel.
    //
    // The per-axis one alone is not a budget: it permits max_resolution^3 voxels PER FIELD, and
    // voxel count is cubic, so one submesh may legitimately ask for 16M voxels while the atlas
    // holds a few hundred thousand bricks for the entire scene. On a model split into thousands
    // of submeshes that overruns the atlas by an order of magnitude -- and since bake time is
    // proportional to voxel count, it is simultaneously the reason the bake takes minutes. The
    // total cap is what makes the cost of a field bounded rather than merely shaped.
    //
    // Iterated because each correction changes the padding, which changes the grid. Growth is
    // monotone, so this converges in a couple of rounds; the bound is a guard, not a limit.
    constexpr int max_sizing_iterations = 8;
    for(int iteration = 0; iteration < max_sizing_iterations; ++iteration)
    {
        float scale = 1.0f;
        const uint32_t largest = math::max(brick_dim.x, math::max(brick_dim.y, brick_dim.z));
        if(largest > max_bricks)
        {
            scale = float(largest) / float(max_bricks);
        }
        const uint64_t grid_voxels = count_grid_voxels(brick_dim);
        if(grid_voxels > max_total_voxels)
        {
            // Cube root: the budget is a volume and the voxel size scales all three axes.
            const float volume_scale = float(std::cbrt(double(grid_voxels) / double(max_total_voxels)));
            scale = math::max(scale, volume_scale);
        }
        if(scale <= 1.0f)
        {
            break;
        }
        voxel_size *= scale;
        brick_dim = compute_brick_dim(voxel_size);
    }
    // Belt and braces: the loop above should already satisfy the per-axis cap, and clamping here
    // can only crop, so it must never be the thing that actually enforces it.
    brick_dim = math::uvec3(math::min(brick_dim.x, max_bricks),
                            math::min(brick_dim.y, max_bricks),
                            math::min(brick_dim.z, max_bricks));
    const math::vec3 padded_min = geometry.bounds.min - math::vec3(compute_padding(voxel_size));
    const math::uvec3 grid_dim(brick_dim.x * mesh_sdf::brick_size,
                               brick_dim.y * mesh_sdf::brick_size,
                               brick_dim.z * mesh_sdf::brick_size);
    out.voxel_size = voxel_size;
    out.grid_dim = grid_dim;
    out.brick_dim = brick_dim;
    out.is_two_sided = use_unsigned;
    // A shell needs enough thickness to actually occlude. When the fallback triggers on a mesh
    // the artist did not mark two-sided, the authored thickness may be zero, so floor it at one
    // voxel -- a thinner shell than the field can represent would not occlude at all.
    out.two_sided_thickness = compute_shell_thickness(voxel_size);
    out.bounds = math::bbox(padded_min,
                            padded_min + math::vec3(float(grid_dim.x), float(grid_dim.y), float(grid_dim.z)) *
                                             voxel_size);
    const uint32_t brick_count = brick_dim.x * brick_dim.y * brick_dim.z;
    out.indirection.assign(brick_count, 0u);
    // Pass 1: classify every brick from a single query at its centre. A brick needs voxel
    // storage when the surface can reach any voxel it stores, including its filter border.
    const float brick_world_size = float(mesh_sdf::brick_size) * voxel_size;
    const float brick_half_diagonal = 0.5f * brick_world_size * std::sqrt(3.0f);
    // Measured in the space the VOXELS are stored in. For a shell that is the unsigned distance
    // minus the thickness, so the thickness does not appear here -- it is folded into the centre
    // distance below instead, which is the same single transformation applied to the same quantity
    // rather than a second application of it.
    const float shell_reach = (mesh_sdf::encode_range + float(mesh_sdf::brick_border)) * voxel_size;
    const float surface_brick_radius = brick_half_diagonal + shell_reach;
    std::vector<float> brick_center_distance(brick_count, 0.0f);
    std::vector<uint32_t> brick_scan(brick_count);
    std::iota(brick_scan.begin(), brick_scan.end(), 0u);
    std::for_each(policy,
                  brick_scan.begin(),
                  brick_scan.end(),
                  [&](uint32_t brick_index)
                  {
                      const uint32_t bx = brick_index % brick_dim.x;
                      const uint32_t by = (brick_index / brick_dim.x) % brick_dim.y;
                      const uint32_t bz = brick_index / (brick_dim.x * brick_dim.y);
                      const math::vec3 center =
                          padded_min + (math::vec3(float(bx), float(by), float(bz)) + math::vec3(0.5f)) *
                                           brick_world_size;
                      brick_center_distance[brick_index] = accelerator.signed_distance(center, use_unsigned);
                  });
    // Pass 2: assign storage slots to surface bricks, and give every other brick a
    // conservative distance valid for all points inside it.
    std::vector<uint32_t> surface_bricks;
    surface_bricks.reserve(brick_count / 4);
    for(uint32_t brick_index = 0; brick_index < brick_count; ++brick_index)
    {
        // Into the field's own space before anything is decided from it. A shell stores
        // `unsigned - thickness`, so classifying and measuring from the raw unsigned distance
        // compares against a quantity the field does not hold: the empty-brick distance then
        // OVER-estimates by exactly the thickness, and an over-estimate is the one direction a
        // conservative field may never err in -- a trace steps that much too far and passes through
        // the shell. Thin open geometry silently stopping occluding is the visible result.
        //
        // It also stops the interior of a thick shell being stored as surface bricks. Those voxels
        // are uniformly deep inside the slab; as empty-inside entries they cost no atlas storage,
        // which matters because a scene of shells is exactly what overruns the atlas.
        const float raw_center = brick_center_distance[brick_index];
        const float signed_center = use_unsigned ? raw_center - out.two_sided_thickness : raw_center;
        if(std::abs(signed_center) <= surface_brick_radius)
        {
            out.indirection[brick_index] = make_sdf_surface_entry(uint32_t(surface_bricks.size()));
            surface_bricks.push_back(brick_index);
            continue;
        }
        // Every point of the brick is at least this far from the surface, so a ray may skip
        // the whole distance without sampling. Under-estimating keeps tracing conservative.
        const float conservative = math::max(std::abs(signed_center) - brick_half_diagonal, 0.0f);
        const uint32_t conservative_voxels = uint32_t(conservative / voxel_size);
        out.indirection[brick_index] = make_sdf_empty_entry(conservative_voxels, signed_center < 0.0f);
    }
    if(surface_bricks.empty())
    {
        // A mesh whose surface never comes within reach of a brick centre is degenerate for
        // GI purposes (typically zero-thickness or sub-voxel). Report failure so the caller
        // stores no field rather than an all-empty one that silently occludes nothing.
        return false;
    }
    // Pass 3: fill the surface bricks, including their filter borders.
    out.brick_voxels.assign(size_t(surface_bricks.size()) * mesh_sdf::brick_voxel_count, 0u);
    std::for_each(policy,
                  surface_bricks.begin(),
                  surface_bricks.end(),
                  [&](uint32_t brick_index)
                  {
                      const uint32_t slot = out.indirection[brick_index];
                      const uint32_t bx = brick_index % brick_dim.x;
                      const uint32_t by = (brick_index / brick_dim.x) % brick_dim.y;
                      const uint32_t bz = brick_index / (brick_dim.x * brick_dim.y);
                      const math::vec3 brick_origin =
                          padded_min + math::vec3(float(bx), float(by), float(bz)) * brick_world_size;
                      uint8_t* dst = out.brick_voxels.data() + size_t(slot) * mesh_sdf::brick_voxel_count;
                      for(uint32_t lz = 0; lz < mesh_sdf::brick_stride; ++lz)
                      {
                          for(uint32_t ly = 0; ly < mesh_sdf::brick_stride; ++ly)
                          {
                              for(uint32_t lx = 0; lx < mesh_sdf::brick_stride; ++lx)
                              {
                                  // Local 0 is the border voxel, so interior voxel i sits at
                                  // i + brick_border and samples at the voxel centre.
                                  const math::vec3 voxel_offset(float(lx) - float(mesh_sdf::brick_border) + 0.5f,
                                                                float(ly) - float(mesh_sdf::brick_border) + 0.5f,
                                                                float(lz) - float(mesh_sdf::brick_border) + 0.5f);
                                  const math::vec3 p = brick_origin + voxel_offset * voxel_size;
                                  float distance = accelerator.signed_distance(p, use_unsigned);
                                  if(use_unsigned)
                                  {
                                      // Unsigned shell: the surface is treated as a slab of the
                                      // authored thickness so a single-quad leaf still occludes.
                                      distance -= out.two_sided_thickness;
                                  }
                                  const uint32_t local =
                                      lx + ly * mesh_sdf::brick_stride +
                                      lz * mesh_sdf::brick_stride * mesh_sdf::brick_stride;
                                  dst[local] = encode_sdf_distance(distance / voxel_size);
                              }
                          }
                      }
                  });
    return true;
}

auto sample_mesh_sdf(const mesh_sdf& sdf, const math::vec3& local_position) -> float
{
    // Deliberately NOT is_valid(): that walks every indirection entry, which is linear in the
    // brick count and was being paid on every one of the millions of samples a clipmap
    // composition makes. The range check it existed to provide is done below, on the single
    // entry actually dereferenced.
    if(!sdf.is_sampleable())
    {
        return std::numeric_limits<float>::max();
    }
    // Grid coordinate in voxels, measured from the field origin at voxel centres.
    const math::vec3 grid_position = (local_position - sdf.bounds.min) / sdf.voxel_size;
    const math::vec3 grid_max(float(sdf.grid_dim.x), float(sdf.grid_dim.y), float(sdf.grid_dim.z));
    if(math::any(math::lessThan(grid_position, math::vec3(0.0f))) ||
       math::any(math::greaterThan(grid_position, grid_max)))
    {
        // Outside the field: distance to the bounds plus the guaranteed padding between the
        // bounds and the surface. See mesh_sdf::get_bounds_padding for why the padding term
        // is required rather than merely nice -- without it this is zero exactly on the
        // boundary, where every entering ray starts.
        return std::sqrt(distance_squared_to_bounds(sdf.bounds, local_position)) + sdf.get_bounds_padding();
    }
    // Resolve the brick ONCE from the sample position, then filter entirely inside that
    // brick's bordered storage. Resolving per trilinear tap instead would defeat the whole
    // point of the border: taps would cross into neighbouring bricks, where an empty
    // neighbour contributes its conservative brick-wide distance rather than a real voxel,
    // producing a large step discontinuity at every brick seam. The GPU tracer does exactly
    // this too -- one indirection lookup, then one filtered fetch from the atlas tile.
    //
    // Scalar integer math throughout: glm's SIMD specialisation has no integer vector
    // division (there is no _mm_div_epi32), so ivec3 / int fails to compile.
    const int brick_size = int(mesh_sdf::brick_size);
    const int brick_x = math::clamp(int(grid_position.x) / brick_size, 0, int(sdf.brick_dim.x) - 1);
    const int brick_y = math::clamp(int(grid_position.y) / brick_size, 0, int(sdf.brick_dim.y) - 1);
    const int brick_z = math::clamp(int(grid_position.z) / brick_size, 0, int(sdf.brick_dim.z) - 1);
    const uint32_t brick_index = uint32_t(brick_x) + uint32_t(brick_y) * sdf.brick_dim.x +
                                 uint32_t(brick_z) * sdf.brick_dim.x * sdf.brick_dim.y;
    const uint32_t entry = sdf.indirection[brick_index];
    if(is_sdf_empty_entry(entry))
    {
        // Constant over the whole brick by construction; there is nothing to filter.
        const float voxels = float(entry & mesh_sdf::indirection_distance_mask);
        const float distance = voxels * sdf.voxel_size;
        return (entry & mesh_sdf::indirection_inside_flag) != 0u ? -distance : distance;
    }
    // Position within the brick, in voxels, measured from the brick's first interior voxel
    // centre. The border makes every tap of the [-1, brick_size] range addressable.
    const math::vec3 brick_origin_voxels(float(brick_x * brick_size),
                                         float(brick_y * brick_size),
                                         float(brick_z * brick_size));
    const math::vec3 sample_position = grid_position - brick_origin_voxels - math::vec3(0.5f);
    const math::ivec3 base = math::ivec3(math::floor(sample_position));
    const math::vec3 frac = sample_position - math::vec3(base);
    // The one range check the per-sample path needs: a surface entry must index storage that
    // exists. A truncated or partially deserialized field would otherwise read past the end here,
    // which renders as plausible-looking noise rather than as an error.
    const size_t stored_bricks = sdf.brick_voxels.size() / mesh_sdf::brick_voxel_count;
    if(size_t(entry) >= stored_bricks)
    {
        return mesh_sdf::encode_range * sdf.voxel_size;
    }
    const uint8_t* brick = sdf.brick_voxels.data() + size_t(entry) * mesh_sdf::brick_voxel_count;
    const auto fetch = [&](int x, int y, int z) -> float
    {
        const int lx = math::clamp(x + int(mesh_sdf::brick_border), 0, int(mesh_sdf::brick_stride) - 1);
        const int ly = math::clamp(y + int(mesh_sdf::brick_border), 0, int(mesh_sdf::brick_stride) - 1);
        const int lz = math::clamp(z + int(mesh_sdf::brick_border), 0, int(mesh_sdf::brick_stride) - 1);
        const uint32_t offset = uint32_t(lx) + uint32_t(ly) * mesh_sdf::brick_stride +
                                uint32_t(lz) * mesh_sdf::brick_stride * mesh_sdf::brick_stride;
        return decode_sdf_distance(brick[offset]);
    };
    const float c000 = fetch(base.x + 0, base.y + 0, base.z + 0);
    const float c100 = fetch(base.x + 1, base.y + 0, base.z + 0);
    const float c010 = fetch(base.x + 0, base.y + 1, base.z + 0);
    const float c110 = fetch(base.x + 1, base.y + 1, base.z + 0);
    const float c001 = fetch(base.x + 0, base.y + 0, base.z + 1);
    const float c101 = fetch(base.x + 1, base.y + 0, base.z + 1);
    const float c011 = fetch(base.x + 0, base.y + 1, base.z + 1);
    const float c111 = fetch(base.x + 1, base.y + 1, base.z + 1);
    const float c00 = math::mix(c000, c100, frac.x);
    const float c10 = math::mix(c010, c110, frac.x);
    const float c01 = math::mix(c001, c101, frac.x);
    const float c11 = math::mix(c011, c111, frac.x);
    const float c0 = math::mix(c00, c10, frac.y);
    const float c1 = math::mix(c01, c11, frac.y);
    return math::mix(c0, c1, frac.z) * sdf.voxel_size;
}

auto summarize_connected_components(const sdf_source_geometry& geometry) -> sdf_component_summary
{
    APP_SCOPE_PERF("GI/Bake/Summarize Components");
    sdf_component_summary summary;
    if(!geometry.is_valid())
    {
        return summary;
    }
    const math::vec3 bounds_dimensions = geometry.bounds.get_dimensions();
    summary.bounds_extent =
        math::max(bounds_dimensions.x, math::max(bounds_dimensions.y, bounds_dimensions.z));
    // Weld first, so a seam does not read as a break between two pieces.
    const uint32_t vertex_count = uint32_t(geometry.positions.size());
    std::unordered_map<welded_position_key, uint32_t, welded_position_hash> welded;
    welded.reserve(vertex_count);
    std::vector<uint32_t> representative(vertex_count, 0u);
    for(uint32_t v = 0; v < vertex_count; ++v)
    {
        const auto& p = geometry.positions[v];
        const auto inserted = welded.emplace(welded_position_key{p.x, p.y, p.z}, v);
        representative[v] = inserted.first->second;
    }
    std::vector<uint32_t> parent(vertex_count);
    std::iota(parent.begin(), parent.end(), 0u);
    // A vertex joins its welded representative, then every triangle joins its three corners.
    for(uint32_t v = 0; v < vertex_count; ++v)
    {
        const uint32_t a = find_root(parent, v);
        const uint32_t b = find_root(parent, representative[v]);
        if(a != b)
        {
            parent[a] = b;
        }
    }
    const uint32_t triangle_count = geometry.get_triangle_count();
    for(uint32_t t = 0; t < triangle_count; ++t)
    {
        const uint32_t i0 = geometry.indices[t * 3 + 0];
        const uint32_t i1 = geometry.indices[t * 3 + 1];
        const uint32_t i2 = geometry.indices[t * 3 + 2];
        for(const uint32_t other : {i1, i2})
        {
            const uint32_t a = find_root(parent, i0);
            const uint32_t b = find_root(parent, other);
            if(a != b)
            {
                parent[a] = b;
            }
        }
    }
    // One bbox per surviving root. Only vertices a triangle actually references are counted, so
    // stray unreferenced positions cannot invent a component.
    std::unordered_map<uint32_t, math::bbox> components;
    components.reserve(16);
    for(uint32_t t = 0; t < triangle_count; ++t)
    {
        for(uint32_t corner = 0; corner < 3; ++corner)
        {
            const uint32_t index = geometry.indices[t * 3 + corner];
            components[find_root(parent, index)].add_point(geometry.positions[index]);
        }
    }
    summary.component_count = uint32_t(components.size());
    for(const auto& entry : components)
    {
        const math::vec3 dimensions = entry.second.get_dimensions();
        summary.largest_component_extent =
            math::max(summary.largest_component_extent,
                      math::max(dimensions.x, math::max(dimensions.y, dimensions.z)));
    }
    return summary;
}

} // namespace unravel
