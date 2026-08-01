/*
 * Validation harness for the surface cache GI bake (USC-GI Phase 1).
 *
 * Not part of the default build. Run it explicitly:
 *   cmake --build <build-dir> --target gi_tests
 *   <build-dir>/bin/gi_tests
 *
 * These are correctness invariants, not smoke tests. The two that matter most are
 * test_conservative_empty_bricks (a sphere trace tunnels through geometry if an empty brick
 * ever over-estimates its distance) and test_brick_seam_continuity (a wrong filter border
 * puts a discontinuity in the field at every brick boundary). Both caught real bugs.
 */

#include <engine/meta/rendering/gi/mesh_sdf.hpp>
#include <engine/rendering/gi/global_sdf_clipmap.h>
#include <engine/rendering/gi/mesh_sdf_baker.h>
#include <engine/rendering/gi/mesh_sdf_source.h>
#include <engine/rendering/gi/radiance_cache.h>
#include <engine/rendering/gi/sdf_instance_grid.h>
// For submesh_pose_mat4: the surface cache places a field wherever model::submit draws the
// submesh, so the two read the same pose structure.
#include <engine/rendering/model.h>

#include <serialization/binary_archive.h>

#include <poolstl/poolstl.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

using namespace unravel;

namespace
{

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void check_near(float actual, float expected, float tolerance, const std::string& what)
{
    ++g_checks;
    if(!(std::fabs(actual - expected) <= tolerance))
    {
        ++g_failures;
        std::printf("  FAIL: %s (got %.4f, expected %.4f +/- %.4f)\n", what.c_str(), actual, expected, tolerance);
    }
}

/// Appends a quad as two triangles. Callers wind them counter-clockwise seen from outside.
void add_quad(sdf_source_geometry& g,
              const math::vec3& a,
              const math::vec3& b,
              const math::vec3& c,
              const math::vec3& d)
{
    const uint32_t base = uint32_t(g.positions.size());
    g.positions.push_back(a);
    g.positions.push_back(b);
    g.positions.push_back(c);
    g.positions.push_back(d);
    g.indices.push_back(base + 0);
    g.indices.push_back(base + 1);
    g.indices.push_back(base + 2);
    g.indices.push_back(base + 0);
    g.indices.push_back(base + 2);
    g.indices.push_back(base + 3);
}

void recompute_bounds(sdf_source_geometry& g)
{
    g.bounds.reset();
    for(const auto& p : g.positions)
    {
        g.bounds.add_point(p);
    }
}

auto make_box(const math::vec3& half_extents) -> sdf_source_geometry
{
    sdf_source_geometry g;
    const float x = half_extents.x;
    const float y = half_extents.y;
    const float z = half_extents.z;
    add_quad(g, {x, -y, -z}, {x, y, -z}, {x, y, z}, {x, -y, z});
    add_quad(g, {-x, -y, z}, {-x, y, z}, {-x, y, -z}, {-x, -y, -z});
    add_quad(g, {-x, y, -z}, {-x, y, z}, {x, y, z}, {x, y, -z});
    add_quad(g, {-x, -y, z}, {-x, -y, -z}, {x, -y, -z}, {x, -y, z});
    add_quad(g, {-x, -y, z}, {x, -y, z}, {x, y, z}, {-x, y, z});
    add_quad(g, {x, -y, -z}, {-x, -y, -z}, {-x, y, -z}, {x, y, -z});
    recompute_bounds(g);
    return g;
}

/// A box with one face missing, so the surface is open. Scanned and hand-modelled props are
/// frequently open like this, and the inside/outside test is undefined on them.
auto make_open_box(const math::vec3& half_extents) -> sdf_source_geometry
{
    sdf_source_geometry g;
    const float x = half_extents.x;
    const float y = half_extents.y;
    const float z = half_extents.z;
    add_quad(g, {x, -y, -z}, {x, y, -z}, {x, y, z}, {x, -y, z});
    add_quad(g, {-x, -y, z}, {-x, y, z}, {-x, y, -z}, {-x, -y, -z});
    add_quad(g, {-x, y, -z}, {-x, y, z}, {x, y, z}, {x, y, -z});
    add_quad(g, {-x, -y, z}, {-x, -y, -z}, {x, -y, -z}, {x, -y, z});
    add_quad(g, {-x, -y, z}, {x, -y, z}, {x, y, z}, {-x, y, z});
    // The -Z face is deliberately omitted.
    recompute_bounds(g);
    return g;
}

auto make_sphere(float radius, int rings, int sectors, bool invert_winding = false) -> sdf_source_geometry
{
    sdf_source_geometry g;
    const float pi = 3.14159265358979323846f;
    for(int r = 0; r <= rings; ++r)
    {
        const float theta = pi * float(r) / float(rings);
        for(int s = 0; s <= sectors; ++s)
        {
            const float phi = 2.0f * pi * float(s) / float(sectors);
            g.positions.push_back(math::vec3(radius * std::sin(theta) * std::cos(phi),
                                             radius * std::cos(theta),
                                             radius * std::sin(theta) * std::sin(phi)));
        }
    }
    const int stride = sectors + 1;
    for(int r = 0; r < rings; ++r)
    {
        for(int s = 0; s < sectors; ++s)
        {
            const uint32_t i0 = uint32_t(r * stride + s);
            const uint32_t i1 = uint32_t(r * stride + s + 1);
            const uint32_t i2 = uint32_t((r + 1) * stride + s + 1);
            const uint32_t i3 = uint32_t((r + 1) * stride + s);
            // Wound counter-clockwise seen from OUTSIDE, so face normals point outward and
            // the bake reads the interior as solid.
            if(invert_winding)
            {
                g.indices.insert(g.indices.end(), {i0, i3, i2, i0, i2, i1});
            }
            else
            {
                g.indices.insert(g.indices.end(), {i0, i2, i3, i0, i1, i2});
            }
        }
    }
    recompute_bounds(g);
    return g;
}

/// Analytic signed distance to an axis-aligned box centred on the origin.
auto box_distance(const math::vec3& p, const math::vec3& half_extents) -> float
{
    const math::vec3 q = math::abs(p) - half_extents;
    const float outside = math::length(math::max(q, math::vec3(0.0f)));
    const float inside = math::min(math::max(q.x, math::max(q.y, q.z)), 0.0f);
    return outside + inside;
}

// ---------------------------------------------------------------------------------------

void test_sphere_accuracy()
{
    std::printf("test_sphere_accuracy\n");
    const float radius = 1.0f;
    const auto geometry = make_sphere(radius, 32, 48);
    mesh_sdf_bake_settings settings;
    settings.resolution = 48;
    settings.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), "sphere bake succeeds");
    check(sdf.is_valid(), "sphere field is valid");
    // Accuracy is only claimed inside the narrow band. Past mesh_sdf::encode_range voxels the
    // storage saturates on purpose (see the contract on mesh_sdf::encode_range), so comparing
    // against the analytic distance out there measures the encoding, not a defect.
    const float band = (mesh_sdf::encode_range - 1.0f) * sdf.voxel_size;
    const float tolerance = 2.0f * sdf.voxel_size;
    double sum_sq = 0.0;
    int samples = 0;
    int outliers = 0;
    for(int i = 0; i < 20000; ++i)
    {
        const float t = float(i) / 20000.0f;
        const math::vec3 dir =
            math::normalize(math::vec3(std::sin(t * 31.0f), std::cos(t * 17.0f), std::sin(t * 7.0f) + 0.3f));
        const float r = 0.2f + 1.4f * t;
        const math::vec3 p = dir * r;
        const float expected = r - radius;
        if(std::fabs(expected) > band)
        {
            continue;
        }
        const float actual = sample_mesh_sdf(sdf, p);
        sum_sq += double(actual - expected) * double(actual - expected);
        ++samples;
        if(std::fabs(actual - expected) > tolerance)
        {
            ++outliers;
        }
    }
    const double rmse = std::sqrt(sum_sq / double(math::max(samples, 1)));
    std::printf("  in-band samples = %d, RMSE = %.5f, voxel = %.5f, outliers = %d\n",
                samples,
                rmse,
                sdf.voxel_size,
                outliers);
    check(samples > 500, "enough in-band samples to be meaningful");
    check(rmse < double(sdf.voxel_size), "sphere RMSE below one voxel inside the band");
    check(outliers == 0, "no in-band sample deviates by more than two voxels");
}

void test_field_is_conservative()
{
    std::printf("test_field_is_conservative\n");
    // The property sphere tracing actually depends on, covering the VOXELS (the empty-brick
    // equivalent is test_conservative_empty_bricks): a sampled magnitude must never exceed
    // the true distance. Over-estimating anywhere lets a trace step past a surface, which is
    // precisely how light leaks through walls. Under-estimating is always safe.
    //
    // A box is used rather than a sphere because a box is represented exactly by its
    // triangles, so the analytic distance is ground truth with no tessellation error.
    const math::vec3 half(0.5f, 0.35f, 0.6f);
    const auto geometry = make_box(half);
    mesh_sdf_bake_settings settings;
    settings.resolution = 48;
    settings.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), "box bake succeeds");
    // Allowance for 8-bit quantisation of the stored voxels plus trilinear reconstruction.
    const float slack = 0.75f * sdf.voxel_size;
    int over_estimates = 0;
    float worst_excess = 0.0f;
    for(int i = 0; i < 30000; ++i)
    {
        const float t = float(i) / 30000.0f;
        const math::vec3 p(half.x * 2.5f * std::sin(t * 53.0f),
                           half.y * 2.5f * std::cos(t * 29.0f),
                           half.z * 2.5f * std::sin(t * 11.0f));
        const float truth = std::fabs(box_distance(p, half));
        const float actual = std::fabs(sample_mesh_sdf(sdf, p));
        if(actual > truth + slack)
        {
            ++over_estimates;
            worst_excess = math::max(worst_excess, actual - truth);
        }
    }
    std::printf("  over-estimates = %d, worst excess = %.5f, slack = %.5f\n",
                over_estimates,
                worst_excess,
                slack);
    check(over_estimates == 0, "the field never over-estimates the distance to the surface");
}

void test_sign_correctness()
{
    std::printf("test_sign_correctness\n");
    const math::vec3 half(0.5f, 0.3f, 0.7f);
    const auto geometry = make_box(half);
    mesh_sdf_bake_settings settings;
    settings.resolution = 48;
    settings.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), "box bake succeeds");
    // Wrong pseudonormal selection flips the sign of voxels near edges and corners, and a
    // wrongly signed voxel makes the GI tracer treat solid geometry as empty space.
    int wrong_sign = 0;
    for(int i = 0; i < 4000; ++i)
    {
        const float t = float(i) / 4000.0f;
        const math::vec3 p(half.x * 2.0f * std::sin(t * 53.0f),
                           half.y * 2.0f * std::cos(t * 29.0f),
                           half.z * 2.0f * std::sin(t * 11.0f));
        const float expected = box_distance(p, half);
        // Skip the band within two voxels of the surface, where a sign flip is meaningless.
        if(std::fabs(expected) < 2.0f * sdf.voxel_size)
        {
            continue;
        }
        if((sample_mesh_sdf(sdf, p) < 0.0f) != (expected < 0.0f))
        {
            ++wrong_sign;
        }
    }
    std::printf("  wrong-sign samples = %d\n", wrong_sign);
    check(wrong_sign == 0, "no wrongly signed samples on a closed box");
}

void test_conservative_empty_bricks()
{
    std::printf("test_conservative_empty_bricks\n");
    const math::vec3 half(0.4f, 0.4f, 0.4f);
    const auto geometry = make_box(half);
    mesh_sdf_bake_settings settings;
    settings.resolution = 64;
    settings.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), "box bake succeeds");
    // THE tracing safety invariant. An empty brick's stored distance must never exceed the
    // true distance to the surface from ANY point in that brick: if it over-estimates, a
    // sphere trace takes too large a step and passes straight through geometry, which shows
    // up as light leaking through walls.
    const float brick_world_size = float(mesh_sdf::brick_size) * sdf.voxel_size;
    int violations = 0;
    int empty_bricks = 0;
    for(uint32_t bz = 0; bz < sdf.brick_dim.z; ++bz)
    {
        for(uint32_t by = 0; by < sdf.brick_dim.y; ++by)
        {
            for(uint32_t bx = 0; bx < sdf.brick_dim.x; ++bx)
            {
                const uint32_t index = bx + by * sdf.brick_dim.x + bz * sdf.brick_dim.x * sdf.brick_dim.y;
                const uint32_t entry = sdf.indirection[index];
                if(!is_sdf_empty_entry(entry))
                {
                    continue;
                }
                ++empty_bricks;
                const float stored = float(entry & mesh_sdf::indirection_distance_mask) * sdf.voxel_size;
                const math::vec3 origin =
                    sdf.bounds.min + math::vec3(float(bx), float(by), float(bz)) * brick_world_size;
                for(int corner = 0; corner < 8; ++corner)
                {
                    const math::vec3 offset(float(corner & 1), float((corner >> 1) & 1), float((corner >> 2) & 1));
                    const float truth = std::fabs(box_distance(origin + offset * brick_world_size, half));
                    if(stored > truth + 1e-4f)
                    {
                        ++violations;
                    }
                }
            }
        }
    }
    std::printf("  empty bricks = %d, violations = %d\n", empty_bricks, violations);
    check(empty_bricks > 0, "the box produced empty bricks (sparsity actually happens)");
    check(violations == 0, "no empty brick over-estimates its distance to the surface");
}

void test_brick_seam_continuity()
{
    std::printf("test_brick_seam_continuity\n");
    const auto geometry = make_sphere(1.0f, 32, 48);
    mesh_sdf_bake_settings settings;
    settings.resolution = 48;
    settings.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), "sphere bake succeeds");
    // Walk a dense line through the field, crossing many brick boundaries. A distance field
    // has |gradient| == 1, so the change per step cannot exceed the step length; a missing or
    // mis-addressed filter border shows up as a step far larger than that.
    //
    // Continuity is only required INSIDE the narrow band. Where a surface brick meets an
    // empty one the field steps by design (see the contract on mesh_sdf::encode_range), so
    // consecutive samples are only compared when both are strictly inside the band -- which
    // is exactly the region where every brick is guaranteed to be a surface brick.
    const int steps = 20000;
    const float span = 2.6f;
    const float step_length = span / float(steps);
    const float allowed_jump = 3.0f * step_length + 0.5f * sdf.voxel_size;
    const float band = (mesh_sdf::encode_range - 1.0f) * sdf.voxel_size;
    float previous = sample_mesh_sdf(sdf, math::vec3(-span * 0.5f, 0.021f, 0.013f));
    float worst_jump = 0.0f;
    int compared = 0;
    for(int i = 1; i <= steps; ++i)
    {
        const float x = -span * 0.5f + span * float(i) / float(steps);
        const float current = sample_mesh_sdf(sdf, math::vec3(x, 0.021f, 0.013f));
        if(std::fabs(current) < band && std::fabs(previous) < band)
        {
            worst_jump = math::max(worst_jump, std::fabs(current - previous));
            ++compared;
        }
        previous = current;
    }
    std::printf("  compared = %d, worst jump = %.5f, allowed = %.5f, voxel = %.5f\n",
                compared,
                worst_jump,
                allowed_jump,
                sdf.voxel_size);
    check(compared > 100, "enough in-band pairs to be meaningful");
    check(worst_jump <= allowed_jump, "no discontinuity at brick seams inside the band");
}

void test_two_sided_shell()
{
    std::printf("test_two_sided_shell\n");
    // A single quad is not a closed surface, so a signed bake is meaningless here.
    sdf_source_geometry g;
    add_quad(g, {-1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 1.0f});
    recompute_bounds(g);
    mesh_sdf_bake_settings settings;
    settings.resolution = 32;
    settings.min_voxel_size = 0.001f;
    settings.two_sided = true;
    settings.two_sided_thickness = 0.05f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(g, settings, sdf), "two-sided quad bake succeeds");
    check(sdf.is_two_sided, "field is flagged two sided");
    // The shell must read solid from both sides, so a ray arriving from either is occluded.
    check(sample_mesh_sdf(sdf, math::vec3(0.0f, 0.0f, 0.0f)) < 0.0f, "shell centre is inside");
    check(sample_mesh_sdf(sdf, math::vec3(0.0f, 0.02f, 0.0f)) < 0.0f, "shell is solid above the quad");
    check(sample_mesh_sdf(sdf, math::vec3(0.0f, -0.02f, 0.0f)) < 0.0f, "shell is solid below the quad");
    check(sample_mesh_sdf(sdf, math::vec3(0.0f, 0.4f, 0.0f)) > 0.0f, "above the shell is outside");
    check(sample_mesh_sdf(sdf, math::vec3(0.0f, -0.4f, 0.0f)) > 0.0f, "below the shell is outside");
}

