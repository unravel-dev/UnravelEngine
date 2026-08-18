// This translation unit depends only on math and the standard library, so the grid can be built
// and validated in isolation from the graphics and ECS layers -- the same arrangement the SDF
// baker and the clipmap use, and what lets the GI test suite compare it against a brute-force
// reference.
#include "sdf_instance_grid.h"

#include <algorithm>
#include <cmath>

namespace unravel
{
namespace
{

/// Cell size floor. Bounds that collapse to a point would otherwise divide by zero and put every
/// instance in cell zero, which is correct but degenerate; this keeps the arithmetic finite.
constexpr float min_cell_size = 1e-4f;

/// Steps a DDA traversal is allowed before giving up. A ray crossing an n-cell grid on the
/// diagonal touches about 3n cells, so this is generous for any grid the guard below permits.
/// Present so a denormal direction cannot spin forever rather than as a real limit.
constexpr int max_traversal_steps = 4096;

} // namespace

void sdf_instance_grid::init(const settings& settings)
{
    settings_ = settings;
    settings_.resolution = math::max(settings_.resolution, 1u);
    settings_.max_cells = math::max(settings_.max_cells, 1u);
    cell_offsets_.clear();
    cell_instances_.clear();
    dim_ = math::uvec3(0u);
    cell_size_ = 0.0f;
}

auto sdf_instance_grid::cell_index(int x, int y, int z) const -> uint32_t
{
    // Scalar integer arithmetic throughout: glm's SIMD specialisation has no integer vector
    // division, and mixing the two here is how index bugs get introduced.
    return uint32_t(x) + uint32_t(y) * dim_.x + uint32_t(z) * dim_.x * dim_.y;
}

auto sdf_instance_grid::to_cell(const math::vec3& world_position) const -> math::ivec3
{
    const math::vec3 local = (world_position - origin_) / cell_size_;
    return math::ivec3(int(std::floor(local.x)), int(std::floor(local.y)), int(std::floor(local.z)));
}

void sdf_instance_grid::build(const std::vector<math::bbox>& instance_bounds)
{
    math::bbox scene;
    scene.reset();
    for(const auto& bounds : instance_bounds)
    {
        scene.add_point(bounds.min);
        scene.add_point(bounds.max);
    }
    build(instance_bounds, scene);
}

auto sdf_instance_grid::find_cell(const math::vec3& world_position) const -> uint32_t
{
    if(!is_valid())
    {
        return uint32_t(get_cell_count());
    }
    const math::ivec3 cell = to_cell(world_position);
    return cell_index(math::clamp(cell.x, 0, int(dim_.x) - 1),
                      math::clamp(cell.y, 0, int(dim_.y) - 1),
                      math::clamp(cell.z, 0, int(dim_.z) - 1));
}

void sdf_instance_grid::build(const std::vector<math::bbox>& instance_bounds, const math::bbox& region)
{
    cell_offsets_.clear();
    cell_instances_.clear();
    dim_ = math::uvec3(0u);
    cell_size_ = 0.0f;
    if(instance_bounds.empty() || !region.is_populated())
    {
        return;
    }
    const math::bbox& scene = region;
    const math::vec3 extent = scene.get_dimensions();
    const float longest = math::max(extent.x, math::max(extent.y, extent.z));
    cell_size_ = math::max(longest / float(settings_.resolution), min_cell_size);
    const auto axis_cells = [&](float axis_extent) -> uint32_t
    {
        return math::max(1u, uint32_t(std::ceil(axis_extent / cell_size_)));
    };
    dim_ = math::uvec3(axis_cells(extent.x), axis_cells(extent.y), axis_cells(extent.z));
    // Coarsen until the cell count fits the guard. Growing the cells keeps every instance
    // reachable; cropping the grid instead would leave some unreachable, and an instance a ray
    // cannot find is geometry that silently stops occluding.
    while(size_t(dim_.x) * dim_.y * dim_.z > size_t(settings_.max_cells))
    {
        cell_size_ *= 2.0f;
        dim_ = math::uvec3(axis_cells(extent.x), axis_cells(extent.y), axis_cells(extent.z));
    }
    origin_ = scene.min;
    const size_t cell_count = size_t(dim_.x) * dim_.y * dim_.z;
    // Counting pass, then a prefix sum, then a fill. Two passes over the instances rather than a
    // vector per cell: the cell count is large and most cells hold a handful of entries, so the
    // per-cell allocation would dominate the build.
    std::vector<uint32_t> counts(cell_count, 0u);
    const auto for_each_overlapped_cell = [&](const math::bbox& bounds, const auto& fn)
    {
        const math::ivec3 lo = to_cell(bounds.min);
        const math::ivec3 hi = to_cell(bounds.max);
        const int x0 = math::clamp(lo.x, 0, int(dim_.x) - 1);
        const int y0 = math::clamp(lo.y, 0, int(dim_.y) - 1);
        const int z0 = math::clamp(lo.z, 0, int(dim_.z) - 1);
        const int x1 = math::clamp(hi.x, 0, int(dim_.x) - 1);
        const int y1 = math::clamp(hi.y, 0, int(dim_.y) - 1);
        const int z1 = math::clamp(hi.z, 0, int(dim_.z) - 1);
        for(int z = z0; z <= z1; ++z)
        {
            for(int y = y0; y <= y1; ++y)
            {
                for(int x = x0; x <= x1; ++x)
                {
                    fn(cell_index(x, y, z));
                }
            }
        }
    };
    for(const auto& bounds : instance_bounds)
    {
        for_each_overlapped_cell(bounds, [&](uint32_t cell) { ++counts[cell]; });
    }
    // Prefix sum into CSR offsets. One extra entry so every cell's end is the next cell's begin
    // and no cell needs a separate count uploaded alongside it.
    cell_offsets_.assign(cell_count + 1u, 0u);
    uint32_t running = 0;
    for(size_t cell = 0; cell < cell_count; ++cell)
    {
        cell_offsets_[cell] = running;
        running += counts[cell];
    }
    cell_offsets_[cell_count] = running;
    cell_instances_.resize(running);
    // Separate write cursors, so the offsets stay the finished CSR structure rather than being
    // consumed as scratch during the fill.
    std::vector<uint32_t> cursor(cell_offsets_.begin(), cell_offsets_.end() - 1);
    for(uint32_t instance = 0; instance < uint32_t(instance_bounds.size()); ++instance)
    {
        for_each_overlapped_cell(instance_bounds[instance],
                                 [&](uint32_t cell)
                                 {
                                     cell_instances_[cursor[cell]] = instance;
                                     ++cursor[cell];
                                 });
    }
}

auto sdf_instance_grid::gather_candidates(const math::vec3& origin,
                                          const math::vec3& direction,
                                          float t_min,
                                          float t_max,
                                          std::vector<uint32_t>& out) const -> bool
{
    out.clear();
    if(!is_valid() || !(t_max > t_min))
    {
        return false;
    }
    const math::vec3 grid_min = origin_;
    const math::vec3 grid_max =
        origin_ + math::vec3(float(dim_.x), float(dim_.y), float(dim_.z)) * cell_size_;
    // Clip the segment to the grid. Rays routinely start outside it -- a cache entry can sit
    // beyond the scene bounds -- so the traversal must begin where the ray enters, not at t_min.
    float enter = t_min;
    float exit = t_max;
    for(int axis = 0; axis < 3; ++axis)
    {
        const float d = direction[axis];
        const float o = origin[axis];
        if(std::fabs(d) < 1e-8f)
        {
            if(o < grid_min[axis] || o > grid_max[axis])
            {
                return false;
            }
            continue;
        }
        const float inv = 1.0f / d;
        float near_t = (grid_min[axis] - o) * inv;
        float far_t = (grid_max[axis] - o) * inv;
        if(near_t > far_t)
        {
            std::swap(near_t, far_t);
        }
        enter = math::max(enter, near_t);
        exit = math::min(exit, far_t);
    }
    if(enter > exit)
    {
        return false;
    }
    // Amanatides-Woo. Start from the cell containing the entry point, nudged inward by a fraction
    // of a cell so a segment that enters exactly on a cell plane does not land outside the grid.
    const math::vec3 entry_point = origin + direction * enter;
    math::ivec3 cell = to_cell(entry_point);
    cell.x = math::clamp(cell.x, 0, int(dim_.x) - 1);
    cell.y = math::clamp(cell.y, 0, int(dim_.y) - 1);
    cell.z = math::clamp(cell.z, 0, int(dim_.z) - 1);
    math::ivec3 step(0);
    math::vec3 t_next(std::numeric_limits<float>::max());
    math::vec3 t_delta(std::numeric_limits<float>::max());
    for(int axis = 0; axis < 3; ++axis)
    {
        const float d = direction[axis];
        if(std::fabs(d) < 1e-8f)
        {
            continue;
        }
        step[axis] = d > 0.0f ? 1 : -1;
        const float inv = 1.0f / d;
        t_delta[axis] = std::fabs(cell_size_ * inv);
        const float boundary =
            grid_min[axis] + float(cell[axis] + (step[axis] > 0 ? 1 : 0)) * cell_size_;
        t_next[axis] = (boundary - origin[axis]) * inv;
    }
    for(int i = 0; i < max_traversal_steps; ++i)
    {
        const uint32_t index = cell_index(cell.x, cell.y, cell.z);
        for(uint32_t entry = cell_offsets_[index]; entry < cell_offsets_[index + 1u]; ++entry)
        {
            out.push_back(cell_instances_[entry]);
        }
        // Advance across the nearest cell plane.
        int axis = 0;
        if(t_next.y < t_next[axis])
        {
            axis = 1;
        }
        if(t_next.z < t_next[axis])
        {
            axis = 2;
        }
        if(t_next[axis] > exit || step[axis] == 0)
        {
            break;
        }
        cell[axis] += step[axis];
        if(cell[axis] < 0 || cell[axis] >= int(dim_[axis]))
        {
            break;
        }
        t_next[axis] += t_delta[axis];
    }
    return true;
}

} // namespace unravel
