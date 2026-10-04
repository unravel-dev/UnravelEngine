#include "lumen_mesh_cards.h"

#include "mesh_ray_tracing.h"
#include "mesh_sdf_baker.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace unravel
{
namespace
{

// Lumen's card-build constants (MeshCardRepresentationUtilities.cpp, MeshCardRepresentation.cpp),
// lengths converted from centimetres to metres.
constexpr int k_direction_count = 6;
///< TargetVoxelSize: the cluster grid aims at 10 cm cells.
constexpr float k_target_voxel_size = 0.10f;
///< Cells per axis at the first attempt; halved while the surfel count exceeds the target.
constexpr int k_max_voxels = 64;
constexpr int k_target_surfel_count = 10000;
///< BuildMeshCards: the bounds grow by 1 cm and are at least 1 cm in half extent.
constexpr float k_bounds_margin = 0.01f;
///< SerializeLOD: card depth may exceed the mesh bounds by 10 cm.
constexpr float k_card_margin_z = 0.10f;
///< Rays per cell when voxelizing one side.
constexpr uint32_t k_rays_per_cell = 32;
///< Requested hemisphere directions for the surfel visibility test (stratified to 5 x 5 = 25).
constexpr int k_hemisphere_ray_request = 32;
///< ComputeSurfelVisibility: visibility rays start 0.1 cm off the surfel.
constexpr float k_surface_ray_bias = 0.001f;
///< Faces closer than this along a ray are one surface (see resolve_coincident_front_face).
constexpr float k_coincident_face_depth = 0.001f;
///< r.MeshCardRepresentation.NormalTreshold: a hit must face the side by at least this cosine.
constexpr float k_normal_threshold = 0.25f;
///< r.MeshCardRepresentation.MinDensity, divided by 3 for the per-cluster density test.
constexpr float k_min_density = 0.2f;
constexpr float k_min_cluster_coverage = 15.0f;
///< A surfel is inside geometry when over 80% of its visibility rays hit and over 20% hit back faces.
constexpr float k_inside_hit_fraction = 0.8f;
constexpr float k_inside_back_face_fraction = 0.2f;
/// The card build's visibility directions: UE's stratified hemisphere set, seed 0.
auto generate_hemisphere_directions() -> std::vector<math::vec3>
{
    mesh_ray::random_stream stream(0u);
    return mesh_ray::generate_stratified_hemisphere_directions(uint32_t(k_hemisphere_ray_request), stream);
}

/// Frisvad's orthonormal basis around @p n: returns the world direction of a +z-hemisphere vector.
auto rotate_to_normal(const math::vec3& n, const math::vec3& local) -> math::vec3
{
    math::vec3 tangent_x;
    math::vec3 tangent_y;
    if(n.z < -0.9999999f)
    {
        tangent_x = math::vec3(0.0f, -1.0f, 0.0f);
        tangent_y = math::vec3(-1.0f, 0.0f, 0.0f);
    }
    else
    {
        const float a = 1.0f / (1.0f + n.z);
        const float b = -n.x * n.y * a;
        tangent_x = math::vec3(1.0f - n.x * n.x * a, b, -n.x);
        tangent_y = math::vec3(b, 1.0f - n.y * n.y * a, -n.y);
    }
    return tangent_x * local.x + tangent_y * local.y + n * local.z;
}

auto reverse_bits(uint32_t value) -> uint32_t
{
    value = ((value >> 1) & 0x55555555u) | ((value & 0x55555555u) << 1);
    value = ((value >> 2) & 0x33333333u) | ((value & 0x33333333u) << 2);
    value = ((value >> 4) & 0x0F0F0F0Fu) | ((value & 0x0F0F0F0Fu) << 4);
    value = ((value >> 8) & 0x00FF00FFu) | ((value & 0x00FF00FFu) << 8);
    return (value >> 16) | (value << 16);
}

auto get_direction_normal(int direction) -> math::vec3
{
    math::vec3 normal(0.0f);
    normal[direction / 2] = (direction & 1) != 0 ? 1.0f : -1.0f;
    return normal;
}

struct int_box
{
    math::ivec3 min{std::numeric_limits<int>::max()};
    math::ivec3 max{-std::numeric_limits<int>::max()};

    void add(const math::ivec3& point)
    {
        min = math::min(min, point);
        max = math::max(max, point);
    }

    auto get_face_area() const -> int
    {
        return (max.x + 1 - min.x) * (max.y + 1 - min.y);
    }

    auto get_center() const -> math::vec3
    {
        return math::vec3(max + min) * 0.5f;
    }
};

/// One side's voxel frame: local (x, y) spans the side, local z goes into the mesh.
struct direction_basis
{
    ///< World (mesh) directions of local x, y, z; z = -normal of the side.
    math::vec3 axis_x{0.0f};
    math::vec3 axis_y{0.0f};
    math::vec3 axis_z{0.0f};
    ///< Mesh-space position of local (0, 0, 0): the corner of the grid on the side's face.
    math::vec3 offset{0.0f};
    math::ivec3 volume_size{1};
    float voxel_size = 0.0f;

    auto to_mesh(const math::vec3& local) const -> math::vec3
    {
        return (axis_x * local.x + axis_y * local.y + axis_z * local.z) * voxel_size + offset;
    }

    auto to_local(const math::vec3& mesh_position) const -> math::vec3
    {
        const math::vec3 p = mesh_position - offset;
        return math::vec3(math::dot(p, axis_x), math::dot(p, axis_y), math::dot(p, axis_z));
    }
};

struct clustering_params
{
    float voxel_size = 0.0f;
    float min_cluster_coverage = 0.0f;
    float min_outer_cluster_coverage = 0.0f;
    float min_density_per_cluster = 0.0f;
    std::array<direction_basis, k_direction_count> basis{};
};

struct surfel
{
    math::ivec3 coord{0};
    ///< The nearest card near plane (in cells) from which this surfel is visible.
    int min_ray_z = 0;
    ///< Fraction of the cell's rays that found this surface layer.
    float coverage = 0.0f;
    ///< Coverage weighted by how visible the surfel is from outside the mesh.
    float weighted_coverage = 0.0f;
};

struct surfel_sample
{
    math::vec3 position{0.0f};
    math::vec3 normal{0.0f};
    int min_ray_z = 0;
    int cell_z = 0;
};

struct surfel_cluster
{
    int_box bounds{};
    std::vector<uint32_t> surfels;
    int near_plane = 0;
    float coverage = 0.0f;
    float weighted_coverage = 0.0f;

    void reset(int plane)
    {
        bounds = int_box{};
        surfels.clear();
        near_plane = plane;
        coverage = 0.0f;
        weighted_coverage = 0.0f;
    }

    auto is_valid(const clustering_params& params) const -> bool
    {
        const float required = near_plane == 0 ? params.min_outer_cluster_coverage : params.min_cluster_coverage;
        return weighted_coverage >= required && get_density() > params.min_density_per_cluster;
    }

    auto get_density() const -> float
    {
        return surfels.empty() ? 0.0f : coverage / float(bounds.get_face_area());
    }

    void add(const std::vector<surfel>& all, uint32_t index)
    {
        const surfel& s = all[index];
        if(s.coord.z >= near_plane && s.min_ray_z <= near_plane)
        {
            surfels.push_back(index);
            bounds.add(s.coord);
            coverage += s.coverage;
            weighted_coverage += s.weighted_coverage;
        }
    }
};

struct build_context
{
    const mesh_ray::triangle_ray_caster& caster;
    const std::vector<math::vec3>& hemisphere;
    bool two_sided = false;
};

/// UE FAxisAlignedDirectionBasis::TransformSurfel: the mesh position of cell @p coord's surfel, centred in-plane on
/// the cell's front face.
auto get_surfel_position(const direction_basis& basis, const math::ivec3& coord) -> math::vec3
{
    return basis.to_mesh(math::vec3(float(coord.x) + 0.5f, float(coord.y) + 0.5f, float(coord.z)));
}

void init_clustering_params(const math::bbox& bounds, int max_voxels, clustering_params& params)
{
    const math::vec3 size = bounds.get_dimensions();
    const float max_size = std::max(size.x, std::max(size.y, size.z));
    const float max_size_in_voxels = std::clamp(max_size / k_target_voxel_size + 0.5f, 1.0f, float(max_voxels));
    const float voxel_size = std::max(k_target_voxel_size, max_size / max_size_in_voxels);
    const math::ivec3 size_in_voxels(int(std::clamp(std::round(size.x / voxel_size), 1.0f, float(max_voxels))),
                                     int(std::clamp(std::round(size.y / voxel_size), 1.0f, float(max_voxels))),
                                     int(std::clamp(std::round(size.z / voxel_size), 1.0f, float(max_voxels))));
    const math::vec3 center = bounds.get_center();
    const math::vec3 voxel_extent = math::vec3(size_in_voxels) * voxel_size * 0.5f;
    const math::vec3 voxel_min = center - voxel_extent;
    const math::vec3 voxel_max = center + voxel_extent;
    for(int direction = 0; direction < k_direction_count; ++direction)
    {
        direction_basis& basis = params.basis[size_t(direction)];
        const int axis = direction / 2;
        basis.voxel_size = voxel_size;
        if(axis == 0)
        {
            basis.axis_x = math::vec3(0.0f, 1.0f, 0.0f);
            basis.axis_y = math::vec3(0.0f, 0.0f, 1.0f);
            basis.volume_size = math::ivec3(size_in_voxels.y, size_in_voxels.z, size_in_voxels.x);
        }
        else if(axis == 1)
        {
            basis.axis_x = math::vec3(1.0f, 0.0f, 0.0f);
            basis.axis_y = math::vec3(0.0f, 0.0f, 1.0f);
            basis.volume_size = math::ivec3(size_in_voxels.x, size_in_voxels.z, size_in_voxels.y);
        }
        else
        {
            basis.axis_x = math::vec3(1.0f, 0.0f, 0.0f);
            basis.axis_y = math::vec3(0.0f, 1.0f, 0.0f);
            basis.volume_size = math::ivec3(size_in_voxels.x, size_in_voxels.y, size_in_voxels.z);
        }
        basis.axis_z = -get_direction_normal(direction);
        basis.offset = voxel_min;
        if((direction & 1) != 0)
        {
            basis.offset[axis] = voxel_max[axis];
        }
    }
    const float average_face_area =
        2.0f *
        float(size_in_voxels.x * size_in_voxels.y + size_in_voxels.x * size_in_voxels.z +
              size_in_voxels.y * size_in_voxels.z) /
        6.0f;
    params.voxel_size = voxel_size;
    params.min_density_per_cluster = k_min_density / 3.0f;
    params.min_cluster_coverage = k_min_cluster_coverage;
    params.min_outer_cluster_coverage = std::min(k_min_cluster_coverage, 0.5f * average_face_area);
}

/// The hit a one-sided build sees. A sheet modelled as two coincident, oppositely wound triangles (the engine plane
/// primitive) returns either triangle first; a back face with a front face within @p tolerance of its depth is that
/// front face, or half of such a sheet would read as back faces and get no card.
auto resolve_coincident_front_face(const build_context& context,
                                   const math::vec3& origin,
                                   const math::vec3& direction,
                                   const mesh_ray::ray_hit& hit,
                                   float tolerance) -> mesh_ray::ray_hit
{
    if(context.two_sided || math::dot(direction, context.caster.get_normal(hit.triangle)) <= 0.0f)
    {
        return hit;
    }
    const mesh_ray::ray_hit twin =
        context.caster.intersect(origin, direction, hit.t - tolerance, hit.triangle, hit.t + tolerance);
    const bool is_front_twin = twin.is_hit && math::dot(direction, context.caster.get_normal(twin.triangle)) < 0.0f;
    return is_front_twin ? twin : hit;
}

struct surfel_visibility
{
    float visibility = 0.0f;
    bool is_valid = false;
};

/// Hemisphere rays from the run's samples (cycled): the escaped fraction weighs the surfel, and a
/// run that mostly hits back faces is inside geometry and dropped.
auto compute_surfel_visibility(const build_context& context,
                               const std::vector<surfel_sample>& samples,
                               uint32_t offset,
                               uint32_t count) -> surfel_visibility
{
    uint32_t hits = 0;
    uint32_t back_face_hits = 0;
    float escaped = 0.0f;
    uint32_t sample_index = 0;
    for(const math::vec3& local_direction : context.hemisphere)
    {
        const surfel_sample& sample = samples[offset + sample_index];
        const math::vec3 direction = rotate_to_normal(sample.normal, local_direction);
        const mesh_ray::ray_hit first =
            context.caster.intersect(sample.position, direction, k_surface_ray_bias, std::numeric_limits<uint32_t>::max());
        if(first.is_hit)
        {
            const mesh_ray::ray_hit hit =
                resolve_coincident_front_face(context, sample.position, direction, first, k_coincident_face_depth);
            ++hits;
            if(math::dot(direction, context.caster.get_normal(hit.triangle)) > 0.0f && !context.two_sided)
            {
                ++back_face_hits;
            }
        }
        else
        {
            escaped += 1.0f;
        }
        sample_index = (sample_index + 1) % count;
    }
    const float ray_count = float(context.hemisphere.size());
    const bool is_inside = float(hits) > k_inside_hit_fraction * ray_count &&
                           float(back_face_hits) > k_inside_back_face_fraction * ray_count;
    return {escaped / ray_count, !is_inside};
}

/// Casts one cell's rays through every layer of the mesh, recording each hit that faces the side
/// and lies at least one empty cell beyond the previous hit.
void trace_cell(const build_context& context,
                const direction_basis& basis,
                int x,
                int y,
                std::vector<surfel_sample>& samples)
{
    const math::vec3 ray_direction = basis.axis_z;
    const float near_plane_offset = 2.0f * basis.voxel_size;
    for(uint32_t sample_index = 0; sample_index < k_rays_per_cell; ++sample_index)
    {
        const float jitter_x = (float(sample_index) + 0.5f) / float(k_rays_per_cell);
        const float jitter_y = float(double(reverse_bits(sample_index)) / 4294967296.0);
        // Pulled back two cells so the ray starts outside the geometry even where the grid's
        // rounding made it smaller than the mesh.
        const math::vec3 origin =
            basis.to_mesh(math::vec3(float(x) + jitter_x, float(y) + jitter_y, 0.0f)) - ray_direction * near_plane_offset;
        int last_hit_z = -2;
        uint32_t skip_triangle = std::numeric_limits<uint32_t>::max();
        float t_near = 0.0f;
        while(last_hit_z + 1 < basis.volume_size.z)
        {
            const mesh_ray::ray_hit first = context.caster.intersect(origin, ray_direction, t_near, skip_triangle);
            if(!first.is_hit)
            {
                break;
            }
            const mesh_ray::ray_hit hit =
                resolve_coincident_front_face(context, origin, ray_direction, first, k_coincident_face_depth);
            const int hit_z =
                int(std::clamp((hit.t - near_plane_offset) / basis.voxel_size, 0.0f, float(basis.volume_size.z - 1)));
            math::vec3 normal = context.caster.get_normal(hit.triangle);
            float n_dot_d = math::dot(-ray_direction, normal);
            if(n_dot_d < 0.0f && context.two_sided)
            {
                n_dot_d = -n_dot_d;
                normal = -normal;
            }
            if(n_dot_d >= k_normal_threshold && hit_z > last_hit_z + 1)
            {
                surfel_sample sample;
                sample.position = origin + ray_direction * hit.t;
                sample.normal = normal;
                sample.cell_z = hit_z;
                sample.min_ray_z = last_hit_z >= 0 ? last_hit_z + 1 : 0;
                samples.push_back(sample);
            }
            last_hit_z = hit_z;
            const float safe_t = std::max(hit.t, t_near);
            t_near = std::nextafter(std::max(near_plane_offset + float(last_hit_z + 1) * basis.voxel_size, safe_t),
                                    std::numeric_limits<float>::infinity());
            skip_triangle = hit.triangle;
        }
    }
}

void generate_surfels_for_direction(const build_context& context,
                                    const direction_basis& basis,
                                    std::vector<surfel>& out,
                                    std::vector<lumen_card_build_debug::surfel>* debug_surfels)
{
    out.clear();
    if(debug_surfels != nullptr)
    {
        debug_surfels->clear();
    }
    std::vector<surfel_sample> samples;
    std::vector<uint32_t> cell_count(size_t(basis.volume_size.z));
    std::vector<uint32_t> cell_offset(size_t(basis.volume_size.z));
    for(int y = 0; y < basis.volume_size.y; ++y)
    {
        for(int x = 0; x < basis.volume_size.x; ++x)
        {
            samples.clear();
            trace_cell(context, basis, x, y, samples);
            std::sort(samples.begin(),
                      samples.end(),
                      [](const surfel_sample& a, const surfel_sample& b)
                      {
                          if(a.cell_z != b.cell_z)
                          {
                              return a.cell_z < b.cell_z;
                          }
                          if(a.min_ray_z != b.min_ray_z)
                          {
                              return a.min_ray_z > b.min_ray_z;
                          }
                          if(a.position.x != b.position.x)
                          {
                              return a.position.x < b.position.x;
                          }
                          if(a.position.y != b.position.y)
                          {
                              return a.position.y < b.position.y;
                          }
                          return a.position.z < b.position.z;
                      });
            std::fill(cell_count.begin(), cell_count.end(), 0u);
            for(const surfel_sample& sample : samples)
            {
                ++cell_count[size_t(sample.cell_z)];
            }
            cell_offset[0] = 0;
            for(size_t z = 1; z < cell_offset.size(); ++z)
            {
                cell_offset[z] = cell_offset[z - 1] + cell_count[z - 1];
            }
            // Each run of equal min_ray_z in a cell spawns one surfel. A run needs at least two of
            // the cell's rays to start (the loop condition of UE's build), so a lone ray's hit
            // never makes a surfel.
            for(int z = 0; z < basis.volume_size.z; ++z)
            {
                const uint32_t count = cell_count[size_t(z)];
                const uint32_t offset = cell_offset[size_t(z)];
                uint32_t run_begin = 0;
                while(run_begin + 1 < count)
                {
                    const int run_min_ray_z = samples[offset + run_begin].min_ray_z;
                    uint32_t run_size = 0;
                    while(run_begin + run_size < count && samples[offset + run_begin + run_size].min_ray_z == run_min_ray_z)
                    {
                        ++run_size;
                    }
                    const surfel_visibility visibility = compute_surfel_visibility(context, samples, offset + run_begin, run_size);
                    if(visibility.is_valid)
                    {
                        surfel s;
                        s.coord = math::ivec3(x, y, z);
                        s.min_ray_z = run_min_ray_z;
                        s.coverage = float(run_size) / float(k_rays_per_cell);
                        s.weighted_coverage = s.coverage * (visibility.visibility + 1.0f);
                        out.push_back(s);
                    }
                    if(debug_surfels != nullptr)
                    {
                        debug_surfels->push_back({get_surfel_position(basis, math::ivec3(x, y, z)),
                                                  -basis.axis_z,
                                                  visibility.is_valid ? lumen_card_build_debug::surfel_type::valid
                                                                      : lumen_card_build_debug::surfel_type::invalid});
                    }
                    run_begin += run_size;
                }
            }
        }
    }
}

void build_cluster(int near_plane,
                   const std::vector<surfel>& surfels,
                   const std::vector<bool>& assigned,
                   surfel_cluster& cluster)
{
    cluster.reset(near_plane);
    for(uint32_t i = 0; i < uint32_t(surfels.size()); ++i)
    {
        if(!assigned[i])
        {
            cluster.add(surfels, i);
        }
    }
}

void commit_cluster(std::vector<surfel_cluster>& clusters, std::vector<bool>& assigned, const surfel_cluster& cluster)
{
    for(uint32_t index : cluster.surfels)
    {
        assigned[index] = true;
    }
    clusters.push_back(cluster);
}

/// The outer layer first; then, for one-sided meshes, the deeper near plane covering the most
/// remaining weight, until no valid layer is left.
void build_direction_clusters(const std::vector<surfel>& surfels,
                              const direction_basis& basis,
                              const clustering_params& params,
                              bool two_sided,
                              std::vector<surfel_cluster>& clusters)
{
    std::vector<bool> assigned(surfels.size(), false);
    surfel_cluster candidate;
    build_cluster(0, surfels, assigned, candidate);
    if(candidate.is_valid(params))
    {
        commit_cluster(clusters, assigned, candidate);
    }
    if(two_sided)
    {
        return;
    }
    surfel_cluster best;
    for(;;)
    {
        best.reset(-1);
        for(int near_plane = 1; near_plane < basis.volume_size.z; ++near_plane)
        {
            build_cluster(near_plane, surfels, assigned, candidate);
            if(candidate.is_valid(params) && candidate.weighted_coverage > best.weighted_coverage)
            {
                best = candidate;
            }
        }
        if(!best.is_valid(params))
        {
            return;
        }
        commit_cluster(clusters, assigned, best);
    }
}

/// Keeps the @p max_cards heaviest clusters: each side sorted by weight, then the lightest last
/// cluster over all sides is dropped until the total fits.
void limit_clusters(uint32_t max_cards, std::array<std::vector<surfel_cluster>, k_direction_count>& clusters)
{
    size_t total = 0;
    for(auto& side : clusters)
    {
        std::sort(side.begin(),
                  side.end(),
                  [](const surfel_cluster& a, const surfel_cluster& b)
                  {
                      if(a.weighted_coverage != b.weighted_coverage)
                      {
                          return a.weighted_coverage > b.weighted_coverage;
                      }
                      const math::vec3 ca = a.bounds.get_center();
                      const math::vec3 cb = b.bounds.get_center();
                      if(ca.x != cb.x)
                      {
                          return ca.x < cb.x;
                      }
                      if(ca.y != cb.y)
                      {
                          return ca.y < cb.y;
                      }
                      return ca.z < cb.z;
                  });
        total += side.size();
    }
    while(total > max_cards)
    {
        float smallest = std::numeric_limits<float>::max();
        size_t smallest_side = 0;
        for(size_t side = 0; side < clusters.size(); ++side)
        {
            if(!clusters[side].empty() && clusters[side].back().weighted_coverage < smallest)
            {
                smallest = clusters[side].back().weighted_coverage;
                smallest_side = side;
            }
        }
        clusters[smallest_side].pop_back();
        --total;
    }
}

/// A cluster's card box: its cells, half a cell in front of the nearest layer and behind the last,
/// clamped in-plane to the mesh bounds and in depth to the bounds plus the depth margin.
auto make_card(const surfel_cluster& cluster,
               const direction_basis& basis,
               const math::bbox& bounds,
               int direction) -> lumen_card
{
    math::vec3 local_min(std::numeric_limits<float>::max());
    math::vec3 local_max(-std::numeric_limits<float>::max());
    for(int corner = 0; corner < 8; ++corner)
    {
        const math::vec3 p((corner & 1) != 0 ? bounds.max.x : bounds.min.x,
                           (corner & 2) != 0 ? bounds.max.y : bounds.min.y,
                           (corner & 4) != 0 ? bounds.max.z : bounds.min.z);
        const math::vec3 local = basis.to_local(p);
        local_min = math::min(local_min, local);
        local_max = math::max(local_max, local);
    }
    math::vec3 card_min = (math::vec3(cluster.bounds.min) - math::vec3(0.0f, 0.0f, 0.5f)) * basis.voxel_size;
    math::vec3 card_max = (math::vec3(cluster.bounds.max) + math::vec3(1.0f, 1.0f, 1.5f)) * basis.voxel_size;
    card_min.x = std::max(card_min.x, local_min.x);
    card_min.y = std::max(card_min.y, local_min.y);
    card_min.z = std::max(card_min.z, local_min.z - k_card_margin_z);
    card_max.x = std::min(card_max.x, local_max.x);
    card_max.y = std::min(card_max.y, local_max.y);
    card_max.z = std::min(card_max.z, local_max.z + k_card_margin_z);
    const math::vec3 local_origin = (card_max + card_min) * 0.5f;
    lumen_card card;
    card.origin = basis.axis_x * local_origin.x + basis.axis_y * local_origin.y + basis.axis_z * local_origin.z + basis.offset;
    card.extent = (card_max - card_min) * 0.5f;
    card.axis_x = basis.axis_x;
    card.axis_y = basis.axis_y;
    card.axis_z = -basis.axis_z;
    card.direction = uint32_t(direction);
    return card;
}

/// UE SerializeLOD's debug cluster of a card: the cluster's surfels with a ray down to the near plane each is seen
/// from, then the side's other surfels, used when an earlier card of the side took them (@p is_in_any_cluster, which
/// gains this cluster's surfels).
void append_cluster_debug(const surfel_cluster& cluster,
                          const std::vector<surfel>& surfels,
                          const direction_basis& basis,
                          int direction,
                          std::vector<bool>& is_in_any_cluster,
                          lumen_card_build_debug& debug)
{
    using surfel_type = lumen_card_build_debug::surfel_type;
    auto& entry = debug.clusters.emplace_back();
    const math::vec3 normal = get_direction_normal(direction);
    std::vector<bool> is_in_cluster(surfels.size(), false);
    for(uint32_t index : cluster.surfels)
    {
        const surfel& s = surfels[index];
        const math::vec3 position = get_surfel_position(basis, s.coord);
        entry.surfels.push_back({position, normal, surfel_type::cluster});
        if(s.min_ray_z > 0)
        {
            const math::ivec3 near_coord(s.coord.x, s.coord.y, s.min_ray_z);
            entry.rays.push_back({position, get_surfel_position(basis, near_coord), false});
        }
        is_in_any_cluster[index] = true;
        is_in_cluster[index] = true;
    }
    for(uint32_t index = 0; index < uint32_t(surfels.size()); ++index)
    {
        if(!is_in_cluster[index])
        {
            entry.surfels.push_back({get_surfel_position(basis, surfels[index].coord),
                                     normal,
                                     is_in_any_cluster[index] ? surfel_type::used : surfel_type::idle});
        }
    }
}

} // namespace

auto build_lumen_mesh_cards(const sdf_source_geometry& geometry,
                            bool two_sided,
                            uint32_t max_cards,
                            lumen_mesh_cards& out,
                            lumen_card_build_debug* debug) -> bool
{
    if(debug != nullptr)
    {
        *debug = lumen_card_build_debug{};
    }
    out = lumen_mesh_cards{};
    out.is_mostly_two_sided = two_sided;
    if(!geometry.is_valid() || max_cards == 0)
    {
        return false;
    }
    // A plane's bounds are flat: grow them so every side has a grid to cast from.
    const math::vec3 center = geometry.bounds.get_center();
    const math::vec3 extent = math::max(geometry.bounds.get_extents() + math::vec3(k_bounds_margin), math::vec3(k_bounds_margin));
    out.bounds = math::bbox(center - extent, center + extent);
    mesh_ray::triangle_ray_caster caster;
    caster.build(geometry);
    const std::vector<math::vec3> hemisphere = generate_hemisphere_directions();
    const build_context context{caster, hemisphere, two_sided};
    clustering_params params;
    std::array<std::vector<surfel>, k_direction_count> surfels;
    // The last attempt's candidates per side (UE keeps the debug surfels of the grid it settles on).
    std::array<std::vector<lumen_card_build_debug::surfel>, k_direction_count> debug_surfels;
    // Dense two-sided meshes make far more surfels than walls; coarsen the grid until the count
    // is affordable.
    int max_voxels = k_max_voxels;
    size_t surfel_count = 0;
    do
    {
        init_clustering_params(out.bounds, max_voxels, params);
        surfel_count = 0;
        for(int direction = 0; direction < k_direction_count; ++direction)
        {
            generate_surfels_for_direction(context,
                                           params.basis[size_t(direction)],
                                           surfels[size_t(direction)],
                                           debug != nullptr ? &debug_surfels[size_t(direction)] : nullptr);
            surfel_count += surfels[size_t(direction)].size();
        }
        max_voxels /= 2;
    } while(surfel_count > size_t(k_target_surfel_count) && max_voxels > 1);
    if(debug != nullptr)
    {
        for(const auto& side : debug_surfels)
        {
            debug->surfels.insert(debug->surfels.end(), side.begin(), side.end());
        }
    }
    std::array<std::vector<surfel_cluster>, k_direction_count> clusters;
    for(int direction = 0; direction < k_direction_count; ++direction)
    {
        build_direction_clusters(surfels[size_t(direction)],
                                 params.basis[size_t(direction)],
                                 params,
                                 two_sided,
                                 clusters[size_t(direction)]);
    }
    limit_clusters(max_cards, clusters);
    for(int direction = 0; direction < k_direction_count; ++direction)
    {
        const auto& basis = params.basis[size_t(direction)];
        std::vector<bool> is_in_any_cluster(surfels[size_t(direction)].size(), false);
        for(const surfel_cluster& cluster : clusters[size_t(direction)])
        {
            out.cards.push_back(make_card(cluster, basis, out.bounds, direction));
            if(debug != nullptr)
            {
                append_cluster_debug(cluster, surfels[size_t(direction)], basis, direction, is_in_any_cluster, *debug);
            }
        }
    }
    return true;
}

} // namespace unravel