void test_thin_wall()
{
    std::printf("test_thin_wall\n");
    // The no-light-leaking case: a 10 cm wall must stay solid, not be averaged away.
    const math::vec3 half(1.5f, 1.5f, 0.05f);
    const auto geometry = make_box(half);
    mesh_sdf_bake_settings settings;
    settings.resolution = 64;
    settings.min_voxel_size = 0.005f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), "thin wall bake succeeds");
    std::printf("  voxel = %.4f, wall half-thickness = %.4f\n", sdf.voxel_size, half.z);
    check(sdf.voxel_size <= half.z, "voxel size resolves the wall thickness");
    check(sample_mesh_sdf(sdf, math::vec3(0.0f, 0.0f, 0.0f)) < 0.0f, "wall centre is solid");
    check(sample_mesh_sdf(sdf, math::vec3(0.5f, -0.4f, 0.0f)) < 0.0f, "wall is solid away from centre");
    check(sample_mesh_sdf(sdf, math::vec3(0.0f, 0.0f, 0.5f)) > 0.0f, "front of the wall is open");
    check(sample_mesh_sdf(sdf, math::vec3(0.0f, 0.0f, -0.5f)) > 0.0f, "back of the wall is open");
}

void test_inverted_winding_is_corrected()
{
    std::printf("test_inverted_winding_is_corrected\n");
    // Mirrored props and negative-scale exports routinely arrive with inverted winding. The
    // sign of the field comes from winding, so without the orientation safeguard such a mesh
    // bakes inside-out: its interior reads as empty and it silently stops occluding, which
    // looks exactly like light leaking through it. Both windings must bake the same field.
    const auto outward = make_sphere(1.0f, 24, 32, false);
    const auto inward = make_sphere(1.0f, 24, 32, true);
    mesh_sdf_bake_settings settings;
    settings.resolution = 32;
    settings.min_voxel_size = 0.001f;
    mesh_sdf sdf_outward;
    mesh_sdf sdf_inward;
    check(bake_mesh_sdf(outward, settings, sdf_outward), "outward-wound bake succeeds");
    check(bake_mesh_sdf(inward, settings, sdf_inward), "inward-wound bake succeeds");
    check(sdf_outward.brick_voxels == sdf_inward.brick_voxels, "winding does not change the voxels");
    check(sdf_outward.indirection == sdf_inward.indirection, "winding does not change the indirection");
    check(sample_mesh_sdf(sdf_outward, math::vec3(0.0f)) < 0.0f, "outward-wound sphere centre is solid");
    check(sample_mesh_sdf(sdf_inward, math::vec3(0.0f)) < 0.0f, "inward-wound sphere centre is solid");
}

/**
 * @brief Mirror of the GPU atlas + sampling path, run on the CPU.
 *
 * The atlas upload rewrites mesh-local brick indices into absolute atlas slots and scatters
 * bricks into a 3D texture, and the shader has to invert exactly that packing. An error in
 * either half samples unrelated voxels, which on screen looks like noise rather than like a
 * wrong address, so it is close to undiagnosable from a screenshot.
 *
 * This models the atlas as a flat array and reimplements the shader's addressing verbatim,
 * then requires the result to agree with sample_mesh_sdf, which the tests above pin down.
 */
class simulated_atlas
{
public:
    /// Matches sdf_atlas::settings::atlas_brick_dim; small here because the test only needs
    /// enough slots for one field, and a 320^3 allocation per test would be wasteful.
    static constexpr uint32_t atlas_brick_dim = 6;

    auto upload(const mesh_sdf& sdf) -> bool
    {
        const uint32_t voxel_dim = atlas_brick_dim * mesh_sdf::brick_stride;
        voxels_.assign(size_t(voxel_dim) * voxel_dim * voxel_dim, 0u);
        const uint32_t surface_bricks = sdf.get_surface_brick_count();
        if(surface_bricks > atlas_brick_dim * atlas_brick_dim * atlas_brick_dim)
        {
            return false;
        }
        // Slots are handed out in order, exactly as sdf_atlas::allocate_brick does from an
        // empty free list.
        std::vector<uint32_t> slots(surface_bricks);
        for(uint32_t i = 0; i < surface_bricks; ++i)
        {
            slots[i] = i;
        }
        indirection_.resize(sdf.indirection.size());
        for(size_t i = 0; i < sdf.indirection.size(); ++i)
        {
            const uint32_t entry = sdf.indirection[i];
            indirection_[i] = is_sdf_empty_entry(entry) ? entry : make_sdf_surface_entry(slots[entry]);
        }
        for(uint32_t i = 0; i < surface_bricks; ++i)
        {
            write_brick(slots[i], sdf.brick_voxels.data() + size_t(i) * mesh_sdf::brick_voxel_count);
        }
        return true;
    }

    /// Verbatim transcription of SdfSampleLocal in gi/sdf_common.sh.
    auto sample(const mesh_sdf& sdf, const math::vec3& local_position) const -> float
    {
        const float voxel_dim = float(atlas_brick_dim * mesh_sdf::brick_stride);
        const math::vec3 grid = (local_position - sdf.bounds.min) / sdf.voxel_size;
        const math::vec3 grid_dim(float(sdf.grid_dim.x), float(sdf.grid_dim.y), float(sdf.grid_dim.z));
        const math::vec3 clamped_grid = math::clamp(grid, math::vec3(0.0f), grid_dim);
        const float outside_distance = math::length((grid - clamped_grid) * sdf.voxel_size);
        if(outside_distance > 0.0f)
        {
            return outside_distance;
        }
        const math::vec3 brick_dim(float(sdf.brick_dim.x), float(sdf.brick_dim.y), float(sdf.brick_dim.z));
        const math::vec3 brick_coord =
            math::clamp(math::floor(grid / float(mesh_sdf::brick_size)), math::vec3(0.0f),
                        brick_dim - math::vec3(1.0f));
        const uint32_t brick_index = uint32_t(brick_coord.x) +
                                     uint32_t(brick_coord.y) * uint32_t(brick_dim.x) +
                                     uint32_t(brick_coord.z) * uint32_t(brick_dim.x) * uint32_t(brick_dim.y);
        const uint32_t entry = indirection_[brick_index];
        if(is_sdf_empty_entry(entry))
        {
            const float distance = float(entry & mesh_sdf::indirection_distance_mask) * sdf.voxel_size;
            return (entry & mesh_sdf::indirection_inside_flag) != 0u ? -distance : distance;
        }
        const float slot = float(entry);
        const float brick_dim_f = float(atlas_brick_dim);
        math::vec3 slot_coord;
        slot_coord.x = std::fmod(slot, brick_dim_f);
        slot_coord.y = std::fmod(std::floor(slot / brick_dim_f), brick_dim_f);
        slot_coord.z = std::floor(slot / (brick_dim_f * brick_dim_f));
        const math::vec3 brick_local = grid - brick_coord * float(mesh_sdf::brick_size);
        const math::vec3 atlas_coord = slot_coord * float(mesh_sdf::brick_stride) + brick_local +
                                       math::vec3(float(mesh_sdf::brick_border));
        const float encoded = sample_trilinear(atlas_coord, voxel_dim);
        const float distance_voxels = (encoded - 0.5f) * (2.0f * mesh_sdf::encode_range);
        // No two_sided_thickness term: the bake already applied it to the stored voxels.
        return distance_voxels * sdf.voxel_size;
    }

private:
    void write_brick(uint32_t slot, const uint8_t* brick)
    {
        const uint32_t voxel_dim = atlas_brick_dim * mesh_sdf::brick_stride;
        const uint32_t bx = slot % atlas_brick_dim;
        const uint32_t by = (slot / atlas_brick_dim) % atlas_brick_dim;
        const uint32_t bz = slot / (atlas_brick_dim * atlas_brick_dim);
        for(uint32_t lz = 0; lz < mesh_sdf::brick_stride; ++lz)
        {
            for(uint32_t ly = 0; ly < mesh_sdf::brick_stride; ++ly)
            {
                for(uint32_t lx = 0; lx < mesh_sdf::brick_stride; ++lx)
                {
                    const uint32_t ax = bx * mesh_sdf::brick_stride + lx;
                    const uint32_t ay = by * mesh_sdf::brick_stride + ly;
                    const uint32_t az = bz * mesh_sdf::brick_stride + lz;
                    const uint32_t src =
                        lx + ly * mesh_sdf::brick_stride + lz * mesh_sdf::brick_stride * mesh_sdf::brick_stride;
                    voxels_[size_t(ax) + size_t(ay) * voxel_dim + size_t(az) * voxel_dim * voxel_dim] = brick[src];
                }
            }
        }
    }

    /// Hardware trilinear over the atlas: texel i covers [i, i+1) with its centre at i + 0.5.
    auto sample_trilinear(const math::vec3& atlas_coord, float voxel_dim) const -> float
    {
        const math::vec3 t = atlas_coord - math::vec3(0.5f);
        const math::ivec3 base = math::ivec3(math::floor(t));
        const math::vec3 frac = t - math::vec3(base);
        const auto fetch = [&](int x, int y, int z) -> float
        {
            const int dim = int(voxel_dim);
            const int cx = math::clamp(x, 0, dim - 1);
            const int cy = math::clamp(y, 0, dim - 1);
            const int cz = math::clamp(z, 0, dim - 1);
            return float(voxels_[size_t(cx) + size_t(cy) * dim + size_t(cz) * size_t(dim) * dim]) / 255.0f;
        };
        const float c00 = math::mix(fetch(base.x, base.y, base.z), fetch(base.x + 1, base.y, base.z), frac.x);
        const float c10 =
            math::mix(fetch(base.x, base.y + 1, base.z), fetch(base.x + 1, base.y + 1, base.z), frac.x);
        const float c01 =
            math::mix(fetch(base.x, base.y, base.z + 1), fetch(base.x + 1, base.y, base.z + 1), frac.x);
        const float c11 =
            math::mix(fetch(base.x, base.y + 1, base.z + 1), fetch(base.x + 1, base.y + 1, base.z + 1), frac.x);
        return math::mix(math::mix(c00, c10, frac.y), math::mix(c01, c11, frac.y), frac.z);
    }

    std::vector<uint8_t> voxels_;
    std::vector<uint32_t> indirection_;
};

void check_gpu_addressing_matches_cpu(const sdf_source_geometry& geometry,
                                      const mesh_sdf_bake_settings& settings,
                                      const std::string& label)
{
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), label + ": bake succeeds");
    simulated_atlas atlas;
    check(atlas.upload(sdf), label + ": field fits the simulated atlas");
    std::printf("  %s: bricks = %u, brick_dim = %ux%ux%u, two_sided = %d\n",
                label.c_str(),
                sdf.get_surface_brick_count(),
                sdf.brick_dim.x,
                sdf.brick_dim.y,
                sdf.brick_dim.z,
                int(sdf.is_two_sided));
    // Both paths reconstruct the same trilinear filter, so agreement should be tight; the
    // slack only covers the different clamp behaviour right at the field boundary.
    const float tolerance = 0.05f * sdf.voxel_size;
    int mismatches = 0;
    float worst = 0.0f;
    const math::vec3 span = sdf.bounds.get_dimensions();
    for(int i = 0; i < 40000; ++i)
    {
        const float t = float(i) / 40000.0f;
        const math::vec3 unit(0.5f + 0.5f * std::sin(t * 91.0f),
                              0.5f + 0.5f * std::cos(t * 57.0f),
                              0.5f + 0.5f * std::sin(t * 33.0f));
        const math::vec3 p = sdf.bounds.min + unit * span;
        const float reference = sample_mesh_sdf(sdf, p);
        const float actual = atlas.sample(sdf, p);
        if(std::fabs(actual - reference) > tolerance)
        {
            if(mismatches < 5)
            {
                std::printf("    at (%.3f %.3f %.3f): reference %.5f, atlas %.5f\n",
                            p.x, p.y, p.z, reference, actual);
            }
            ++mismatches;
            worst = math::max(worst, std::fabs(actual - reference));
        }
    }
    std::printf("  mismatches = %d, worst = %.6f, tolerance = %.6f\n", mismatches, worst, tolerance);
    check(mismatches == 0, label + ": atlas sampling reproduces the reference sampler exactly");
}

void test_gpu_addressing_matches_cpu()
{
    std::printf("test_gpu_addressing_matches_cpu\n");
    mesh_sdf_bake_settings settings;
    settings.resolution = 24;
    settings.min_voxel_size = 0.001f;
    check_gpu_addressing_matches_cpu(make_sphere(0.7f, 24, 32), settings, "signed sphere");
    // A two-sided field must be covered too. The shell thickness is applied by the bake, and
    // an implementation that also subtracts it at sample time agrees perfectly on every signed
    // field (where the thickness is zero) while being wrong by a whole thickness on every
    // two-sided one -- a divergence a signed-only test cannot see.
    mesh_sdf_bake_settings shell_settings = settings;
    shell_settings.two_sided = true;
    shell_settings.two_sided_thickness = 0.05f;
    check_gpu_addressing_matches_cpu(make_sphere(0.7f, 24, 32), shell_settings, "two-sided sphere");
    // And an open mesh, which reaches the same path through the automatic fallback.
    check_gpu_addressing_matches_cpu(make_open_box(math::vec3(0.5f)), settings, "open box");
}

void test_serialization_round_trip()
{
    std::printf("test_serialization_round_trip\n");
    // The field is baked at asset compile time and read back at load time, so everything the
    // tracer sees has been through this round trip. The voxel payload is by far the largest
    // member, and mesh_sdf::is_valid deliberately reports on it, because a field that loses
    // its voxels but keeps its indirection would otherwise pass validation and then index its
    // brick slots out of bounds during upload -- rendering as noise, not as an error.
    const auto geometry = make_sphere(0.6f, 24, 32);
    mesh_sdf_bake_settings settings;
    settings.resolution = 32;
    settings.min_voxel_size = 0.001f;
    mesh_sdf original;
    check(bake_mesh_sdf(geometry, settings, original), "bake succeeds");
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    {
        ser20::oarchive_binary_t archive(stream);
        try_save(archive, ser20::make_nvp("sdf", original));
    }
    mesh_sdf restored;
    {
        ser20::iarchive_binary_t archive(stream);
        try_load(archive, ser20::make_nvp("sdf", restored));
    }
    check(restored.is_valid(), "restored field is valid");
    check(restored.voxel_size == original.voxel_size, "voxel size survives");
    check(restored.grid_dim == original.grid_dim, "grid dimension survives");
    check(restored.brick_dim == original.brick_dim, "brick dimension survives");
    check(restored.is_two_sided == original.is_two_sided, "two-sided flag survives");
    check(restored.bounds.min == original.bounds.min, "bounds min survives");
    check(restored.bounds.max == original.bounds.max, "bounds max survives");
    check(restored.indirection == original.indirection, "indirection survives");
    check(restored.brick_voxels == original.brick_voxels, "voxel payload survives");
    std::printf("  bricks %u -> %u, voxel bytes %zu -> %zu\n",
                original.get_surface_brick_count(),
                restored.get_surface_brick_count(),
                original.brick_voxels.size(),
                restored.brick_voxels.size());
}

