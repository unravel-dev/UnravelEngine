#include "scene_backend.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace raytracing::detail
{
namespace
{

/// Triangles one ray tests at once: a leaf is tested a block of this many at a time.
constexpr uint32_t k_block_triangles = 4;
/// Ranges of at most this many triangles are always leaves.
constexpr uint32_t k_min_leaf_triangles = k_block_triangles;
/// Leaves hold at most this many triangles. Up to it, the build stops splitting wherever a split would not pay.
constexpr uint32_t k_max_leaf_triangles = 4 * k_block_triangles;
/// Bins per axis an object split sorts centroids into, and a spatial split cuts a node into (Embree's NUM_OBJECT_BINS
/// and NUM_SPATIAL_BINS).
constexpr uint32_t k_object_bins = 32;
constexpr uint32_t k_spatial_bins = 16;
/// Spatial splits may add this share of the triangles again as extra references (Embree's
/// max_spatial_split_replications, 1.2).
constexpr float k_spatial_split_budget = 0.2f;
/// A spatial split is searched only where the best object split's two boxes overlap by this share of the node's
/// surface and of the root's (Embree's SPATIAL_ASPLIT_OVERLAP_THRESHOLD and SPATIAL_ASPLIT_AREA_THRESHOLD), and taken
/// only where it costs less than this share of the object split (SPATIAL_ASPLIT_SAH_THRESHOLD).
constexpr float k_spatial_overlap_threshold = 0.1f;
constexpr float k_spatial_root_overlap_threshold = 0.000005f;
constexpr float k_spatial_cost_threshold = 0.99f;
/// Costs of a traversal step and of testing one block of triangles, for the surface area heuristic.
constexpr float k_traversal_cost = 1.0f;
constexpr float k_block_cost = 1.0f;
/// A clipped box grows by this many float steps of its coordinates, so the rounding of a clip never leaves part of a
/// triangle outside the boxes of its references.
constexpr float k_clip_padding_steps = 4.0f;
/// Up to this depth the build splits by the surface area heuristic, below it at the centroid median: the median halves
/// every range, so the tree stays shallow whatever the geometry, and with it every traversal stack.
constexpr uint32_t k_max_heuristic_depth = 48;
/// Children of a traversal node.
constexpr uint32_t k_node_width = 4;
/// Rays a packet traces together, one per lane of a math::vec4.
constexpr uint32_t k_packet_rays = 4;
/// Traversal stack entries. A node pushes at most k_node_width - 1 more entries than it pops, and the build bounds the
/// depth at k_max_heuristic_depth plus the 32 levels of median splits a 32-bit reference count can need.
constexpr uint32_t k_stack_size = 256;
/// A box's exit distance is widened by 1 + 2 gamma(3), gamma(n) = n u / (1 - n u) for the float unit roundoff u, so
/// rounding in the slab test never culls a box the ray touches (Ize, Robust BVH Ray Traversal, 2013).
constexpr float k_unit_roundoff = 0.5f * std::numeric_limits<float>::epsilon();
constexpr float k_box_exit_widening = 1.0f + 2.0f * (3.0f * k_unit_roundoff) / (1.0f - 3.0f * k_unit_roundoff);
/// The smallest direction component the slab test divides by; smaller ones are taken as this, with their sign.
constexpr float k_min_direction_component = 1e-20f;

/// An axis-aligned box, local so the build's inner loops stay inline.
struct box
{
    math::vec3 min{std::numeric_limits<float>::max()};
    math::vec3 max{-std::numeric_limits<float>::max()};

    void grow(const math::vec3& point)
    {
        min = math::min(min, point);
        max = math::max(max, point);
    }

    void grow(const box& other)
    {
        min = math::min(min, other.min);
        max = math::max(max, other.max);
    }

    auto is_empty() const -> bool
    {
        return min.x > max.x || min.y > max.y || min.z > max.z;
    }

    auto get_intersection(const box& other) const -> box
    {
        box result;
        result.min = math::max(min, other.min);
        result.max = math::min(max, other.max);
        return result;
    }

    auto get_center() const -> math::vec3
    {
        return (min + max) * 0.5f;
    }

    /// Half the surface area, which is all the heuristic compares; zero for an empty box.
    auto get_surface() const -> float
    {
        if(is_empty())
        {
            return 0.0f;
        }
        const math::vec3 size = max - min;
        return size.x * size.y + size.y * size.z + size.z * size.x;
    }
};

/// A triangle, or the part of one a spatial split left on one side of its plane, as the build sees it.
struct reference
{
    box bounds;
    ///< The triangle, indexed as in the input.
    uint32_t triangle = 0;
};

/// A node of the binary tree the build makes before it is widened.
struct build_node
{
    box bounds;
    uint32_t left = 0;
    uint32_t right = 0;
    ///< Leaves: the first of their references in the build's leaf order.
    uint32_t first = 0;
    ///< Leaves: how many references they hold. Zero for an internal node.
    uint32_t count = 0;
};

/// The blocks a leaf of @p triangles takes.
auto get_block_count(uint32_t triangles) -> uint32_t
{
    return (triangles + k_block_triangles - 1u) / k_block_triangles;
}

/// The bin of @p coordinate among @p bins equal bins from @p minimum, @p scale bins per unit.
auto get_bin(float coordinate, float minimum, float scale, uint32_t bins) -> uint32_t
{
    const float scaled = (coordinate - minimum) * scale;
    return scaled > 0.0f ? std::min(bins - 1u, uint32_t(scaled)) : 0u;
}

/// @p bounds grown by k_clip_padding_steps float steps of its own coordinates.
auto pad_clipped(const box& bounds) -> box
{
    const math::vec3 magnitude = math::max(math::abs(bounds.min), math::abs(bounds.max));
    const math::vec3 padding = magnitude * (k_clip_padding_steps * std::numeric_limits<float>::epsilon());
    box result;
    result.min = bounds.min - padding;
    result.max = bounds.max + padding;
    return result;
}

/**
 * @brief The boxes of the parts of a triangle within @p bounds below and above a plane (Embree's splitPolygon). A
 *        box is empty when the triangle has no part on that side.
 */
void split_triangle(const std::array<math::vec3, 3>& corners,
                    const box& bounds,
                    uint32_t axis,
                    float position,
                    box& below,
                    box& above)
{
    below = box{};
    above = box{};
    for(uint32_t i = 0; i < 3; ++i)
    {
        const math::vec3& from = corners[i];
        const math::vec3& to = corners[(i + 1u) % 3u];
        const float from_coordinate = from[axis];
        const float to_coordinate = to[axis];
        if(from_coordinate <= position)
        {
            below.grow(from);
        }
        if(from_coordinate >= position)
        {
            above.grow(from);
        }
        const bool crosses = (from_coordinate < position && position < to_coordinate) ||
                             (to_coordinate < position && position < from_coordinate);
        if(crosses)
        {
            const float along = (position - from_coordinate) / (to_coordinate - from_coordinate);
            const math::vec3 crossing = from + (to - from) * along;
            below.grow(crossing);
            above.grow(crossing);
        }
    }
    if(!below.is_empty())
    {
        below = pad_clipped(below).get_intersection(bounds);
    }
    if(!above.is_empty())
    {
        above = pad_clipped(above).get_intersection(bounds);
    }
}

/**
 * @brief The binary tree over a triangle soup, built by the surface area heuristic with spatial splits (Stich,
 *        Friedrich, Dietrich, Spatial Splits in Bounding Volume Hierarchies, 2009), as Embree's high quality build.
 *
 * Each node takes the cheaper of the best binned object split and, where that split's two boxes overlap, the best
 * spatial split: a plane that cuts the triangles across it into a reference on each side, each with the box of its
 * part. The extra references come out of a budget the subtrees share in proportion to their size. Costs count
 * triangles in blocks, as a leaf is tested, and a range becomes a leaf wherever splitting it does not pay. Past
 * k_max_heuristic_depth, or where no split separates anything, a range is split at the centroid median.
 */
class binary_tree_builder
{
public:
    binary_tree_builder(const std::vector<math::vec3>& positions, const std::vector<uint32_t>& indices)
        : positions_(positions)
        , indices_(indices)
    {
    }

    /// Builds the tree over @p references; node 0 is the root.
    void build(std::vector<reference> references)
    {
        nodes_.clear();
        nodes_.reserve(references.size() / 2 + 1);
        leaf_references_.clear();
        leaf_references_.reserve(references.size() + references.size() / 4);
        box bounds;
        for(const reference& item : references)
        {
            bounds.grow(item.bounds);
        }
        root_surface_ = bounds.get_surface();
        const uint32_t budget = uint32_t(k_spatial_split_budget * float(references.size()));
        build_node_from(std::move(references), 0, budget);
    }

    auto get_nodes() const -> const std::vector<build_node>&
    {
        return nodes_;
    }

    /// The references in leaf order: a leaf holds [first, first + count) of this.
    auto get_leaf_references() const -> const std::vector<reference>&
    {
        return leaf_references_;
    }

private:
    /// The cheapest split found for a node: an object split by centroid bin, or a spatial split at a plane.
    struct split_choice
    {
        float cost = std::numeric_limits<float>::max();
        uint32_t axis = 0;
        ///< Bins left of the split.
        uint32_t position = 0;
        bool is_spatial = false;
        ///< A spatial split's references on each side, a triangle across the plane counted on both.
        uint32_t left_count = 0;
        uint32_t right_count = 0;
        ///< An object split's boxes, for the overlap that decides whether a spatial split is worth a look.
        box left_bounds;
        box right_bounds;

        auto is_valid() const -> bool
        {
            return cost < std::numeric_limits<float>::max();
        }
    };

    struct object_bin
    {
        box bounds;
        uint32_t count = 0;
    };

    struct spatial_bin
    {
        box bounds;
        ///< References whose part in this bin is their first, and their last.
        uint32_t enter = 0;
        uint32_t exit = 0;
    };

    auto get_corners(uint32_t triangle) const -> std::array<math::vec3, 3>
    {
        return {positions_[indices_[size_t(triangle) * 3 + 0]],
                positions_[indices_[size_t(triangle) * 3 + 1]],
                positions_[indices_[size_t(triangle) * 3 + 2]]};
    }

    auto build_node_from(std::vector<reference> references, uint32_t depth, uint32_t budget) -> uint32_t
    {
        const uint32_t index = uint32_t(nodes_.size());
        nodes_.emplace_back();
        box bounds;
        box centroid_bounds;
        for(const reference& item : references)
        {
            bounds.grow(item.bounds);
            centroid_bounds.grow(item.bounds.get_center());
        }
        nodes_[index].bounds = bounds;
        const uint32_t count = uint32_t(references.size());
        if(count <= k_min_leaf_triangles)
        {
            make_leaf(index, references);
            return index;
        }
        std::vector<reference> left;
        std::vector<reference> right;
        uint32_t remaining_budget = budget;
        if(depth < k_max_heuristic_depth)
        {
            const split_choice split = choose_split(references, bounds, centroid_bounds, budget);
            const float surface = bounds.get_surface();
            const float leaf_cost = k_block_cost * float(get_block_count(count)) * surface;
            const float split_cost = k_traversal_cost * surface + k_block_cost * split.cost;
            if(count <= k_max_leaf_triangles && leaf_cost <= split_cost)
            {
                make_leaf(index, references);
                return index;
            }
            if(split.is_valid())
            {
                partition(references, bounds, centroid_bounds, split, left, right, remaining_budget);
            }
        }
        if(left.empty() || right.empty())
        {
            left.clear();
            right.clear();
            remaining_budget = budget;
            partition_at_median(references, centroid_bounds, left, right);
        }
        references = {};
        // The budget left over is shared in proportion to what each side holds.
        const uint32_t left_budget =
            uint32_t(uint64_t(remaining_budget) * left.size() / std::max<size_t>(left.size() + right.size(), 1));
        const uint32_t right_budget = remaining_budget - left_budget;
        const uint32_t left_index = build_node_from(std::move(left), depth + 1, left_budget);
        const uint32_t right_index = build_node_from(std::move(right), depth + 1, right_budget);
        nodes_[index].left = left_index;
        nodes_[index].right = right_index;
        return index;
    }

    void make_leaf(uint32_t index, const std::vector<reference>& references)
    {
        nodes_[index].first = uint32_t(leaf_references_.size());
        nodes_[index].count = uint32_t(references.size());
        leaf_references_.insert(leaf_references_.end(), references.begin(), references.end());
    }

    auto choose_split(const std::vector<reference>& references,
                      const box& bounds,
                      const box& centroid_bounds,
                      uint32_t budget) const -> split_choice
    {
        const split_choice object = find_object_split(references, centroid_bounds);
        if(budget == 0 || !object.is_valid())
        {
            return object;
        }
        const float overlap = object.left_bounds.get_intersection(object.right_bounds).get_surface();
        if(overlap < k_spatial_overlap_threshold * bounds.get_surface() ||
           overlap < k_spatial_root_overlap_threshold * root_surface_)
        {
            return object;
        }
        const split_choice spatial = find_spatial_split(references, bounds);
        const uint64_t extra = uint64_t(spatial.left_count) + spatial.right_count - references.size();
        if(spatial.is_valid() && spatial.cost < k_spatial_cost_threshold * object.cost && extra <= budget)
        {
            return spatial;
        }
        return object;
    }

    /// The cheapest object split: centroids binned along each axis, the plane between two bins.
    auto find_object_split(const std::vector<reference>& references, const box& centroid_bounds) const -> split_choice
    {
        split_choice best;
        for(uint32_t axis = 0; axis < 3; ++axis)
        {
            const float minimum = centroid_bounds.min[axis];
            const float extent = centroid_bounds.max[axis] - minimum;
            if(!(extent > 0.0f))
            {
                continue;
            }
            const float scale = float(k_object_bins) / extent;
            std::array<object_bin, k_object_bins> bins{};
            for(const reference& item : references)
            {
                object_bin& target = bins[get_bin(item.bounds.get_center()[axis], minimum, scale, k_object_bins)];
                target.bounds.grow(item.bounds);
                ++target.count;
            }
            // Split s puts bins [0, s) left and [s, k_object_bins) right.
            std::array<box, k_object_bins> right_bounds{};
            std::array<uint32_t, k_object_bins> right_count{};
            box right;
            uint32_t right_total = 0;
            for(uint32_t split = k_object_bins - 1; split > 0; --split)
            {
                right.grow(bins[split].bounds);
                right_total += bins[split].count;
                right_bounds[split] = right;
                right_count[split] = right_total;
            }
            box left;
            uint32_t left_total = 0;
            for(uint32_t split = 1; split < k_object_bins; ++split)
            {
                left.grow(bins[split - 1].bounds);
                left_total += bins[split - 1].count;
                if(left_total == 0 || right_count[split] == 0)
                {
                    continue;
                }
                const float cost = float(get_block_count(left_total)) * left.get_surface() +
                                   float(get_block_count(right_count[split])) * right_bounds[split].get_surface();
                if(cost < best.cost)
                {
                    best.cost = cost;
                    best.axis = axis;
                    best.position = split;
                    best.left_bounds = left;
                    best.right_bounds = right_bounds[split];
                }
            }
        }
        return best;
    }

    /**
     * @brief The cheapest spatial split: the node cut into equal bins along each axis, every reference clipped into
     *        the bins it spans (Embree's SpatialBinInfo::bin2), the plane between two bins.
     */
    auto find_spatial_split(const std::vector<reference>& references, const box& bounds) const -> split_choice
    {
        split_choice best;
        for(uint32_t axis = 0; axis < 3; ++axis)
        {
            const float minimum = bounds.min[axis];
            const float extent = bounds.max[axis] - minimum;
            if(!(extent > 0.0f))
            {
                continue;
            }
            const float scale = float(k_spatial_bins) / extent;
            const float width = extent / float(k_spatial_bins);
            std::array<spatial_bin, k_spatial_bins> bins{};
            for(const reference& item : references)
            {
                const uint32_t first = get_bin(item.bounds.min[axis], minimum, scale, k_spatial_bins);
                const uint32_t last = get_bin(item.bounds.max[axis], minimum, scale, k_spatial_bins);
                ++bins[first].enter;
                ++bins[last].exit;
                if(first == last)
                {
                    bins[first].bounds.grow(item.bounds);
                    continue;
                }
                const std::array<math::vec3, 3> corners = get_corners(item.triangle);
                box rest = item.bounds;
                for(uint32_t bin = first; bin < last; ++bin)
                {
                    box below;
                    box above;
                    split_triangle(corners, rest, axis, minimum + float(bin + 1u) * width, below, above);
                    if(!below.is_empty())
                    {
                        bins[bin].bounds.grow(below);
                    }
                    rest = above;
                }
                if(!rest.is_empty())
                {
                    bins[last].bounds.grow(rest);
                }
            }
            std::array<box, k_spatial_bins> right_bounds{};
            std::array<uint32_t, k_spatial_bins> right_count{};
            box right;
            uint32_t right_total = 0;
            for(uint32_t split = k_spatial_bins - 1; split > 0; --split)
            {
                right.grow(bins[split].bounds);
                right_total += bins[split].exit;
                right_bounds[split] = right;
                right_count[split] = right_total;
            }
            box left;
            uint32_t left_total = 0;
            for(uint32_t split = 1; split < k_spatial_bins; ++split)
            {
                left.grow(bins[split - 1].bounds);
                left_total += bins[split - 1].enter;
                if(left_total == 0 || right_count[split] == 0)
                {
                    continue;
                }
                const float cost = float(get_block_count(left_total)) * left.get_surface() +
                                   float(get_block_count(right_count[split])) * right_bounds[split].get_surface();
                if(cost < best.cost)
                {
                    best.cost = cost;
                    best.axis = axis;
                    best.position = split;
                    best.is_spatial = true;
                    best.left_count = left_total;
                    best.right_count = right_count[split];
                }
            }
        }
        return best;
    }

    /**
     * @brief Sends each reference to the side of @p split it falls on. A spatial split cuts a reference across its
     *        plane into one on each side while @p budget lasts, and sends it whole by its centre once it is spent.
     */
    void partition(const std::vector<reference>& references,
                   const box& bounds,
                   const box& centroid_bounds,
                   const split_choice& split,
                   std::vector<reference>& left,
                   std::vector<reference>& right,
                   uint32_t& budget) const
    {
        const uint32_t axis = split.axis;
        if(!split.is_spatial)
        {
            const float minimum = centroid_bounds.min[axis];
            const float scale = float(k_object_bins) / (centroid_bounds.max[axis] - minimum);
            for(const reference& item : references)
            {
                const uint32_t bin = get_bin(item.bounds.get_center()[axis], minimum, scale, k_object_bins);
                (bin < split.position ? left : right).push_back(item);
            }
            return;
        }
        const float minimum = bounds.min[axis];
        const float extent = bounds.max[axis] - minimum;
        const float scale = float(k_spatial_bins) / extent;
        const float plane = minimum + float(split.position) * (extent / float(k_spatial_bins));
        left.reserve(split.left_count);
        right.reserve(split.right_count);
        for(const reference& item : references)
        {
            const uint32_t first = get_bin(item.bounds.min[axis], minimum, scale, k_spatial_bins);
            const uint32_t last = get_bin(item.bounds.max[axis], minimum, scale, k_spatial_bins);
            if(last < split.position)
            {
                left.push_back(item);
                continue;
            }
            if(first >= split.position)
            {
                right.push_back(item);
                continue;
            }
            if(budget == 0)
            {
                (item.bounds.get_center()[axis] < plane ? left : right).push_back(item);
                continue;
            }
            box below;
            box above;
            split_triangle(get_corners(item.triangle), item.bounds, axis, plane, below, above);
            if(below.is_empty())
            {
                right.push_back(item);
                continue;
            }
            if(above.is_empty())
            {
                left.push_back(item);
                continue;
            }
            left.push_back({below, item.triangle});
            right.push_back({above, item.triangle});
            --budget;
        }
    }

    /// Splits the references in half by centroid along the axis they spread most on.
    static void partition_at_median(std::vector<reference>& references,
                                    const box& centroid_bounds,
                                    std::vector<reference>& left,
                                    std::vector<reference>& right)
    {
        const math::vec3 spread = centroid_bounds.max - centroid_bounds.min;
        const uint32_t axis = spread.x > spread.y ? (spread.x > spread.z ? 0u : 2u) : (spread.y > spread.z ? 1u : 2u);
        const auto middle = references.begin() + std::ptrdiff_t(references.size() / 2);
        std::nth_element(references.begin(),
                         middle,
                         references.end(),
                         [&](const reference& lhs, const reference& rhs)
                         {
                             return lhs.bounds.get_center()[axis] < rhs.bounds.get_center()[axis];
                         });
        left.assign(references.begin(), middle);
        right.assign(middle, references.end());
    }

    const std::vector<math::vec3>& positions_;
    const std::vector<uint32_t>& indices_;
    float root_surface_ = 0.0f;
    std::vector<build_node> nodes_;
    std::vector<reference> leaf_references_;
};

/// Rows of a wide node's bounds: the minimum and the maximum along x, then y, then z.
constexpr uint32_t k_bounds_rows = 6;

/// A child of a wide node or an entry of a traversal stack: a node's index, or a leaf's first triangle block with its
/// block count in the top bits (Embree tags its node references alike), so a child is one 32-bit word.
constexpr uint32_t k_leaf_blocks_shift = 29;
constexpr uint32_t k_child_index_mask = (1u << k_leaf_blocks_shift) - 1u;
static_assert(k_max_leaf_triangles / k_block_triangles < (1u << (32u - k_leaf_blocks_shift)), "block count fits");

auto make_leaf_child(uint32_t first_block, uint32_t blocks) -> uint32_t
{
    return (blocks << k_leaf_blocks_shift) | first_block;
}

/// A child's leaf block count; zero for a node.
auto get_leaf_blocks(uint32_t child) -> uint32_t
{
    return child >> k_leaf_blocks_shift;
}

/// A child's node index or first triangle block.
auto get_child_index(uint32_t child) -> uint32_t
{
    return child & k_child_index_mask;
}

/**
 * @brief A traversal node: the bounds of up to four children, laid out so one ray tests all four at once.
 *
 * The tests run on all four lanes; only the first @ref child_count are read. Two cache lines.
 */
struct wide_node
{
    ///< One row per plane, minimum x, maximum x, minimum y, ..., one lane per child: a ray reads the row it enters a
    ///< box through and the row it leaves through by index (ray_frame::near_plane).
    std::array<math::vec4, k_bounds_rows> bounds{};
    ///< Each child as make_leaf_child makes a leaf, or its node index.
    std::array<uint32_t, k_node_width> child{};
    uint32_t child_count = 0;
};

/**
 * @brief Up to four triangles of a leaf, laid out so one ray tests all of them at once.
 *
 * Each corner is three rows (x, y, z) of four lanes, one triangle per lane. Lanes past @ref count repeat the last
 * triangle and are never read back.
 */
struct triangle_block
{
    std::array<math::vec4, 3> a{};
    std::array<math::vec4, 3> b{};
    std::array<math::vec4, 3> c{};
    ///< Each lane's triangle, indexed as in the input.
    std::array<uint32_t, k_block_triangles> id{};
    uint32_t count = 0;
};

/**
 * @brief A node or leaf waiting on a traversal stack, with the distance at which the ray enters its bounds.
 *
 * Trivial on purpose: a traversal declares a stack of these per ray, and initialising them would cost more than many
 * a ray's whole traversal.
 */
struct stack_entry
{
    ///< As wide_node::child.
    uint32_t child;
    float distance;
};

/// A node or leaf waiting on a packet's traversal stack: the rays that enter it, one bit each, and where each enters.
/// Trivial, as stack_entry.
struct packet_stack_entry
{
    ///< As wide_node::child.
    uint32_t child;
    uint32_t rays;
    std::array<float, k_packet_rays> distance;
};

/// Where a ray enters each child box of a node, and the children it enters before it leaves them and within its range,
/// one bit each.
struct child_entries
{
    math::vec4 enter{};
    uint32_t entered = 0;
};

/// Everything one ray's traversal and triangle tests precompute, each value spread across the four lanes.
struct ray_frame
{
    ///< The origin, one row per axis.
    std::array<math::vec4, 3> origin{};
    ///< One over the direction, one row per axis (get_safe_inverse).
    std::array<math::vec4, 3> inverse{};
    ///< Per axis, the wide_node::bounds row a ray enters a box through and the row it leaves through: the minimum then
    ///< the maximum along a positive direction, the other way round along a negative one. Picked once per ray, so a
    ///< node needs no minimum and maximum of the two per axis (Embree's nearX / farX).
    std::array<uint32_t, 3> near_plane{};
    std::array<uint32_t, 3> far_plane{};
    ///< The axes reordered so the direction's largest component comes last.
    uint32_t axis_x = 0;
    uint32_t axis_y = 1;
    uint32_t axis_z = 2;
    ///< The shear that maps the direction onto that last axis.
    math::vec4 shear_x{0.0f};
    math::vec4 shear_y{0.0f};
    math::vec4 shear_z{1.0f};
};

auto get_safe_inverse(float component) -> float
{
    if(std::fabs(component) > k_min_direction_component)
    {
        return 1.0f / component;
    }
    return 1.0f / std::copysign(k_min_direction_component, component);
}

auto make_ray_frame(const math::vec3& origin, const math::vec3& direction) -> ray_frame
{
    ray_frame frame;
    for(uint32_t axis = 0; axis < 3; ++axis)
    {
        const float inverse = get_safe_inverse(direction[axis]);
        frame.origin[axis] = math::vec4(origin[axis]);
        frame.inverse[axis] = math::vec4(inverse);
        frame.near_plane[axis] = 2u * axis + (inverse < 0.0f ? 1u : 0u);
        frame.far_plane[axis] = frame.near_plane[axis] ^ 1u;
    }
    const math::vec3 magnitude = math::abs(direction);
    frame.axis_z =
        magnitude.x > magnitude.y ? (magnitude.x > magnitude.z ? 0u : 2u) : (magnitude.y > magnitude.z ? 1u : 2u);
    frame.axis_x = (frame.axis_z + 1u) % 3u;
    frame.axis_y = (frame.axis_x + 1u) % 3u;
    // The largest component is never one the safe inverse replaced, so its inverse is plain 1 / component.
    const float inverse_z = frame.inverse[frame.axis_z][0];
    frame.shear_x = math::vec4(-direction[frame.axis_x] * inverse_z);
    frame.shear_y = math::vec4(-direction[frame.axis_y] * inverse_z);
    frame.shear_z = math::vec4(inverse_z);
    return frame;
}

/**
 * @brief The lanes of @p values as plain floats. Indexing a SIMD vector by a lane only known at run time compiles to a
 *        branch per possible lane, and the lanes a ray picks are unpredictable; one store makes every read a load.
 */
auto get_lanes(const math::vec4& values) -> std::array<float, 4>
{
    std::array<float, 4> lanes;
    std::memcpy(lanes.data(), &values, sizeof(lanes));
    return lanes;
}

/**
 * @brief One bit per lane, set where @p lanes is true. Applied to a comparison of two math::vec4, the compiler fuses
 *        the two into one vector compare and one gather of the lanes' sign bits.
 */
auto get_mask(const math::bvec4& lanes) -> uint32_t
{
    return uint32_t(lanes.x) | (uint32_t(lanes.y) << 1) | (uint32_t(lanes.z) << 2) | (uint32_t(lanes.w) << 3);
}

/// One corner of each lane's triangle, relative to the ray origin in the ray's sheared frame: there the ray runs along
/// +z from (0, 0).
struct sheared_corners
{
    math::vec4 x{};
    math::vec4 y{};
    math::vec4 z{};
};

/// One function for every corner, so a corner several triangles share lands on exactly the same point for each.
auto shear_corners(const ray_frame& frame, const std::array<math::vec4, 3>& corner) -> sheared_corners
{
    const math::vec4 along = corner[frame.axis_z] - frame.origin[frame.axis_z];
    return {corner[frame.axis_x] - frame.origin[frame.axis_x] + frame.shear_x * along,
            corner[frame.axis_y] - frame.origin[frame.axis_y] + frame.shear_y * along,
            frame.shear_z * along};
}

/**
 * @brief Decides one lane whose float edge functions included an exact zero, in double, where the products of two
 *        floats are exact. False when the ray misses the triangle; otherwise @p t is where it hits.
 */
auto intersect_lane_in_double(const sheared_corners& a,
                              const sheared_corners& b,
                              const sheared_corners& c,
                              uint32_t lane,
                              float& t) -> bool
{
    const double edge_a = double(b.x[lane]) * double(c.y[lane]) - double(b.y[lane]) * double(c.x[lane]);
    const double edge_b = double(c.x[lane]) * double(a.y[lane]) - double(c.y[lane]) * double(a.x[lane]);
    const double edge_c = double(a.x[lane]) * double(b.y[lane]) - double(a.y[lane]) * double(b.x[lane]);
    const bool has_negative = edge_a < 0.0 || edge_b < 0.0 || edge_c < 0.0;
    const bool has_positive = edge_a > 0.0 || edge_b > 0.0 || edge_c > 0.0;
    const double determinant = edge_a + edge_b + edge_c;
    if((has_negative && has_positive) || determinant == 0.0)
    {
        return false;
    }
    const double scaled = edge_a * double(a.z[lane]) + edge_b * double(b.z[lane]) + edge_c * double(c.z[lane]);
    t = float(scaled / determinant);
    return true;
}

/// A block's triangles in one ray's sheared frame, with their edge functions and the lanes that may hold a hit.
struct block_test
{
    sheared_corners a;
    sheared_corners b;
    sheared_corners c;
    math::vec4 edge_a{};
    math::vec4 edge_b{};
    math::vec4 edge_c{};
    math::vec4 lowest{};
    math::vec4 highest{};
    ///< One bit per lane: its edge functions share a sign, or one is exactly zero.
    uint32_t candidates = 0;
    ///< One bit per lane with an edge function of exactly zero.
    uint32_t on_an_edge = 0;
};

/**
 * @brief Watertight ray-triangle test (Woop, Benthin, Wald, Watertight Ray/Triangle Intersection, 2013), two-sided,
 *        on every triangle of a block at once: this half finds the lanes that may hold a hit, @ref take_block_hits
 *        takes them.
 *
 * In the sheared frame the ray is the z axis, so the ray hits a triangle when the origin lies inside the triangle's
 * projection: when its three edge functions share a sign. Two triangles sharing an edge compute exactly opposite
 * values on it, so no ray slips between them; this file is compiled without fused multiply-adds, which would round the
 * two differently. An edge function of exactly zero is redone in double, so a ray through an edge or a corner is
 * decided alike by every triangle there. Points on an edge count as inside.
 *
 * Most rays miss every triangle of most leaves they reach, so this half settles that for all four lanes without a
 * branch, and stays small enough for the compiler to inline into both traversals.
 */
auto test_block(const ray_frame& frame, const triangle_block& block) -> block_test
{
    block_test test;
    test.a = shear_corners(frame, block.a);
    test.b = shear_corners(frame, block.b);
    test.c = shear_corners(frame, block.c);
    test.edge_a = test.b.x * test.c.y - test.b.y * test.c.x;
    test.edge_b = test.c.x * test.a.y - test.c.y * test.a.x;
    test.edge_c = test.a.x * test.b.y - test.a.y * test.b.x;
    // The lowest edge function times the highest is negative exactly where the signs are mixed. A lane with an edge
    // function of zero is decided in double whatever its other signs, as every triangle at that edge or corner is.
    test.lowest = math::min(test.edge_a, math::min(test.edge_b, test.edge_c));
    test.highest = math::max(test.edge_a, math::max(test.edge_b, test.edge_c));
    const math::vec4 crossing = test.lowest * test.highest;
    const math::vec4 product = test.edge_a * test.edge_b * test.edge_c;
    const math::vec4 zero(0.0f);
    test.on_an_edge = get_mask(math::equal(product, zero));
    test.candidates = (get_mask(math::lessThanEqual(zero, crossing)) | test.on_an_edge) & ((1u << block.count) - 1u);
    return test;
}

/**
 * @brief The second half of the watertight test, for the lanes @ref test_block could not rule out: takes a hit with t
 *        in [@p t_near, @p best.t]; a tie replaces the earlier hit, as Embree's does.
 */
void take_block_hits(const block_test& test,
                     const triangle_block& block,
                     float t_near,
                     uint32_t skip_triangle,
                     ray_hit& best)
{
    const std::array<float, 4> distance =
        get_lanes((test.edge_a * test.a.z + test.edge_b * test.b.z + test.edge_c * test.c.z) /
                  (test.edge_a + test.edge_b + test.edge_c));
    const std::array<float, 4> lowest_lanes = get_lanes(test.lowest);
    const std::array<float, 4> highest_lanes = get_lanes(test.highest);
    for(uint32_t candidates = test.candidates; candidates != 0; candidates &= candidates - 1u)
    {
        const uint32_t lane = uint32_t(std::countr_zero(candidates));
        if(block.id[lane] == skip_triangle)
        {
            continue;
        }
        float t = distance[lane];
        if((test.on_an_edge >> lane) & 1u)
        {
            if(!intersect_lane_in_double(test.a, test.b, test.c, lane, t))
            {
                continue;
            }
        }
        else if(!(lowest_lanes[lane] > 0.0f || highest_lanes[lane] < 0.0f))
        {
            // Mixed signs, whose lowest times highest underflowed to zero.
            continue;
        }
        if(!(t >= t_near && t <= best.t))
        {
            continue;
        }
        best.t = t;
        best.triangle = block.id[lane];
        best.is_hit = true;
    }
}

auto is_finite(const math::vec3& point) -> bool
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

auto to_lanes(const std::array<float, 4>& values) -> math::vec4
{
    return math::vec4(values[0], values[1], values[2], values[3]);
}

/**
 * @brief The native backend: a spatial-split surface-area-heuristic tree widened to four children per node, traversed
 *        by one ray, or by a packet of four that walk it together, with the four child boxes and a block's four
 *        triangles each tested together on the math library's SIMD vectors.
 */
class native_scene final : public scene_backend
{
public:
    auto build(const std::vector<math::vec3>& positions, const std::vector<uint32_t>& indices) -> bool override
    {
        const uint32_t triangle_count = uint32_t(indices.size() / 3);
        // Triangles with a non-finite corner cannot be hit and would poison the tree's bounds; they are left out.
        std::vector<reference> references;
        references.reserve(triangle_count);
        for(uint32_t t = 0; t < triangle_count; ++t)
        {
            reference item;
            item.triangle = t;
            bool is_valid = true;
            for(uint32_t corner = 0; corner < 3; ++corner)
            {
                const math::vec3& position = positions[indices[t * 3 + corner]];
                is_valid = is_valid && is_finite(position);
                item.bounds.grow(position);
            }
            if(is_valid)
            {
                references.push_back(item);
            }
        }
        blocks_.clear();
        nodes_.clear();
        if(references.empty())
        {
            return true;
        }
        binary_tree_builder builder(positions, indices);
        builder.build(std::move(references));
        const std::vector<build_node>& binary = builder.get_nodes();
        const std::vector<reference>& leaf_references = builder.get_leaf_references();
        // Each leaf's triangles as consecutive blocks.
        std::vector<uint32_t> block_of_node(binary.size(), 0u);
        for(uint32_t node = 0; node < uint32_t(binary.size()); ++node)
        {
            const build_node& leaf = binary[node];
            if(leaf.count == 0)
            {
                continue;
            }
            block_of_node[node] = uint32_t(blocks_.size());
            for(uint32_t first = 0; first < leaf.count; first += k_block_triangles)
            {
                const uint32_t lanes = std::min(k_block_triangles, leaf.count - first);
                blocks_.push_back(make_block(positions, indices, &leaf_references[leaf.first + first], lanes));
            }
        }
        nodes_.reserve(binary.size() / 2 + 1);
        widen(binary, block_of_node, 0);
        // A child carries its index in 29 bits; a scene past that cannot be addressed.
        if(blocks_.size() > k_child_index_mask || nodes_.size() > k_child_index_mask)
        {
            blocks_.clear();
            nodes_.clear();
            return false;
        }
        return true;
    }

    auto intersect(const math::vec3& origin,
                   const math::vec3& direction,
                   float t_near,
                   uint32_t skip_triangle,
                   float t_far) const -> ray_hit override
    {
        return trace(origin, direction, t_near, skip_triangle, t_far);
    }

    void intersect(hpp::span<const ray> rays, hpp::span<ray_hit> hits) const override
    {
        for(size_t first = 0; first < rays.size(); first += k_packet_rays)
        {
            const size_t count = std::min<size_t>(k_packet_rays, rays.size() - first);
            if(count == 1)
            {
                // A lone ray is cheaper on its own than as a packet of one.
                const ray& lone = rays[first];
                hits[first] =
                    trace(lone.origin, lone.direction, lone.t_near, triangle_scene::invalid_triangle, lone.t_far);
                continue;
            }
            trace_packet(rays.subspan(first, count), hits.subspan(first, count));
        }
    }

private:
    /// The closest hit of one ray: the single-ray intersect, and a batch's lone ray.
    auto trace(const math::vec3& origin,
               const math::vec3& direction,
               float t_near,
               uint32_t skip_triangle,
               float t_far) const -> ray_hit
    {
        ray_hit best;
        best.t = t_far;
        if(nodes_.empty())
        {
            return best;
        }
        const ray_frame frame = make_ray_frame(origin, direction);
        const math::vec4 near_limit(t_near);
        std::array<stack_entry, k_stack_size> stack;
        uint32_t stack_size = 0;
        stack_entry current{0u, t_near};
        while(true)
        {
            if(const uint32_t blocks = get_leaf_blocks(current.child); blocks > 0)
            {
                const uint32_t first = get_child_index(current.child);
                for(uint32_t block = first; block < first + blocks; ++block)
                {
                    // Only the test inlines: the hits, which few blocks have, go through a call.
                    const block_test test = test_block(frame, blocks_[block]);
                    if(test.candidates != 0)
                    {
                        take_block_hits(test, blocks_[block], t_near, skip_triangle, best);
                    }
                }
            }
            else if(enter_children(frame, near_limit, best.t, nodes_[current.child], current, stack, stack_size))
            {
                continue;
            }
            // Next from the stack, skipping whatever the hits found since lie beyond.
            do
            {
                if(stack_size == 0)
                {
                    return best;
                }
                current = stack[--stack_size];
            } while(current.distance > best.t);
        }
    }

    /**
     * @brief The closest hits of two to four rays traced together: the tree is walked once for all of them, each node
     *        fetched and its children ordered once for the rays that enter it.
     *
     * Each ray tests boxes and triangles exactly as @ref trace does, against its own closest hit so far, so it finds
     * its hit at the same distance; only the order of the walk is shared. That pays for short rays leaving one point,
     * such as a sign vote's, which walk the same nodes; long rays part too early to share much.
     */
    void trace_packet(hpp::span<const ray> rays, hpp::span<ray_hit> hits) const
    {
        const uint32_t count = uint32_t(rays.size());
        // Lanes past the rays repeat the first and never take part.
        const auto get_ray = [&](uint32_t lane) -> const ray&
        {
            return rays[lane < count ? lane : 0u];
        };
        const std::array<ray_frame, k_packet_rays> frames = {make_ray_frame(get_ray(0).origin, get_ray(0).direction),
                                                             make_ray_frame(get_ray(1).origin, get_ray(1).direction),
                                                             make_ray_frame(get_ray(2).origin, get_ray(2).direction),
                                                             make_ray_frame(get_ray(3).origin, get_ray(3).direction)};
        std::array<float, k_packet_rays> t_near;
        std::array<ray_hit, k_packet_rays> best;
        for(uint32_t lane = 0; lane < k_packet_rays; ++lane)
        {
            t_near[lane] = get_ray(lane).t_near;
            best[lane].t = get_ray(lane).t_far;
        }
        if(!nodes_.empty())
        {
            walk_packet(frames, t_near, (1u << count) - 1u, best);
        }
        for(uint32_t lane = 0; lane < count; ++lane)
        {
            hits[lane] = best[lane];
        }
    }

    /// The traversal of @ref trace_packet, for the rays of @p lanes.
    void walk_packet(const std::array<ray_frame, k_packet_rays>& frames,
                     const std::array<float, k_packet_rays>& t_near,
                     uint32_t lanes,
                     std::array<ray_hit, k_packet_rays>& best) const
    {
        std::array<math::vec4, k_packet_rays> near_limits;
        for(uint32_t lane = 0; lane < k_packet_rays; ++lane)
        {
            near_limits[lane] = math::vec4(t_near[lane]);
        }
        std::array<packet_stack_entry, k_stack_size> stack;
        uint32_t stack_size = 0;
        packet_stack_entry current;
        current.child = 0u;
        current.rays = lanes;
        current.distance = t_near;
        while(true)
        {
            if(const uint32_t blocks = get_leaf_blocks(current.child); blocks > 0)
            {
                const uint32_t first = get_child_index(current.child);
                for(uint32_t block = first; block < first + blocks; ++block)
                {
                    for(uint32_t rays = current.rays; rays != 0; rays &= rays - 1u)
                    {
                        const uint32_t lane = uint32_t(std::countr_zero(rays));
                        const block_test test = test_block(frames[lane], blocks_[block]);
                        if(test.candidates != 0)
                        {
                            take_block_hits(test,
                                            blocks_[block],
                                            t_near[lane],
                                            triangle_scene::invalid_triangle,
                                            best[lane]);
                        }
                    }
                }
            }
            else if(enter_children_packet(frames, near_limits, best, nodes_[current.child], current, stack, stack_size))
            {
                continue;
            }
            // Next from the stack, for the rays whose hits found since do not lie before it.
            do
            {
                if(stack_size == 0)
                {
                    return;
                }
                current = stack[--stack_size];
                const math::vec4 limit(best[0].t, best[1].t, best[2].t, best[3].t);
                current.rays &= get_mask(math::lessThanEqual(to_lanes(current.distance), limit));
            } while(current.rays == 0);
        }
    }

public:
    auto collect_triangles(const math::bbox& region, float reach, hpp::span<uint32_t> out) const -> uint32_t override
    {
        if(nodes_.empty())
        {
            return 0;
        }
        const uint32_t capacity = uint32_t(out.size());
        const float reach_squared = reach * reach;
        const std::array<math::vec4, 3> region_min = {math::vec4(region.min.x),
                                                      math::vec4(region.min.y),
                                                      math::vec4(region.min.z)};
        const std::array<math::vec4, 3> region_max = {math::vec4(region.max.x),
                                                      math::vec4(region.max.y),
                                                      math::vec4(region.max.z)};
        uint32_t count = 0;
        std::array<uint32_t, k_stack_size> stack;
        uint32_t stack_size = 0;
        stack[stack_size++] = 0u;
        while(stack_size > 0)
        {
            const wide_node& node = nodes_[stack[--stack_size]];
            const std::array<float, 4> distance_squared = get_gap_squared(node.bounds[0],
                                                                          node.bounds[2],
                                                                          node.bounds[4],
                                                                          node.bounds[1],
                                                                          node.bounds[3],
                                                                          node.bounds[5],
                                                                          region_min,
                                                                          region_max);
            for(uint32_t lane = 0; lane < node.child_count; ++lane)
            {
                if(!(distance_squared[lane] <= reach_squared))
                {
                    continue;
                }
                const uint32_t blocks = get_leaf_blocks(node.child[lane]);
                if(blocks == 0)
                {
                    assert(stack_size < stack.size());
                    stack[stack_size++] = node.child[lane];
                    continue;
                }
                const uint32_t first = get_child_index(node.child[lane]);
                for(uint32_t block = first; block < first + blocks; ++block)
                {
                    if(!collect_block(blocks_[block], region_min, region_max, reach_squared, out, count))
                    {
                        return capacity + 1;
                    }
                }
            }
        }
        return count;
    }

private:
    /**
     * @brief Adds the triangles of @p block whose bounds come within reach of the region to @p out, once each: a
     *        spatial split leaves a triangle in more than one leaf. False when @p out overflows.
     */
    static auto collect_block(const triangle_block& block,
                              const std::array<math::vec4, 3>& region_min,
                              const std::array<math::vec4, 3>& region_max,
                              float reach_squared,
                              hpp::span<uint32_t> out,
                              uint32_t& count) -> bool
    {
        const std::array<float, 4> distance_squared =
            get_gap_squared(math::min(block.a[0], math::min(block.b[0], block.c[0])),
                            math::min(block.a[1], math::min(block.b[1], block.c[1])),
                            math::min(block.a[2], math::min(block.b[2], block.c[2])),
                            math::max(block.a[0], math::max(block.b[0], block.c[0])),
                            math::max(block.a[1], math::max(block.b[1], block.c[1])),
                            math::max(block.a[2], math::max(block.b[2], block.c[2])),
                            region_min,
                            region_max);
        for(uint32_t lane = 0; lane < block.count; ++lane)
        {
            if(!(distance_squared[lane] <= reach_squared))
            {
                continue;
            }
            const auto found = out.begin() + count;
            if(std::find(out.begin(), found, block.id[lane]) != found)
            {
                continue;
            }
            if(count == uint32_t(out.size()))
            {
                return false;
            }
            out[count++] = block.id[lane];
        }
        return true;
    }

    /**
     * @brief Tests a ray against a node's children: makes the nearest one it enters @p current and pushes the others,
     *        farthest first. False when the ray enters none of them.
     */
    static auto enter_children(const ray_frame& frame,
                               const math::vec4& near_limit,
                               float far_limit,
                               const wide_node& node,
                               stack_entry& current,
                               std::array<stack_entry, k_stack_size>& stack,
                               uint32_t& stack_size) -> bool
    {
        const child_entries tested = test_children(frame, near_limit, far_limit, node);
        uint32_t entered = tested.entered;
        if(entered == 0)
        {
            return false;
        }
        const std::array<float, 4> enter_lanes = get_lanes(tested.enter);
        const uint32_t first = uint32_t(std::countr_zero(entered));
        entered &= entered - 1u;
        const stack_entry nearest{node.child[first], enter_lanes[first]};
        if(entered == 0)
        {
            // Most often the ray enters just one child, which needs no stack at all.
            current = nearest;
            return true;
        }
        const uint32_t second = uint32_t(std::countr_zero(entered));
        if((entered & (entered - 1u)) == 0)
        {
            // Two: the nearer goes on, the farther waits.
            const stack_entry other{node.child[second], enter_lanes[second]};
            const bool other_is_nearer = other.distance < nearest.distance;
            assert(stack_size < stack.size());
            stack[stack_size++] = other_is_nearer ? nearest : other;
            current = other_is_nearer ? other : nearest;
            return true;
        }
        entered |= 1u << first;
        std::array<stack_entry, k_node_width> sorted;
        uint32_t sorted_count = 0;
        for(; entered != 0; entered &= entered - 1u)
        {
            const uint32_t lane = uint32_t(std::countr_zero(entered));
            const stack_entry candidate{node.child[lane], enter_lanes[lane]};
            uint32_t position = sorted_count++;
            while(position > 0 && sorted[position - 1].distance < candidate.distance)
            {
                sorted[position] = sorted[position - 1];
                --position;
            }
            sorted[position] = candidate;
        }
        // Farthest first, so the nearest of the rest comes off the stack next.
        assert(stack_size + sorted_count - 1u <= stack.size());
        for(uint32_t i = 0; i + 1 < sorted_count; ++i)
        {
            stack[stack_size++] = sorted[i];
        }
        current = sorted[sorted_count - 1];
        return true;
    }

    /// The slab test of one ray against a node's four child boxes, with the ray's range [@p near_limit, @p far_limit].
    static auto test_children(const ray_frame& frame,
                              const math::vec4& near_limit,
                              float far_limit,
                              const wide_node& node) -> child_entries
    {
        const math::vec4 enter_x = (node.bounds[frame.near_plane[0]] - frame.origin[0]) * frame.inverse[0];
        const math::vec4 enter_y = (node.bounds[frame.near_plane[1]] - frame.origin[1]) * frame.inverse[1];
        const math::vec4 enter_z = (node.bounds[frame.near_plane[2]] - frame.origin[2]) * frame.inverse[2];
        const math::vec4 exit_x = (node.bounds[frame.far_plane[0]] - frame.origin[0]) * frame.inverse[0];
        const math::vec4 exit_y = (node.bounds[frame.far_plane[1]] - frame.origin[1]) * frame.inverse[1];
        const math::vec4 exit_z = (node.bounds[frame.far_plane[2]] - frame.origin[2]) * frame.inverse[2];
        child_entries tested;
        tested.enter = math::max(math::max(enter_x, enter_y), math::max(enter_z, near_limit));
        const math::vec4 exit =
            math::min(math::min(exit_x, exit_y), math::min(exit_z, math::vec4(far_limit))) * k_box_exit_widening;
        tested.entered = get_mask(math::lessThanEqual(tested.enter, exit)) & ((1u << node.child_count) - 1u);
        return tested;
    }

    /**
     * @brief The packet form of @ref enter_children: each ray of @p current tests the children as it would alone, and
     *        the children any of them enters are walked nearest first, by the nearest entry of any ray.
     */
    static auto enter_children_packet(const std::array<ray_frame, k_packet_rays>& frames,
                                      const std::array<math::vec4, k_packet_rays>& near_limits,
                                      const std::array<ray_hit, k_packet_rays>& best,
                                      const wide_node& node,
                                      packet_stack_entry& current,
                                      std::array<packet_stack_entry, k_stack_size>& stack,
                                      uint32_t& stack_size) -> bool
    {
        std::array<std::array<float, k_node_width>, k_packet_rays> enter_lanes{};
        std::array<uint32_t, k_packet_rays> entered{};
        uint32_t entered_by_any = 0;
        // Only an order: a ray's entry counts here even for a child it misses.
        math::vec4 nearest(std::numeric_limits<float>::infinity());
        for(uint32_t rays = current.rays; rays != 0; rays &= rays - 1u)
        {
            const uint32_t lane = uint32_t(std::countr_zero(rays));
            const child_entries tested = test_children(frames[lane], near_limits[lane], best[lane].t, node);
            entered[lane] = tested.entered;
            entered_by_any |= tested.entered;
            enter_lanes[lane] = get_lanes(tested.enter);
            nearest = math::min(nearest, tested.enter);
        }
        if(entered_by_any == 0)
        {
            return false;
        }
        const std::array<float, k_node_width> order = get_lanes(nearest);
        const auto make_entry = [&](uint32_t child) -> packet_stack_entry
        {
            packet_stack_entry entry;
            entry.child = node.child[child];
            entry.rays = 0;
            for(uint32_t lane = 0; lane < k_packet_rays; ++lane)
            {
                entry.rays |= ((entered[lane] >> child) & 1u) << lane;
                entry.distance[lane] = enter_lanes[lane][child];
            }
            return entry;
        };
        const uint32_t first = uint32_t(std::countr_zero(entered_by_any));
        entered_by_any &= entered_by_any - 1u;
        if(entered_by_any == 0)
        {
            current = make_entry(first);
            return true;
        }
        const uint32_t second = uint32_t(std::countr_zero(entered_by_any));
        if((entered_by_any & (entered_by_any - 1u)) == 0)
        {
            const bool second_is_nearer = order[second] < order[first];
            assert(stack_size < stack.size());
            stack[stack_size++] = make_entry(second_is_nearer ? first : second);
            current = make_entry(second_is_nearer ? second : first);
            return true;
        }
        entered_by_any |= 1u << first;
        std::array<uint32_t, k_node_width> sorted;
        uint32_t sorted_count = 0;
        for(; entered_by_any != 0; entered_by_any &= entered_by_any - 1u)
        {
            const uint32_t child = uint32_t(std::countr_zero(entered_by_any));
            uint32_t position = sorted_count++;
            while(position > 0 && order[sorted[position - 1]] < order[child])
            {
                sorted[position] = sorted[position - 1];
                --position;
            }
            sorted[position] = child;
        }
        // Farthest first, as enter_children.
        assert(stack_size + sorted_count - 1u <= stack.size());
        for(uint32_t i = 0; i + 1 < sorted_count; ++i)
        {
            stack[stack_size++] = make_entry(sorted[i]);
        }
        current = make_entry(sorted[sorted_count - 1]);
        return true;
    }

    /**
     * @brief Squared distance from each of four boxes, given as rows of their minimum and maximum corners, to a
     *        region: the per-axis gaps between them, squared and summed in the order math::dot sums them.
     */
    static auto get_gap_squared(const math::vec4& min_x,
                                const math::vec4& min_y,
                                const math::vec4& min_z,
                                const math::vec4& max_x,
                                const math::vec4& max_y,
                                const math::vec4& max_z,
                                const std::array<math::vec4, 3>& region_min,
                                const std::array<math::vec4, 3>& region_max) -> std::array<float, 4>
    {
        const math::vec4 zero(0.0f);
        const math::vec4 gap_x = math::max(math::max(min_x - region_max[0], region_min[0] - max_x), zero);
        const math::vec4 gap_y = math::max(math::max(min_y - region_max[1], region_min[1] - max_y), zero);
        const math::vec4 gap_z = math::max(math::max(min_z - region_max[2], region_min[2] - max_z), zero);
        return get_lanes((gap_x * gap_x + gap_y * gap_y) + gap_z * gap_z);
    }

    /// @p lanes triangles of a leaf in lanes; fewer than four repeat the last one in the lanes left over.
    static auto make_block(const std::vector<math::vec3>& positions,
                           const std::vector<uint32_t>& indices,
                           const reference* references,
                           uint32_t lanes) -> triangle_block
    {
        triangle_block block;
        block.count = lanes;
        // Rows a.x, a.y, a.z, b.x, ... c.z, one lane per triangle.
        std::array<std::array<float, k_block_triangles>, 9> rows{};
        for(uint32_t lane = 0; lane < k_block_triangles; ++lane)
        {
            const uint32_t id = references[std::min(lane, lanes - 1u)].triangle;
            block.id[lane] = id;
            for(uint32_t corner = 0; corner < 3; ++corner)
            {
                const math::vec3& position = positions[indices[size_t(id) * 3 + corner]];
                for(uint32_t axis = 0; axis < 3; ++axis)
                {
                    rows[corner * 3 + axis][lane] = position[axis];
                }
            }
        }
        for(uint32_t axis = 0; axis < 3; ++axis)
        {
            block.a[axis] = to_lanes(rows[0 + axis]);
            block.b[axis] = to_lanes(rows[3 + axis]);
            block.c[axis] = to_lanes(rows[6 + axis]);
        }
        return block;
    }

    /**
     * @brief Turns the binary subtree at @p binary_index into wide nodes and returns the index of its top one.
     *
     * A wide node takes the binary node's children and keeps opening the internal one with the largest surface until
     * it holds four, or only leaves are left (Embree fills its wide nodes the same way).
     */
    auto widen(const std::vector<build_node>& binary, const std::vector<uint32_t>& block_of_node, uint32_t binary_index)
        -> uint32_t
    {
        std::array<uint32_t, k_node_width> picked{};
        uint32_t picked_count = 0;
        const build_node& top = binary[binary_index];
        if(top.count > 0)
        {
            picked[picked_count++] = binary_index;
        }
        else
        {
            picked[picked_count++] = top.left;
            picked[picked_count++] = top.right;
        }
        while(picked_count < k_node_width)
        {
            uint32_t largest = k_node_width;
            float largest_area = -1.0f;
            for(uint32_t i = 0; i < picked_count; ++i)
            {
                const build_node& candidate = binary[picked[i]];
                const float area = candidate.bounds.get_surface();
                if(candidate.count == 0 && area > largest_area)
                {
                    largest = i;
                    largest_area = area;
                }
            }
            if(largest == k_node_width)
            {
                break;
            }
            const build_node& opened = binary[picked[largest]];
            picked[largest] = opened.left;
            picked[picked_count++] = opened.right;
        }
        const uint32_t index = uint32_t(nodes_.size());
        nodes_.emplace_back();
        // Built aside: widening a child appends to nodes_, which may move this node.
        wide_node node;
        node.child_count = picked_count;
        std::array<std::array<float, k_node_width>, k_bounds_rows> rows{};
        for(uint32_t lane = 0; lane < picked_count; ++lane)
        {
            const build_node& child = binary[picked[lane]];
            for(uint32_t axis = 0; axis < 3; ++axis)
            {
                rows[2 * axis + 0][lane] = child.bounds.min[axis];
                rows[2 * axis + 1][lane] = child.bounds.max[axis];
            }
            if(child.count > 0)
            {
                node.child[lane] = make_leaf_child(block_of_node[picked[lane]], get_block_count(child.count));
                continue;
            }
            node.child[lane] = widen(binary, block_of_node, picked[lane]);
        }
        for(uint32_t row = 0; row < k_bounds_rows; ++row)
        {
            node.bounds[row] = to_lanes(rows[row]);
        }
        nodes_[index] = node;
        return index;
    }

    std::vector<triangle_block> blocks_;
    std::vector<wide_node> nodes_;
};

} // namespace

auto make_native_scene() -> std::unique_ptr<scene_backend>
{
    return std::make_unique<native_scene>();
}

} // namespace raytracing::detail
