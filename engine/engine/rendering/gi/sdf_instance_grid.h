#pragma once

#include <engine/engine_export.h>

#include <math/math.h>

#include <cstdint>
#include <vector>

namespace unravel
{

/**
 * @brief Uniform world-space grid over the resident field placements, so a ray tests only the
 *        instances it could actually reach.
 *
 * The per-instance tier is exact where it applies, and it is what makes thin geometry occlude, but
 * it costs a bounds test per instance per ray. That is invisible in a test scene and O(instances)
 * in a real one -- a model like Bistro registers a field per submesh, so a single ray was testing
 * over a thousand bounds to find the handful it crosses.
 *
 * WORLD SPACE, not view space. The name "froxel grid" comes from clustered lighting, where the
 * grid is a subdivision of the camera frustum; that would be the wrong structure here, because the
 * radiance cache update pass casts rays from cache entries that are deliberately OUTSIDE the
 * frustum. Culling those against a frustum-shaped grid would silently drop the near-field tier for
 * exactly the offscreen geometry the world-space cache exists to serve.
 *
 * SCENE BOUNDS, not camera centred. The grid spans the union of every instance's bounds, so no
 * instance is ever outside it. A camera-centred box would be cheaper to size but would remove the
 * near-field tier from anything beyond it, which does not read as a performance choice -- it reads
 * as thin geometry that stops occluding once you walk away from it.
 *
 * Layout is CSR: @ref get_cell_ranges gives each cell a [begin, end) slice of
 * @ref get_cell_instances. An instance appears in every cell its bounds overlap, so a traversal
 * may encounter the same instance more than once; that is deliberate. Testing an instance twice
 * costs one extra broad-phase rejection, while a scheme that tries to guarantee exactly-once has
 * to reason about a ray's parametric position at cell boundaries, where a floating-point tie can
 * skip an instance entirely -- and a skipped instance is geometry that silently stops occluding.
 */
class sdf_instance_grid
{
public:
    struct settings
    {
        ///< Cells along the LONGEST axis of the scene bounds. The other axes get however many
        ///< cells of that same cube size they need, so an elongated scene does not waste cells on
        ///< empty space.
        uint32_t resolution = 32;
        ///< Guard on total cells, in case degenerate bounds produce an extreme aspect ratio.
        ///< Exceeding it coarsens the cells rather than cropping the grid: a cropped grid would
        ///< leave instances unreachable, which is the one failure this structure must not have.
        uint32_t max_cells = 262144;
    };

    void init(const settings& settings);

    /**
     * @brief Rebuilds the grid over the given world-space instance bounds.
     *
     * Rebuilt in full every frame because the instance list is: this is an acceleration structure
     * with no state worth carrying between frames.
     */
    void build(const std::vector<math::bbox>& instance_bounds);

    /**
     * @brief Builds the grid over an EXPLICIT region rather than over the instances' own extent.
     *
     * For consumers that query a known volume -- clipmap composition walks one level's voxels --
     * where sizing the cells to the whole scene would put the entire level inside a single cell
     * and cull nothing.
     *
     * Instances reaching outside @p region are clamped into the edge cells rather than dropped,
     * which over-reports at the boundary and never under-reports. A caller that needs instances
     * NEAR the region as well as inside it must inflate their bounds before passing them in;
     * the grid answers questions about the boxes it is given, not about their surfaces.
     */
    void build(const std::vector<math::bbox>& instance_bounds, const math::bbox& region);

    /**
     * @brief Cell containing a world position, clamped to the grid.
     *
     * For a point inside the built region, the returned cell lists every instance whose bounds
     * cover that point. @return @ref get_cell_count when the grid is unusable.
     */
    auto find_cell(const math::vec3& world_position) const -> uint32_t;

    /**
     * @brief Appends the instances of every cell a ray segment passes through to @p out.
     *
     * Reference implementation of the traversal the tracing shader performs, and the thing the
     * tests compare against a brute-force bounds test. May contain duplicates.
     *
     * @return false when the segment misses the grid entirely, in which case no instance can be
     *         reached and the caller may skip the tier.
     */
    auto gather_candidates(const math::vec3& origin,
                           const math::vec3& direction,
                           float t_min,
                           float t_max,
                           std::vector<uint32_t>& out) const -> bool;

    /// CSR offsets, `cell_count + 1` entries: cell c owns
    /// `[get_cell_offsets()[c], get_cell_offsets()[c + 1])` of @ref get_cell_instances. Uploaded
    /// to the GPU verbatim, so the shader indexes it exactly as this does.
    auto get_cell_offsets() const -> const std::vector<uint32_t>&
    {
        return cell_offsets_;
    }

    auto get_cell_instances() const -> const std::vector<uint32_t>&
    {
        return cell_instances_;
    }

    auto get_cell_count() const -> size_t
    {
        return cell_offsets_.empty() ? 0u : cell_offsets_.size() - 1u;
    }

    auto get_origin() const -> const math::vec3&
    {
        return origin_;
    }

    auto get_dim() const -> const math::uvec3&
    {
        return dim_;
    }

    auto get_cell_size() const -> float
    {
        return cell_size_;
    }

    auto is_valid() const -> bool
    {
        return cell_size_ > 0.0f && dim_.x > 0u && dim_.y > 0u && dim_.z > 0u && !cell_offsets_.empty();
    }

    auto get_memory_usage() const -> size_t
    {
        return (cell_offsets_.size() + cell_instances_.size()) * sizeof(uint32_t);
    }

    /// Instances referenced across all cells. Larger than the instance count, because an instance
    /// spanning several cells is listed in each; the ratio is what the grid trades memory for.
    auto get_reference_count() const -> size_t
    {
        return cell_instances_.size();
    }

private:
    auto to_cell(const math::vec3& world_position) const -> math::ivec3;
    auto cell_index(int x, int y, int z) const -> uint32_t;

    settings settings_{};
    math::vec3 origin_{0.0f};
    math::uvec3 dim_{0u, 0u, 0u};
    float cell_size_ = 0.0f;
    std::vector<uint32_t> cell_offsets_;
    std::vector<uint32_t> cell_instances_;
};

} // namespace unravel