void test_invalid_field_is_rejected()
{
    std::printf("test_invalid_field_is_rejected\n");
    // A field whose voxels are missing must not pass validation. Upload would otherwise
    // resolve every surface entry through an empty slot table and read out of bounds.
    const auto geometry = make_sphere(0.5f, 16, 24);
    mesh_sdf_bake_settings settings;
    settings.resolution = 24;
    settings.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), "bake succeeds");
    check(sdf.is_valid(), "baked field is valid");
    mesh_sdf missing_voxels = sdf;
    missing_voxels.brick_voxels.clear();
    check(!missing_voxels.is_valid(), "a field with no voxel storage is rejected");
    mesh_sdf truncated_voxels = sdf;
    truncated_voxels.brick_voxels.resize(truncated_voxels.brick_voxels.size() - mesh_sdf::brick_voxel_count);
    check(!truncated_voxels.is_valid(), "a field with too few bricks for its indirection is rejected");
}

void check_bounds_entry_is_not_a_hit(const mesh_sdf& sdf, const std::string& label)
{
    const float padding = sdf.get_bounds_padding();
    check(padding > 0.0f, label + ": the field reports a positive bounds padding");
    const math::vec3 min = sdf.bounds.min;
    const math::vec3 max = sdf.bounds.max;
    const math::vec3 span = sdf.bounds.get_dimensions();
    float smallest = std::numeric_limits<float>::max();
    // Walk the whole boundary surface, plus points just outside it, which is where the
    // degenerate case actually bit.
    for(int i = 0; i < 6000; ++i)
    {
        const float t = float(i) / 6000.0f;
        const float u = 0.5f + 0.5f * std::sin(t * 71.0f);
        const float v = 0.5f + 0.5f * std::cos(t * 43.0f);
        const int face = i % 6;
        math::vec3 p(min.x + u * span.x, min.y + v * span.y, min.z + u * span.z);
        if(face == 0) { p.x = min.x; }
        else if(face == 1) { p.x = max.x; }
        else if(face == 2) { p.y = min.y; }
        else if(face == 3) { p.y = max.y; }
        else if(face == 4) { p.z = min.z; }
        else { p.z = max.z; }
        smallest = math::min(smallest, sample_mesh_sdf(sdf, p));
        // And a hair outside, where the outside branch definitely runs.
        const math::vec3 outward = math::normalize(p - sdf.bounds.get_center());
        smallest = math::min(smallest, sample_mesh_sdf(sdf, p + outward * 1e-4f));
    }
    std::printf("  %s: smallest boundary sample = %.5f, padding = %.5f, shell = %.5f\n",
                label.c_str(),
                smallest,
                padding,
                sdf.two_sided_thickness);
    // Allow a little slack for the trilinear reconstruction of boundary voxels.
    check(smallest > 0.5f * padding, label + ": no sample on the field bounds reads as a hit");
}

void test_bounds_entry_is_not_a_hit()
{
    std::printf("test_bounds_entry_is_not_a_hit\n");
    // Every ray entering a field starts exactly on its bounds. If sampling there reports a
    // near-zero distance, the sphere trace stops immediately and draws the bounding box --
    // shaded by the box's own face normals -- instead of the mesh inside it.
    //
    // The bake pads the bounds away from the surface, so a sample on the boundary must report
    // at least that padding.
    const auto geometry = make_sphere(0.6f, 24, 32);
    mesh_sdf_bake_settings settings;
    settings.resolution = 32;
    settings.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), "bake succeeds");
    check_bounds_entry_is_not_a_hit(sdf, "signed sphere");
    // A shell expands the effective surface outward by its thickness, so the padding has to
    // account for it. A thickness larger than the encode range would otherwise push the shell
    // past the bounds and make the entry samples negative -- the same bounding-box artefact,
    // reachable through a completely different route.
    mesh_sdf_bake_settings thick_shell = settings;
    thick_shell.two_sided = true;
    thick_shell.two_sided_thickness = 0.25f;
    mesh_sdf shell_sdf;
    check(bake_mesh_sdf(geometry, thick_shell, shell_sdf), "thick shell bake succeeds");
    check(shell_sdf.two_sided_thickness > mesh_sdf::encode_range * shell_sdf.voxel_size,
          "the shell really is thicker than the encode range");
    check_bounds_entry_is_not_a_hit(shell_sdf, "thick shell");
}

void test_trace_from_outside_hits_the_surface_not_the_bounds()
{
    std::printf("test_trace_from_outside_hits_the_surface_not_the_bounds\n");
    // Reproduces what the debug tracer does, on the CPU: a ray starting OUTSIDE the field,
    // clipped to the bounds, then sphere traced. The reported artefact is that such rays stop
    // at the bounds instead of at the mesh, so this walks many entry directions and checks
    // where each one actually stops.
    //
    // Rays starting inside the bounds never showed the problem, which is why it only appears
    // when the camera is outside the field.
    const float radius = 0.6f;
    const auto geometry = make_sphere(radius, 24, 32);
    mesh_sdf_bake_settings settings;
    settings.resolution = 32;
    settings.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), "bake succeeds");
    // Same defaults as sdf_debug_pass::settings.
    const float surface_bias = 0.01f;
    const int max_steps = 96;
    const math::vec3 center = sdf.bounds.get_center();
    int stopped_at_bounds = 0;
    int stopped_at_surface = 0;
    int missed = 0;
    float worst_entry_sample = std::numeric_limits<float>::max();
    for(int i = 0; i < 4000; ++i)
    {
        const float t = float(i) / 4000.0f;
        const math::vec3 dir = math::normalize(
            math::vec3(std::sin(t * 61.0f), std::cos(t * 37.0f), std::sin(t * 23.0f) + 0.2f));
        // Start well outside the bounds, aimed at the centre so every ray should hit the sphere.
        const math::vec3 origin = center + dir * 12.0f;
        const math::vec3 ray_dir = -dir;
        const math::vec3 inv_dir(1.0f / ray_dir.x, 1.0f / ray_dir.y, 1.0f / ray_dir.z);
        const math::vec3 t0 = (sdf.bounds.min - origin) * inv_dir;
        const math::vec3 t1 = (sdf.bounds.max - origin) * inv_dir;
        const math::vec3 t_small = math::min(t0, t1);
        const math::vec3 t_big = math::max(t0, t1);
        const float t_near = math::max(math::max(t_small.x, t_small.y), math::max(t_small.z, 0.0f));
        const float t_far = math::min(math::min(t_big.x, t_big.y), t_big.z);
        if(t_near > t_far)
        {
            continue;
        }
        worst_entry_sample = math::min(worst_entry_sample, sample_mesh_sdf(sdf, origin + ray_dir * t_near));
        float ray_t = t_near;
        bool hit = false;
        for(int step = 0; step < max_steps && ray_t <= t_far; ++step)
        {
            const float distance = sample_mesh_sdf(sdf, origin + ray_dir * ray_t);
            if(distance < surface_bias)
            {
                hit = true;
                break;
            }
            ray_t += math::max(distance, surface_bias);
        }
        if(!hit)
        {
            ++missed;
            continue;
        }
        // The sphere is centred in its bounds, so a correct hit lands about one radius from
        // the centre; a spurious one lands right where the ray entered the bounds.
        const float hit_radius = math::length(origin + ray_dir * ray_t - center);
        if(std::fabs(hit_radius - radius) < 4.0f * sdf.voxel_size)
        {
            ++stopped_at_surface;
        }
        else if(ray_t < t_near + 4.0f * sdf.voxel_size)
        {
            ++stopped_at_bounds;
        }
    }
    std::printf("  at surface = %d, at bounds = %d, missed = %d, worst entry sample = %.5f\n",
                stopped_at_surface,
                stopped_at_bounds,
                missed,
                worst_entry_sample);
    check(stopped_at_bounds == 0, "no ray stops at the field bounds");
    check(missed == 0, "every ray aimed at the mesh finds it");
    check(stopped_at_surface > 3000, "rays stop at the mesh surface");
}

void test_open_mesh_does_not_produce_inside_regions()
{
    std::printf("test_open_mesh_does_not_produce_inside_regions\n");
    // Scanned props are routinely not closed surfaces. The angle-weighted pseudonormal test is
    // exact only for closed manifolds, so on an open mesh it reports "inside" for regions that
    // are plainly outside.
    //
    // That is not a cosmetic error. An empty brick flagged inside returns a NEGATIVE distance,
    // the tracer reads any negative sample as a surface hit, and the result is that the field's
    // whole bounding box renders solid -- shaded by the box's own face normals -- instead of
    // the mesh. The baker must detect an open surface and fall back to an unsigned shell, where
    // the sign is never consulted.
    const math::vec3 half(0.5f, 0.5f, 0.5f);
    const auto geometry = make_open_box(half);
    mesh_sdf_bake_settings settings;
    settings.resolution = 32;
    settings.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), "open box bake succeeds");
    check(sdf.is_two_sided, "an open surface falls back to an unsigned shell");
    // No brick may be flagged inside, and no sample well outside the mesh may read negative.
    int inside_bricks = 0;
    for(uint32_t entry : sdf.indirection)
    {
        if(is_sdf_empty_entry(entry) && (entry & mesh_sdf::indirection_inside_flag) != 0u)
        {
            ++inside_bricks;
        }
    }
    int negative_outside = 0;
    const math::vec3 span = sdf.bounds.get_dimensions();
    for(int i = 0; i < 20000; ++i)
    {
        const float t = float(i) / 20000.0f;
        const math::vec3 unit(0.5f + 0.5f * std::sin(t * 91.0f),
                              0.5f + 0.5f * std::cos(t * 57.0f),
                              0.5f + 0.5f * std::sin(t * 33.0f));
        const math::vec3 p = sdf.bounds.min + unit * span;
        // Only points comfortably outside the shell, where a negative reading is unambiguous.
        if(box_distance(p, half) < 4.0f * sdf.voxel_size)
        {
            continue;
        }
        if(sample_mesh_sdf(sdf, p) < 0.0f)
        {
            ++negative_outside;
        }
    }
    std::printf("  inside-flagged bricks = %d, negative samples outside = %d\n",
                inside_bricks,
                negative_outside);
    check(inside_bricks == 0, "no brick of an open mesh is flagged inside");
    check(negative_outside == 0, "no point outside an open mesh reads as solid");
}

void test_determinism()
{
    std::printf("test_determinism\n");
    // The bake is multi-threaded. Identical input must produce a bit-identical field, or
    // world-space stability is lost the moment an asset is recompiled.
    const auto geometry = make_sphere(0.8f, 24, 32);
    mesh_sdf_bake_settings settings;
    settings.resolution = 40;
    settings.min_voxel_size = 0.001f;
    mesh_sdf a;
    mesh_sdf b;
    check(bake_mesh_sdf(geometry, settings, a), "first bake succeeds");
    check(bake_mesh_sdf(geometry, settings, b), "second bake succeeds");
    check(a.indirection == b.indirection, "indirection is deterministic");
    check(a.brick_voxels == b.brick_voxels, "voxels are deterministic");
    check(a.voxel_size == b.voxel_size, "voxel size is deterministic");
}

// ---------------------------------------------------------------------------------------
// Global SDF clipmap
// ---------------------------------------------------------------------------------------

/// Places a baked field in the world at a translation, ready for the clipmap composer.
auto make_clipmap_instance(const mesh_sdf& sdf, const math::vec3& translation) -> global_sdf_instance
{
    global_sdf_instance instance;
    instance.sdf = &sdf;
    const math::mat4 local_to_world = glm::translate(math::mat4(1.0f), translation);
    instance.world_to_local = glm::inverse(local_to_world);
    instance.local_to_world_scale = 1.0f;
    instance.world_bounds.reset();
    for(const auto& corner : sdf.bounds.get_corners())
    {
        instance.world_bounds.add_point(corner + translation);
    }
    return instance;
}

auto make_scaled_clipmap_instance(const mesh_sdf& sdf, const math::vec3& translation, float scale)
    -> global_sdf_instance
{
    global_sdf_instance instance;
    instance.sdf = &sdf;
    const math::mat4 local_to_world =
        glm::scale(glm::translate(math::mat4(1.0f), translation), math::vec3(scale));
    instance.world_to_local = glm::inverse(local_to_world);
    instance.local_to_world_scale = scale;
    instance.world_bounds.reset();
    for(const auto& corner : sdf.bounds.get_corners())
    {
        const math::vec4 world_corner = local_to_world * math::vec4(corner, 1.0f);
        instance.world_bounds.add_point(math::vec3(world_corner));
    }
    return instance;
}

void test_sampling_cost_does_not_scale_with_field_size()
{
    std::printf("test_sampling_cost_does_not_scale_with_field_size\n");
    // A field lookup resolves ONE brick, so its cost must not depend on how many bricks the field
    // has. This exists because it did: sample_mesh_sdf guarded itself with is_valid(), which walks
    // the whole indirection array, so every sample paid a scan proportional to the field's size.
    //
    // Nothing looked wrong. The function was not obviously slow, the counts around it were
    // healthy, and the visible symptom was a clipmap composition that stayed expensive no matter
    // how much work was culled from it -- because the work that survived was hundreds of times
    // more expensive than it should have been.
    const auto geometry = make_sphere(1.0f, 32, 48);
    const auto measure = [&](uint32_t resolution, uint32_t& out_bricks) -> double
    {
        mesh_sdf_bake_settings settings;
        settings.resolution = resolution;
        settings.min_voxel_size = 0.0001f;
        mesh_sdf sdf;
        check(bake_mesh_sdf(geometry, settings, sdf), "bake succeeds");
        out_bricks = uint32_t(sdf.indirection.size());
        constexpr int sample_count = 400000;
        // Accumulated so the compiler cannot discard the calls.
        float sink = 0.0f;
        const auto start = std::chrono::steady_clock::now();
        for(int i = 0; i < sample_count; ++i)
        {
            const float t = float(i) * 0.001f;
            const math::vec3 p(1.4f * std::sin(t * 7.0f), 1.4f * std::cos(t * 3.0f), 1.4f * std::sin(t * 5.0f));
            sink += sample_mesh_sdf(sdf, p);
        }
        const double ns =
            std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() /
            double(sample_count);
        check(sink != 0.0f, "samples were actually taken");
        std::printf("  %3u resolution: %5u indirection entries, %6.1f ns per sample\n",
                    resolution,
                    out_bricks,
                    ns);
        return ns;
    };
    uint32_t small_bricks = 0;
    uint32_t large_bricks = 0;
    const double small_ns = measure(16, small_bricks);
    const double large_ns = measure(96, large_bricks);
    const double brick_ratio = double(large_bricks) / double(math::max(small_bricks, 1u));
    const double cost_ratio = large_ns / math::max(small_ns, 1e-3);
    std::printf("  %.0fx the bricks cost %.2fx per sample\n", brick_ratio, cost_ratio);
    check(brick_ratio > 8.0, "the two fields really do differ a lot in brick count");
    // A bigger field is legitimately a little slower per sample -- it spans more memory, so its
    // bricks are colder. What must NOT happen is cost tracking brick COUNT, which is what a scan
    // over the indirection array produces; that measured many times this bound.
    check(cost_ratio < 4.0, "sampling cost is independent of the field's brick count");
}

void test_clipmap_is_conservative()
{
    std::printf("test_clipmap_is_conservative\n");
    // The clipmap is the structure that lets offscreen geometry occlude, so a sphere trace
    // relies on it exactly as it relies on the per-instance fields: the value must never
    // exceed the true distance, or rays tunnel through whatever is out there.
    const float radius = 0.8f;
    const auto geometry = make_sphere(radius, 24, 32);
    mesh_sdf_bake_settings settings;
    settings.resolution = 32;
    settings.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), "bake succeeds");
    // Two spheres, one of them well away from the camera, which is the case a screen-space
    // technique cannot see at all.
    const math::vec3 near_at(1.0f, 0.0f, 0.0f);
    const math::vec3 far_at(-3.0f, 1.0f, 2.0f);
    std::vector<global_sdf_instance> instances{make_clipmap_instance(sdf, near_at),
                                               make_clipmap_instance(sdf, far_at)};
    global_sdf_clipmap clipmap;
    global_sdf_clipmap::settings clipmap_settings;
    clipmap_settings.resolution = 48;
    clipmap_settings.base_extent = 12.0f;
    // Compose every level up front: the per-update budget exists to spread the runtime
    // cost over frames, and stepping through it would only obscure what is under test.
    clipmap_settings.max_levels_per_update = global_sdf_clipmap::level_count;
    clipmap.init(clipmap_settings);
    const uint32_t composed = clipmap.update(instances, math::vec3(0.0f));
    check(composed == global_sdf_clipmap::level_count, "every level composes on the first update");
    std::printf("  levels composed = %u, memory = %zu KB\n", composed, clipmap.get_memory_usage() / 1024);
    const float level0_voxel = clipmap.get_level(0).voxel_size;
    int over_estimates = 0;
    float worst_excess = 0.0f;
    int samples = 0;
    for(int i = 0; i < 20000; ++i)
    {
        const float t = float(i) / 20000.0f;
        const math::vec3 p(5.0f * std::sin(t * 47.0f), 5.0f * std::cos(t * 31.0f), 5.0f * std::sin(t * 19.0f));
        // Ground truth: distance to the nearer of the two analytic spheres.
        const float truth = math::min(math::length(p - near_at), math::length(p - far_at)) - radius;
        const float actual = clipmap.sample(p);
        if(actual >= 1e5f)
        {
            continue;
        }
        ++samples;
        // Slack covers the coarse voxel quantisation of the cascade plus the per-instance
        // field it composes from. Saturation only ever under-reports, which is safe.
        const float slack = 2.0f * level0_voxel;
        if(actual > truth + slack)
        {
            ++over_estimates;
            worst_excess = math::max(worst_excess, actual - truth);
        }
    }
    std::printf("  samples = %d, over-estimates = %d, worst excess = %.4f, level0 voxel = %.4f\n",
                samples,
                over_estimates,
                worst_excess,
                level0_voxel);
    check(samples > 5000, "enough samples land inside the cascade");
    check(over_estimates == 0, "the clipmap never over-estimates the distance to the nearest surface");
}

void test_clipmap_recomposes_moved_geometry_within_budget()
{
    std::printf("test_clipmap_recomposes_moved_geometry_within_budget\n");
    // Two properties in tension, which is why they are asserted together.
    //
    // A moved instance MUST eventually be recomposed: the cascade is what every other stage asks
    // "what is there", so an object that leaves its geometry behind goes on occluding and
    // lighting from where it used to be, and nothing downstream can recover.
    //
    // But recomposition must stay BUDGETED. Composing a level is expensive enough to be a visible
    // hitch, and the previous rule -- any change recomposes every level immediately -- meant one
    // moving object cost four levels in a single frame, every frame it moved.
    const float radius = 0.8f;
    const auto geometry = make_sphere(radius, 20, 28);
    mesh_sdf_bake_settings bake;
    bake.resolution = 24;
    bake.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, bake, sdf), "bake succeeds");
    std::vector<global_sdf_instance> instances{make_clipmap_instance(sdf, math::vec3(0.0f, 0.0f, 0.0f)),
                                               make_clipmap_instance(sdf, math::vec3(3.0f, 0.0f, 0.0f))};
    global_sdf_clipmap clipmap;
    global_sdf_clipmap::settings clipmap_settings;
    clipmap_settings.resolution = 32;
    clipmap_settings.base_extent = 12.0f;
    clipmap.init(clipmap_settings);
    const math::vec3 camera(0.0f);
    // Settle: repeated updates with nothing changing must converge to composing nothing, or the
    // cascade would be rebuilding itself forever.
    uint32_t settle_updates = 0;
    while(clipmap.update(instances, camera) > 0 && settle_updates < 32)
    {
        ++settle_updates;
    }
    check(settle_updates < 32, "the cascade settles when nothing changes");
    check(clipmap.update(instances, camera) == 0, "and stays settled");
    check(clipmap.get_stale_level_count() == 0, "with no level left stale");
    std::printf("  settled after %u updates\n", settle_updates);
    // Move one instance. Every level contains it, so all four are stale -- and the budget must
    // still hold, which is the whole point.
    instances[1] = make_clipmap_instance(sdf, math::vec3(3.0f, 4.0f, 1.0f));
    const uint32_t composed_now = clipmap.update(instances, camera);
    check(composed_now <= clipmap_settings.max_levels_per_update,
          "a move does not blow through the per-update budget");
    check(composed_now > 0, "but does start recomposing immediately");
    // And it must finish. Bounded staleness is the trade; unbounded staleness is the old bug.
    uint32_t catch_up = 1;
    while(clipmap.update(instances, camera) > 0 && catch_up < 32)
    {
        ++catch_up;
    }
    check(catch_up < 32, "and catches up within a bounded number of updates");
    check(clipmap.get_stale_level_count() == 0, "leaving no level stale");
    std::printf("  a move composed %u level(s) that update, fully caught up after %u\n",
                composed_now,
                catch_up);
    // The recomposed cascade must actually reflect the new position: sampling where the instance
    // used to be must no longer report a surface there.
    const float at_old_position = clipmap.sample(math::vec3(3.0f, 0.0f, 0.0f));
    const float at_new_position = clipmap.sample(math::vec3(3.0f, 4.0f, 1.0f));
    std::printf("  old position reads %.3f, new position reads %.3f\n", at_old_position, at_new_position);
    check(at_new_position < radius, "the moved instance is present at its new position");
    check(at_old_position > 0.5f * radius, "and no longer occupies the old one");
}

void test_clipmap_culled_composition_matches_brute_force()
{
    std::printf("test_clipmap_culled_composition_matches_brute_force\n");
    // Binning the instances per level cell is PURE acceleration: it must change how long
    // composition takes and nothing else. Byte equality is the right assertion because a cull
    // bug does not corrupt a voxel, it omits an instance from one -- so the voxel reports a
    // larger distance than the truth, which is the over-estimate that lets a trace step straight
    // through geometry. Comparing images or tolerances would hide exactly that.
    const float radius = 0.8f;
    const auto geometry = make_sphere(radius, 20, 28);
    mesh_sdf_bake_settings settings;
    settings.resolution = 24;
    settings.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), "bake succeeds");
    // Enough instances, spread widely enough, that the coarse levels keep nearly all of them
    // after the per-level cull -- which is the case the per-cell grid exists for and the only
    // one where the two paths could disagree.
    std::vector<global_sdf_instance> instances;
    instances.reserve(600);
    for(int i = 0; i < 600; ++i)
    {
        const float t = float(i);
        // Deliberately building-shaped rather than a cloud of small props: most submeshes of
        // a real model are large, overlapping and concentrated, so many land in the SAME cull
        // cell. That is the distribution the grid helps least on, and therefore the one worth
        // measuring -- scattered props flatter it by roughly a factor of three.
        const float scale = 1.0f + 9.0f * std::fabs(std::sin(t * 0.41f));
        instances.push_back(make_scaled_clipmap_instance(
            sdf,
            math::vec3(35.0f * std::sin(t * 1.7f), 8.0f * std::cos(t * 2.3f), 35.0f * std::sin(t * 0.9f)),
            scale));
    }
    const auto compose = [&](bool cull) -> std::array<std::vector<uint8_t>, global_sdf_clipmap::level_count>
    {
        global_sdf_clipmap clipmap;
        global_sdf_clipmap::settings clipmap_settings;
        clipmap_settings.resolution = 48;
        clipmap_settings.base_extent = 12.0f;
        // Compose every level up front: the per-update budget exists to spread the runtime
        // cost over frames, and stepping through it would only obscure what is under test.
        clipmap_settings.max_levels_per_update = global_sdf_clipmap::level_count;
        clipmap_settings.cull_composition = cull;
        clipmap.init(clipmap_settings);
        const auto start = std::chrono::steady_clock::now();
        clipmap.update(instances, math::vec3(0.0f));
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("  %-12s %7.1f ms for %u levels\n",
                    cull ? "per-cell:" : "brute force:",
                    ms,
                    global_sdf_clipmap::level_count);
        std::array<std::vector<uint8_t>, global_sdf_clipmap::level_count> voxels;
        for(uint32_t i = 0; i < global_sdf_clipmap::level_count; ++i)
        {
            voxels[i] = clipmap.get_level(i).voxels;
        }
        return voxels;
    };
    const auto brute_force = compose(false);
    const auto culled = compose(true);
    bool all_match = true;
    for(uint32_t i = 0; i < global_sdf_clipmap::level_count; ++i)
    {
        all_match = all_match && brute_force[i] == culled[i];
    }
    check(!brute_force[0].empty(), "composition produced voxels");
    check(all_match, "per-cell culling composes byte-identical voxels to the brute-force path");
}

void test_clipmap_transition_is_continuous()
{
    std::printf("test_clipmap_transition_is_continuous\n");
    // A distance field is 1-Lipschitz: moving by d cannot change the reported distance by more
    // than d. Levels are composed independently at different voxel sizes, so their isosurfaces do
    // not coincide, and switching between them abruptly breaks that -- the value jumps at the
    // boundary by far more than the step taken to cross it.
    //
    // That matters because two consumers resolving a surface either side of a boundary land on
    // points a voxel apart, derive different cache cells from them, and never see each other's
    // work. Continuity is what makes them quote one function.
    const float radius = 0.8f;
    const auto geometry = make_sphere(radius, 24, 32);
    mesh_sdf_bake_settings settings;
    settings.resolution = 32;
    settings.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), "bake succeeds");
    // Geometry spread through the whole cascade. A boundary crossed in empty space cannot show
    // the seam -- both levels saturate there and agree -- so the spheres have to reach every
    // level's edge, which is why this is a cloud rather than a line.
    std::vector<global_sdf_instance> instances;
    for(int i = 0; i < 220; ++i)
    {
        const float t = float(i);
        const math::vec3 at(11.0f * std::sin(t * 1.7f) + 0.35f * t,
                            7.0f * std::cos(t * 2.3f),
                            9.0f * std::sin(t * 0.9f) - 0.2f * t);
        instances.push_back(make_clipmap_instance(sdf, at));
    }
    global_sdf_clipmap clipmap;
    global_sdf_clipmap::settings clipmap_settings;
    clipmap_settings.resolution = 48;
    clipmap_settings.base_extent = 12.0f;
    // Compose every level up front: the per-update budget exists to spread the runtime
    // cost over frames, and stepping through it would only obscure what is under test.
    clipmap_settings.max_levels_per_update = global_sdf_clipmap::level_count;
    clipmap.init(clipmap_settings);
    clipmap.update(instances, math::vec3(0.0f));
    // How far apart the two levels are WHERE THEY OVERLAP. This is the seam itself, independent
    // of how it is sampled, and it is what the blend exists to hide.
    float worst_disagreement = 0.0f;
    int overlap_samples = 0;
    for(int i = 0; i < 40000; ++i)
    {
        const float t = float(i) / 40000.0f;
        const math::vec3 p(26.0f * std::sin(t * 47.0f), 26.0f * std::cos(t * 31.0f), 26.0f * std::sin(t * 19.0f));
        float blend = 0.0f;
        const uint32_t level = clipmap.find_level(p, blend);
        if(level >= global_sdf_clipmap::level_count || blend <= 0.0f)
        {
            continue;
        }
        const float fine = clipmap.sample_level(level, p);
        const float coarse = clipmap.sample_level(level + 1u, p);
        if(coarse >= global_sdf_clipmap::outside_distance)
        {
            continue;
        }
        ++overlap_samples;
        worst_disagreement = math::max(worst_disagreement, std::fabs(fine - coarse));
    }
    // Now the property that matters to a consumer: walking through a boundary must not step the
    // value. Swept over many directions because a single line crosses the boundary in one place,
    // which is very unlikely to be the worst one.
    //
    // Both the blended field and the hard switch it replaced are measured from ONE composition:
    // sample_level at the level find_level chose IS the old behaviour, so no second cascade and
    // no rebuild is needed to compare them. Measuring the old one alongside is what makes this a
    // regression test rather than a description -- a bound the previous code would also have
    // passed proves nothing.
    const float step = 0.25f * clipmap.get_level(0).voxel_size;
    float worst_jump = 0.0f;
    float worst_hard_jump = 0.0f;
    int crossings = 0;
    for(int d = 0; d < 240; ++d)
    {
        const float a = float(d) * 2.399963f;
        const float z = 1.0f - 2.0f * (float(d) + 0.5f) / 240.0f;
        const float r = std::sqrt(math::max(0.0f, 1.0f - z * z));
        const math::vec3 direction(r * std::cos(a), r * std::sin(a), z);
        uint32_t previous_level = global_sdf_clipmap::level_count;
        float previous = 0.0f;
        float previous_hard = 0.0f;
        for(int i = 1; i < 2200; ++i)
        {
            const math::vec3 p = direction * (float(i) * step);
            float blend = 0.0f;
            const uint32_t level = clipmap.find_level(p, blend);
            if(level >= global_sdf_clipmap::level_count)
            {
                break;
            }
            const float current = clipmap.sample(p);
            const float current_hard = clipmap.sample_level(level, p);
            if(previous_level != global_sdf_clipmap::level_count)
            {
                worst_jump = math::max(worst_jump, std::fabs(current - previous));
                worst_hard_jump = math::max(worst_hard_jump, std::fabs(current_hard - previous_hard));
                if(level != previous_level)
                {
                    ++crossings;
                }
            }
            previous = current;
            previous_hard = current_hard;
            previous_level = level;
        }
    }
    std::printf("  levels disagree by up to %.4f over %d overlap samples (level 0 voxel %.4f)\n",
                worst_disagreement,
                overlap_samples,
                clipmap.get_level(0).voxel_size);
    std::printf("  %d crossings, worst jump: hard switch %.4f (%.1fx step), blended %.4f (%.1fx step)\n",
                crossings,
                worst_hard_jump,
                worst_hard_jump / step,
                worst_jump,
                worst_jump / step);
    check(overlap_samples > 500, "enough samples land where two levels overlap");
    check(crossings >= 100, "the sweep actually crosses level boundaries");
    check(worst_disagreement > 0.5f * clipmap.get_level(0).voxel_size,
          "the levels really do disagree, so this test is exercising the seam");
    // The hard switch has to be shown to fail the bound, or the bound is not measuring anything.
    check(worst_hard_jump > 3.0f * step, "the hard switch really does step the field");
    // The bound is on the SAMPLED field, not on the levels: they are allowed to disagree, and the
    // blend is what stops that disagreement reaching a consumer as a step. Trilinear
    // reconstruction of a quantised field is not exactly 1-Lipschitz, so the multiple is loose.
    check(worst_jump < 3.0f * step, "no level transition puts a step discontinuity in the field");
}

void test_clipmap_blend_stays_conservative()
{
    std::printf("test_clipmap_blend_stays_conservative\n");
    // The blend must not buy continuity at the cost of the invariant everything else rests on.
    // A convex combination of two under-estimates is an under-estimate, so this should hold by
    // construction -- but it is the property whose failure lets rays tunnel through geometry, so
    // it is worth asserting where the blend is actually active.
    const float radius = 0.8f;
    const auto geometry = make_sphere(radius, 24, 32);
    mesh_sdf_bake_settings settings;
    settings.resolution = 32;
    settings.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), "bake succeeds");
    std::vector<math::vec3> centers;
    for(int i = 0; i < 24; ++i)
    {
        const float t = float(i) * 0.9f;
        centers.emplace_back(t, 0.35f * t, -0.2f * t);
    }
    std::vector<global_sdf_instance> instances;
    for(const auto& c : centers)
    {
        instances.push_back(make_clipmap_instance(sdf, c));
    }
    global_sdf_clipmap clipmap;
    global_sdf_clipmap::settings clipmap_settings;
    clipmap_settings.resolution = 48;
    clipmap_settings.base_extent = 12.0f;
    // Compose every level up front: the per-update budget exists to spread the runtime
    // cost over frames, and stepping through it would only obscure what is under test.
    clipmap_settings.max_levels_per_update = global_sdf_clipmap::level_count;
    clipmap.init(clipmap_settings);
    clipmap.update(instances, math::vec3(0.0f));
    int blended_samples = 0;
    int over_estimates = 0;
    float worst_excess = 0.0f;
    for(int i = 0; i < 60000; ++i)
    {
        const float t = float(i) / 60000.0f;
        const math::vec3 p(30.0f * std::sin(t * 47.0f), 30.0f * std::cos(t * 31.0f), 30.0f * std::sin(t * 19.0f));
        float blend = 0.0f;
        const uint32_t level = clipmap.find_level(p, blend);
        if(level >= global_sdf_clipmap::level_count || blend <= 0.0f)
        {
            continue;
        }
        ++blended_samples;
        float truth = std::numeric_limits<float>::max();
        for(const auto& c : centers)
        {
            truth = math::min(truth, math::length(p - c) - radius);
        }
        const float actual = clipmap.sample(p);
        // Slack sized to the COARSER level, which is the one the blend mixes in.
        const float slack = 2.0f * clipmap.get_level(math::min(level + 1u, 3u)).voxel_size;
        if(actual > truth + slack)
        {
            ++over_estimates;
            worst_excess = math::max(worst_excess, actual - truth);
        }
    }
    std::printf("  %d samples inside a blend band, %d over-estimates, worst excess %.4f\n",
                blended_samples,
                over_estimates,
                worst_excess);
    check(blended_samples > 200, "enough samples land where the blend is active");
    check(over_estimates == 0, "the blended value never over-estimates the true distance");
}

/// Slab test against an AABB over a ray SEGMENT, matching SdfIntersectBounds in the tracer.
auto segment_hits_bounds(const math::bbox& bounds,
                         const math::vec3& origin,
                         const math::vec3& direction,
                         float t_min,
                         float t_max) -> bool
{
    float enter = t_min;
    float exit = t_max;
    for(int axis = 0; axis < 3; ++axis)
    {
        const float d = direction[axis];
        const float o = origin[axis];
        if(std::fabs(d) < 1e-8f)
        {
            if(o < bounds.min[axis] || o > bounds.max[axis])
            {
                return false;
            }
            continue;
        }
        const float inv = 1.0f / d;
        float near_t = (bounds.min[axis] - o) * inv;
        float far_t = (bounds.max[axis] - o) * inv;
        if(near_t > far_t)
        {
            std::swap(near_t, far_t);
        }
        enter = math::max(enter, near_t);
        exit = math::min(exit, far_t);
    }
    return enter <= exit;
}

void test_instance_grid_never_misses_an_instance()
{
    std::printf("test_instance_grid_never_misses_an_instance\n");
    // The one property a culling structure must have. A false POSITIVE costs a broad-phase
    // rejection; a false NEGATIVE is an instance the ray never tests, which means geometry that
    // silently stops occluding -- indistinguishable from a bad field, and the exact failure this
    // whole tier exists to prevent.
    //
    // Compared against the brute-force test the tracer used to do, over rays that deliberately
    // include the awkward cases: axis-aligned directions that run along cell planes, and origins
    // outside the grid, which happen constantly because the cache update pass casts from entries
    // anywhere in the world.
    std::vector<math::bbox> bounds;
    for(int i = 0; i < 400; ++i)
    {
        const float t = float(i);
        const math::vec3 center(9.0f * std::sin(t * 1.7f) + 0.3f * t,
                                5.0f * std::cos(t * 2.3f),
                                7.0f * std::sin(t * 0.9f) - 0.2f * t);
        // Deliberately mixed sizes: a few instances span many cells, which is the case that
        // makes an instance appear in several cell lists at once.
        const float half = 0.3f + 2.5f * std::fabs(std::sin(t * 0.37f));
        math::bbox b;
        b.reset();
        b.add_point(center - math::vec3(half));
        b.add_point(center + math::vec3(half));
        bounds.push_back(b);
    }
    sdf_instance_grid grid;
    sdf_instance_grid::settings grid_settings;
    grid_settings.resolution = 24;
    grid.init(grid_settings);
    grid.build(bounds);
    check(grid.is_valid(), "the grid builds");
    std::vector<math::vec3> directions = {math::vec3(1.0f, 0.0f, 0.0f),
                                          math::vec3(0.0f, 1.0f, 0.0f),
                                          math::vec3(0.0f, 0.0f, 1.0f),
                                          math::vec3(-1.0f, 0.0f, 0.0f),
                                          math::normalize(math::vec3(1.0f, 1.0f, 1.0f)),
                                          math::normalize(math::vec3(-0.3f, 0.9f, -0.4f))};
    int missed = 0;
    int expected_total = 0;
    int candidate_total = 0;
    int rays = 0;
    std::vector<uint32_t> candidates;
    for(int i = 0; i < 4000; ++i)
    {
        const float t = float(i) * 0.611f;
        const math::vec3 origin(24.0f * std::sin(t), 14.0f * std::cos(t * 1.31f), 20.0f * std::sin(t * 0.77f));
        math::vec3 direction = directions[size_t(i) % directions.size()];
        if(i % 3 == 0)
        {
            direction = math::normalize(math::vec3(std::sin(t * 2.1f), std::cos(t * 1.3f), std::sin(t * 0.5f)));
        }
        const float t_max = 30.0f;
        ++rays;
        const bool any_cell = grid.gather_candidates(origin, direction, 0.0f, t_max, candidates);
        std::vector<bool> found(bounds.size(), false);
        for(uint32_t index : candidates)
        {
            found[index] = true;
        }
        candidate_total += int(candidates.size());
        for(uint32_t index = 0; index < uint32_t(bounds.size()); ++index)
        {
            if(!segment_hits_bounds(bounds[index], origin, direction, 0.0f, t_max))
            {
                continue;
            }
            ++expected_total;
            if(!found[index] || !any_cell)
            {
                ++missed;
            }
        }
    }
    std::printf("  %d rays, %d instances truly hit, %d candidates returned, %d missed\n",
                rays,
                expected_total,
                candidate_total,
                missed);
    std::printf("  grid %ux%ux%u, cell %.3f, %zu references for %zu instances\n",
                grid.get_dim().x,
                grid.get_dim().y,
                grid.get_dim().z,
                grid.get_cell_size(),
                grid.get_reference_count(),
                bounds.size());
    check(expected_total > 2000, "the rays actually cross a lot of instances");
    check(missed == 0, "the grid never culls an instance the ray really crosses");
    // The point of the structure. Without it every ray tests every instance, so the comparison
    // is against rays * instances.
    const double brute_force = double(rays) * double(bounds.size());
    std::printf("  %.0f brute-force tests -> %d candidates (%.1fx fewer)\n",
                brute_force,
                candidate_total,
                brute_force / math::max(double(candidate_total), 1.0));
    check(double(candidate_total) < brute_force * 0.25, "the grid removes most of the work");
}

/**
 * @brief Transcription of the grid walk in SdfTraceInstances, expressed the way the shader does.
 *
 * The shader cannot reuse sdf_instance_grid, so its traversal is a second implementation of the
 * same algorithm -- and it is written with vector masks and step() where the CPU uses scalar
 * per-axis branches, so "obviously the same" is exactly the claim worth checking. A divergence
 * here does not fail loudly: it is an instance the GPU never tests, which renders as geometry
 * that quietly stops occluding.
 */
auto simulate_shader_grid_walk(const sdf_instance_grid& grid,
                               const math::vec3& origin,
                               const math::vec3& direction,
                               float t_min,
                               float t_max,
                               std::vector<uint32_t>& out) -> bool
{
    out.clear();
    const auto splat = [](float v) -> math::vec3 { return math::vec3(v, v, v); };
    const math::vec3 inv_dir =
        math::vec3(1.0f) / math::max(math::abs(direction), splat(1e-8f)) * math::sign(direction + splat(1e-20f));
    const math::vec3 dim(float(grid.get_dim().x), float(grid.get_dim().y), float(grid.get_dim().z));
    const math::vec3 grid_min = grid.get_origin();
    const math::vec3 grid_max = grid_min + dim * grid.get_cell_size();
    // SdfIntersectBounds
    const math::vec3 t0 = (grid_min - origin) * inv_dir;
    const math::vec3 t1 = (grid_max - origin) * inv_dir;
    const math::vec3 t_small = math::min(t0, t1);
    const math::vec3 t_big = math::max(t0, t1);
    float t_enter = math::max(math::max(t_small.x, t_small.y), math::max(t_small.z, 0.0f));
    const float t_exit = math::min(math::min(t_big.x, t_big.y), math::min(t_big.z, t_max));
    if(t_enter > t_exit)
    {
        return false;
    }
    t_enter = math::max(t_enter, t_min);
    if(t_enter > t_exit)
    {
        return false;
    }
    const math::vec3 entry = origin + direction * t_enter;
    math::vec3 cell_f = math::floor((entry - grid_min) / grid.get_cell_size());
    cell_f = math::clamp(cell_f, splat(0.0f), dim - splat(1.0f));
    const math::vec3 dir_sign = math::sign(direction);
    const math::vec3 abs_dir = math::max(math::abs(direction), splat(1e-8f));
    math::vec3 t_delta = splat(grid.get_cell_size()) / abs_dir;
    const math::vec3 next_plane =
        grid_min + (cell_f + math::max(dir_sign, splat(0.0f))) * grid.get_cell_size();
    math::vec3 t_next = (next_plane - origin) * inv_dir;
    // step(edge, x) is 1 where x >= edge.
    const math::vec3 moving(float(std::fabs(direction.x) >= 1e-7f),
                            float(std::fabs(direction.y) >= 1e-7f),
                            float(std::fabs(direction.z) >= 1e-7f));
    const float outside = 1e6f;
    t_next = math::mix(splat(outside), t_next, moving);
    t_delta = math::mix(splat(outside), t_delta, moving);
    const auto& offsets = grid.get_cell_offsets();
    const auto& cell_instances = grid.get_cell_instances();
    for(int visited = 0; visited < 256; ++visited)
    {
        const int index = int(cell_f.x + cell_f.y * dim.x + cell_f.z * dim.x * dim.y);
        for(uint32_t entry_index = offsets[size_t(index)]; entry_index < offsets[size_t(index) + 1u];
            ++entry_index)
        {
            out.push_back(cell_instances[entry_index]);
        }
        const float t_step = math::min(t_next.x, math::min(t_next.y, t_next.z));
        if(t_step > t_exit)
        {
            break;
        }
        const math::vec3 mask(float(t_next.x <= t_step), float(t_next.y <= t_step), float(t_next.z <= t_step));
        cell_f += mask * dir_sign;
        t_next += mask * t_delta;
        if(math::any(math::lessThan(cell_f, splat(0.0f))) ||
           math::any(math::greaterThan(cell_f, dim - splat(1.0f))))
        {
            break;
        }
    }
    return true;
}

void test_instance_grid_shader_walk_matches_cpu()
{
    std::printf("test_instance_grid_shader_walk_matches_cpu\n");
    std::vector<math::bbox> bounds;
    for(int i = 0; i < 300; ++i)
    {
        const float t = float(i);
        const math::vec3 center(8.0f * std::sin(t * 1.7f) + 0.4f * t,
                                6.0f * std::cos(t * 2.3f),
                                7.0f * std::sin(t * 0.9f));
        const float half = 0.4f + 1.8f * std::fabs(std::sin(t * 0.37f));
        math::bbox b;
        b.reset();
        b.add_point(center - math::vec3(half));
        b.add_point(center + math::vec3(half));
        bounds.push_back(b);
    }
    sdf_instance_grid grid;
    sdf_instance_grid::settings grid_settings;
    grid_settings.resolution = 20;
    grid.init(grid_settings);
    grid.build(bounds);
    check(grid.is_valid(), "the grid builds");
    int mismatches = 0;
    int compared = 0;
    std::vector<uint32_t> from_cpu;
    std::vector<uint32_t> from_shader;
    for(int i = 0; i < 3000; ++i)
    {
        const float t = float(i) * 0.437f;
        const math::vec3 origin(22.0f * std::sin(t), 12.0f * std::cos(t * 1.31f), 18.0f * std::sin(t * 0.77f));
        math::vec3 direction = math::normalize(
            math::vec3(std::sin(t * 2.1f), std::cos(t * 1.3f), std::sin(t * 0.5f)));
        // Axis-aligned rays every few iterations: they run exactly along cell planes, which is
        // where a mask-based tie-break and a scalar branch are most likely to part company.
        if(i % 5 == 0)
        {
            direction = math::vec3(float(i % 3 == 0), float(i % 3 == 1), float(i % 3 == 2));
            if(math::length(direction) < 0.5f)
            {
                direction = math::vec3(1.0f, 0.0f, 0.0f);
            }
        }
        const bool cpu_hit = grid.gather_candidates(origin, direction, 0.0f, 30.0f, from_cpu);
        const bool shader_hit = simulate_shader_grid_walk(grid, origin, direction, 0.0f, 30.0f, from_shader);
        ++compared;
        if(cpu_hit != shader_hit)
        {
            ++mismatches;
            continue;
        }
        // Compared as SETS: the two may legitimately visit cells in a different order or repeat
        // an instance a different number of times. What must not differ is which instances a ray
        // can reach at all.
        std::sort(from_cpu.begin(), from_cpu.end());
        from_cpu.erase(std::unique(from_cpu.begin(), from_cpu.end()), from_cpu.end());
        std::sort(from_shader.begin(), from_shader.end());
        from_shader.erase(std::unique(from_shader.begin(), from_shader.end()), from_shader.end());
        if(from_cpu != from_shader)
        {
            ++mismatches;
        }
    }
    std::printf("  %d rays compared, %d mismatches\n", compared, mismatches);
    check(mismatches == 0, "the shader's grid walk reaches the same instances as the CPU reference");
}

void test_instance_grid_handles_degenerate_input()
{
    std::printf("test_instance_grid_handles_degenerate_input\n");
    sdf_instance_grid grid;
    grid.init({});
    std::vector<uint32_t> candidates;
    // No instances: nothing to build, and a query must decline rather than address an empty grid.
    grid.build({});
    check(!grid.is_valid(), "an empty instance list produces no grid");
    check(!grid.gather_candidates(math::vec3(0.0f), math::vec3(1.0f, 0.0f, 0.0f), 0.0f, 10.0f, candidates),
          "querying an unbuilt grid declines");
    // A single point-sized instance collapses the scene bounds to zero extent, which is where a
    // cell size derived from the extent would divide by zero.
    math::bbox point;
    point.reset();
    point.add_point(math::vec3(3.0f, -2.0f, 1.0f));
    grid.build({point});
    check(grid.is_valid(), "degenerate bounds still produce a usable grid");
    check(grid.gather_candidates(math::vec3(3.0f, -2.0f, -20.0f),
                                 math::vec3(0.0f, 0.0f, 1.0f),
                                 0.0f,
                                 100.0f,
                                 candidates),
          "a ray through the point finds the grid");
    check(!candidates.empty(), "and finds the instance in it");
}

void test_clipmap_is_world_stable()
{
    std::printf("test_clipmap_is_world_stable\n");
    // The reason for snapping each level's origin to its own voxel grid. Any two camera
    // positions inside the same voxel must produce a bit-identical cascade, otherwise the
    // lighting derived from it crawls continuously as the camera moves -- exactly the failure
    // the whole world-space approach exists to avoid.
    const auto geometry = make_sphere(0.8f, 24, 32);
    mesh_sdf_bake_settings settings;
    settings.resolution = 32;
    settings.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), "bake succeeds");
    std::vector<global_sdf_instance> instances{make_clipmap_instance(sdf, math::vec3(1.0f, 0.0f, 0.0f))};
    global_sdf_clipmap::settings clipmap_settings;
    clipmap_settings.resolution = 32;
    clipmap_settings.base_extent = 8.0f;
    // Compose every level up front: the per-update budget exists to spread the runtime
    // cost over frames, and stepping through it would only obscure what is under test.
    clipmap_settings.max_levels_per_update = global_sdf_clipmap::level_count;
    global_sdf_clipmap a;
    global_sdf_clipmap b;
    a.init(clipmap_settings);
    b.init(clipmap_settings);
    const float level0_voxel = a.get_level(0).voxel_size;
    // Two camera positions inside the same level-0 voxel.
    const math::vec3 camera_a(0.0f, 0.0f, 0.0f);
    const math::vec3 camera_b = camera_a + math::vec3(level0_voxel * 0.4f, 0.0f, level0_voxel * 0.3f);
    a.update(instances, camera_a);
    b.update(instances, camera_b);
    bool identical = true;
    for(uint32_t i = 0; i < global_sdf_clipmap::level_count; ++i)
    {
        identical = identical && a.get_level(i).origin == b.get_level(i).origin;
        identical = identical && a.get_level(i).voxels == b.get_level(i).voxels;
    }
    check(identical, "cameras within one voxel produce a bit-identical cascade");
    // And moving a whole voxel must actually re-snap, or the cascade would drift out from
    // under the camera and stop covering it.
    global_sdf_clipmap c;
    c.init(clipmap_settings);
    c.update(instances, camera_a + math::vec3(level0_voxel * 4.0f, 0.0f, 0.0f));
    check(c.get_level(0).origin != a.get_level(0).origin, "moving several voxels re-snaps the origin");
    // A second update from the same position must recompose nothing.
    const uint32_t recomposed = a.update(instances, camera_a);
    std::printf("  recomposed on an unchanged update = %u\n", recomposed);
    check(recomposed == 0, "an unchanged camera recomposes no levels");
}

void test_clipmap_sees_offscreen_geometry()
{
    std::printf("test_clipmap_sees_offscreen_geometry\n");
    // The requirement the screen-space path cannot meet at all: geometry BEHIND the camera
    // still has to occlude and bounce. Nothing about the cascade depends on the view
    // direction, so a surface behind the camera is found exactly like one in front.
    const float radius = 0.8f;
    const auto geometry = make_sphere(radius, 24, 32);
    mesh_sdf_bake_settings settings;
    settings.resolution = 32;
    settings.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), "bake succeeds");
    // The camera looks down +Z; the sphere sits behind it at -Z.
    const math::vec3 behind(0.0f, 0.0f, -3.0f);
    std::vector<global_sdf_instance> instances{make_clipmap_instance(sdf, behind)};
    global_sdf_clipmap clipmap;
    global_sdf_clipmap::settings clipmap_settings;
    clipmap_settings.resolution = 48;
    clipmap_settings.base_extent = 12.0f;
    // Compose every level up front: the per-update budget exists to spread the runtime
    // cost over frames, and stepping through it would only obscure what is under test.
    clipmap_settings.max_levels_per_update = global_sdf_clipmap::level_count;
    clipmap.init(clipmap_settings);
    clipmap.update(instances, math::vec3(0.0f));
    // Sphere trace backwards, away from the view direction, and require it to find the sphere.
    const math::vec3 origin(0.0f, 0.0f, 0.0f);
    const math::vec3 direction(0.0f, 0.0f, -1.0f);
    const float hit_threshold = 0.5f * clipmap.get_level(0).voxel_size;
    float t = 0.0f;
    bool hit = false;
    for(int step = 0; step < 256 && t < 10.0f; ++step)
    {
        const float distance = clipmap.sample(origin + direction * t);
        if(distance < hit_threshold)
        {
            hit = true;
            break;
        }
        t += math::max(distance, hit_threshold);
    }
    const float expected = math::length(behind - origin) - radius;
    std::printf("  hit at t = %.4f, expected ~%.4f\n", t, expected);
    check(hit, "a ray finds geometry behind the camera");
    check(std::fabs(t - expected) < 4.0f * clipmap.get_level(0).voxel_size,
          "the hit lands on the sphere surface");
}

void test_raw_buffer_extraction_matches_direct_geometry()
{
    std::printf("test_raw_buffer_extraction_matches_direct_geometry\n");
    // The extraction path shared by compiled assets and runtime-generated primitives. Meshes
    // built procedurally never pass through the asset compiler, so this is the only route by
    // which they get a field at all; if it silently produced nothing, primitives would render
    // normally while being completely absent from global illumination.
    const auto direct = make_box(math::vec3(0.5f, 0.35f, 0.6f));
    // Pack the same geometry the way a prepared mesh holds it: an interleaved vertex buffer
    // described by a layout, plus a flat index array.
    gfx::vertex_layout layout;
    layout.begin(bgfx::RendererType::Noop).add(gfx::attribute::Position, 3, gfx::attribute_type::Float).end();
    const uint32_t stride = layout.getStride();
    std::vector<uint8_t> vertex_data(size_t(direct.positions.size()) * stride, 0u);
    for(size_t i = 0; i < direct.positions.size(); ++i)
    {
        const float packed[4] = {direct.positions[i].x, direct.positions[i].y, direct.positions[i].z, 0.0f};
        gfx::vertex_pack(packed, false, gfx::attribute::Position, layout, vertex_data.data(), uint32_t(i));
    }
    sdf_source_geometry extracted;
    check(extract_sdf_source_geometry(vertex_data.data(),
                                      uint32_t(direct.positions.size()),
                                      layout,
                                      direct.indices.data(),
                                      direct.get_triangle_count(),
                                      extracted),
          "extraction from raw buffers succeeds");
    check(extracted.positions.size() == direct.positions.size(), "vertex count round-trips");
    check(extracted.indices == direct.indices, "indices round-trip");
    check(extracted.bounds.min == direct.bounds.min, "bounds min round-trips");
    check(extracted.bounds.max == direct.bounds.max, "bounds max round-trips");
    // And the field baked from it must match the one baked from the geometry directly.
    mesh_sdf_bake_settings settings;
    settings.resolution = 24;
    settings.min_voxel_size = 0.001f;
    mesh_sdf from_direct;
    mesh_sdf from_extracted;
    check(bake_mesh_sdf(direct, settings, from_direct), "direct bake succeeds");
    check(bake_mesh_sdf(extracted, settings, from_extracted), "extracted bake succeeds");
    check(from_direct.brick_voxels == from_extracted.brick_voxels, "both routes bake the same voxels");
    std::printf("  vertices = %zu, triangles = %u, bricks = %u\n",
                extracted.positions.size(),
                extracted.get_triangle_count(),
                from_extracted.get_surface_brick_count());
}

// ---------------------------------------------------------------------------------------
// Radiance cache
// ---------------------------------------------------------------------------------------

auto make_cache() -> radiance_cache
{
    radiance_cache cache;
    radiance_cache::settings settings;
    settings.capacity = 1u << 14;
    settings.base_cell_size = 0.25f;
    settings.base_distance = 8.0f;
    cache.init(settings);
    return cache;
}

void test_cache_keys_are_camera_independent()
{
    std::printf("test_cache_keys_are_camera_independent\n");
    // The property the whole world-space approach rests on. An entry is addressed by where a
    // surface is and which way it faces, never by anything derived from the camera, so the same
    // point must resolve to the same entry from any viewpoint. If this fails, cached lighting
    // is rebuilt as the camera moves and the result crawls -- exactly the screen-space failure
    // this design exists to avoid.
    const auto cache = make_cache();
    int mismatches = 0;
    for(int i = 0; i < 20000; ++i)
    {
        const float t = float(i) / 20000.0f;
        const math::vec3 p(10.0f * std::sin(t * 47.0f), 3.0f * std::cos(t * 29.0f), 10.0f * std::sin(t * 13.0f));
        const math::vec3 n =
            math::normalize(math::vec3(std::sin(t * 91.0f), std::cos(t * 57.0f), std::sin(t * 33.0f) + 0.1f));
        // Two cameras at very different places, but the SAME level, so only the camera-derived
        // part of the key is being varied.
        const uint32_t level = 2;
        if(cache.compute_key(p, n, level) != cache.compute_key(p, n, level))
        {
            ++mismatches;
        }
    }
    check(mismatches == 0, "the key is a pure function of position, normal and level");
    // And distinct cells must not collide wholesale.
    std::vector<uint32_t> keys;
    keys.reserve(8000);
    for(int i = 0; i < 20; ++i)
    {
        for(int j = 0; j < 20; ++j)
        {
            for(int k = 0; k < 20; ++k)
            {
                const math::vec3 p(float(i) * 0.3f, float(j) * 0.3f, float(k) * 0.3f);
                keys.push_back(cache.compute_key(p, math::vec3(0.0f, 1.0f, 0.0f), 0));
            }
        }
    }
    std::sort(keys.begin(), keys.end());
    const size_t total = keys.size();
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    const size_t unique_keys = keys.size();
    // Cells are 0.25 and samples are 0.3 apart, so most but not all are distinct cells; what
    // matters is that the hash is not collapsing them.
    std::printf("  %zu samples -> %zu unique keys\n", total, unique_keys);
    check(unique_keys > total / 2, "distinct cells produce distinct keys");
}

void test_cache_separates_opposing_faces()
{
    std::printf("test_cache_separates_opposing_faces\n");
    // The two sides of a thin wall occupy the same cell but face opposite ways. If they shared
    // an entry, the lit side's radiance would be served to the dark side -- light leaking
    // through the wall, via the cache rather than via the trace.
    const auto cache = make_cache();
    const math::vec3 p(1.0f, 2.0f, 3.0f);
    const uint32_t front = cache.compute_key(p, math::vec3(0.0f, 0.0f, 1.0f), 0);
    const uint32_t back = cache.compute_key(p, math::vec3(0.0f, 0.0f, -1.0f), 0);
    const uint32_t up = cache.compute_key(p, math::vec3(0.0f, 1.0f, 0.0f), 0);
    check(front != back, "opposite normals in one cell get different entries");
    check(front != up, "perpendicular normals in one cell get different entries");
    // But a flat surface must not fragment: nearby normals on one plane should share an entry,
    // or convergence is spread across many entries for no benefit.
    const uint32_t nearly_up = cache.compute_key(p, math::normalize(math::vec3(0.05f, 1.0f, 0.03f)), 0);
    check(up == nearly_up, "small normal variation on a flat surface shares an entry");
}

void test_cache_binning_is_stable_at_axis_aligned_normals()
{
    std::printf("test_cache_binning_is_stable_at_axis_aligned_normals\n");
    // Axis-aligned normals are the overwhelmingly common case in a built scene, and a writer and
    // a reader obtain them from different sources -- the G-buffer versus the field gradient --
    // so they agree only to within an epsilon. Any binning whose boundary passes through an axis
    // direction therefore splits the most common surfaces in the scene at random, and every large
    // flat face becomes a permanent cache miss.
    //
    // The earlier 2x2-per-face subdivision failed exactly here, and the single-sided perturbation
    // in the test above did not catch it. Perturb in EVERY sign combination.
    const auto cache = make_cache();
    const math::vec3 p(1.0f, 2.0f, 3.0f);
    const math::vec3 axes[] = {math::vec3(1.0f, 0.0f, 0.0f),
                               math::vec3(-1.0f, 0.0f, 0.0f),
                               math::vec3(0.0f, 1.0f, 0.0f),
                               math::vec3(0.0f, -1.0f, 0.0f),
                               math::vec3(0.0f, 0.0f, 1.0f),
                               math::vec3(0.0f, 0.0f, -1.0f)};
    int mismatches = 0;
    for(const auto& axis : axes)
    {
        const uint32_t expected = cache.compute_key(p, axis, 0);
        for(int sx = -1; sx <= 1; ++sx)
        {
            for(int sy = -1; sy <= 1; ++sy)
            {
                for(int sz = -1; sz <= 1; ++sz)
                {
                    const math::vec3 jitter(float(sx) * 1e-6f, float(sy) * 1e-6f, float(sz) * 1e-6f);
                    const uint32_t actual = cache.compute_key(p, math::normalize(axis + jitter), 0);
                    if(actual != expected)
                    {
                        ++mismatches;
                    }
                }
            }
        }
    }
    check(mismatches == 0, "an epsilon of normal noise never changes an axis-aligned surface's entry");
}

void test_cache_cells_do_not_split_a_surface_on_a_grid_plane()
{
    std::printf("test_cache_cells_do_not_split_a_surface_on_a_grid_plane\n");
    // A ground plane at y = 0 lies exactly on a cell boundary, so without the half-cell shift the
    // grid plane runs THROUGH the surface and the sign of a rounding error picks the cell. That
    // is systematic rather than occasional: it splits the whole floor, which is the single
    // largest surface in most scenes.
    const auto cache = make_cache();
    const float cell = cache.get_settings().base_cell_size;
    const math::vec3 up(0.0f, 1.0f, 0.0f);
    int mismatches = 0;
    for(int i = 0; i < 400; ++i)
    {
        // Walk along the plane so the x cell genuinely varies; only y is degenerate.
        const float x = float(i) * cell * 0.37f;
        const uint32_t expected = cache.compute_key(math::vec3(x, 0.0f, 0.0f), up, 0);
        const uint32_t below = cache.compute_key(math::vec3(x, -1e-6f, 0.0f), up, 0);
        const uint32_t above = cache.compute_key(math::vec3(x, 1e-6f, 0.0f), up, 0);
        if(below != expected || above != expected)
        {
            ++mismatches;
        }
    }
    check(mismatches == 0, "an epsilon of position noise never splits a surface lying on a cell plane");
    // The shift must not cost the leak protection it sits beside: a floor and a ceiling meeting at
    // the same plane still have to land in different entries.
    const math::vec3 down(0.0f, -1.0f, 0.0f);
    const uint32_t floor_key = cache.compute_key(math::vec3(0.0f), up, 0);
    const uint32_t ceiling_key = cache.compute_key(math::vec3(0.0f), down, 0);
    check(floor_key != ceiling_key, "the half-cell shift still separates opposing faces on one plane");
}

void test_cache_find_surface_tolerates_a_boundary_facing()
{
    std::printf("test_cache_find_surface_tolerates_a_boundary_facing\n");
    // A 45-degree surface sits between two cube faces, so a writer and a reader can order the top
    // two differently. find_surface probes both, which covers the writer's choice either way.
    auto cache = make_cache();
    const math::vec3 p(4.0f, 1.5f, -2.0f);
    const math::vec3 written = math::normalize(math::vec3(1.0f, 1.0f + 1e-6f, 0.0f));
    const math::vec3 read = math::normalize(math::vec3(1.0f, 1.0f - 1e-6f, 0.0f));
    const uint32_t written_key = cache.compute_key(p, written, 0);
    check(written_key != cache.compute_key(p, read, 0),
          "the two facings really do bin differently, so this exercises the fallback");
    const uint32_t slot = cache.insert(written_key, 1);
    check(slot != radiance_cache::invalid_slot, "the writer's entry is resident");
    check(cache.find_surface(p, read, 0) == slot, "a reader on the other side of the boundary finds it");
    check(cache.find_surface(p, written, 0) == slot, "and the writer's own facing still finds it");
}

void test_cache_level_is_stable_within_a_band()
{
    std::printf("test_cache_level_is_stable_within_a_band\n");
    // Cells grow with distance so the cache stays bounded regardless of world size. The level
    // has to be a STEP function: if it varied continuously, the grid would reshape under the
    // cached values every time the camera moved at all.
    const auto cache = make_cache();
    const math::vec3 camera(0.0f);
    uint32_t transitions = 0;
    uint32_t previous = cache.compute_level(math::vec3(0.0f, 0.0f, 0.1f), camera);
    for(int i = 1; i < 4000; ++i)
    {
        const float distance = 0.1f + float(i) * 0.05f;
        const uint32_t level = cache.compute_level(math::vec3(0.0f, 0.0f, distance), camera);
        check(level >= previous, "level never decreases with distance");
        transitions += level != previous ? 1u : 0u;
        previous = level;
    }
    std::printf("  transitions over 200 units = %u, max level = %u\n", transitions, previous);
    check(transitions <= cache.get_settings().max_level, "level changes at most once per octave");
    check(cache.get_cell_size(1) == cache.get_cell_size(0) * 2.0f, "cells double each level");
}

void test_cache_insert_find_and_evict()
{
    std::printf("test_cache_insert_find_and_evict\n");
    auto cache = make_cache();
    // Insert, then find what was inserted.
    const uint32_t slot = cache.insert(12345u, 1u);
    check(slot != radiance_cache::invalid_slot, "insert succeeds into an empty cache");
    check(cache.find(12345u) == slot, "find returns the inserted slot");
    check(cache.find(999u) == radiance_cache::invalid_slot, "find misses an absent key");
    check(cache.insert(12345u, 2u) == slot, "re-inserting an existing key reuses its slot");
    // Fill many cells and confirm the structure stays healthy rather than degenerating.
    auto busy = make_cache();
    uint32_t inserted = 0;
    for(uint32_t i = 0; i < 8000; ++i)
    {
        const math::vec3 p(float(i % 20) * 0.3f, float((i / 20) % 20) * 0.3f, float(i / 400) * 0.3f);
        const uint32_t key = busy.compute_key(p, math::vec3(0.0f, 1.0f, 0.0f), 0);
        if(busy.insert(key, 1u) != radiance_cache::invalid_slot)
        {
            ++inserted;
        }
    }
    std::printf("  inserted %u of 8000 into a %u-entry cache, %u occupied\n",
                inserted,
                busy.get_capacity(),
                busy.count_occupied());
    check(inserted > 7000, "nearly every insert succeeds well below capacity");
    // An entry touched this frame must never be evicted, or two cells contending for one chain
    // would replace each other every frame and neither would ever accumulate.
    auto contended = make_cache();
    const uint32_t frame = 7u;
    uint32_t survivors = 0;
    for(uint32_t i = 0; i < radiance_cache::probe_length + 4u; ++i)
    {
        // Keys chosen to land on the same base slot, forcing contention.
        const uint32_t key = 1u + i * contended.get_capacity();
        if(contended.insert(key, frame) != radiance_cache::invalid_slot)
        {
            ++survivors;
        }
    }
    std::printf("  same-chain inserts in one frame: %u accepted of %u\n",
                survivors,
                radiance_cache::probe_length + 4u);
    check(survivors == radiance_cache::probe_length, "a full chain refuses rather than thrashing");
}

void test_cache_accumulation_converges_and_stays_responsive()
{
    std::printf("test_cache_accumulation_converges_and_stays_responsive\n");
    auto cache = make_cache();
    const uint32_t slot = cache.insert(42u, 1u);
    const math::vec3 target(1.0f, 0.5f, 0.25f);
    // A fresh entry must converge fast, or newly visible geometry stays dark for many frames.
    for(uint32_t i = 0; i < 16; ++i)
    {
        cache.accumulate(slot, target, i + 1u, 0.05f, 32u);
    }
    const math::vec3 converged = cache.get_entry(slot).radiance;
    std::printf("  after 16 samples: (%.4f %.4f %.4f)\n", converged.x, converged.y, converged.z);
    check(math::length(converged - target) < 0.02f, "a fresh entry converges within a few frames");
    // And a mature entry must still respond to change, or a light switching off stays visible
    // forever. This is what the alpha floor is for.
    const math::vec3 changed(0.0f, 0.0f, 0.0f);
    for(uint32_t i = 0; i < 200; ++i)
    {
        cache.accumulate(slot, changed, 100u + i, 0.05f, 32u);
    }
    const math::vec3 after = cache.get_entry(slot).radiance;
    std::printf("  after the source changes: (%.4f %.4f %.4f)\n", after.x, after.y, after.z);
    check(math::length(after) < 0.02f, "a mature entry still follows a change in the source");
}

/**
 * @brief Literal re-transcription of gi/radiance_cache.sh, for comparison against the C++.
 *
 * The shader cannot be executed here, but the two expressions can be checked for equivalence,
 * and that is where the bugs in a transcription actually live: operator precedence, signed
 * versus unsigned conversion of a negative coordinate, and shift semantics. A key that differs
 * by one bit between the writer and the reader never finds anything, which presents as an empty
 * cache rather than as a transcription error.
 *
 * Kept deliberately verbatim -- matching the shader line for line, not tidied -- so a diff
 * against the .sh file is meaningful.
 */
namespace shader_mirror
{

auto GiHashUint(uint32_t value) -> uint32_t
{
    uint32_t state = value * 747796405u + 2891336453u;
    uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

auto GiHashCombine(uint32_t seed, uint32_t value) -> uint32_t
{
    return GiHashUint(seed ^ (value + 0x9e3779b9u + (seed << 6u) + (seed >> 2u)));
}

auto GiFaceFromAxis(const math::vec3& normal, uint32_t axis) -> uint32_t
{
    float axis_value = normal.z;
    if(axis == 0u)
    {
        axis_value = normal.x;
    }
    else if(axis == 1u)
    {
        axis_value = normal.y;
    }
    return axis * 2u + (axis_value < 0.0f ? 1u : 0u);
}

auto GiDominantAxis(const math::vec3& magnitude) -> uint32_t
{
    if(magnitude.y > magnitude.x && magnitude.y >= magnitude.z)
    {
        return 1u;
    }
    if(magnitude.z > magnitude.x && magnitude.z >= magnitude.y)
    {
        return 2u;
    }
    return 0u;
}

auto GiQuantizeNormal(const math::vec3& normal) -> uint32_t
{
    return GiFaceFromAxis(normal, GiDominantAxis(math::abs(normal)));
}

auto GiFaceDirection(uint32_t face) -> math::vec3
{
    const float face_sign = (face & 1u) != 0u ? -1.0f : 1.0f;
    math::vec3 direction(0.0f);
    direction[int(face >> 1u)] = face_sign;
    return direction;
}

auto GiCacheKey(const math::vec3& position, const math::vec3& normal, uint32_t level, float base_cell)
    -> uint32_t
{
    const float cell_size = base_cell * float(1u << level);
    const uint32_t face = GiQuantizeNormal(normal);
    const math::vec3 snapped = position + GiFaceDirection(face) * (cell_size * 0.5f);
    const math::vec3 cell = math::floor(snapped / cell_size);
    uint32_t key = GiHashUint(uint32_t(int(cell.x)));
    key = GiHashCombine(key, uint32_t(int(cell.y)));
    key = GiHashCombine(key, uint32_t(int(cell.z)));
    key = GiHashCombine(key, level);
    key = GiHashCombine(key, face);
    return key == 0u ? 1u : key;
}

auto GiCacheLevel(const math::vec3& position, const math::vec3& camera, float base_distance, float max_level)
    -> uint32_t
{
    const float distance = math::length(position - camera);
    if(distance <= base_distance)
    {
        return 0u;
    }
    const float ratio = distance / base_distance;
    const float level = std::floor(std::log2(ratio)) + 1.0f;
    return uint32_t(math::clamp(level, 0.0f, max_level));
}

} // namespace shader_mirror

void test_cache_shader_transcription_matches_cpu()
{
    std::printf("test_cache_shader_transcription_matches_cpu\n");
    const auto cache = make_cache();
    const auto& settings = cache.get_settings();
    int key_mismatches = 0;
    int normal_mismatches = 0;
    int level_mismatches = 0;
    const math::vec3 camera(3.0f, 2.0f, -5.0f);
    for(int i = 0; i < 40000; ++i)
    {
        const float t = float(i) / 40000.0f;
        // Deliberately spans negative coordinates, where a truncation-versus-floor mistake and
        // a signed-to-unsigned conversion mistake both show up and nowhere else.
        const math::vec3 p(60.0f * std::sin(t * 47.0f) - 20.0f,
                           40.0f * std::cos(t * 29.0f) - 15.0f,
                           60.0f * std::sin(t * 13.0f) - 25.0f);
        const math::vec3 n =
            math::normalize(math::vec3(std::sin(t * 91.0f), std::cos(t * 57.0f), std::sin(t * 33.0f) + 0.1f));
        if(quantize_normal(n) != shader_mirror::GiQuantizeNormal(n))
        {
            ++normal_mismatches;
        }
        const uint32_t level = cache.compute_level(p, camera);
        if(level != shader_mirror::GiCacheLevel(p, camera, settings.base_distance, float(settings.max_level)))
        {
            ++level_mismatches;
        }
        if(cache.compute_key(p, n, level) !=
           shader_mirror::GiCacheKey(p, n, level, settings.base_cell_size))
        {
            ++key_mismatches;
        }
    }
    std::printf("  mismatches over 40000 samples: keys %d, normals %d, levels %d\n",
                key_mismatches,
                normal_mismatches,
                level_mismatches);
    check(normal_mismatches == 0, "normal quantisation transcribes exactly");
    check(level_mismatches == 0, "level selection transcribes exactly");
    check(key_mismatches == 0, "key derivation transcribes exactly");
}

// ---------------------------------------------------------------------------------------
// Per-submesh extraction
// ---------------------------------------------------------------------------------------

/// Local-space gap between the synthetic submeshes below. Any value larger than a box is
/// fine; it only has to keep the boxes disjoint so a bounds check can tell them apart.
constexpr float submesh_spacing = 4.0f;

/**
 * @brief Builds a load_data shaped like a real imported model.
 *
 * Many small submeshes over a handful of MATERIALS, all sharing one vertex buffer. That is
 * what a scene model actually looks like (Bistro is thousands of submeshes over a few dozen
 * materials), and it is the shape that separates "select the triangles of one submesh" from
 * "select the triangles of one material" -- on every single-submesh test asset the two are
 * indistinguishable.
 */
auto make_multi_submesh_load_data(uint32_t submesh_count, uint32_t material_count) -> mesh::load_data
{
    mesh::load_data data;
    data.vertex_format.begin(bgfx::RendererType::Noop)
        .add(gfx::attribute::Position, 3, gfx::attribute_type::Float)
        .end();
    const auto box = make_box(math::vec3(0.5f));
    const uint32_t box_vertices = uint32_t(box.positions.size());
    const uint32_t box_triangles = box.get_triangle_count();
    data.vertex_count = submesh_count * box_vertices;
    data.vertex_data.assign(size_t(data.vertex_count) * data.vertex_format.getStride(), 0u);
    data.triangle_data.reserve(size_t(submesh_count) * box_triangles);
    data.submeshes.reserve(submesh_count);
    data.bbox.reset();
    for(uint32_t s = 0; s < submesh_count; ++s)
    {
        // The importer appends each submesh's vertices to the shared buffer and offsets its
        // indices, so a submesh's corners reference one contiguous range.
        const uint32_t vertex_offset = s * box_vertices;
        const math::vec3 offset(float(s) * submesh_spacing, 0.0f, 0.0f);
        for(uint32_t v = 0; v < box_vertices; ++v)
        {
            const math::vec3 p = box.positions[v] + offset;
            const float packed[4] = {p.x, p.y, p.z, 0.0f};
            gfx::vertex_pack(packed,
                             false,
                             gfx::attribute::Position,
                             data.vertex_format,
                             data.vertex_data.data(),
                             vertex_offset + v);
            data.bbox.add_point(p);
        }
        auto& submesh = data.submeshes.emplace_back();
        submesh.data_group_id = s % material_count;
        submesh.vertex_start = int32_t(vertex_offset);
        submesh.vertex_count = box_vertices;
        submesh.face_start = int32_t(data.triangle_data.size());
        submesh.face_count = box_triangles;
        for(uint32_t t = 0; t < box_triangles; ++t)
        {
            auto& tri = data.triangle_data.emplace_back();
            tri.data_group_id = submesh.data_group_id;
            tri.indices[0] = box.indices[t * 3 + 0] + vertex_offset;
            tri.indices[1] = box.indices[t * 3 + 1] + vertex_offset;
            tri.indices[2] = box.indices[t * 3 + 2] + vertex_offset;
        }
    }
    data.triangle_count = uint32_t(data.triangle_data.size());
    return data;
}

void test_submesh_extraction_selects_only_its_own_submesh()
{
    std::printf("test_submesh_extraction_selects_only_its_own_submesh\n");
    // Submeshes deliberately outnumber materials. A field is placed at its submesh's node
    // transform, so pulling in a sibling that merely shares a material bakes that sibling's
    // geometry a second time at the wrong place -- the phantom-copy failure again, and it
    // also makes the bake quadratic, since every submesh then re-bakes its whole material.
    constexpr uint32_t submesh_count = 64;
    constexpr uint32_t material_count = 4;
    const auto data = make_multi_submesh_load_data(submesh_count, material_count);
    const uint32_t expected_triangles = data.submeshes[0].face_count;
    size_t extracted_triangles = 0;
    bool all_extracted = true;
    bool all_own_triangle_count = true;
    bool all_own_bounds = true;
    for(uint32_t s = 0; s < submesh_count; ++s)
    {
        sdf_source_geometry g;
        if(!extract_sdf_source_geometry(data, data.submeshes[s], g))
        {
            all_extracted = false;
            continue;
        }
        extracted_triangles += g.get_triangle_count();
        all_own_triangle_count = all_own_triangle_count && g.get_triangle_count() == expected_triangles;
        const math::vec3 center = (g.bounds.min + g.bounds.max) * 0.5f;
        all_own_bounds = all_own_bounds && std::fabs(center.x - float(s) * submesh_spacing) < 1e-3f;
    }
    check(all_extracted, "every submesh extracts");
    check(all_own_triangle_count, "each submesh extracts only its own triangles");
    check(all_own_bounds, "each submesh's bounds are its own, not its material group's");
    // The complexity assertion, stated as data rather than as a timing threshold so it holds
    // on any machine: the whole per-submesh pass must touch each triangle exactly once. Any
    // selection that widens to a material -- or that rescans the model per submesh -- makes
    // this superlinear, which is what turned a seconds-long bake into a minutes-long one.
    check(extracted_triangles == data.triangle_count,
          "the per-submesh pass extracts each model triangle exactly once");
    std::printf("  %u submeshes over %u materials, %zu triangles extracted (model has %u)\n",
                submesh_count,
                material_count,
                extracted_triangles,
                data.triangle_count);
}

void test_unmapped_submesh_reports_no_transforms()
{
    std::printf("test_unmapped_submesh_reports_no_transforms\n");
    // The discriminator the GI registration uses to decide between "draw this submesh at its own
    // node transforms" and "draw it at the model's transform". It has to be asked about THE
    // SUBMESH, because the outer list is sized to the submesh count up front and says nothing
    // about whether any submesh was actually mapped.
    //
    // That distinction is the whole bug it guards: a primitive has no child entity carrying a
    // submesh_component, so its pose is reserved and never mapped. Testing the outer list reads
    // as "the hierarchy resolved" and is TRUE there, so a check written that way places no field
    // at all and the primitive disappears from GI while still rendering perfectly.
    submesh_pose_mat4 pose;
    pose.reserve(1);
    check(!pose.submesh_to_transform_indices.empty(), "reserve populates the outer list");
    check(!pose.has_transforms(0), "an unmapped submesh still reports no transforms of its own");
    check(pose.get_transform_count(0) == 0, "and no transform instances");
    // Once mapped, the same submesh answers the other way, and an inactive instance resolves to
    // null exactly as it does for the renderer -- a submesh switched off is not drawn, so it must
    // not occlude or bounce light either.
    submesh_pose_mat4 mapped;
    mapped.reserve(2);
    const uint32_t active_index = mapped.add_transform(math::mat4(1.0f));
    const uint32_t inactive_index = mapped.add_transform(math::mat4(1.0f));
    mapped.map_submesh(0, active_index, true, true);
    mapped.map_submesh(1, inactive_index, false, true);
    check(mapped.has_transforms(0), "a mapped submesh reports its transforms");
    check(mapped.get_transform(0, 0) != nullptr, "an active instance resolves to a transform");
    check(mapped.has_transforms(1), "an inactive instance is still mapped");
    check(mapped.get_transform(1, 0) == nullptr, "but resolves to null, so it places no field");
}

void test_submesh_bake_pass_cost_is_linear()
{
    std::printf("test_submesh_bake_pass_cost_is_linear\n");
    // Times the extract-and-bake pass the asset compiler runs, at two submesh counts over a
    // FIXED material count. Per-submesh work is constant here (every submesh is the same
    // box), so linear scaling means the pass is O(model); anything that widens selection to
    // the material group or rescans the model per submesh shows up as a growing ratio.
    mesh_sdf_bake_settings settings;
    settings.resolution = 16;
    const auto run = [&](uint32_t submesh_count) -> double
    {
        const auto data = make_multi_submesh_load_data(submesh_count, 4);
        const auto start = std::chrono::steady_clock::now();
        uint32_t baked = 0;
        for(uint32_t s = 0; s < submesh_count; ++s)
        {
            sdf_source_geometry g;
            if(!extract_sdf_source_geometry(data, data.submeshes[s], g))
            {
                continue;
            }
            mesh_sdf field;
            if(bake_mesh_sdf(g, settings, field))
            {
                ++baked;
            }
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("  %4u submeshes: %8.1f ms (%u baked, %.3f ms each)\n",
                    submesh_count,
                    ms,
                    baked,
                    ms / double(submesh_count));
        check(baked == submesh_count, "every submesh bakes a field");
        return ms;
    };
    // Not named `small` / `large`: <rpcndr.h> defines `small` as a macro on Windows.
    const double few_submeshes = run(64);
    const double many_submeshes = run(256);
    // Four times the submeshes must not cost much more than four times the time. The bound is
    // loose because it competes with whatever else the machine is doing; it is here to catch a
    // change of COMPLEXITY (the observed failure was ~16x for 4x the submeshes), not to police
    // a constant factor.
    const double ratio = many_submeshes / math::max(few_submeshes, 1e-3);
    std::printf("  4x the submeshes cost %.2fx the time\n", ratio);
    check(ratio < 8.0, "the per-submesh bake pass scales linearly with submesh count");
}

void test_bake_grid_scales_with_world_size()
{
    std::printf("test_bake_grid_scales_with_world_size\n");
    // `resolution` is a TARGET voxel count along the longest axis, so on its own it would give a
    // 10 cm bolt the same 64x64x64 grid as a 5 m wall. Voxel count is cubic, so on a model made
    // of thousands of small parts that is a large constant factor spent representing nothing.
    //
    // min_voxel_size is what prevents it, and the property that matters is that clamping the
    // voxel SIZE also bounds the voxel COUNT -- a clamp that only changed the spacing while the
    // grid stayed at the target resolution would fix nothing.
    mesh_sdf_bake_settings settings;
    settings.resolution = 64;
    settings.min_voxel_size = 0.01f;
    settings.max_voxel_size = 1.0f;
    mesh_sdf tiny_field;
    mesh_sdf large_field;
    check(bake_mesh_sdf(make_box(math::vec3(0.05f)), settings, tiny_field), "10 cm box bakes");
    check(bake_mesh_sdf(make_box(math::vec3(1.0f)), settings, large_field), "2 m box bakes");
    check_near(tiny_field.voxel_size, settings.min_voxel_size, 1e-6f, "the small box is clamped to min_voxel_size");
    check(large_field.voxel_size > settings.min_voxel_size, "the large box is not clamped");
    check(tiny_field.grid_dim.x < large_field.grid_dim.x,
          "clamping the voxel size bounds the voxel count, not just the spacing");
    std::printf("  0.1 m box: %ux%ux%u voxels, %u surface bricks (voxel %.4f)\n",
                tiny_field.grid_dim.x,
                tiny_field.grid_dim.y,
                tiny_field.grid_dim.z,
                tiny_field.get_surface_brick_count(),
                tiny_field.voxel_size);
    std::printf("  2.0 m box: %ux%ux%u voxels, %u surface bricks (voxel %.4f)\n",
                large_field.grid_dim.x,
                large_field.grid_dim.y,
                large_field.grid_dim.z,
                large_field.get_surface_brick_count(),
                large_field.voxel_size);
}

void test_total_voxel_budget_bounds_a_field()
{
    std::printf("test_total_voxel_budget_bounds_a_field\n");
    // The per-axis cap bounds a field's SHAPE, not its cost: max_resolution^3 voxels is still
    // permitted in one field, which is more than the whole scene's atlas holds. Both bake time
    // and atlas footprint scale with voxel count, so the total cap is the one that makes a
    // field's cost bounded.
    //
    // The mesh here is large enough that max_voxel_size (1.0) clamps the derived voxel size,
    // which is exactly how a field escapes its resolution target and runs to the per-axis cap.
    mesh_sdf_bake_settings settings;
    settings.resolution = 64;
    settings.min_voxel_size = 0.01f;
    settings.max_voxel_size = 1.0f;
    settings.max_resolution = 256;
    const auto geometry = make_box(math::vec3(60.0f));
    mesh_sdf unbudgeted;
    settings.max_total_voxels = uint64_t(1) << 40;
    check(bake_mesh_sdf(geometry, settings, unbudgeted), "unbudgeted bake succeeds");
    mesh_sdf budgeted;
    settings.max_total_voxels = 262144;
    check(bake_mesh_sdf(geometry, settings, budgeted), "budgeted bake succeeds");
    const auto count_voxels = [](const mesh_sdf& sdf) -> uint64_t
    {
        return uint64_t(sdf.grid_dim.x) * sdf.grid_dim.y * sdf.grid_dim.z;
    };
    check(count_voxels(unbudgeted) > 262144, "without the budget the field runs past it");
    check(count_voxels(budgeted) <= 262144, "the budget bounds the total voxel count");
    // Coarser, never cropped: a field that stopped covering its mesh would let rays pass
    // straight through the geometry.
    check(budgeted.voxel_size > unbudgeted.voxel_size, "the budget is met by coarsening, not cropping");
    check(budgeted.bounds.min.x <= geometry.bounds.min.x && budgeted.bounds.max.x >= geometry.bounds.max.x,
          "the budgeted field still covers the whole mesh");
    check(budgeted.get_surface_brick_count() < unbudgeted.get_surface_brick_count(),
          "and costs fewer bricks, which is what the atlas is short of");
    std::printf("  unbudgeted: %ux%ux%u voxels, %u bricks -- budgeted: %ux%ux%u voxels, %u bricks\n",
                unbudgeted.grid_dim.x,
                unbudgeted.grid_dim.y,
                unbudgeted.grid_dim.z,
                unbudgeted.get_surface_brick_count(),
                budgeted.grid_dim.x,
                budgeted.grid_dim.y,
                budgeted.grid_dim.z,
                budgeted.get_surface_brick_count());
}

void test_lod_extraction_clamps_rather_than_failing()
{
    std::printf("test_lod_extraction_clamps_rather_than_failing\n");
    constexpr uint32_t submesh_count = 4;
    auto data = make_multi_submesh_load_data(submesh_count, 2);
    check(data.lods.empty(), "the fixture starts with no generated LODs");
    sdf_source_geometry base;
    check(extract_sdf_source_geometry(data, data.submeshes[1], base), "base extraction succeeds");
    // With nothing generated, every request resolves to the base. Baking a coarser field is an
    // optimisation, so declining it must cost detail and never the field itself -- a silent
    // failure would remove the mesh from GI while it kept rendering normally, which is the
    // hardest shape of bug to attribute.
    sdf_source_geometry from_missing_lod;
    check(extract_sdf_source_geometry(data, 3, 1, from_missing_lod), "a missing LOD still extracts");
    check(from_missing_lod.indices == base.indices, "and yields the base topology rather than nothing");
    // LOD 0 is the base by definition, not a lookup into the generated levels.
    sdf_source_geometry from_lod0;
    check(extract_sdf_source_geometry(data, 0, 1, from_lod0), "LOD 0 extracts");
    check(from_lod0.indices == base.indices, "LOD 0 is the base topology");
    // Now give it ONE generated level, distinguishable from the base by triangle count. A request
    // past it must land on that level, not fall back to the base: asking for a higher LOD means
    // "cheaper", and full detail is the most expensive possible answer to that.
    //
    // data.lods holds the GENERATED levels only, so this single entry is LOD 1.
    auto& lod = data.lods.emplace_back();
    lod.submeshes = data.submeshes;
    lod.index_data.reserve(data.triangle_data.size() * 3);
    for(auto& lod_submesh : lod.submeshes)
    {
        // Half the faces of each submesh, which is roughly what a real LOD 1 targets.
        const uint32_t kept = math::max(1u, lod_submesh.face_count / 2u);
        const auto base_face_start = uint32_t(lod_submesh.face_start);
        lod_submesh.face_start = int32_t(lod.index_data.size() / 3);
        for(uint32_t face = 0; face < kept; ++face)
        {
            const auto& tri = data.triangle_data[base_face_start + face];
            lod.index_data.insert(lod.index_data.end(), {tri.indices[0], tri.indices[1], tri.indices[2]});
        }
        lod_submesh.face_count = kept;
    }
    lod.face_count = uint32_t(lod.index_data.size() / 3);
    sdf_source_geometry from_lod1;
    sdf_source_geometry from_clamped;
    check(extract_sdf_source_geometry(data, 1, 1, from_lod1), "LOD 1 extracts");
    check(extract_sdf_source_geometry(data, 5, 1, from_clamped), "a request past the last level extracts");
    check(from_lod1.get_triangle_count() < base.get_triangle_count(), "LOD 1 is simpler than the base");
    check(from_clamped.indices == from_lod1.indices,
          "a request past the last level clamps to the coarsest available, not to the base");
    // Out-of-range submeshes are rejected, not clamped: a field placed against the wrong submesh
    // would be geometry at the wrong transform.
    sdf_source_geometry out_of_range;
    check(!extract_sdf_source_geometry(data, 0, submesh_count, out_of_range),
          "an out-of-range submesh is rejected");
    std::printf("  base %u triangles, LOD 1 %u, request for LOD 5 resolved to %u\n",
                base.get_triangle_count(),
                from_lod1.get_triangle_count(),
                from_clamped.get_triangle_count());
}

void test_bake_cost_is_dominated_by_voxels_not_triangles()
{
    std::printf("test_bake_cost_is_dominated_by_voxels_not_triangles\n");
    // Settles what baking from a lower LOD can and cannot buy. Both spheres have the same radius,
    // so they produce an IDENTICAL grid and identical voxel work; only the triangle count differs,
    // by 16x. Whatever separates the two timings is all a simplified mesh could ever recover.
    //
    // The expectation is that it recovers little: the accelerator build is linear-ish and tiny
    // next to the voxel passes, and a closest-point query is logarithmic in triangles, so 16x the
    // triangles is a couple of extra BVH levels rather than 16x the work.
    mesh_sdf_bake_settings settings;
    settings.resolution = 48;
    settings.min_voxel_size = 0.001f;
    const auto measure = [&](int rings, int sectors) -> double
    {
        const auto geometry = make_sphere(1.0f, rings, sectors);
        mesh_sdf sdf;
        const auto start = std::chrono::steady_clock::now();
        const bool baked = bake_mesh_sdf(geometry, settings, sdf, sdf_bake_threading::serial);
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        check(baked, "sphere bakes");
        std::printf("  %6u triangles: %7.1f ms (%ux%ux%u voxels, %u surface bricks)\n",
                    geometry.get_triangle_count(),
                    ms,
                    sdf.grid_dim.x,
                    sdf.grid_dim.y,
                    sdf.grid_dim.z,
                    sdf.get_surface_brick_count());
        return ms;
    };
    const double coarse = measure(16, 24);
    const double fine = measure(64, 96);
    const double ratio = fine / math::max(coarse, 1e-3);
    std::printf("  16x the triangles cost %.2fx the time\n", ratio);
    // Strongly sublinear in triangles. Stated loosely because it competes with whatever else the
    // machine is doing; the point is the ORDER -- if this ever approached 16x, the query would
    // have stopped pruning and the BVH would be the thing to fix, not the resolution.
    check(ratio < 4.0, "bake time is sublinear in triangle count");
}

void test_parallel_submesh_bake_matches_serial()
{
    std::printf("test_parallel_submesh_bake_matches_serial\n");
    // Mirrors the loop the asset compiler runs: submeshes in parallel, each individual bake
    // serial. That nesting rule is invisible in the output -- breaking it deadlocks the whole
    // pool rather than producing a wrong field -- so it is worth pinning here, where it runs in
    // milliseconds, instead of discovering it on a model with thousands of parts.
    constexpr uint32_t submesh_count = 128;
    const auto data = make_multi_submesh_load_data(submesh_count, 4);
    mesh_sdf_bake_settings settings;
    settings.resolution = 16;
    const auto bake_all = [&](const char* label, bool parallel_submeshes, sdf_bake_threading threading)
        -> std::vector<mesh_sdf>
    {
        std::vector<mesh_sdf> fields(submesh_count);
        std::vector<size_t> order(submesh_count);
        std::iota(order.begin(), order.end(), size_t(0));
        const auto start = std::chrono::steady_clock::now();
        std::for_each(poolstl::par.par_if(parallel_submeshes),
                      order.begin(),
                      order.end(),
                      [&](size_t i)
                      {
                          sdf_source_geometry g;
                          if(!extract_sdf_source_geometry(data, data.submeshes[i], g))
                          {
                              return;
                          }
                          mesh_sdf field;
                          if(!bake_mesh_sdf(g, settings, field, threading))
                          {
                              return;
                          }
                          fields[i] = std::move(field);
                      });
        std::printf("  %-34s %7.1f ms\n",
                    label,
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        return fields;
    };
    // Three ways to spend the same work. The first is the reference; the other two are the two
    // places the parallelism can go, and on a model of many small parts the outer one wins --
    // a bake this size cannot fill the pool on its own, so it pays the dispatch overhead per
    // submesh and gets little back.
    const auto reference = bake_all("single threaded:", false, sdf_bake_threading::serial);
    const auto inner_parallel = bake_all("one bake at a time, pool inside:", false, sdf_bake_threading::parallel);
    const auto outer_parallel = bake_all("submeshes in parallel:", true, sdf_bake_threading::serial);
    bool all_baked = true;
    bool all_match = true;
    for(uint32_t s = 0; s < submesh_count; ++s)
    {
        all_baked = all_baked && reference[s].is_valid() && outer_parallel[s].is_valid();
        // Where the parallelism sits must not change a single voxel.
        all_match = all_match && reference[s].brick_voxels == outer_parallel[s].brick_voxels &&
                    reference[s].indirection == outer_parallel[s].indirection &&
                    reference[s].brick_voxels == inner_parallel[s].brick_voxels;
    }
    check(all_baked, "both threading modes bake every submesh");
    check(all_match, "the parallel submesh pass produces the same fields as the serial one");
}

void test_degenerate_inputs()
{
    std::printf("test_degenerate_inputs\n");
    mesh_sdf_bake_settings settings;
    mesh_sdf sdf;
    sdf_source_geometry empty;
    check(!bake_mesh_sdf(empty, settings, sdf), "empty geometry is rejected");
    // A zero-area triangle has no usable normal and must not produce a field.
    sdf_source_geometry degenerate;
    degenerate.positions = {math::vec3(0.0f), math::vec3(0.0f), math::vec3(0.0f)};
    degenerate.indices = {0, 1, 2};
    degenerate.bounds.reset();
    degenerate.bounds.add_point(math::vec3(0.0f));
    check(!bake_mesh_sdf(degenerate, settings, sdf), "degenerate geometry is rejected");
}

} // namespace

int main()
{
    test_sphere_accuracy();
    test_field_is_conservative();
    test_sign_correctness();
    test_conservative_empty_bricks();
    test_brick_seam_continuity();
    test_two_sided_shell();
    test_thin_wall();
    test_inverted_winding_is_corrected();
    test_gpu_addressing_matches_cpu();
    test_bounds_entry_is_not_a_hit();
    test_trace_from_outside_hits_the_surface_not_the_bounds();
    test_open_mesh_does_not_produce_inside_regions();
    test_serialization_round_trip();
    test_invalid_field_is_rejected();
    test_determinism();
    test_sampling_cost_does_not_scale_with_field_size();
    test_clipmap_is_conservative();
    test_instance_grid_never_misses_an_instance();
    test_instance_grid_shader_walk_matches_cpu();
    test_instance_grid_handles_degenerate_input();
    test_clipmap_recomposes_moved_geometry_within_budget();
    test_clipmap_culled_composition_matches_brute_force();
    test_clipmap_transition_is_continuous();
    test_clipmap_blend_stays_conservative();
    test_clipmap_is_world_stable();
    test_clipmap_sees_offscreen_geometry();
    test_raw_buffer_extraction_matches_direct_geometry();
    test_submesh_extraction_selects_only_its_own_submesh();
    test_unmapped_submesh_reports_no_transforms();
    test_submesh_bake_pass_cost_is_linear();
    test_bake_grid_scales_with_world_size();
    test_total_voxel_budget_bounds_a_field();
    test_lod_extraction_clamps_rather_than_failing();
    test_bake_cost_is_dominated_by_voxels_not_triangles();
    test_parallel_submesh_bake_matches_serial();
    test_cache_keys_are_camera_independent();
    test_cache_separates_opposing_faces();
    test_cache_binning_is_stable_at_axis_aligned_normals();
    test_cache_cells_do_not_split_a_surface_on_a_grid_plane();
    test_cache_find_surface_tolerates_a_boundary_facing();
    test_cache_level_is_stable_within_a_band();
    test_cache_insert_find_and_evict();
    test_cache_accumulation_converges_and_stays_responsive();
    test_cache_shader_transcription_matches_cpu();
    test_degenerate_inputs();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
