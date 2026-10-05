/*
 * Validation suite for the surface cache GI bake (USC-GI Phase 1).
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "gi bake"
 *
 * These are correctness invariants, not smoke tests. The two that matter most are
 * test_conservative_empty_bricks (a sphere trace tunnels through geometry if an empty brick
 * ever over-estimates its distance) and test_brick_seam_continuity (a wrong filter border
 * puts a discontinuity in the field at every brick boundary). Both caught real bugs.
 */

#include "../tests.h"

#include <engine/meta/rendering/gi/lumen_mesh_cards.hpp>
#include <engine/meta/rendering/gi/mesh_sdf.hpp>
#include <engine/rendering/gi/global_sdf_clipmap.h>
#include <engine/rendering/gi/gi_constants.h>
#include <engine/rendering/gi/lumen_constants.h>
#include <engine/rendering/gi/lumen_mesh_cards.h>
#include <engine/rendering/gi/lumen_scene.h>
#include <engine/rendering/gi/mesh_sdf_baker.h>
#include <engine/rendering/gi/mesh_sdf_source.h>
#include <engine/rendering/gi/sdf_instance_grid.h>
#include <engine/rendering/pipeline/passes/lumen_pass_common.h>
// The generator templates mesh::create_plane builds primitives from, so the plane test bakes the
// exact geometry the embedded plane asset carries.
#include <engine/rendering/generator/generator.hpp>
// For submesh_pose_mat4: the surface cache places a field wherever model::submit draws the
// submesh, so the two read the same pose structure.
#include <engine/rendering/model.h>

#include <logging/logging.h>
#include <serialization/binary_archive.h>

#include <concurrency/parallel.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
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

/**
 * @brief Closest point on a triangle, by clamped barycentric projection.
 *
 * Deliberately a DIFFERENT formulation from the baker's Voronoi-region branches: a reference that
 * shares the implementation cannot catch a bug in it. This one projects onto the triangle plane,
 * and if the projection falls outside, takes the nearest point of the three edge segments. Slower
 * and simpler, which is exactly what a reference should be.
 */
auto closest_point_on_triangle_reference(const math::vec3& p,
                                         const math::vec3& a,
                                         const math::vec3& b,
                                         const math::vec3& c) -> math::vec3
{
    const auto closest_on_segment = [&](const math::vec3& s, const math::vec3& e) -> math::vec3
    {
        const math::vec3 se = e - s;
        const float length_sq = math::dot(se, se);
        if(!(length_sq > 0.0f))
        {
            return s;
        }
        const float t = math::clamp(math::dot(p - s, se) / length_sq, 0.0f, 1.0f);
        return s + se * t;
    };
    const math::vec3 normal = math::cross(b - a, c - a);
    const float normal_length_sq = math::dot(normal, normal);
    if(normal_length_sq > 0.0f)
    {
        const math::vec3 projected = p - normal * (math::dot(p - a, normal) / normal_length_sq);
        // Inside test by the sign of the three edge cross products against the face normal.
        const bool inside = math::dot(math::cross(b - a, projected - a), normal) >= 0.0f &&
                            math::dot(math::cross(c - b, projected - b), normal) >= 0.0f &&
                            math::dot(math::cross(a - c, projected - c), normal) >= 0.0f;
        if(inside)
        {
            return projected;
        }
    }
    const math::vec3 candidates[3] = {closest_on_segment(a, b),
                                      closest_on_segment(b, c),
                                      closest_on_segment(c, a)};
    math::vec3 best = candidates[0];
    float best_sq = math::dot(p - best, p - best);
    for(int i = 1; i < 3; ++i)
    {
        const float dist_sq = math::dot(p - candidates[i], p - candidates[i]);
        if(dist_sq < best_sq)
        {
            best_sq = dist_sq;
            best = candidates[i];
        }
    }
    return best;
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

/**
 * @brief Every stored voxel holds the distance to the TRUE nearest triangle, not merely a plausible
 *        one.
 *
 * The bake bounds each closest-point query by a Lipschitz estimate carried over from the previous
 * voxel, so the BVH can reject subtrees on their bounds instead of descending to a leaf. That bound
 * is exact in principle, and its failure mode is silent: a bound that is a hair too tight makes the
 * traversal miss the nearest triangle and return a slightly LARGER distance. Nothing crashes,
 * nothing looks wrong, and the field over-estimates -- which is the one direction a sphere trace
 * cannot survive, because it steps straight through the surface.
 *
 * `test_field_is_conservative` would catch a gross version of this, but it samples the RECONSTRUCTED
 * field and therefore has to allow three quarters of a voxel for quantisation and trilinear
 * filtering. This checks the stored bytes against a brute-force scan over every triangle, so the
 * only tolerance is the encoding step itself and a single mis-selected triangle shows up.
 *
 * A sphere rather than a box: 12 triangles fit in a handful of BVH nodes, where there is nothing to
 * prune and the bound is never exercised.
 */
void test_stored_voxels_match_brute_force()
{
    std::printf("test_stored_voxels_match_brute_force\n");
    const auto geometry = make_sphere(0.8f, 20, 28);
    mesh_sdf_bake_settings settings;
    settings.resolution = 24;
    settings.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), "sphere bake succeeds");
    const uint32_t triangle_count = geometry.get_triangle_count();
    // Half an encoding step, which is all the difference a correct query may show.
    const float quantisation = (2.0f * mesh_sdf::encode_range) / 255.0f;
    const float tolerance = 0.5f * quantisation + 1e-4f;
    int mismatches = 0;
    float worst = 0.0f;
    uint64_t compared = 0;
    for(uint32_t brick_index = 0; brick_index < uint32_t(sdf.indirection.size()); ++brick_index)
    {
        const uint32_t entry = sdf.indirection[brick_index];
        if(is_sdf_empty_entry(entry))
        {
            continue;
        }
        const uint32_t bx = brick_index % sdf.brick_dim.x;
        const uint32_t by = (brick_index / sdf.brick_dim.x) % sdf.brick_dim.y;
        const uint32_t bz = brick_index / (sdf.brick_dim.x * sdf.brick_dim.y);
        const float brick_world_size = float(mesh_sdf::brick_size) * sdf.voxel_size;
        const math::vec3 brick_origin =
            sdf.bounds.min + math::vec3(float(bx), float(by), float(bz)) * brick_world_size;
        const uint8_t* brick = sdf.brick_voxels.data() + size_t(entry) * mesh_sdf::brick_voxel_count;
        for(uint32_t lz = 0; lz < mesh_sdf::brick_stride; ++lz)
        {
            for(uint32_t ly = 0; ly < mesh_sdf::brick_stride; ++ly)
            {
                for(uint32_t lx = 0; lx < mesh_sdf::brick_stride; ++lx)
                {
                    // Same addressing the bake writes with: local 0 is the border voxel and the
                    // sample sits at the voxel centre.
                    const math::vec3 voxel_offset(float(lx) - float(mesh_sdf::brick_border) + 0.5f,
                                                  float(ly) - float(mesh_sdf::brick_border) + 0.5f,
                                                  float(lz) - float(mesh_sdf::brick_border) + 0.5f);
                    const math::vec3 p = brick_origin + voxel_offset * sdf.voxel_size;
                    float truth_sq = std::numeric_limits<float>::max();
                    for(uint32_t t = 0; t < triangle_count; ++t)
                    {
                        const math::vec3& a = geometry.positions[geometry.indices[t * 3 + 0]];
                        const math::vec3& b = geometry.positions[geometry.indices[t * 3 + 1]];
                        const math::vec3& c = geometry.positions[geometry.indices[t * 3 + 2]];
                        const math::vec3 delta = p - closest_point_on_triangle_reference(p, a, b, c);
                        truth_sq = math::min(truth_sq, math::dot(delta, delta));
                    }
                    const float truth_voxels = std::sqrt(truth_sq) / sdf.voxel_size;
                    // Saturated voxels carry no distance to compare, only a sign.
                    if(truth_voxels >= mesh_sdf::encode_range - 0.5f)
                    {
                        continue;
                    }
                    const uint32_t local = lx + ly * mesh_sdf::brick_stride +
                                           lz * mesh_sdf::brick_stride * mesh_sdf::brick_stride;
                    const float stored = std::fabs(decode_sdf_distance(brick[local]));
                    ++compared;
                    const float excess = std::fabs(stored - truth_voxels);
                    if(excess > tolerance)
                    {
                        ++mismatches;
                        worst = math::max(worst, excess);
                    }
                }
            }
        }
    }
    std::printf("  compared %llu in-band voxels, mismatches = %d, worst = %.5f voxels (tol %.5f)\n",
                (unsigned long long)compared,
                mismatches,
                worst,
                tolerance);
    check(compared > 10000, "the fixture actually covers a meaningful number of in-band voxels");
    check(mismatches == 0, "every stored voxel matches a brute-force nearest-triangle scan");
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

/**
 * @brief Exact distance from a point to a triangle.
 *
 * Ground truth for an arbitrary soup, which an OPEN mesh needs: the closed-box formula stops being
 * the answer anywhere near the missing face, and that region is precisely where a shell's
 * behaviour differs from a signed field's.
 */
auto point_triangle_distance(const math::vec3& p,
                             const math::vec3& a,
                             const math::vec3& b,
                             const math::vec3& c) -> float
{
    const math::vec3 ab = b - a;
    const math::vec3 ac = c - a;
    const math::vec3 ap = p - a;
    const float d1 = math::dot(ab, ap);
    const float d2 = math::dot(ac, ap);
    if(d1 <= 0.0f && d2 <= 0.0f)
    {
        return math::length(p - a);
    }
    const math::vec3 bp = p - b;
    const float d3 = math::dot(ab, bp);
    const float d4 = math::dot(ac, bp);
    if(d3 >= 0.0f && d4 <= d3)
    {
        return math::length(p - b);
    }
    const float vc = d1 * d4 - d3 * d2;
    if(vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
    {
        return math::length(p - (a + ab * (d1 / (d1 - d3))));
    }
    const math::vec3 cp = p - c;
    const float d5 = math::dot(ab, cp);
    const float d6 = math::dot(ac, cp);
    if(d6 >= 0.0f && d5 <= d6)
    {
        return math::length(p - c);
    }
    const float vb = d5 * d2 - d1 * d6;
    if(vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
    {
        return math::length(p - (a + ac * (d2 / (d2 - d6))));
    }
    const float va = d3 * d6 - d5 * d4;
    if(va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
    {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return math::length(p - (b + (c - b) * w));
    }
    const float denom = 1.0f / (va + vb + vc);
    return math::length(p - (a + ab * (vb * denom) + ac * (vc * denom)));
}

/// Unsigned distance from a point to a triangle soup, by brute force. Exact, and fast enough for a
/// fixture of a handful of triangles.
auto soup_distance(const sdf_source_geometry& geometry, const math::vec3& p) -> float
{
    float best = std::numeric_limits<float>::max();
    for(size_t i = 0; i + 2 < geometry.indices.size(); i += 3)
    {
        best = math::min(best,
                         point_triangle_distance(p,
                                                 geometry.positions[geometry.indices[i + 0]],
                                                 geometry.positions[geometry.indices[i + 1]],
                                                 geometry.positions[geometry.indices[i + 2]]));
    }
    return best;
}

/**
 * @brief An unsigned SHELL's empty bricks must under-estimate, exactly as a signed field's do.
 *
 * The same tracing safety invariant as test_conservative_empty_bricks, on the case that test cannot
 * reach. A shell stores `unsigned - thickness`, so brick classification and the empty-brick distance
 * have to work in that space too. They did not: they used the raw unsigned distance, and every empty
 * entry therefore over-reported by exactly the thickness -- enough for a trace to step through the
 * shell, which is thin open geometry silently ceasing to occlude.
 *
 * It hid because the thickness is ZERO for a signed field, so the two spaces coincide and a fixture
 * of closed meshes agrees perfectly. `lessons.md` records that shape from the last shell bug and
 * says the parity test has to cover a case where the quantity is non-zero; that was done for
 * CPU/GPU parity and not for this.
 */
void test_conservative_empty_bricks_in_a_shell()
{
    std::printf("test_conservative_empty_bricks_in_a_shell\n");
    const math::vec3 half(0.4f, 0.4f, 0.4f);
    const auto geometry = make_open_box(half);
    mesh_sdf_bake_settings settings;
    // Coarse on purpose, with a thickness several voxels wide. The defect over-reports by exactly
    // the thickness, so a fixture where the thickness is a small fraction of a brick cannot see it:
    // every empty brick sits far enough from the surface that the loose centre-minus-half-diagonal
    // bound absorbs the error. Written first with a fine voxel, it passed with the fix reverted --
    // which is the whole reason for checking that a regression test actually fails.
    settings.resolution = 12;
    settings.min_voxel_size = 0.001f;
    settings.two_sided = true;
    settings.two_sided_thickness = 0.15f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), "open box bake succeeds");
    check(sdf.is_two_sided, "an open surface bakes as a shell");
    check(sdf.two_sided_thickness > sdf.voxel_size,
          "the shell is several voxels thick, or the error is too small to detect");
    const float brick_world_size = float(mesh_sdf::brick_size) * sdf.voxel_size;
    int violations = 0;
    int empty_bricks = 0;
    float worst_excess = 0.0f;
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
                // A GRID through the brick, not just its corners. The point of a brick closest to
                // the surface is generally on a face or in the interior, and the corners can all
                // sit far enough away for the bound to look safe while the real minimum violates
                // it. Corners alone is a sample of size eight aimed at the wrong place.
                constexpr int samples_per_axis = 5;
                for(int sz = 0; sz < samples_per_axis; ++sz)
                {
                    for(int sy = 0; sy < samples_per_axis; ++sy)
                    {
                        for(int sx = 0; sx < samples_per_axis; ++sx)
                        {
                            // Braces, not parentheses: with float(sx) as the arguments the
                            // parenthesised form is a function declaration, not a variable.
                            const math::vec3 offset{float(sx), float(sy), float(sz)};
                            const math::vec3 p =
                                origin + offset * (brick_world_size / float(samples_per_axis - 1));
                            // The field a shell actually stores, which is what a trace steps against.
                            const float truth =
                                std::fabs(soup_distance(geometry, p) - sdf.two_sided_thickness);
                            if(stored > truth + 1e-4f)
                            {
                                ++violations;
                                worst_excess = math::max(worst_excess, stored - truth);
                            }
                        }
                    }
                }
            }
        }
    }
    std::printf("  shell thickness = %.4f, empty bricks = %d, violations = %d, worst excess = %.4f\n",
                sdf.two_sided_thickness,
                empty_bricks,
                violations,
                worst_excess);
    check(empty_bricks > 0, "the shell produced empty bricks (sparsity actually happens)");
    check(violations == 0, "no empty brick of a shell over-estimates its distance to the surface");
}

/**
 * @brief A large open one-sided submesh bakes as UE bakes it: signed by the backface vote, with no shell
 *        and no thickness, solid behind its faces and empty in front of them.
 */
void test_large_open_submesh_bakes_signed()
{
    std::printf("test_large_open_submesh_bakes_signed\n");
    const auto geometry = make_open_box(math::vec3(15.0f));
    mesh_sdf_bake_settings settings;
    settings.resolution = 64;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), "large open box bakes at the defaults");
    std::printf("  voxel %.3f, two-sided %d, thickness %.3f\n",
                sdf.voxel_size,
                int(sdf.is_two_sided),
                sdf.two_sided_thickness);
    check(!sdf.is_two_sided, "an open one-sided submesh bakes signed");
    check(sdf.two_sided_thickness == 0.0f, "a signed field carries no shell");
    const float offset = 1.5f * sdf.voxel_size;
    check(sample_mesh_sdf(sdf, math::vec3(15.0f - offset, 0.0f, 0.0f)) < 0.0f, "behind the +X face is solid");
    check(sample_mesh_sdf(sdf, math::vec3(15.0f + offset, 0.0f, 0.0f)) > 0.0f, "in front of the +X face is empty");
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
            // The padding term is part of the shader's answer, not a detail of it: the bake keeps
            // the surface at least encode_range voxels inside the bounds, so a point outside is at
            // least that much further away than the distance to the box. Without it this reads
            // zero exactly on the boundary -- which is where every entering ray starts -- and the
            // transcription silently stops matching the sampler it claims to mirror. It went
            // unnoticed because the addressing test only ever samples inside the field.
            return outside_distance + mesh_sdf::encode_range * sdf.voxel_size;
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
    // A camera march's bias and step budget.
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
    // the mesh. The baker signs an open one-sided surface by UE's backface vote instead, which only
    // signs the band around the surface.
    const math::vec3 half(0.5f, 0.5f, 0.5f);
    const auto geometry = make_open_box(half);
    mesh_sdf_bake_settings settings;
    settings.resolution = 32;
    settings.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), "open box bake succeeds");
    check(!sdf.is_two_sided, "an open one-sided surface bakes signed, by the backface vote");
    // The vote makes the walls solid: just behind a face (inside the box) reads negative, just in
    // front of it positive.
    const float probe_offset = 1.5f * sdf.voxel_size;
    int wrong_sides = 0;
    for(const math::vec3& axis : {math::vec3(1.0f, 0.0f, 0.0f), math::vec3(-1.0f, 0.0f, 0.0f),
                                  math::vec3(0.0f, 1.0f, 0.0f), math::vec3(0.0f, -1.0f, 0.0f),
                                  math::vec3(0.0f, 0.0f, 1.0f)})
    {
        const math::vec3 face_center = axis * half;
        const float behind = sample_mesh_sdf(sdf, face_center - axis * probe_offset);
        const float in_front = sample_mesh_sdf(sdf, face_center + axis * probe_offset);
        std::printf("  face (%+.0f %+.0f %+.0f): behind %.3f in front %.3f\n", axis.x, axis.y, axis.z, behind,
                    in_front);
        if(!(behind < 0.0f) || !(in_front > 0.0f))
        {
            ++wrong_sides;
        }
    }
    check(wrong_sides == 0, "every closed face of an open box is solid behind and empty in front");
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

void test_doubled_sheet_bakes_unsigned()
{
    std::printf("test_doubled_sheet_bakes_unsigned\n");
    // The engine's plane primitive (mesh::create_plane) is a sheet merged with a coincident,
    // oppositely wound copy of itself so it renders from both sides. After welding, every edge of
    // that geometry carries an EVEN face count -- interior edges four, rim edges two -- so a
    // closedness test of "at least two faces per edge" reports it closed and bakes it SIGNED. The
    // coincident opposite faces then cancel every vertex and edge pseudonormal to numerical zero,
    // the sign of each voxel degenerates to floating-point noise, and the field renders as random
    // phantom walls and staircases quantised at brick granularity where a flat slab should be.
    //
    // A doubled sheet is non-manifold, not closed. It must take the unsigned-shell path, where the
    // sign is never consulted and the plane occludes as a thin slab.
    sdf_source_geometry g;
    const float half = 2.0f;
    constexpr int segments = 4;
    const float step = (2.0f * half) / float(segments);
    for(int row = 0; row < segments; ++row)
    {
        for(int col = 0; col < segments; ++col)
        {
            const float x0 = -half + float(col) * step;
            const float z0 = -half + float(row) * step;
            const float x1 = x0 + step;
            const float z1 = z0 + step;
            // Up-facing sheet, then the same quad wound the other way, with its own vertices --
            // exactly what merge_mesh produces for the two rotated copies.
            add_quad(g, {x0, 0.0f, z0}, {x0, 0.0f, z1}, {x1, 0.0f, z1}, {x1, 0.0f, z0});
            add_quad(g, {x0, 0.0f, z0}, {x1, 0.0f, z0}, {x1, 0.0f, z1}, {x0, 0.0f, z1});
        }
    }
    recompute_bounds(g);
    mesh_sdf_bake_settings settings;
    settings.resolution = 32;
    settings.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(g, settings, sdf), "doubled sheet bake succeeds");
    check(sdf.is_two_sided, "a doubled sheet bakes as an unsigned shell, not a signed field");
    int inside_bricks = 0;
    for(uint32_t entry : sdf.indirection)
    {
        if(is_sdf_empty_entry(entry) && (entry & mesh_sdf::indirection_inside_flag) != 0u)
        {
            ++inside_bricks;
        }
    }
    check(inside_bricks == 0, "no brick of a doubled sheet is flagged inside");
    // The field must read as a thin slab: clearly positive away from the plane, on both sides.
    int negative_off_plane = 0;
    for(int i = 0; i < 4000; ++i)
    {
        const float t = float(i) / 4000.0f;
        const float x = (t * 2.0f - 1.0f) * half * 0.9f;
        const float z = (std::sin(t * 113.0f)) * half * 0.9f;
        const float y = (i % 2 == 0 ? 1.0f : -1.0f) *
                        (sdf.two_sided_thickness + 4.0f * sdf.voxel_size);
        if(sample_mesh_sdf(sdf, math::vec3(x, y, z)) < 0.0f)
        {
            ++negative_off_plane;
        }
    }
    std::printf("  inside-flagged bricks = %d, negative samples off the plane = %d\n",
                inside_bricks,
                negative_off_plane);
    check(negative_off_plane == 0, "no point clear of the slab reads as solid");
}

/// The EXACT geometry mesh::create_plane produces for the embedded "engine:/embedded/plane" asset
/// (defaults::init_assets): a generator plane rotated -90 and +90 degrees about X and merged, i.e. two
/// coincident, oppositely wound sheets - through the same generator templates, float trig and merge.
auto make_engine_plane_geometry() -> sdf_source_geometry
{
    using namespace generator;
    plane_mesh_t plane({5.0f, 5.0f}, {1, 1});
    math::quat rot1(math::vec3(math::radians(-90.0f), 0.f, 0.0f));
    math::quat rot2(math::vec3(math::radians(90.0f), 0.f, 0.0f));
    auto plane1 = rotate_mesh(plane, rot1);
    auto plane2 = rotate_mesh(plane, rot2);
    auto merged = merge_mesh(plane1, plane2);
    sdf_source_geometry g;
    const generator::any_mesh soup(merged);
    for(const auto& v : soup.vertices())
    {
        const math::vec3 position = v.position;
        g.positions.push_back(position);
    }
    for(const auto& triangle : soup.triangles())
    {
        g.indices.push_back(uint32_t(triangle.vertices[0]));
        g.indices.push_back(uint32_t(triangle.vertices[1]));
        g.indices.push_back(uint32_t(triangle.vertices[2]));
    }
    recompute_bounds(g);
    return g;
}

void test_engine_plane_primitive_bakes_flat()
{
    std::printf("test_engine_plane_primitive_bakes_flat\n");
    // The doubled-sheet fixture above is a hand-built analog of the engine plane; a divergence between the two
    // names the fixture as unfaithful rather than leaving it to be inferred from a screenshot.
    const sdf_source_geometry g = make_engine_plane_geometry();
    std::printf("  %zu vertices, %zu triangles, bounds y [%.6f, %.6f]\n",
                g.positions.size(),
                g.indices.size() / 3,
                g.bounds.min.y,
                g.bounds.max.y);
    // The runtime bake settings primitives actually use (mesh::runtime_sdf_bake_settings).
    mesh_sdf_bake_settings settings;
    settings.resolution = 32;
    mesh_sdf sdf;
    check(bake_mesh_sdf(g, settings, sdf), "embedded plane bake succeeds");
    check(sdf.is_two_sided, "the embedded plane bakes as an unsigned shell");
    int inside_bricks = 0;
    for(uint32_t entry : sdf.indirection)
    {
        if(is_sdf_empty_entry(entry) && (entry & mesh_sdf::indirection_inside_flag) != 0u)
        {
            ++inside_bricks;
        }
    }
    check(inside_bricks == 0, "no brick of the embedded plane is flagged inside");
    // Sweep the WHOLE padded field volume above and below the slab. The reported artefact is
    // phantom walls and stairs standing inside the field bounds, so the sweep has to cover the
    // bounds, not just a line of probes: any point clear of the slab that samples negative would
    // trace as exactly such a wall.
    int negative_off_plane = 0;
    float worst = 0.0f;
    const float clear = sdf.two_sided_thickness + 2.0f * sdf.voxel_size;
    const math::vec3 span = sdf.bounds.get_dimensions();
    constexpr int samples_per_axis = 24;
    for(int sz = 0; sz < samples_per_axis; ++sz)
    {
        for(int sy = 0; sy < samples_per_axis; ++sy)
        {
            for(int sx = 0; sx < samples_per_axis; ++sx)
            {
                const math::vec3 unit(float(sx) / float(samples_per_axis - 1),
                                      float(sy) / float(samples_per_axis - 1),
                                      float(sz) / float(samples_per_axis - 1));
                const math::vec3 p = sdf.bounds.min + unit * span;
                if(std::fabs(p.y) < clear)
                {
                    continue;
                }
                const float sampled = sample_mesh_sdf(sdf, p);
                if(sampled < 0.0f)
                {
                    ++negative_off_plane;
                    worst = math::min(worst, sampled);
                }
            }
        }
    }
    std::printf("  inside bricks = %d, negative off-plane samples = %d (worst %.4f), shell = %.4f\n",
                inside_bricks,
                negative_off_plane,
                worst,
                sdf.two_sided_thickness);
    check(negative_off_plane == 0, "no point clear of the embedded plane's slab reads as solid");
}

namespace
{
/// CPU transcription of the shader's per-instance sphere trace (SdfTestInstance), with the
/// launch-surface suppression switchable so the test can demonstrate the failure it fixes.
/// Identity transform, unit scale: the fixture geometry is authored in world space.
struct instance_trace_result
{
    bool hit = false;
    float t = 0.0f;
};

auto trace_instance_field(const mesh_sdf& sdf,
                          const math::vec3& origin,
                          const math::vec3& direction,
                          float t_max,
                          float surface_bias,
                          bool suppress_launch_surface) -> instance_trace_result
{
    instance_trace_result result;
    const float hit_threshold = math::max(surface_bias * sdf.voxel_size, 1e-6f);
    const bool two_sided = sdf.is_two_sided;
    // Suppression decision from the RAY ORIGIN, exactly as the shader derives it.
    bool suppressed = false;
    if(suppress_launch_surface)
    {
        const float origin_distance = sample_mesh_sdf(sdf, origin);
        suppressed = origin_distance < hit_threshold && (two_sided || origin_distance > -hit_threshold);
    }
    float t = 0.0f;
    for(int step = 0; step < 256; ++step)
    {
        if(t > t_max)
        {
            return result;
        }
        const float distance = sample_mesh_sdf(sdf, origin + direction * t);
        const float accept = hit_threshold;
        if(suppressed)
        {
            if(distance >= accept)
            {
                suppressed = false;
            }
            else if(two_sided || distance > -hit_threshold)
            {
                t += math::max(std::fabs(distance), hit_threshold);
                continue;
            }
            // Signed and clearly negative: genuinely buried in solid geometry, fall through.
        }
        if(distance < accept)
        {
            result.hit = true;
            result.t = t;
            return result;
        }
        t += math::max(distance, hit_threshold);
    }
    return result;
}
} // namespace

void test_open_sheet_bakes_solid_below()
{
    std::printf("test_open_sheet_bakes_solid_below\n");
    // A street-sized one-sided sheet baked with the ASSET IMPORTER'S defaults, as UE bakes it: signed by
    // the backface vote. Within the band the field is solid below the walkable surface and empty above
    // it, so nothing born on the surface starts inside a slab of its own geometry.
    sdf_source_geometry g;
    const float half = 30.0f;
    add_quad(g, {-half, 0.0f, -half}, {-half, 0.0f, half}, {half, 0.0f, half}, {half, 0.0f, -half});
    recompute_bounds(g);
    mesh_sdf_bake_settings settings; // Importer defaults.
    mesh_sdf sdf;
    check(bake_mesh_sdf(g, settings, sdf), "street-sized sheet bake succeeds");
    std::printf("  voxel = %.3f, two-sided %d\n", sdf.voxel_size, int(sdf.is_two_sided));
    check(!sdf.is_two_sided, "a one-sided street sheet bakes signed");
    const float offset = 1.5f * sdf.voxel_size;
    check(sample_mesh_sdf(sdf, math::vec3(3.0f, offset, 2.0f)) > 0.0f, "above the sheet is empty");
    check(sample_mesh_sdf(sdf, math::vec3(3.0f, -offset, 2.0f)) < 0.0f, "below the sheet is solid");
    // A one-sided wall in front of a ray still occludes it.
    sdf_source_geometry wall_geometry;
    add_quad(wall_geometry,
             {6.0f, 0.0f, -half},
             {6.0f, 0.0f, half},
             {6.0f, 8.0f, half},
             {6.0f, 8.0f, -half});
    recompute_bounds(wall_geometry);
    mesh_sdf_bake_settings wall_settings;
    wall_settings.resolution = 64;
    mesh_sdf wall;
    check(bake_mesh_sdf(wall_geometry, wall_settings, wall), "wall bake succeeds");
    const math::vec3 ray_origin(3.0f, 0.064f, 2.0f);
    const math::vec3 ray_dir = math::normalize(math::vec3(1.0f, 1.0f, 0.0f));
    const auto occluded = trace_instance_field(wall, ray_origin, ray_dir, 40.0f, 0.1f, false);
    check(occluded.hit, "a wall in front of the ray occludes it");
    // A ray genuinely inside a signed solid reports the burial as a hit.
    const auto solid_geometry = make_box(math::vec3(2.0f));
    mesh_sdf solid;
    mesh_sdf_bake_settings solid_settings;
    solid_settings.resolution = 32;
    solid_settings.min_voxel_size = 0.001f;
    check(bake_mesh_sdf(solid_geometry, solid_settings, solid), "solid bake succeeds");
    const auto inside_solid =
        trace_instance_field(solid, math::vec3(0.0f, 0.0f, 0.0f), ray_dir, 40.0f, 0.1f, true);
    check(inside_solid.hit && inside_solid.t < solid.voxel_size,
          "a ray buried in a signed solid still hits immediately");
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
    instance.axis_scale = math::vec3(scale);
    instance.world_bounds.reset();
    for(const auto& corner : sdf.bounds.get_corners())
    {
        const math::vec4 world_corner = local_to_world * math::vec4(corner, 1.0f);
        instance.world_bounds.add_point(math::vec3(world_corner));
    }
    return instance;
}

/// A field placed with a per-axis scale, as surface_cache_system places a stretched primitive.
auto make_stretched_clipmap_instance(const mesh_sdf& sdf, const math::vec3& translation, const math::vec3& scale)
    -> global_sdf_instance
{
    global_sdf_instance instance;
    instance.sdf = &sdf;
    const math::mat4 local_to_world = glm::scale(glm::translate(math::mat4(1.0f), translation), scale);
    instance.world_to_local = glm::inverse(local_to_world);
    instance.local_to_world_scale = math::min(scale.x, math::min(scale.y, scale.z));
    instance.axis_scale = scale;
    instance.world_bounds.reset();
    for(const auto& corner : sdf.bounds.get_corners())
    {
        instance.world_bounds.add_point(math::vec3(local_to_world * math::vec4(corner, 1.0f)));
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

/// A GPU-composed cascade holds no CPU copy of its voxels, yet plans exactly as the CPU-composed one does: the same
/// levels go dirty for the same instances, and the CPU samplers report nothing rather than stale bytes.
void test_gpu_composed_clipmap_keeps_no_cpu_copy()
{
    std::printf("test_gpu_composed_clipmap_keeps_no_cpu_copy\n");
    const auto geometry = make_sphere(0.8f, 16, 24);
    mesh_sdf_bake_settings settings;
    settings.resolution = 24;
    settings.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), "bake succeeds");
    const std::vector<global_sdf_instance> instances{make_clipmap_instance(sdf, math::vec3(1.0f, 0.0f, 0.0f))};
    global_sdf_clipmap::settings clipmap_settings;
    clipmap_settings.resolution = 32;
    clipmap_settings.base_extent = 12.0f;
    clipmap_settings.max_levels_per_update = global_sdf_clipmap::level_count;
    global_sdf_clipmap cpu;
    cpu.init(clipmap_settings);
    clipmap_settings.compose_on_gpu = true;
    global_sdf_clipmap gpu;
    gpu.init(clipmap_settings);
    const uint32_t cpu_composed = cpu.update(instances, math::vec3(0.0f));
    const uint32_t gpu_composed = gpu.update(instances, math::vec3(0.0f));
    std::printf("  CPU copy: %zu bytes CPU-composed, %zu GPU-composed; levels dirty %x / %x\n",
                cpu.get_memory_usage(),
                gpu.get_memory_usage(),
                cpu.get_dirty_levels(),
                gpu.get_dirty_levels());
    check(gpu.get_memory_usage() == 0, "the GPU-composed cascade keeps no CPU voxels");
    check(cpu.get_memory_usage() > 0, "the CPU-composed one does");
    check(gpu_composed == cpu_composed && gpu.get_dirty_levels() == cpu.get_dirty_levels() && gpu_composed > 0,
          "both plan the same levels for recomposition");
    for(uint32_t i = 0; i < global_sdf_clipmap::level_count; ++i)
    {
        check(gpu.get_level(i).is_valid() && gpu.get_level(i).origin == cpu.get_level(i).origin,
              "every level is placed where the CPU-composed one is");
    }
    check(gpu.sample(math::vec3(1.0f, 0.0f, 0.0f)) == global_sdf_clipmap::outside_distance,
          "the CPU samplers read nothing from a GPU-composed level");
    check(cpu.sample(math::vec3(1.0f, 0.0f, 0.0f)) < 0.0f, "the CPU-composed one is inside the sphere there");
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

void test_clipmap_bounds_stretched_boxes_per_axis()
{
    std::printf("test_clipmap_bounds_stretched_boxes_per_axis\n");
    // Walls built from a stretched unit cube, as primitive levels are. A local distance converted with the smallest
    // axis scale (0.2 here) reads a point past a wall's end 20x too close, so the field swelled along the long axes
    // and the 1 m doorway between the two walls read as solid. The per-axis bounds must keep the doorway open and
    // the field conservative.
    mesh_sdf_bake_settings settings;
    settings.resolution = 32;
    settings.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(make_box(math::vec3(0.5f)), settings, sdf), "unit cube bakes");
    const math::vec3 scale(4.0f, 2.0f, 0.2f);
    const math::vec3 left_center(-2.5f, 0.0f, 0.0f);
    const math::vec3 right_center(2.5f, 0.0f, 0.0f);
    const math::vec3 half = 0.5f * scale;
    std::vector<global_sdf_instance> instances{make_stretched_clipmap_instance(sdf, left_center, scale),
                                               make_stretched_clipmap_instance(sdf, right_center, scale)};
    const auto box_distance = [&](const math::vec3& p, const math::vec3& center) -> float
    {
        const math::vec3 q = math::abs(p - center) - half;
        return math::length(math::max(q, math::vec3(0.0f))) + math::min(math::max(q.x, math::max(q.y, q.z)), 0.0f);
    };
    const auto truth = [&](const math::vec3& p) -> float
    {
        return math::min(box_distance(p, left_center), box_distance(p, right_center));
    };
    // The instance distance itself, against the analytic box: past the end of the long axis and in the doorway.
    const auto& left = instances[0];
    const auto instance_distance = [&](const math::vec3& p) -> float
    {
        const math::vec3 local(left.world_to_local * math::vec4(p, 1.0f));
        return sample_instance_distance(*left.sdf, local, left.axis_scale, left.local_to_world_scale, true);
    };
    const math::vec3 past_end(-5.5f, 0.0f, 0.0f);
    const math::vec3 in_doorway(0.0f, 0.0f, 0.0f);
    std::printf("  past the end: %.3f (truth %.3f), doorway: %.3f (truth %.3f)\n",
                instance_distance(past_end),
                box_distance(past_end, left_center),
                instance_distance(in_doorway),
                box_distance(in_doorway, left_center));
    check(instance_distance(past_end) > 0.9f && instance_distance(past_end) <= 1.0f + 1e-3f,
          "1 m past a stretched wall's end reads about 1 m");
    check(instance_distance(in_doorway) > 0.45f && instance_distance(in_doorway) <= 0.5f + 1e-3f,
          "the doorway centre reads its half width from the wall end");
    global_sdf_clipmap clipmap;
    global_sdf_clipmap::settings clipmap_settings;
    clipmap_settings.resolution = 64;
    clipmap_settings.base_extent = 8.0f;
    clipmap_settings.max_levels_per_update = global_sdf_clipmap::level_count;
    clipmap.init(clipmap_settings);
    clipmap.update(instances, math::vec3(0.0f));
    const float voxel = clipmap.get_level(0).voxel_size;
    const float doorway = clipmap.sample(in_doorway);
    std::printf("  composed doorway: %.3f, level 0 voxel %.3f\n", doorway, voxel);
    check(doorway > 0.5f - 2.0f * voxel, "the composed field keeps the doorway open");
    int over_estimates = 0;
    int samples = 0;
    float worst_excess = 0.0f;
    for(int i = 0; i < 20000; ++i)
    {
        const float t = float(i) / 20000.0f;
        const math::vec3 p(3.8f * std::sin(t * 47.0f), 1.8f * std::cos(t * 31.0f), 1.8f * std::sin(t * 19.0f));
        const float actual = clipmap.sample(p);
        if(actual >= 1e5f)
        {
            continue;
        }
        ++samples;
        const float excess = actual - truth(p);
        if(excess > 2.0f * voxel)
        {
            ++over_estimates;
            worst_excess = math::max(worst_excess, excess);
        }
    }
    std::printf("  samples = %d, over-estimates = %d, worst excess = %.4f\n", samples, over_estimates, worst_excess);
    check(samples > 5000, "enough samples land inside the cascade");
    check(over_estimates == 0, "the stretched walls' composed field never over-estimates the distance");
}

void test_clipmap_rotated_room_matches_boxes()
{
    std::printf("test_clipmap_rotated_room_matches_boxes\n");
    // test_watcher's Scene3D room: unit cubes placed T * R * S exactly as transform_t composes them (a glm quat from
    // the authored Euler degrees), the roof turned about Z and the side walls about Y and Z. The composed field must
    // match the analytic boxes: no surface above the roof, the doorway open.
    struct placement
    {
        const char* name;
        math::vec3 position;
        math::vec3 euler_degrees;
        math::vec3 scale;
    };
    const std::array<placement, 6> room{{
        {"back", {0.0f, 0.5f, 0.0f}, {0.0f, -0.0f, 0.0f}, {1.0f, 3.0696418285369873f, 8.96141242980957f}},
        {"front_left",
         {3.3172574043273926f, 0.5f, -2.890000104904175f},
         {0.0f, -0.0f, 0.0f},
         {1.0f, 3.0696418285369873f, 4.301478385925293f}},
        {"roof",
         {1.6602678298950195f, 2.363849401473999f, 0.0f},
         {0.0f, 0.0f, -90.0000228881836f},
         {0.9999991655349731f, 4.234346866607666f, 7.337596416473389f}},
        {"side_right",
         {1.6602692604064941f, 0.363639235496521f, 2.5925261974334717f},
         {0.12171151489019394f, 89.9720230102539f, -89.88166809082031f},
         {0.999987006187439f, 4.234256267547607f, 4.798711776733398f}},
        {"side_left",
         {1.6602522134780884f, 0.3612446188926697f, -2.40747332572937f},
         {-0.41711702942848206f, 90.0f, -90.42427062988281f},
         {1.000025987625122f, 4.234201431274414f, 4.798858642578125f}},
        {"front_right",
         {3.3172574043273926f, 0.5f, 2.8555197715759277f},
         {0.0f, -0.0f, 0.0f},
         {1.0f, 3.0696418285369873f, 4.301478385925293f}},
    }};
    // The runtime primitive bake (mesh::runtime_sdf_bake_settings) of the embedded unit cube.
    mesh_sdf_bake_settings settings;
    settings.resolution = 64;
    mesh_sdf sdf;
    check(bake_mesh_sdf(make_box(math::vec3(0.5f)), settings, sdf), "unit cube bakes");
    std::printf("  cube field: voxel %.4f, grid %u x %u x %u, bounds [%.4f, %.4f]\n",
                sdf.voxel_size,
                sdf.grid_dim.x,
                sdf.grid_dim.y,
                sdf.grid_dim.z,
                sdf.bounds.min.x,
                sdf.bounds.max.x);
    std::vector<global_sdf_instance> instances;
    for(const auto& p : room)
    {
        const math::mat3 rotation = glm::mat3_cast(glm::normalize(math::quat(math::radians(p.euler_degrees))));
        math::mat4 local_to_world(1.0f);
        local_to_world[0] = math::vec4(rotation[0] * p.scale.x, 0.0f);
        local_to_world[1] = math::vec4(rotation[1] * p.scale.y, 0.0f);
        local_to_world[2] = math::vec4(rotation[2] * p.scale.z, 0.0f);
        local_to_world[3] = math::vec4(p.position, 1.0f);
        global_sdf_instance instance;
        instance.sdf = &sdf;
        instance.world_to_local = glm::inverse(local_to_world);
        instance.axis_scale = math::vec3(math::length(math::vec3(local_to_world[0])),
                                         math::length(math::vec3(local_to_world[1])),
                                         math::length(math::vec3(local_to_world[2])));
        instance.local_to_world_scale =
            math::min(instance.axis_scale.x, math::min(instance.axis_scale.y, instance.axis_scale.z));
        instance.world_bounds.reset();
        for(const auto& corner : sdf.bounds.get_corners())
        {
            instance.world_bounds.add_point(math::vec3(local_to_world * math::vec4(corner, 1.0f)));
        }
        std::printf("  %-11s axis scale (%.3f %.3f %.3f) world bounds (%.2f %.2f %.2f) - (%.2f %.2f %.2f)\n",
                    p.name,
                    instance.axis_scale.x,
                    instance.axis_scale.y,
                    instance.axis_scale.z,
                    instance.world_bounds.min.x,
                    instance.world_bounds.min.y,
                    instance.world_bounds.min.z,
                    instance.world_bounds.max.x,
                    instance.world_bounds.max.y,
                    instance.world_bounds.max.z);
        instances.push_back(instance);
    }
    const auto box_distance = [](const global_sdf_instance& inst, const math::vec3& p) -> float
    {
        const math::vec3 local(inst.world_to_local * math::vec4(p, 1.0f));
        const math::vec3 q = (math::abs(local) - math::vec3(0.5f)) * inst.axis_scale;
        return math::length(math::max(q, math::vec3(0.0f))) + math::min(math::max(q.x, math::max(q.y, q.z)), 0.0f);
    };
    const auto truth = [&](const math::vec3& p) -> float
    {
        float nearest = 1e9f;
        for(const auto& inst : instances)
        {
            nearest = math::min(nearest, box_distance(inst, p));
        }
        return nearest;
    };
    const auto& roof = instances[2];
    int roof_misreads = 0;
    for(float height : {3.0f, 3.5f, 4.5f, 6.0f, 9.0f})
    {
        const math::vec3 p(1.66f, height, 0.3f);
        const math::vec3 local(roof.world_to_local * math::vec4(p, 1.0f));
        const float actual = sample_instance_distance(*roof.sdf, local, roof.axis_scale, roof.local_to_world_scale, true);
        const float expected = box_distance(roof, p);
        std::printf("  roof instance at y %.1f: %.3f (box %.3f)\n", height, actual, expected);
        roof_misreads += (actual < expected - 0.05f || actual > expected + 1e-3f) ? 1 : 0;
    }
    check(roof_misreads == 0, "the roof instance reads its box distance above it");
    global_sdf_clipmap clipmap;
    global_sdf_clipmap::settings clipmap_settings;
    clipmap_settings.resolution = 64;
    clipmap_settings.base_extent = 12.8f;
    clipmap_settings.max_levels_per_update = global_sdf_clipmap::level_count;
    clipmap.init(clipmap_settings);
    clipmap.update(instances, math::vec3(1.66f, 1.0f, 0.0f));
    // Outside, an over-read is a sphere trace stepping through a wall. Inside, the smallest axis scale reads the
    // depth shallower than the stretched box's (conservative, as UE's VolumeScale); only a deep point reading
    // outside would be a hole.
    int under_reads = 0;
    int over_reads = 0;
    int inside_holes = 0;
    int samples = 0;
    float worst_under = 0.0f;
    math::vec3 worst_under_at(0.0f);
    float worst_over = 0.0f;
    math::vec3 worst_over_at(0.0f);
    float worst_over_voxel = 0.0f;
    for(float x = -3.0f; x <= 7.0f; x += 0.25f)
    {
        for(float y = -0.5f; y <= 6.0f; y += 0.25f)
        {
            for(float z = -7.0f; z <= 7.0f; z += 0.25f)
            {
                const math::vec3 p(x, y, z);
                float voxel = 0.0f;
                const float actual = clipmap.sample_ex(p, voxel);
                if(actual >= 1e5f)
                {
                    continue;
                }
                ++samples;
                const float exact = truth(p);
                const float expected = math::min(exact, clipmap_settings.encode_range * voxel);
                inside_holes += (exact < -1.5f * voxel && actual > 0.0f) ? 1 : 0;
                if(actual < expected - 1.5f * voxel)
                {
                    ++under_reads;
                    if(expected - actual > worst_under)
                    {
                        worst_under = expected - actual;
                        worst_under_at = p;
                    }
                }
                if(exact > 0.0f && actual > exact + 1.5f * voxel)
                {
                    ++over_reads;
                    if(actual - exact > worst_over)
                    {
                        worst_over = actual - exact;
                        worst_over_at = p;
                        worst_over_voxel = voxel;
                    }
                }
            }
        }
    }
    std::printf("  samples %d, under-reads %d (worst %.3f at %.2f %.2f %.2f), over-reads %d (worst %.3f at %.2f %.2f "
                "%.2f, voxel %.3f)\n",
                samples,
                under_reads,
                worst_under,
                worst_under_at.x,
                worst_under_at.y,
                worst_under_at.z,
                over_reads,
                worst_over,
                worst_over_at.x,
                worst_over_at.y,
                worst_over_at.z,
                worst_over_voxel);
    std::printf("  deep inside reading outside: %d\n", inside_holes);
    for(float height : {3.0f, 3.5f, 4.5f})
    {
        float voxel = 0.0f;
        const math::vec3 p(1.66f, height, 0.3f);
        const float composed = clipmap.sample_ex(p, voxel);
        std::printf("  composed above the roof at y %.1f: %.3f (box %.3f, voxel %.3f)\n",
                    height,
                    composed,
                    truth(p),
                    voxel);
    }
    const math::vec3 doorway(3.3172574f, 0.9f, -0.02f);
    std::printf("  composed doorway: %.3f (box %.3f)\n", clipmap.sample(doorway), truth(doorway));
    check(under_reads == 0, "the room's composed field never swells past its boxes");
    check(over_reads == 0, "the room's composed field never over-estimates the distance outside");
    check(inside_holes == 0, "no point deep inside a wall reads as outside");
}

void test_engine_plane_composes_a_hittable_sheet()
{
    std::printf("test_engine_plane_composes_a_hittable_sheet\n");
    // test_watcher's floor: the engine plane scaled 2.52, as GI represents it (mesh::create_plane_gi_geometry, the
    // embedded plane's 10 x 10). Lumen's global-SDF trace registers a surface within half a voxel of the composed
    // field (its surface expand), so the composed distance must come down to that over the plane, or rays pass
    // through the floor; and the floor's hits need a card that faces up and spans it.
    const sdf_source_geometry geometry = mesh::create_plane_gi_geometry(10.0f, 10.0f, 1, 1);
    mesh_sdf_bake_settings settings;
    settings.resolution = 64;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), "engine plane bakes");
    check(!sdf.is_two_sided, "the plane's GI sheet bakes signed, solid below");
    lumen_mesh_cards cards;
    check(build_lumen_mesh_cards(geometry, false, 12, cards), "the plane's GI sheet builds cards");
    check(cards.cards.size() == 1 && cards.cards[0].direction == 3u, "the plane gets one card, facing up");
    check(!cards.cards.empty() && cards.cards[0].extent.x > 4.75f && cards.cards[0].extent.y > 4.75f,
          "the plane's card spans it");
    const float scale = 2.52f;
    std::vector<global_sdf_instance> instances{make_scaled_clipmap_instance(sdf, math::vec3(0.0f), scale)};
    const auto& plane = instances[0];
    global_sdf_clipmap clipmap;
    global_sdf_clipmap::settings clipmap_settings;
    clipmap_settings.resolution = 128;
    clipmap_settings.base_extent = 128.0f * 50.0f / 252.0f;
    clipmap_settings.max_levels_per_update = global_sdf_clipmap::level_count;
    clipmap.init(clipmap_settings);
    clipmap.update(instances, math::vec3(0.31f, 1.0f, 0.17f));
    const float voxel = clipmap.get_level(0).voxel_size;
    float worst_instance = 0.0f;
    float worst_composed = 0.0f;
    float worst_below = -1e9f;
    const float below_floor = -0.3f;
    for(const math::vec2& column : {math::vec2(0.13f, 0.27f), math::vec2(-2.41f, 1.77f), math::vec2(3.3f, -0.9f),
                                    math::vec2(-0.71f, -3.13f), math::vec2(5.05f, 4.4f)})
    {
        float instance_min = 1e9f;
        float composed_min = 1e9f;
        for(float y = -0.6f; y <= 0.6f; y += 0.005f)
        {
            const math::vec3 p(column.x, y, column.y);
            const math::vec3 local(plane.world_to_local * math::vec4(p, 1.0f));
            instance_min = math::min(
                instance_min,
                sample_instance_distance(*plane.sdf, local, plane.axis_scale, plane.local_to_world_scale, true));
            composed_min = math::min(composed_min, clipmap.sample(p));
        }
        worst_instance = math::max(worst_instance, instance_min);
        worst_composed = math::max(worst_composed, composed_min);
        worst_below = math::max(worst_below, clipmap.sample(math::vec3(column.x, below_floor, column.y)));
    }
    std::printf("  plane voxel %.3f m, level-0 voxel %.3f m; worst column minimum: instance %.3f m, composed %.3f m\n",
                sdf.voxel_size * scale,
                voxel,
                worst_instance,
                worst_composed);
    check(worst_instance < 0.05f * sdf.voxel_size * scale, "the plane's own field reaches zero at the sheet");
    check(worst_composed <= 0.5f * voxel, "the composed floor comes within Lumen's half-voxel expand");
    std::printf("  composed %.1f m below the floor: at most %.3f m\n", -below_floor, worst_below);
    check(worst_below < 0.0f, "the composed floor is solid below, as UE's plane is");
}

/// The cascade a fresh clipmap composes in full for @p instances around @p camera: the reference every partial update
/// must reproduce byte for byte.
auto compose_reference_clipmap(const std::vector<global_sdf_instance>& instances,
                               const math::vec3& camera,
                               global_sdf_clipmap::settings settings) -> global_sdf_clipmap
{
    settings.max_levels_per_update = global_sdf_clipmap::level_count;
    global_sdf_clipmap reference;
    reference.init(settings);
    reference.update(instances, camera);
    return reference;
}

/// Levels whose voxels differ from @p reference's, as a bit per level (each reported with its differing voxels).
auto get_mismatched_levels(const global_sdf_clipmap& clipmap, const global_sdf_clipmap& reference) -> uint32_t
{
    uint32_t mismatched = 0;
    for(uint32_t i = 0; i < global_sdf_clipmap::level_count; ++i)
    {
        const auto& lvl = clipmap.get_level(i);
        const auto& expected = reference.get_level(i);
        const bool same = lvl.origin == expected.origin && lvl.voxels == expected.voxels;
        if(same)
        {
            continue;
        }
        mismatched |= 1u << i;
        size_t differing = 0;
        for(size_t v = 0; v < lvl.voxels.size() && v < expected.voxels.size(); ++v)
        {
            differing += lvl.voxels[v] != expected.voxels[v] ? 1u : 0u;
        }
        std::printf("  level %u differs from a fresh composition: %zu voxels, origin %s, last recompose %s\n",
                    i,
                    differing,
                    lvl.origin == expected.origin ? "equal" : "DIFFERENT",
                    lvl.is_partial ? "partial" : "full");
    }
    return mismatched;
}

/// Updates until nothing composes (at most @p limit updates); returns the updates that composed.
auto settle_clipmap(global_sdf_clipmap& clipmap,
                    const std::vector<global_sdf_instance>& instances,
                    const math::vec3& camera,
                    uint32_t limit = 32) -> uint32_t
{
    uint32_t updates = 0;
    while(clipmap.update(instances, camera) > 0 && updates < limit)
    {
        ++updates;
    }
    return updates;
}

/**
 * @brief A moved instance updates the cascade PARTIALLY (UE GlobalDistanceField.cpp:1196-1265): only the voxels within
 * reach of its old and new bounds, on each level's staggered cadence (UE ShouldUpdateClipmapThisFrame: the first level
 * every update, the others every 2 / 4 / 4), and the result is byte-identical to composing the cascade afresh.
 *
 * Both halves matter. A moved instance MUST land - an object that leaves its geometry behind goes on occluding and
 * lighting from where it used to be - and it must land without a full recompose, whose cost made the old cascade lag
 * movers by up to eight frames per level.
 */
void test_clipmap_partial_update_follows_moved_geometry()
{
    std::printf("test_clipmap_partial_update_follows_moved_geometry\n");
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
    clipmap_settings.resolution = 64;
    clipmap_settings.base_extent = 12.0f;
    clipmap.init(clipmap_settings);
    const math::vec3 camera(0.0f);
    // Settle: repeated updates with nothing changing must converge to composing nothing, or the
    // cascade would be rebuilding itself forever.
    const uint32_t settle_updates = settle_clipmap(clipmap, instances, camera);
    check(settle_updates < 32, "the cascade settles when nothing changes");
    check(clipmap.update(instances, camera) == 0, "and stays settled");
    check(clipmap.get_stale_level_count() == 0, "with no level left stale");
    std::array<uint64_t, global_sdf_clipmap::level_count> serials{};
    for(uint32_t i = 0; i < global_sdf_clipmap::level_count; ++i)
    {
        serials[i] = clipmap.get_level(i).compose_serial;
    }
    // Move one instance. Every level contains it.
    instances[1] = make_clipmap_instance(sdf, math::vec3(3.0f, 4.0f, 1.0f));
    const uint32_t res = clipmap_settings.resolution;
    const double level_voxels = double(res) * res * res;
    uint32_t max_per_update = 0;
    bool every_update_partial = true;
    bool first_level_at_once = false;
    for(uint32_t update = 0; update < 4u; ++update)
    {
        const uint32_t composed = clipmap.update(instances, camera);
        max_per_update = std::max(max_per_update, composed);
        for(uint32_t i = 0; i < global_sdf_clipmap::level_count; ++i)
        {
            const auto& lvl = clipmap.get_level(i);
            if(lvl.compose_serial == serials[i])
            {
                continue;
            }
            serials[i] = lvl.compose_serial;
            first_level_at_once = first_level_at_once || (i == 0u && update == 0u);
            double voxels = 0.0;
            for(const auto& box : lvl.partial_boxes)
            {
                voxels += double(box.size.x) * box.size.y * box.size.z;
            }
            const bool small_partial = lvl.is_partial && !lvl.partial_boxes.empty() &&
                                       voxels <= double(gi::GI_CLIPMAP_MAX_PARTIAL_FRACTION) * level_voxels;
            every_update_partial = every_update_partial && small_partial;
            std::printf("  update %u level %u: %s, %zu box(es), %.0f of %.0f voxels\n",
                        update,
                        i,
                        lvl.is_partial ? "partial" : "FULL",
                        lvl.partial_boxes.size(),
                        voxels,
                        level_voxels);
        }
    }
    check(first_level_at_once, "the first level takes the move on the update it happens");
    check(max_per_update <= uint32_t(gi::GI_CLIPMAP_PARTIAL_UPDATES_PER_FRAME),
          "at most GI_CLIPMAP_PARTIAL_UPDATES_PER_FRAME levels update together");
    check(every_update_partial, "every level takes the move as a partial update of the instance's reach");
    check(clipmap.get_stale_level_count() == 0, "and every level has it within four updates");
    const auto reference = compose_reference_clipmap(instances, camera, clipmap_settings);
    check(get_mismatched_levels(clipmap, reference) == 0u,
          "the partially updated cascade is byte-identical to a fresh composition");
    const float at_old_position = clipmap.sample(math::vec3(3.0f, 0.0f, 0.0f));
    const float at_new_position = clipmap.sample(math::vec3(3.0f, 4.0f, 1.0f));
    std::printf("  old position reads %.3f, new position reads %.3f\n", at_old_position, at_new_position);
    check(at_new_position < radius, "the moved instance is present at its new position");
    check(at_old_position > 0.5f * radius, "and no longer occupies the old one");
}

/**
 * @brief A continuous edit (a mover, an editor drag) updates partially on every due update: no full recompose, the
 * moved instance's reach alone, and the final state equals a fresh composition.
 */
void test_clipmap_continuous_edits_update_partially()
{
    std::printf("test_clipmap_continuous_edits_update_partially\n");
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
    clipmap_settings.resolution = 64;
    clipmap_settings.base_extent = 12.0f;
    clipmap.init(clipmap_settings);
    const math::vec3 camera(0.0f);
    check(settle_clipmap(clipmap, instances, camera) < 32, "the cascade settles before the drag");
    const uint32_t drag_frames = 32;
    uint32_t first_level_updates = 0;
    uint32_t full_recomposes = 0;
    std::array<uint64_t, global_sdf_clipmap::level_count> serials{};
    for(uint32_t i = 0; i < global_sdf_clipmap::level_count; ++i)
    {
        serials[i] = clipmap.get_level(i).compose_serial;
    }
    for(uint32_t frame = 0; frame < drag_frames; ++frame)
    {
        instances[1] = make_clipmap_instance(sdf, math::vec3(3.0f, 4.0f + 0.05f * float(frame + 1), 1.0f));
        clipmap.update(instances, camera);
        for(uint32_t i = 0; i < global_sdf_clipmap::level_count; ++i)
        {
            const auto& lvl = clipmap.get_level(i);
            if(lvl.compose_serial == serials[i])
            {
                continue;
            }
            serials[i] = lvl.compose_serial;
            first_level_updates += i == 0u ? 1u : 0u;
            full_recomposes += lvl.is_partial ? 0u : 1u;
        }
    }
    std::printf("  drag: level 0 updated on %u of %u frames, %u full recomposes\n",
                first_level_updates,
                drag_frames,
                full_recomposes);
    check(first_level_updates == drag_frames, "the first level follows the drag on every frame");
    check(full_recomposes == 0u, "and no level recomposes in full");
    for(uint32_t update = 0; update < 4u; ++update)
    {
        clipmap.update(instances, camera);
    }
    check(clipmap.get_stale_level_count() == 0, "the drag's end lands within four updates");
    const auto reference = compose_reference_clipmap(instances, camera, clipmap_settings);
    check(get_mismatched_levels(clipmap, reference) == 0u,
          "the cascade after the drag is byte-identical to a fresh composition");
}

/**
 * @brief Continuous edits too large for a partial update coalesce to the throttle cadence; the final state still
 * lands.
 *
 * An instance whose reach covers more than GI_CLIPMAP_MAX_PARTIAL_FRACTION of a level recomposes that level in full,
 * a full non-toroidal distance volume; dragged, that is one full recompose per frame without the leading-edge throttle
 * (GI_CLIPMAP_EDIT_THROTTLE_FRAMES). The FIRST edit after a quiet stretch composes immediately, a continuous stream
 * composes at the window cadence rather than per frame, and the final position lands once the stream ends.
 */
void test_clipmap_full_edit_coalescing()
{
    std::printf("test_clipmap_full_edit_coalescing\n");
    const float radius = 0.8f;
    const auto geometry = make_sphere(radius, 20, 28);
    mesh_sdf_bake_settings bake;
    bake.resolution = 24;
    bake.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, bake, sdf), "bake succeeds");
    // Radius 4.8 m in a 12 m first level: the sphere's reach covers most of it.
    const float scale = 6.0f;
    std::vector<global_sdf_instance> instances{make_clipmap_instance(sdf, math::vec3(0.0f, -3.0f, 0.0f)),
                                               make_scaled_clipmap_instance(sdf, math::vec3(0.5f, 0.0f, 0.0f), scale)};
    global_sdf_clipmap clipmap;
    global_sdf_clipmap::settings clipmap_settings;
    clipmap_settings.resolution = 64;
    clipmap_settings.base_extent = 12.0f;
    clipmap.init(clipmap_settings);
    const math::vec3 camera(0.0f);
    check(settle_clipmap(clipmap, instances, camera) < 32, "the cascade settles before the drag");
    const uint32_t drag_frames = 32;
    uint32_t full_first_level = 0;
    uint64_t serial = clipmap.get_level(0).compose_serial;
    for(uint32_t frame = 0; frame < drag_frames; ++frame)
    {
        instances[1] = make_scaled_clipmap_instance(sdf, math::vec3(0.5f + 0.05f * float(frame + 1), 0.0f, 0.0f), scale);
        clipmap.update(instances, camera);
        const auto& lvl = clipmap.get_level(0);
        if(lvl.compose_serial != serial)
        {
            serial = lvl.compose_serial;
            full_first_level += lvl.is_partial ? 0u : 1u;
        }
    }
    std::printf("  drag: %u full recomposes of level 0 over %u frames\n", full_first_level, drag_frames);
    check(full_first_level > 0, "a drag too large for a partial update still composes");
    check(full_first_level <= (drag_frames * 3u) / 4u,
          "continuous full recomposes coalesce below one per frame (got " + std::to_string(full_first_level) + ")");
    uint32_t catch_up = 0;
    const uint32_t catch_up_bound = uint32_t(gi::GI_CLIPMAP_EDIT_THROTTLE_FRAMES) +
                                    global_sdf_clipmap::level_count + 4u;
    while(catch_up <= catch_up_bound)
    {
        const uint32_t composed = clipmap.update(instances, camera);
        ++catch_up;
        if(composed == 0 && clipmap.get_stale_level_count() == 0)
        {
            break;
        }
    }
    check(catch_up <= catch_up_bound, "the final edit lands within one window of release");
    const auto reference = compose_reference_clipmap(instances, camera, clipmap_settings);
    check(get_mismatched_levels(clipmap, reference) == 0u,
          "the cascade after the drag is byte-identical to a fresh composition");
}

/// UE GetMaxFramesAccumulated and NumProbesToTraceBudget while the view is being edited: half the history, ten times
/// the radiance cache's trace budget.
void test_editing_scales_history_and_trace_budget()
{
    std::printf("test_editing_scales_history_and_trace_budget\n");
    const float max_frames = float(gi::lumen::LUMEN_TEMPORAL_MAX_FRAMES);
    check(lumen_pass::get_temporal_max_frames(1.0f) == max_frames, "the default speed keeps every frame");
    check(lumen_pass::get_temporal_max_frames(1.0f, true) == std::round(max_frames * 0.5f), "editing halves them");
    check(lumen_pass::get_temporal_max_frames(4.0f, true) == std::round(max_frames / 2.0f * 0.5f),
          "after the update speed's square root, rounded as UE rounds");
    const uint32_t budget = lumen_pass::get_radiance_cache_trace_budget(1.0f);
    check(lumen_pass::get_radiance_cache_trace_budget(1.0f, true) == budget * 10u, "editing traces ten times the probes");
}

/**
 * @brief The staggered cadence of partial updates is UE's (GlobalDistanceField.cpp:712-753 with two updates per frame):
 * the first level every update, the second every other, the third and fourth every fourth on distinct phases.
 */
void test_clipmap_partial_cadence_is_staggered()
{
    std::printf("test_clipmap_partial_cadence_is_staggered\n");
    std::array<uint32_t, global_sdf_clipmap::level_count> updates{};
    uint32_t max_together = 0;
    const uint32_t frames = 64;
    for(uint64_t frame = 0; frame < frames; ++frame)
    {
        uint32_t together = 0;
        for(uint32_t i = 0; i < global_sdf_clipmap::level_count; ++i)
        {
            const bool due = global_sdf_clipmap::is_partial_update_due(i, frame);
            updates[i] += due ? 1u : 0u;
            together += due ? 1u : 0u;
        }
        max_together = std::max(max_together, together);
    }
    check(updates[0] == frames, "the first level updates every frame");
    check(updates[1] == frames / 2u, "the second every other frame");
    check(updates[2] == frames / 4u && updates[3] == frames / 4u, "the third and fourth every fourth");
    check(max_together == uint32_t(gi::GI_CLIPMAP_PARTIAL_UPDATES_PER_FRAME),
          "and never more than two levels in one frame");
}

/**
 * @brief The compose DISPATCH must produce the same voxels as the CPU composer.
 *
 * Transcription of cs_gi_clipmap_compose.sc, in the same style as
 * `test_cache_shader_transcription_matches_cpu`: the shader cannot be run here, so its algorithm is
 * reimplemented against the same data and the two are compared byte for byte.
 *
 * Byte equality rather than a tolerance, for the reason the brute-force test already gives: a
 * composition bug does not corrupt a voxel, it OMITS an instance from one, so the voxel reports a
 * larger distance than the truth. That is an over-estimate, which a sphere trace turns into stepping
 * straight through a wall, and a tolerance would hide exactly it.
 *
 * The difference that makes this worth testing is the CANDIDATE SET. The CPU composer bins instances
 * into a private per-level grid with their bounds INFLATED by the reach; the dispatch reuses the
 * tracer's world grid, which is binned from RAW bounds. Walking only the containing cell there misses
 * every instance that is within reach of a voxel without containing it -- which is what this pins.
 */
void test_clipmap_compose_shader_transcription_matches_cpu()
{
    std::printf("test_clipmap_compose_shader_transcription_matches_cpu\n");
    const auto geometry = make_sphere(0.8f, 20, 28);
    mesh_sdf_bake_settings bake_settings;
    bake_settings.resolution = 24;
    bake_settings.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, bake_settings, sdf), "bake succeeds");
    // Spread widely and at mixed scales, so plenty of voxels sit NEAR an instance without being
    // inside its bounds. Those are the only voxels the two candidate sets can disagree on, so a
    // fixture of overlapping instances would pass whatever the gather did.
    std::vector<global_sdf_instance> instances;
    instances.reserve(120);
    for(int i = 0; i < 120; ++i)
    {
        const float t = float(i);
        const float scale = 1.0f + 3.0f * std::fabs(std::sin(t * 0.41f));
        instances.push_back(make_scaled_clipmap_instance(
            sdf,
            math::vec3(18.0f * std::sin(t * 1.7f), 6.0f * std::cos(t * 2.3f), 18.0f * std::sin(t * 0.9f)),
            scale));
    }
    global_sdf_clipmap clipmap;
    global_sdf_clipmap::settings clipmap_settings;
    clipmap_settings.resolution = 32;
    clipmap_settings.base_extent = 12.0f;
    clipmap_settings.max_levels_per_update = global_sdf_clipmap::level_count;
    clipmap.init(clipmap_settings);
    clipmap.update(instances, math::vec3(0.0f));
    // The tracer's grid, built exactly as surface_cache_system::upload_instance_grid builds it:
    // from RAW world bounds, over the instances' own extent. Reproducing that is the whole point --
    // the dispatch reads this grid, not one sized for composition.
    std::vector<math::bbox> raw_bounds;
    raw_bounds.reserve(instances.size());
    for(const auto& inst : instances)
    {
        raw_bounds.push_back(inst.world_bounds);
    }
    sdf_instance_grid tracer_grid;
    tracer_grid.init({});
    tracer_grid.build(raw_bounds);
    check(tracer_grid.is_valid(), "the tracer grid builds");
    const auto& offsets = tracer_grid.get_cell_offsets();
    const auto& cell_instances = tracer_grid.get_cell_instances();
    const math::vec3 grid_origin = tracer_grid.get_origin();
    const float cell_size = tracer_grid.get_cell_size();
    const math::uvec3 grid_dim = tracer_grid.get_dim();
    size_t compared = 0;
    size_t mismatches = 0;
    size_t transcription_closer = 0;
    size_t on_level_face = 0;
    size_t cpu_closer = 0;
    float worst_difference = 0.0f;
    for(uint32_t level = 0; level < global_sdf_clipmap::level_count; ++level)
    {
        const auto& lvl = clipmap.get_level(level);
        if(!lvl.is_valid())
        {
            continue;
        }
        const uint32_t resolution = clipmap_settings.resolution;
        const float reach = clipmap_settings.encode_range * lvl.voxel_size;
        for(uint32_t z = 0; z < resolution; ++z)
        {
            for(uint32_t y = 0; y < resolution; ++y)
            {
                for(uint32_t x = 0; x < resolution; ++x)
                {
                    const math::vec3 world_position =
                        lvl.origin +
                        (math::vec3(float(x), float(y), float(z)) + math::vec3(0.5f)) * lvl.voxel_size;
                    float nearest = reach;
                    // The shader's cell-range gather, transcribed.
                    const auto to_cell = [&](const math::vec3& p) -> math::ivec3
                    {
                        const math::vec3 f = math::floor((p - grid_origin) / cell_size);
                        return math::ivec3(
                            int(math::clamp(f.x, 0.0f, float(grid_dim.x) - 1.0f)),
                            int(math::clamp(f.y, 0.0f, float(grid_dim.y) - 1.0f)),
                            int(math::clamp(f.z, 0.0f, float(grid_dim.z) - 1.0f)));
                    };
                    const math::ivec3 lo = to_cell(world_position - math::vec3(reach));
                    const math::ivec3 hi = to_cell(world_position + math::vec3(reach));
                    for(int cz = lo.z; cz <= hi.z; ++cz)
                    {
                        for(int cy = lo.y; cy <= hi.y; ++cy)
                        {
                            for(int cx = lo.x; cx <= hi.x; ++cx)
                            {
                                const size_t cell = size_t(cx) + size_t(cy) * grid_dim.x +
                                                    size_t(cz) * size_t(grid_dim.x) * grid_dim.y;
                                for(size_t c = offsets[cell]; c < offsets[cell + 1]; ++c)
                                {
                                    const auto& inst = instances[cell_instances[c]];
                                    const math::vec3 clamped = math::clamp(world_position,
                                                                           inst.world_bounds.min,
                                                                           inst.world_bounds.max);
                                    if(nearest >= 0.0f && math::length(world_position - clamped) >= nearest)
                                    {
                                        continue;
                                    }
                                    const math::vec4 local =
                                        inst.world_to_local * math::vec4(world_position, 1.0f);
                                    nearest = math::min(nearest,
                                                        sample_instance_distance(*inst.sdf,
                                                                                 math::vec3(local),
                                                                                 inst.axis_scale,
                                                                                 inst.local_to_world_scale,
                                                                                 true));
                                }
                            }
                        }
                    }
                    const float normalized =
                        nearest / lvl.voxel_size / (2.0f * clipmap_settings.encode_range) + 0.5f;
                    const auto encoded =
                        uint8_t(math::clamp(normalized, 0.0f, 1.0f) * 255.0f + 0.5f);
                    const size_t offset = x + size_t(y) * resolution + size_t(z) * resolution * resolution;
                    ++compared;
                    if(encoded != lvl.voxels[offset])
                    {
                        ++mismatches;
                        // The SIGN identifies which side missed an instance, which is the only way
                        // the two can differ: a smaller value means the transcription found geometry
                        // the CPU did not, a larger one the reverse. Reporting only a count leaves
                        // the two indistinguishable.
                        if(encoded < lvl.voxels[offset])
                        {
                            ++transcription_closer;
                        }
                        else
                        {
                            ++cpu_closer;
                        }
                        // Whether the disagreement lives on a level's outer shell separates a
                        // boundary/clamping fault from one affecting the whole volume.
                        const uint32_t last = resolution - 1u;
                        if(x == 0u || y == 0u || z == 0u || x == last || y == last || z == last)
                        {
                            ++on_level_face;
                        }
                        if(mismatches == 1)
                        {
                            std::printf("  first mismatch: level %u voxel (%u,%u,%u) transcription %u "
                                        "cpu %u, voxel size %.3f reach %.3f\n",
                                        level,
                                        x,
                                        y,
                                        z,
                                        uint32_t(encoded),
                                        uint32_t(lvl.voxels[offset]),
                                        lvl.voxel_size,
                                        reach);
                        }
                        worst_difference = math::max(worst_difference,
                                                     std::fabs(float(encoded) - float(lvl.voxels[offset])));
                    }
                }
            }
        }
    }
    std::printf("  %zu voxels compared, %zu mismatches (%zu transcription closer, %zu cpu closer, "
                "%zu on a level face), worst byte difference %.0f\n",
                compared,
                mismatches,
                transcription_closer,
                cpu_closer,
                on_level_face,
                worst_difference);
    check(compared > 0, "the fixture actually composed voxels");
    check(mismatches == 0, "the dispatch transcription composes byte-identical voxels to the CPU");
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
/// Passed as @p hit_t to model "no hit found yet", so the walk never takes its early exit.
constexpr float walk_no_hit = 1e30f;

auto simulate_shader_grid_walk(const sdf_instance_grid& grid,
                               const math::vec3& origin,
                               const math::vec3& direction,
                               float t_min,
                               float t_max,
                               std::vector<uint32_t>& out,
                               float hit_t = walk_no_hit) -> bool
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
        // Mirrors the early exit in SdfTraceInstances: once a hit is known, a cell that only begins
        // beyond it cannot hold anything nearer. With walk_no_hit this can never fire, so the
        // set-equality test above still exercises the full traversal.
        if(t_step > hit_t)
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

/**
 * @brief The walk may stop once a hit is found, but only past that hit.
 *
 * The early exit is a pure optimisation, and the failure mode of getting it wrong is the one this
 * whole structure must not have: an instance the ray never tests is geometry that silently stops
 * occluding. So this asserts BOTH halves. Nothing that could have been nearer than the hit may be
 * dropped -- checked against the instances the full walk reaches, filtered by their own slab
 * intersection -- and the work must actually fall, or the exit is a no-op dressed up as a saving.
 */
/// One (instance, cell) visit and the ray segment that visit is allowed to trace.
struct walk_segment
{
    uint32_t instance = 0;
    float t_min = 0.0f;
    float t_max = 0.0f;
};

/**
 * @brief The grid walk, recording the CLAMPED segment handed to each instance test.
 *
 * Transcribes the same traversal as @ref simulate_shader_grid_walk; it records the per-cell range
 * rather than only which instances are reached, because that range is the thing under test.
 */
auto simulate_shader_grid_walk_segments(const sdf_instance_grid& grid,
                                        const math::vec3& origin,
                                        const math::vec3& direction,
                                        float t_min,
                                        float t_max,
                                        std::vector<walk_segment>& out) -> bool
{
    out.clear();
    const auto splat = [](float v) -> math::vec3 { return math::vec3(v, v, v); };
    const math::vec3 inv_dir =
        math::vec3(1.0f) / math::max(math::abs(direction), splat(1e-8f)) * math::sign(direction + splat(1e-20f));
    const math::vec3 dim(float(grid.get_dim().x), float(grid.get_dim().y), float(grid.get_dim().z));
    const math::vec3 grid_min = grid.get_origin();
    const math::vec3 grid_max = grid_min + dim * grid.get_cell_size();
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
    const math::vec3 moving(float(std::fabs(direction.x) >= 1e-7f),
                            float(std::fabs(direction.y) >= 1e-7f),
                            float(std::fabs(direction.z) >= 1e-7f));
    const float outside = 1e6f;
    t_next = math::mix(splat(outside), t_next, moving);
    t_delta = math::mix(splat(outside), t_delta, moving);
    const auto& offsets = grid.get_cell_offsets();
    const auto& cell_instances = grid.get_cell_instances();
    float t_cell_enter = t_enter;
    for(int visited = 0; visited < 256; ++visited)
    {
        const float t_step = math::min(t_next.x, math::min(t_next.y, t_next.z));
        const float cell_min = math::max(t_min, t_cell_enter);
        const float cell_max = math::min(t_max, math::min(t_step, t_exit));
        const int index = int(cell_f.x + cell_f.y * dim.x + cell_f.z * dim.x * dim.y);
        for(uint32_t entry_index = offsets[size_t(index)]; entry_index < offsets[size_t(index) + 1u];
            ++entry_index)
        {
            out.push_back({cell_instances[entry_index], cell_min, cell_max});
        }
        if(t_step > t_exit)
        {
            break;
        }
        const math::vec3 mask(float(t_next.x <= t_step), float(t_next.y <= t_step), float(t_next.z <= t_step));
        cell_f += mask * dir_sign;
        t_next += mask * t_delta;
        t_cell_enter = t_step;
        if(math::any(math::lessThan(cell_f, splat(0.0f))) ||
           math::any(math::greaterThan(cell_f, dim - splat(1.0f))))
        {
            break;
        }
    }
    return true;
}

/**
 * @brief Clamping each instance test to its cell must lose no coverage, and must remove duplication.
 *
 * `SdfTraceInstances` used to hand every instance the WHOLE ray's [t_min, t_max] in every cell it
 * appears in. An instance is listed in each cell its bounds touch, so a submesh spanning ten cells
 * was sphere-traced ten times over the identical range from the identical start -- and the
 * per-instance broad phase only rejects the repeats once a hit exists, so the waste was worst for
 * rays that do NOT hit early, which is the grazing case that already dominated this tier.
 *
 * Clamping each test to the cell's own segment fixes that, and rests on one invariant: the segments
 * are disjoint, contiguous and visited in increasing t, so their union still covers the instance's
 * whole overlap with the ray. This asserts exactly that -- a GAP would let a surface fall between
 * two cells and go unhit, which in a shadow ray reads as light through a wall.
 */
void test_instance_grid_cell_clamping_covers_every_instance()
{
    std::printf("test_instance_grid_cell_clamping_covers_every_instance\n");
    std::vector<math::bbox> bounds;
    for(int i = 0; i < 240; ++i)
    {
        const float t = float(i);
        const math::vec3 center(9.0f * std::sin(t * 1.7f) + 0.3f * t,
                                5.0f * std::cos(t * 2.3f),
                                8.0f * std::sin(t * 0.9f));
        // Deliberately spanning many cells: a small instance sits in one cell and cannot show
        // either the duplication or a coverage gap.
        const float half = 1.0f + 3.0f * std::fabs(std::sin(t * 0.37f));
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
    constexpr float ray_t_min = 0.0f;
    constexpr float ray_t_max = 30.0f;
    size_t rays = 0;
    size_t gaps = 0;
    double clamped_length = 0.0;
    double unclamped_length = 0.0;
    float worst_gap = 0.0f;
    for(int i = 0; i < 1500; ++i)
    {
        const float t = float(i) * 0.437f;
        const math::vec3 origin(20.0f * std::sin(t), 11.0f * std::cos(t * 1.31f), 16.0f * std::sin(t * 0.77f));
        const math::vec3 direction =
            math::normalize(math::vec3(std::sin(t * 2.1f), std::cos(t * 1.3f), std::sin(t * 0.5f)));
        std::vector<walk_segment> segments;
        if(!simulate_shader_grid_walk_segments(grid, origin, direction, ray_t_min, ray_t_max, segments))
        {
            continue;
        }
        ++rays;
        // Group each instance's clamped segments and merge them.
        std::sort(segments.begin(),
                  segments.end(),
                  [](const walk_segment& lhs, const walk_segment& rhs)
                  {
                      return lhs.instance != rhs.instance ? lhs.instance < rhs.instance
                                                          : lhs.t_min < rhs.t_min;
                  });
        for(size_t begin = 0; begin < segments.size();)
        {
            size_t end = begin;
            while(end < segments.size() && segments[end].instance == segments[begin].instance)
            {
                ++end;
            }
            const uint32_t instance = segments[begin].instance;
            // What the OLD code traced on every one of these visits: the whole ray, once per cell.
            unclamped_length += double(end - begin) * double(ray_t_max - ray_t_min);
            // The instance's true overlap with the ray, which the union must cover.
            const math::vec3 inv = math::vec3(1.0f) /
                                   math::max(math::abs(direction), math::vec3(1e-8f)) *
                                   math::sign(direction + math::vec3(1e-20f));
            const math::vec3 b0 = (bounds[instance].min - origin) * inv;
            const math::vec3 b1 = (bounds[instance].max - origin) * inv;
            const math::vec3 lo = math::min(b0, b1);
            const math::vec3 hi = math::max(b0, b1);
            const float overlap_min =
                math::max(math::max(lo.x, lo.y), math::max(lo.z, ray_t_min));
            const float overlap_max = math::min(math::min(hi.x, hi.y), math::min(hi.z, ray_t_max));
            if(overlap_min < overlap_max)
            {
                // Walk the merged segments and look for a hole inside the overlap. A tolerance of a
                // float epsilon scaled to the range, since the boundaries are computed two ways.
                const float tolerance = 1e-3f;
                float covered_to = overlap_min;
                for(size_t s = begin; s < end; ++s)
                {
                    const float seg_min = math::max(segments[s].t_min, overlap_min);
                    const float seg_max = math::min(segments[s].t_max, overlap_max);
                    if(seg_max <= seg_min)
                    {
                        continue;
                    }
                    if(seg_min > covered_to + tolerance)
                    {
                        ++gaps;
                        worst_gap = math::max(worst_gap, seg_min - covered_to);
                    }
                    covered_to = math::max(covered_to, seg_max);
                }
                if(covered_to + tolerance < overlap_max)
                {
                    ++gaps;
                    worst_gap = math::max(worst_gap, overlap_max - covered_to);
                }
                clamped_length += double(overlap_max - overlap_min);
            }
            begin = end;
        }
    }
    std::printf("  %zu rays, %zu coverage gaps (worst %.4f), traced length %.0f -> %.0f (%.2fx less)\n",
                rays,
                gaps,
                worst_gap,
                unclamped_length,
                clamped_length,
                unclamped_length / math::max(clamped_length, 1.0));
    check(rays > 0, "the fixture actually produced walks");
    check(gaps == 0, "the per-cell segments cover every instance's whole overlap with the ray");
    // The point of the change. Stated as a ratio rather than a timing so it holds on any machine:
    // this is the sphere-trace range the old code covered versus what the clamped one does.
    check(unclamped_length > clamped_length * 2.0,
          "clamping removes a large majority of the duplicated trace range");
}

void test_instance_grid_walk_stops_past_the_nearest_hit()
{
    std::printf("test_instance_grid_walk_stops_past_the_nearest_hit\n");
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
    // Ray parameter at which a ray enters an instance's bounds, or a negative value when it misses.
    // Same slab test the tracer's broad phase uses, so "could have been nearer" is decided the way
    // the tracer would decide it.
    const auto entry_parameter = [](const math::vec3& origin,
                                    const math::vec3& direction,
                                    const math::bbox& box) -> float
    {
        const math::vec3 inv = math::vec3(1.0f) / math::max(math::abs(direction), math::vec3(1e-8f)) *
                               math::sign(direction + math::vec3(1e-20f));
        const math::vec3 t0 = (box.min - origin) * inv;
        const math::vec3 t1 = (box.max - origin) * inv;
        const math::vec3 lo = math::min(t0, t1);
        const math::vec3 hi = math::max(t0, t1);
        const float t_near = math::max(math::max(lo.x, lo.y), math::max(lo.z, 0.0f));
        const float t_far = math::min(math::min(hi.x, hi.y), hi.z);
        return t_near <= t_far ? t_near : -1.0f;
    };
    const float hit_distances[3] = {3.0f, 8.0f, 15.0f};
    int missed = 0;
    int compared = 0;
    size_t full_candidates = 0;
    size_t early_candidates = 0;
    std::vector<uint32_t> full_walk;
    std::vector<uint32_t> early_walk;
    for(int i = 0; i < 3000; ++i)
    {
        const float t = float(i) * 0.437f;
        const math::vec3 origin(22.0f * std::sin(t), 12.0f * std::cos(t * 1.31f), 18.0f * std::sin(t * 0.77f));
        const math::vec3 direction = math::normalize(
            math::vec3(std::sin(t * 2.1f), std::cos(t * 1.3f), std::sin(t * 0.5f)));
        if(!simulate_shader_grid_walk(grid, origin, direction, 0.0f, 30.0f, full_walk))
        {
            continue;
        }
        const float hit_t = hit_distances[i % 3];
        simulate_shader_grid_walk(grid, origin, direction, 0.0f, 30.0f, early_walk, hit_t);
        ++compared;
        full_candidates += full_walk.size();
        early_candidates += early_walk.size();
        std::sort(early_walk.begin(), early_walk.end());
        for(uint32_t instance : full_walk)
        {
            const float t_near = entry_parameter(origin, direction, bounds[instance]);
            // Only instances the ray actually enters at or before the hit could have won.
            if(t_near < 0.0f || t_near > hit_t)
            {
                continue;
            }
            if(!std::binary_search(early_walk.begin(), early_walk.end(), instance))
            {
                ++missed;
            }
        }
    }
    std::printf("  %d rays, %zu candidates -> %zu with the early exit (%.2fx fewer), %d missed\n",
                compared,
                full_candidates,
                early_candidates,
                double(full_candidates) / math::max(double(early_candidates), 1.0),
                missed);
    check(missed == 0, "the early exit never drops an instance nearer than the hit");
    // Measured at 1.99x fewer candidates (12868 -> 6455) when this was written. The bound sits
    // between that and 1.0, so removing the exit makes the two walks identical and fails this
    // outright, while normal drift in the fixture cannot.
    check(double(early_candidates) < double(full_candidates) * 0.75,
          "the early exit actually removes work");
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
    // And moving past a whole snap cell must actually re-snap, or the cascade would drift out
    // from under the camera and stop covering it. The snap granularity is origin_snap_voxels
    // fine voxels (the recompose-frequency coarsening); anything within one cell is the
    // world-stability case above.
    const float snap_cell = level0_voxel * float(global_sdf_clipmap::origin_snap_voxels);
    global_sdf_clipmap c;
    c.init(clipmap_settings);
    c.update(instances, camera_a + math::vec3(snap_cell * 1.5f, 0.0f, 0.0f));
    check(c.get_level(0).origin != a.get_level(0).origin, "moving past a snap cell re-snaps the origin");
    // A second update from the same position must recompose nothing.
    const uint32_t recomposed = a.update(instances, camera_a);
    std::printf("  recomposed on an unchanged update = %u\n", recomposed);
    check(recomposed == 0, "an unchanged camera recomposes no levels");
}

/// SCROLL recompose (global_sdf_clipmap::level::is_partial with a scroll shift): a level whose origin moved
/// by whole snap cells holds, in the overlap of its old and new windows, exactly the bytes a recompose
/// writes outside the reach of instances that changed - the premise on which the GPU composer copies
/// the overlap and composes only the exposed slabs and the changed instances' boxes. Pinned on the CPU
/// reference composer for the distance voxels, together with the slab decomposition the pass
/// dispatches.
void test_clipmap_scroll_copy_matches_recompose()
{
    std::printf("test_clipmap_scroll_copy_matches_recompose\n");
    const auto geometry = make_sphere(0.8f, 24, 32);
    mesh_sdf_bake_settings settings;
    settings.resolution = 32;
    settings.min_voxel_size = 0.001f;
    mesh_sdf sdf;
    check(bake_mesh_sdf(geometry, settings, sdf), "bake succeeds");
    std::vector<global_sdf_instance> instances{make_clipmap_instance(sdf, math::vec3(1.5f, 0.3f, -0.7f)),
                                               make_clipmap_instance(sdf, math::vec3(-2.5f, 1.0f, 2.0f))};
    global_sdf_clipmap::settings clipmap_settings;
    clipmap_settings.resolution = 32;
    clipmap_settings.base_extent = 8.0f;
    clipmap_settings.max_levels_per_update = global_sdf_clipmap::level_count;
    global_sdf_clipmap clipmap;
    clipmap.init(clipmap_settings);
    const uint64_t revision = 7;
    clipmap.update(instances, math::vec3(0.0f), revision);
    std::array<global_sdf_clipmap::level, global_sdf_clipmap::level_count> before;
    for(uint32_t i = 0; i < global_sdf_clipmap::level_count; ++i)
    {
        before[i] = clipmap.get_level(i);
    }
    // One snap cell along x and along z at level 0; the coarser levels' snap is larger, so
    // they stay put and must report no scroll.
    const float level0_voxel = before[0].voxel_size;
    const float snap_cell = level0_voxel * float(global_sdf_clipmap::origin_snap_voxels);
    const math::vec3 camera_b(snap_cell, 0.0f, snap_cell);
    const uint32_t recomposed = clipmap.update(instances, camera_b, revision);
    check(recomposed >= 1, "the scroll recomposed at least level 0");
    const int res = int(clipmap_settings.resolution);
    for(uint32_t i = 0; i < global_sdf_clipmap::level_count; ++i)
    {
        const auto& after = clipmap.get_level(i);
        const std::string level_tag = "level " + std::to_string(i) + ": ";
        if(after.origin == before[i].origin)
        {
            check(after.scroll_shift == math::ivec3(0), level_tag + "an unmoved level reports no scroll");
            check(after.voxels == before[i].voxels, level_tag + "an unmoved level keeps its voxels");
            continue;
        }
        check(after.is_partial && after.partial_boxes.empty(),
              level_tag + "a moved level with unchanged content scrolls without composing boxes");
        const math::vec3 shift_f = (after.origin - before[i].origin) / after.voxel_size;
        const math::ivec3 shift(int(std::lround(shift_f.x)), int(std::lround(shift_f.y)), int(std::lround(shift_f.z)));
        check(after.scroll_shift == shift, level_tag + "the recorded shift is the origin's move in voxels");
        global_sdf_clipmap::voxel_box overlap;
        std::array<global_sdf_clipmap::voxel_box, 3> exposed;
        const uint32_t exposed_count =
            global_sdf_clipmap::compute_scroll_boxes(shift, clipmap_settings.resolution, overlap, exposed);
        check(exposed_count > 0, level_tag + "the decomposition has exposed slabs");
        // The overlap and the slabs tile the window exactly once.
        std::vector<uint8_t> covered(size_t(res) * res * res, 0u);
        const auto mark = [&](const global_sdf_clipmap::voxel_box& box)
        {
            for(int z = box.min.z; z < box.min.z + box.size.z; ++z)
            {
                for(int y = box.min.y; y < box.min.y + box.size.y; ++y)
                {
                    for(int x = box.min.x; x < box.min.x + box.size.x; ++x)
                    {
                        ++covered[size_t(x) + size_t(y) * res + size_t(z) * res * res];
                    }
                }
            }
        };
        mark(overlap);
        for(uint32_t b = 0; b < exposed_count; ++b)
        {
            mark(exposed[b]);
        }
        const bool tiled = std::all_of(covered.begin(), covered.end(), [](uint8_t c) { return c == 1u; });
        check(tiled, level_tag + "overlap + exposed slabs tile the window exactly once");
        // The overlap: new voxel v holds what the old window held at v + shift, byte for byte.
        size_t mismatched = 0;
        size_t compared = 0;
        for(int z = overlap.min.z; z < overlap.min.z + overlap.size.z; ++z)
        {
            for(int y = overlap.min.y; y < overlap.min.y + overlap.size.y; ++y)
            {
                for(int x = overlap.min.x; x < overlap.min.x + overlap.size.x; ++x)
                {
                    const size_t new_index = size_t(x) + size_t(y) * res + size_t(z) * res * res;
                    const size_t old_index = size_t(x + shift.x) + size_t(y + shift.y) * res +
                                             size_t(z + shift.z) * res * res;
                    ++compared;
                    if(after.voxels[new_index] != before[i].voxels[old_index])
                    {
                        ++mismatched;
                    }
                }
            }
        }
        check(compared > 0 && mismatched == 0,
              level_tag + "every overlap voxel is byte-identical to its old-window source (" +
                  std::to_string(mismatched) + " of " + std::to_string(compared) + " differ)");
    }
    check(get_mismatched_levels(clipmap, compose_reference_clipmap(instances, camera_b, clipmap_settings)) == 0u,
          "the scrolled cascade is byte-identical to a fresh composition");
    // A content revision that moved without changing what the levels hold still scrolls: the set, not the
    // revision, decides.
    global_sdf_clipmap revised;
    revised.init(clipmap_settings);
    revised.update(instances, math::vec3(0.0f), revision);
    revised.update(instances, camera_b, revision + 1);
    check(revised.get_level(0).is_partial && revised.get_level(0).partial_boxes.empty(),
          "a moved origin under a moved revision with the same instances scrolls");
    // An instance that moved during the scroll: the slabs and the instance's boxes, and the bytes of a fresh
    // composition.
    global_sdf_clipmap edited;
    edited.init(clipmap_settings);
    edited.update(instances, math::vec3(0.0f), revision);
    auto moved = instances;
    moved[0] = make_clipmap_instance(sdf, math::vec3(1.0f, 0.6f, -0.2f));
    edited.update(moved, camera_b, revision + 1);
    check(edited.get_level(0).is_partial && !edited.get_level(0).partial_boxes.empty(),
          "a moved origin with a moved instance scrolls and composes the instance's boxes");
    // The coarser levels take the move on their staggered cadence: four updates bring every level current.
    for(uint32_t update = 0; update < 4u; ++update)
    {
        edited.update(moved, camera_b, revision + 1);
    }
    check(get_mismatched_levels(edited, compose_reference_clipmap(moved, camera_b, clipmap_settings)) == 0u,
          "the scrolled and edited cascade is byte-identical to a fresh composition");
    // The decomposition's edge cases: no shift, and a shift past the window.
    global_sdf_clipmap::voxel_box overlap;
    std::array<global_sdf_clipmap::voxel_box, 3> exposed;
    check(global_sdf_clipmap::compute_scroll_boxes(math::ivec3(0), 32, overlap, exposed) == 0,
          "a zero shift has nothing to compose");
    check(global_sdf_clipmap::compute_scroll_boxes(math::ivec3(32, 0, 0), 32, overlap, exposed) == 0,
          "a shift past the window has no overlap and composes in full");
    check(global_sdf_clipmap::compute_scroll_boxes(math::ivec3(-16, 16, 16), 32, overlap, exposed) == 3,
          "a three-axis shift yields three slabs");
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
    bgfx::VertexLayout layout;
    layout.begin(bgfx::RendererType::Noop).add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
    const uint32_t stride = layout.getStride();
    std::vector<uint8_t> vertex_data(size_t(direct.positions.size()) * stride, 0u);
    for(size_t i = 0; i < direct.positions.size(); ++i)
    {
        const float packed[4] = {direct.positions[i].x, direct.positions[i].y, direct.positions[i].z, 0.0f};
        bgfx::vertexPack(packed, false, bgfx::Attrib::Position, layout, vertex_data.data(), uint32_t(i));
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

} // ---------------------------------------------------------------------------------------
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
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
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
            bgfx::vertexPack(packed,
                             false,
                             bgfx::Attrib::Position,
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

/**
 * @brief Voxel Size is the knob, and it means what it says.
 *
 * Two properties an author has to be able to rely on, or the setting is not tweakable:
 *   - asking for a size gets that size, on any mesh, whatever its bounds. This is the whole
 *     reason it exists: Resolution is relative to the bounding box, so the same number means
 *     wildly different things on a wall and on a prop;
 *   - leaving it at 0 changes nothing, so every asset already in the project bakes exactly as
 *     it did.
 *
 * The third property -- a request the limits cannot meet is COARSENED, never exceeded -- is the
 * one that keeps the field covering its mesh, and it is what the compiler reports.
 */
void test_voxel_size_is_honoured_and_scale_free()
{
    std::printf("test_voxel_size_is_honoured_and_scale_free\n");
    mesh_sdf_bake_settings settings;
    settings.min_voxel_size = 0.001f;
    settings.target_voxel_size = 0.05f;
    // Afforded on purpose. Voxel Size says what you WANT and the budget says what you can pay
    // for; scale-freedom is a property of the request, and it only survives into the result when
    // the budget can cover it. The starved case at the end of this test pins the other side.
    settings.max_total_voxels = uint64_t(1) << 24;
    // Two meshes an order of magnitude apart. Under Resolution these get voxels that differ by
    // the same order; under Voxel Size they must not.
    mesh_sdf small_field;
    mesh_sdf large_field;
    check(bake_mesh_sdf(make_box(math::vec3(0.5f)), settings, small_field), "small box bakes");
    check(bake_mesh_sdf(make_box(math::vec3(5.0f)), settings, large_field), "large box bakes");
    std::printf("  requested %.4f -> 1 m box %.4f, 10 m box %.4f\n",
                settings.target_voxel_size,
                small_field.voxel_size,
                large_field.voxel_size);
    check_near(small_field.voxel_size, settings.target_voxel_size, 1e-5f, "the 1 m box gets the size asked for");
    check_near(large_field.voxel_size, settings.target_voxel_size, 1e-5f, "the 10 m box gets the same size");
    // Auto is a true no-op: identical to what the same settings produced before the knob existed.
    mesh_sdf auto_field;
    mesh_sdf resolution_field;
    mesh_sdf_bake_settings auto_settings = settings;
    auto_settings.target_voxel_size = 0.0f;
    auto_settings.resolution = 32;
    check(bake_mesh_sdf(make_box(math::vec3(0.5f)), auto_settings, auto_field), "auto bake succeeds");
    mesh_sdf_bake_settings explicit_settings = auto_settings;
    explicit_settings.target_voxel_size = 1.0f / 32.0f;
    check(bake_mesh_sdf(make_box(math::vec3(0.5f)), explicit_settings, resolution_field), "explicit bake succeeds");
    check_near(auto_field.voxel_size,
               resolution_field.voxel_size,
               1e-5f,
               "Auto reproduces the longest-axis-over-Resolution size exactly");
    // Refused rather than ignored: a size the budget cannot afford comes back COARSER, and the
    // field still covers the whole mesh.
    mesh_sdf_bake_settings starved = settings;
    starved.target_voxel_size = 0.002f;
    starved.max_total_voxels = 262144;
    mesh_sdf starved_field;
    check(bake_mesh_sdf(make_box(math::vec3(5.0f)), starved, starved_field), "starved bake succeeds");
    std::printf("  requested %.4f under a tight budget -> %.4f\n",
                starved.target_voxel_size,
                starved_field.voxel_size);
    check(starved_field.voxel_size > starved.target_voxel_size,
          "a size the budget cannot afford is coarsened, not silently met");
    check(starved_field.bounds.min.x <= -5.0f && starved_field.bounds.max.x >= 5.0f,
          "and the coarsened field still covers the whole mesh");
}

/**
 * @brief A mip chain is coarser, cheaper, and still safe to trace at every level.
 *
 * The chain exists so the atlas can fall back to a level that fits instead of dropping a mesh
 * from GI entirely. That only works if a coarse level is a usable field in its own right, which
 * means the property tracing actually depends on has to survive the whole chain: a sampled
 * magnitude must never EXCEED the true distance. An over-estimate anywhere lets a sphere trace
 * step past a surface, and a coarse mip is exactly where an approximate downsample would produce
 * one -- which is why the levels are baked from the geometry rather than resampled.
 */
void test_mip_chain_is_coarser_cheaper_and_conservative()
{
    std::printf("test_mip_chain_is_coarser_cheaper_and_conservative\n");
    const math::vec3 half(0.5f, 0.35f, 0.6f);
    const auto geometry = make_box(half);
    mesh_sdf_bake_settings settings;
    settings.resolution = 48;
    settings.min_voxel_size = 0.001f;
    std::vector<mesh_sdf> mips;
    check(bake_mesh_sdf_mips(geometry, settings, mips), "the chain bakes");
    check(mips.size() == mesh_sdf::mip_count, "the chain has the requested number of levels");
    for(size_t mip = 0; mip < mips.size(); ++mip)
    {
        std::printf("  mip %zu: voxel %.4f, %ux%ux%u voxels, %u bricks\n",
                    mip,
                    mips[mip].voxel_size,
                    mips[mip].grid_dim.x,
                    mips[mip].grid_dim.y,
                    mips[mip].grid_dim.z,
                    mips[mip].get_surface_brick_count());
    }
    for(size_t mip = 1; mip < mips.size(); ++mip)
    {
        check(mips[mip].voxel_size > mips[mip - 1].voxel_size * 1.5f,
              "each level is materially coarser than the one before it");
        check(mips[mip].get_surface_brick_count() < mips[mip - 1].get_surface_brick_count(),
              "and holds fewer bricks, which is the whole point of falling back to it");
    }
    // The invariant, checked on EVERY level rather than only the finest.
    for(size_t mip = 0; mip < mips.size(); ++mip)
    {
        const auto& sdf = mips[mip];
        const float slack = 0.75f * sdf.voxel_size;
        int over_estimates = 0;
        float worst_excess = 0.0f;
        for(int i = 0; i < 20000; ++i)
        {
            const float t = float(i) / 20000.0f;
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
        std::printf("  mip %zu: over-estimates = %d, worst excess = %.5f (slack %.5f)\n",
                    mip,
                    over_estimates,
                    worst_excess,
                    slack);
        check(over_estimates == 0, "the level never over-estimates the distance to the surface");
    }
    // What the chain COSTS, on triangle-heavy geometry where the answer is not obvious. Voxel work
    // shrinks by about four per level, so the extra levels should add roughly a third -- but the
    // accelerator build and the component scan are linear in TRIANGLES and do not shrink at all.
    // Rebuilding them per level made the chain cost about twice a single bake (measured on Bistro:
    // 11.1 s -> 21.7 s) until they were hoisted, and nothing else in the suite would have caught
    // that, because every level was still correct.
    {
        // Many triangles, few voxels. That ratio is what makes the per-geometry work visible:
        // a voxel-dominated fixture hides it completely, and an earlier version of this test
        // measured 1.35x whether the work was shared or repeated -- it asserted nothing.
        const auto heavy = make_sphere(1.0f, 128, 192);
        mesh_sdf_bake_settings heavy_settings;
        heavy_settings.resolution = 8;
        heavy_settings.min_voxel_size = 0.001f;
        mesh_sdf single;
        const auto single_start = std::chrono::steady_clock::now();
        check(bake_mesh_sdf(heavy, heavy_settings, single, sdf_bake_threading::serial), "single bake");
        const double single_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - single_start).count();
        std::vector<mesh_sdf> chain;
        const auto chain_start = std::chrono::steady_clock::now();
        check(bake_mesh_sdf_mips(heavy, heavy_settings, chain, mesh_sdf::mip_count, sdf_bake_threading::serial),
              "chain bake");
        const double chain_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - chain_start).count();
        const double ratio = chain_ms / math::max(single_ms, 1e-3);
        std::printf("  %u triangles: single %.1f ms, %zu-level chain %.1f ms (%.2fx)\n",
                    heavy.get_triangle_count(),
                    single_ms,
                    chain.size(),
                    chain_ms,
                    ratio);
        // A coarser level is not automatically a cheaper one. The padding is encode_range voxels
        // per side and grows WITH the voxel, so once it dominates the mesh's own extent the grid
        // stops shrinking and a "coarse" level costs exactly what the fine one did. This fixture
        // is deliberately in that regime, and the chain must decline to build there: without the
        // prediction it bakes three identical-cost levels at 2.5x a single bake and stores all
        // three, which is how a 1591-submesh scene doubled its bake time for nothing.
        check(chain.size() == 1, "a padding-dominated field is given no levels to fall back to");
        check(ratio < 1.3, "and therefore costs no more than the single bake it is");
    }
    // The COST PROFILE of a chain, printed rather than asserted because it is a machine timing --
    // but printed because it is the opposite of what the brick counts suggest, and anyone tuning
    // mip_count needs to see it.
    //
    // A coarse level has far fewer bricks and yet costs far more PER brick, so the chain is
    // roughly 1.8x a single bake rather than the 1.33x its brick counts imply. Both bake
    // optimisations are tuned for fine voxels and degrade as the voxel grows: the per-brick
    // candidate list is collected over encode_range voxels, so doubling the voxel makes that
    // region eight times bigger and overflows the cap into the slower traversal, and the
    // Lipschitz query bound loosens in absolute terms as neighbouring samples spread apart.
    {
        const auto profile_geometry = make_sphere(1.0f, 40, 56);
        mesh_sdf_bake_settings profile_settings;
        profile_settings.resolution = 48;
        profile_settings.min_voxel_size = 0.001f;
        mesh_sdf base;
        check(bake_mesh_sdf(profile_geometry, profile_settings, base, sdf_bake_threading::serial),
              "profile base bake");
        for(uint32_t level = 0; level < mesh_sdf::mip_count; ++level)
        {
            mesh_sdf_bake_settings level_settings = profile_settings;
            level_settings.target_voxel_size = base.voxel_size * float(1u << level);
            level_settings.max_voxel_size =
                math::max(profile_settings.max_voxel_size, level_settings.target_voxel_size);
            level_settings.min_voxel_size =
                math::min(profile_settings.min_voxel_size, level_settings.target_voxel_size);
            mesh_sdf level_field;
            const auto start_time = std::chrono::steady_clock::now();
            check(bake_mesh_sdf(profile_geometry, level_settings, level_field, sdf_bake_threading::serial),
                  "profile level bake");
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_time)
                    .count();
            std::printf("  level %u: voxel %.4f, %u bricks, %.1f ms, %.3f ms per brick\n",
                        level,
                        level_field.voxel_size,
                        level_field.get_surface_brick_count(),
                        ms,
                        ms / double(math::max(1u, level_field.get_surface_brick_count())));
        }
    }
}

/**
 * @brief The point of the chain: a coarse level is resident where the finest one will not fit.
 *
 * This is the behaviour that replaces a hard refusal. Before it, a field the atlas had no room
 * for meant the submesh contributed nothing to global illumination -- invisible in the image,
 * because a missing occluder does not draw anything, it just leaks light somewhere else. After
 * it, the same submesh is resident at lower resolution.
 *
 * Checked end to end against the atlas fixture rather than by asserting on brick counts: the
 * claim is that the coarse level UPLOADS and then SAMPLES correctly through the atlas addressing,
 * which is what the tracer actually does with it.
 */
void test_a_coarse_mip_is_resident_where_the_finest_does_not_fit()
{
    std::printf("test_a_coarse_mip_is_resident_where_the_finest_does_not_fit\n");
    const math::vec3 half(0.5f, 0.35f, 0.6f);
    const auto geometry = make_box(half);
    mesh_sdf_bake_settings settings;
    settings.resolution = 48;
    settings.min_voxel_size = 0.001f;
    std::vector<mesh_sdf> mips;
    check(bake_mesh_sdf_mips(geometry, settings, mips), "the chain bakes");
    check(mips.size() >= 2, "the chain has a level to fall back to");
    constexpr uint32_t capacity =
        simulated_atlas::atlas_brick_dim * simulated_atlas::atlas_brick_dim * simulated_atlas::atlas_brick_dim;
    check(mips.front().get_surface_brick_count() > capacity,
          "the fixture is actually too small for the finest level, or this proves nothing");
    // Finest that fits, which is the rule the residency walk applies.
    size_t chosen = mips.size() - 1;
    for(size_t mip = 0; mip < mips.size(); ++mip)
    {
        if(mips[mip].get_surface_brick_count() <= capacity)
        {
            chosen = mip;
            break;
        }
    }
    std::printf("  atlas holds %u bricks; finest level needs %u, level %zu needs %u\n",
                capacity,
                mips.front().get_surface_brick_count(),
                chosen,
                mips[chosen].get_surface_brick_count());
    check(chosen > 0, "the fallback picked a coarser level than the finest");
    simulated_atlas refused;
    check(!refused.upload(mips.front()), "the finest level is genuinely refused by this atlas");
    simulated_atlas atlas;
    check(atlas.upload(mips[chosen]), "the coarser level is accepted");
    // And it is usable through the atlas, not merely resident: same addressing contract the
    // tracer relies on, so a fallback level traces rather than reading someone else's bricks.
    int mismatches = 0;
    float worst = 0.0f;
    const auto& sdf = mips[chosen];
    for(int i = 0; i < 20000; ++i)
    {
        const float t = float(i) / 20000.0f;
        const math::vec3 p(half.x * 2.2f * std::sin(t * 53.0f),
                           half.y * 2.2f * std::cos(t * 29.0f),
                           half.z * 2.2f * std::sin(t * 11.0f));
        const float reference = sample_mesh_sdf(sdf, p);
        const float through_atlas = atlas.sample(sdf, p);
        if(std::fabs(reference - through_atlas) > 0.05f * sdf.voxel_size)
        {
            ++mismatches;
            worst = math::max(worst, std::fabs(reference - through_atlas));
        }
    }
    std::printf("  resident level samples: %d mismatches, worst %.6f\n", mismatches, worst);
    check(mismatches == 0, "the fallback level samples through the atlas exactly as the reference does");
}

/// An open sheet signs the space behind it solid as far as its backface vote reaches (half the reach, where a quarter
/// of all rays still hit its back). Every level of a chain votes with the finest level's reach, so all of them
/// describe one solid, a coarse level as far as its sampling resolves that layer: reaching four of its own voxel
/// diagonals it would claim a layer several times thicker, which a coarse-first distance composes past whatever
/// separate submesh bounds the real solid.
void test_mip_chain_bakes_one_solid()
{
    std::printf("test_mip_chain_bakes_one_solid\n");
    // A vault's soffit: one quad facing down, open, so the bake signs it by the vote.
    sdf_source_geometry geometry;
    add_quad(geometry, {-3.0f, 0.0f, -3.0f}, {3.0f, 0.0f, -3.0f}, {3.0f, 0.0f, 3.0f}, {-3.0f, 0.0f, 3.0f});
    recompute_bounds(geometry);
    mesh_sdf_bake_settings settings;
    settings.target_voxel_size = 0.1f;
    settings.max_total_voxels = 4u * 1024u * 1024u;
    std::vector<mesh_sdf> mips;
    check(bake_mesh_sdf_mips(geometry, settings, mips), "the chain bakes");
    check(mips.size() == mesh_sdf::mip_count, "the chain has every level");
    const math::vec3 below(0.4f, -0.5f, -0.3f);
    const float vote_reach = mesh_sdf::encode_range * mips.front().voxel_size * std::sqrt(3.0f);
    const math::vec3 behind_near(0.4f, 0.25f, -0.3f);
    const math::vec3 behind_far(0.4f, vote_reach + 0.3f, -0.3f);
    for(size_t mip = 0; mip < mips.size(); ++mip)
    {
        const float in_front = sample_mesh_sdf(mips[mip], below);
        const float near_reading = sample_mesh_sdf(mips[mip], behind_near);
        const float far_reading = sample_mesh_sdf(mips[mip], behind_far);
        std::printf("  mip %zu (voxel %.2f): in front %.3f, behind %.3f near, %.3f past the vote reach (%.2f m)\n",
                    mip,
                    mips[mip].voxel_size,
                    in_front,
                    near_reading,
                    far_reading,
                    vote_reach);
        check(in_front > 0.0f, "the side the sheet faces is outside at every level");
        check(mip > 0 || near_reading < 0.0f, "the space just behind the sheet is solid in the finest level");
        check(far_reading > 0.0f, "past the finest level's vote reach no level claims the solid");
    }
}

/// Two parallel slabs @p gap apart along z as one mesh: the space between them lies inside the geometry's box, where
/// only the field, not the box, tells how far the surface is.
auto make_slab_pair(const math::vec3& half_extents, float gap) -> sdf_source_geometry
{
    sdf_source_geometry g = make_box(half_extents);
    const sdf_source_geometry second = make_box(half_extents);
    const uint32_t base = uint32_t(g.positions.size());
    const math::vec3 offset(0.0f, 0.0f, 2.0f * half_extents.z + gap);
    for(const auto& p : second.positions)
    {
        g.positions.push_back(p + offset);
    }
    for(const uint32_t index : second.indices)
    {
        g.indices.push_back(base + index);
    }
    recompute_bounds(g);
    return g;
}

/// UE composes its global distance field from each mesh's always-resident coarsest level wherever that level reads
/// more than one of its voxels from the surface, and from the finest resident level nearer
/// (DistanceToMeshSurfaceStandalone; SdfInstanceStandaloneDistance in gi/sdf_common.sh). The Lumen coverage band
/// reaches past the traced level's narrow band on a Sponza-scale mesh; this is the rule that keeps the distances in it
/// exact.
void test_coarsest_mip_answers_the_lumen_coverage_band()
{
    std::printf("test_coarsest_mip_answers_the_lumen_coverage_band\n");
    // Sponza's walls trace at about 10 cm voxels, so their narrow band ends near 0.35 m.
    const math::vec3 half(5.0f, 3.0f, 0.15f);
    const float gap = 3.7f;
    const auto geometry = make_slab_pair(half, gap);
    mesh_sdf_bake_settings settings;
    settings.target_voxel_size = 0.1f;
    settings.max_total_voxels = 4u * 1024u * 1024u;
    std::vector<mesh_sdf> mips;
    check(bake_mesh_sdf_mips(geometry, settings, mips), "the chain bakes");
    check(mips.size() == mesh_sdf::mip_count, "the chain has every level");
    const mesh_sdf& traced = mips.front();
    const mesh_sdf& coarsest = mips.back();
    const float coverage_band = gi::lumen::LUMEN_GLOBAL_SDF_COVERAGE_BAND_VOXELS * gi::lumen::LUMEN_GLOBAL_SDF_EXTENT /
                                float(gi::lumen::LUMEN_GLOBAL_SDF_RESOLUTION);
    const float traced_reach = (mesh_sdf::encode_range - 0.5f) * traced.voxel_size;
    const float coarse_threshold = 0.25f * mesh_sdf::encode_range * coarsest.voxel_size;
    std::printf("  traced voxel %.3f m (exact to %.3f m), coarsest voxel %.3f m, coverage band %.3f m\n",
                traced.voxel_size,
                traced_reach,
                coarsest.voxel_size,
                coverage_band);
    check(traced_reach < coverage_band, "the traced band ends inside the coverage band, or this proves nothing");
    constexpr int steps = 32;
    int traced_short = 0;
    int standalone_wrong = 0;
    float worst_standalone = 0.0f;
    float worst_traced = 0.0f;
    for(int i = 0; i <= steps; ++i)
    {
        const float distance = traced_reach + (coverage_band - traced_reach) * float(i) / float(steps);
        const math::vec3 p(0.3f, -0.2f, half.z + distance);
        const float traced_reading = sample_instance_distance(traced, p, math::vec3(1.0f), 1.0f, true);
        const float coarse_reading = sample_instance_distance(coarsest, p, math::vec3(1.0f), 1.0f, true);
        const float standalone = std::fabs(coarse_reading) > coarse_threshold ? coarse_reading : traced_reading;
        worst_traced = math::max(worst_traced, distance - traced_reading);
        if(traced_reading < distance - 0.5f * traced.voxel_size)
        {
            ++traced_short;
        }
        const float error = std::fabs(standalone - distance);
        worst_standalone = math::max(worst_standalone, error);
        if(error > 0.5f * traced.voxel_size)
        {
            ++standalone_wrong;
        }
    }
    std::printf("  traced level short by up to %.3f m (%d of %d samples), standalone off by up to %.3f m\n",
                worst_traced,
                traced_short,
                steps + 1,
                worst_standalone);
    check(traced_short > 0, "the traced level saturates inside the coverage band");
    check(standalone_wrong == 0, "the coarsest-first rule reads the true distance across the coverage band");
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

/**
 * @brief The surface test must not eat ordinary tessellation.
 *
 * `carries_no_surface` rejects triangles by how thin they are, and the threshold is the whole
 * design: too low and near-degenerate junk survives to size the field, too high and legitimate
 * geometry silently disappears from GI. The second failure is much harder to notice than the first --
 * a missing occluder leaks light somewhere across the project rather than drawing a block in front of
 * the camera -- so it needs a test rather than a judgement.
 *
 * A UV sphere is the honest fixture: its polar rows are genuinely thin triangles, thin enough that an
 * aggressive threshold removes the caps and leaves holes an SDF traces straight through.
 */
void test_surface_test_keeps_ordinary_tessellation()
{
    std::printf("test_surface_test_keeps_ordinary_tessellation\n");
    bgfx::VertexLayout format;
    format.begin(bgfx::RendererType::Noop).add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
    const auto measure = [&](const char* label, const sdf_source_geometry& geometry) -> uint32_t
    {
        std::vector<uint8_t> vertex_data(geometry.positions.size() * format.getStride(), 0u);
        for(size_t v = 0; v < geometry.positions.size(); ++v)
        {
            const float packed[4] = {geometry.positions[v].x,
                                     geometry.positions[v].y,
                                     geometry.positions[v].z,
                                     0.0f};
            bgfx::vertexPack(packed, false, bgfx::Attrib::Position, format, vertex_data.data(), uint32_t(v));
        }
        sdf_source_geometry extracted;
        const bool ok = extract_sdf_source_geometry(vertex_data.data(),
                                                    uint32_t(geometry.positions.size()),
                                                    format,
                                                    geometry.indices.data(),
                                                    geometry.get_triangle_count(),
                                                    extracted);
        check(ok, "ordinary geometry extracts");
        std::printf("  %-28s %5u triangles, %4u discarded (%.1f%%)\n",
                    label,
                    geometry.get_triangle_count(),
                    extracted.discarded_triangles,
                    100.0f * float(extracted.discarded_triangles) /
                        float(math::max(geometry.get_triangle_count(), 1u)));
        return extracted.discarded_triangles;
    };
    // Mirrors min_height_ratio in mesh_sdf_source.cpp. Duplicated deliberately: the point of the
    // test is to pin that value, so reading it from the implementation would make it unfalsifiable.
    constexpr float expected_min_height_ratio = 0.0001f;
    // Triangles the RULE says should go, computed independently of the code under test. Comparing
    // against this rather than against zero is what makes the test meaningful on real tessellation:
    // a UV sphere's pole fan is genuinely degenerate, so discarding it is correct, and asserting
    // zero would only prove the fixture had no junk in it.
    const auto count_below_threshold = [&](const sdf_source_geometry& geometry) -> uint32_t
    {
        uint32_t below = 0;
        for(uint32_t t = 0; t < geometry.get_triangle_count(); ++t)
        {
            const math::vec3& a = geometry.positions[geometry.indices[t * 3 + 0]];
            const math::vec3& b = geometry.positions[geometry.indices[t * 3 + 1]];
            const math::vec3& c = geometry.positions[geometry.indices[t * 3 + 2]];
            const float longest =
                math::max(math::length(b - a), math::max(math::length(c - a), math::length(c - b)));
            if(longest <= 0.0f ||
               math::length(math::cross(b - a, c - a)) <= longest * longest * expected_min_height_ratio)
            {
                ++below;
            }
        }
        return below;
    };
    bool discards_exactly_the_rule = true;
    const auto check_fixture = [&](const char* label, const sdf_source_geometry& geometry)
    {
        const uint32_t expected = count_below_threshold(geometry);
        const uint32_t actual = measure(label, geometry);
        discards_exactly_the_rule = discards_exactly_the_rule && actual == expected;
    };
    check_fixture("box", make_box(math::vec3(0.5f)));
    check_fixture("sphere 16x24", make_sphere(1.0f, 16, 24));
    check_fixture("sphere 64x96 (fine)", make_sphere(1.0f, 64, 96));
    // A long thin wall panel: one quad, 40:1. Trim, mullions and floor strips are routinely this
    // shape, and every one of them is a real occluder.
    sdf_source_geometry panel;
    panel.positions = {math::vec3(0.0f, 0.0f, 0.0f),
                       math::vec3(4.0f, 0.0f, 0.0f),
                       math::vec3(4.0f, 0.1f, 0.0f),
                       math::vec3(0.0f, 0.1f, 0.0f)};
    for(const auto& p : panel.positions)
    {
        panel.bounds.add_point(p);
    }
    panel.indices = {0u, 1u, 2u, 0u, 2u, 3u};
    const uint32_t panel_discarded = measure("thin panel (40:1)", panel);
    check(discards_exactly_the_rule, "the filter discards exactly what the threshold defines");
    // The assertion the threshold exists for. A 40:1 panel measures 0.025, so anything from about
    // 0.02 upward deletes it outright -- and with both its triangles gone the geometry produces NO
    // FIELD AT ALL, silently removing a real occluder from GI. Trim, mullions and floor strips are
    // routinely this shape, so this is the bound that must not be crossed.
    check(panel_discarded == 0, "a 40:1 panel is real occlusion and must survive");
    check(expected_min_height_ratio < 0.02f, "the threshold stays clear of ordinary thin geometry");
}

/**
 * @brief A submesh of scattered parts is detectable, and its field is not.
 *
 * The failure this measures produced the worst artefact in the scene while looking correct at every
 * step: the submesh renders fine, the field is sized exactly to the submesh, and every bake setting
 * is honoured. What is wrong is the RELATIONSHIP between the two -- the voxel comes from the spread
 * of the parts rather than from the parts, so each part falls below one voxel and the field cannot
 * represent it.
 *
 * Shaped from the real asset that prompted it: 384 faces over a 3804-unit bbox, in three pieces.
 */
void test_scattered_parts_are_detected_and_cannot_be_resolved()
{
    std::printf("test_scattered_parts_are_detected_and_cannot_be_resolved\n");
    constexpr uint32_t part_count = 3;
    constexpr float part_size = 1.0f;
    constexpr float part_spacing = 1200.0f;
    sdf_source_geometry geometry;
    for(uint32_t part = 0; part < part_count; ++part)
    {
        const auto box = make_box(math::vec3(part_size * 0.5f));
        const uint32_t base = uint32_t(geometry.positions.size());
        const math::vec3 offset(float(part) * part_spacing, 0.0f, 0.0f);
        for(const auto& position : box.positions)
        {
            geometry.positions.push_back(position + offset);
            geometry.bounds.add_point(geometry.positions.back());
        }
        for(const uint32_t index : box.indices)
        {
            geometry.indices.push_back(index + base);
        }
    }
    const auto summary = summarize_connected_components(geometry);
    std::printf("  %u pieces, largest %.2f, bounds %.2f, sparsity %.0fx\n",
                summary.component_count,
                summary.largest_component_extent,
                summary.bounds_extent,
                summary.get_sparsity());
    check(summary.component_count == part_count, "each disconnected part is found");
    check(std::fabs(summary.largest_component_extent - part_size) < 1e-3f,
          "the largest piece is measured, not the spread");
    check(summary.get_sparsity() > 100.0f, "a scatter of small parts reads as extremely sparse");
    // The bake REFUSES it. Producing a field here is worse than producing none: the voxel would be
    // sized to the spread, every part would sit below one voxel, and what came out would trace as a
    // solid block the size of the whole scatter.
    mesh_sdf_bake_settings settings;
    mesh_sdf sdf;
    check(!bake_mesh_sdf(geometry, settings, sdf), "a scatter too sparse to resolve is refused");
    // ... and the refusal is specifically about the SPREAD, not about the geometry. The identical
    // parts bake fine once the check is off, which is what proves the rule is the thing rejecting
    // them rather than anything wrong with the triangles.
    mesh_sdf_bake_settings unchecked = settings;
    unchecked.max_component_spread = 0.0f;
    mesh_sdf unchecked_sdf;
    check(bake_mesh_sdf(geometry, unchecked, unchecked_sdf), "the same geometry bakes with the check off");
    std::printf("  unchecked voxel %.3f for parts %.2f across -- %.2f voxels per part\n",
                unchecked_sdf.voxel_size,
                part_size,
                part_size / unchecked_sdf.voxel_size);
    check(unchecked_sdf.voxel_size > part_size,
          "and that field's voxel is coarser than the parts, which is why it is refused");
    // A solid submesh of the same triangle budget stays resolvable, which is what makes the metric
    // discriminating rather than merely a proxy for "large".
    const auto solid = make_box(math::vec3(part_size * 0.5f));
    const auto solid_summary = summarize_connected_components(solid);
    check(solid_summary.component_count == 1, "a solid part is one piece");
    check(solid_summary.get_sparsity() < 2.0f, "and reads as dense");
    mesh_sdf solid_sdf;
    check(bake_mesh_sdf(solid, settings, solid_sdf), "the solid submesh bakes, with the check ON");
    check(solid_sdf.voxel_size < part_size, "whose voxel does resolve it");
    std::printf("  solid comparison: sparsity %.2fx, voxel %.4f\n",
                solid_summary.get_sparsity(),
                solid_sdf.voxel_size);
}

/**
 * @brief An invisible sliver must not size the field.
 *
 * A triangle whose vertices are collinear has zero area: the renderer draws nothing, so the submesh
 * looks empty in the viewport and changing its material does nothing. The bake used to take its
 * corners into the bounds anyway, and the bounds are what pick the voxel size -- so one sliver
 * spanning a model produced a field thousands of units across with a voxel to match. An unsigned
 * shell is floored at one voxel, so that field then traced as a solid block big enough to swallow a
 * street, sourced from geometry nobody can see.
 *
 * Measured on the asset that prompted this: submeshes reporting extents of 7,000 to 10,000 units in
 * a scene whose buildings are a few tens across.
 */
void test_degenerate_triangles_do_not_size_the_field()
{
    std::printf("test_degenerate_triangles_do_not_size_the_field\n");
    auto geometry = make_box(math::vec3(0.5f));
    const math::bbox clean_bounds = geometry.bounds;
    const uint32_t clean_triangles = geometry.get_triangle_count();
    // Three corners on one line, reaching far outside the box. Collinear rather than merely thin,
    // so it is unambiguously a sliver rather than a judgement about how thin is too thin.
    const uint32_t base = uint32_t(geometry.positions.size());
    geometry.positions.emplace_back(0.0f, 0.0f, 0.0f);
    geometry.positions.emplace_back(5000.0f, 0.0f, 0.0f);
    geometry.positions.emplace_back(10000.0f, 0.0f, 0.0f);
    geometry.indices.insert(geometry.indices.end(), {base, base + 1u, base + 2u});
    // A NaN triangle too: it never compares true, so a pure area test lets it through and one
    // corner poisons the bounds of the whole field.
    const float nan_value = std::numeric_limits<float>::quiet_NaN();
    const uint32_t nan_base = uint32_t(geometry.positions.size());
    geometry.positions.emplace_back(nan_value, 0.0f, 0.0f);
    geometry.positions.emplace_back(0.0f, nan_value, 1.0f);
    geometry.positions.emplace_back(1.0f, 1.0f, nan_value);
    geometry.indices.insert(geometry.indices.end(), {nan_base, nan_base + 1u, nan_base + 2u});
    // Round-trip through the raw-buffer extractor, which is the path a runtime primitive takes.
    bgfx::VertexLayout format;
    format.begin(bgfx::RendererType::Noop).add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
    std::vector<uint8_t> vertex_data(geometry.positions.size() * format.getStride(), 0u);
    for(size_t v = 0; v < geometry.positions.size(); ++v)
    {
        const float packed[4] = {geometry.positions[v].x, geometry.positions[v].y, geometry.positions[v].z, 0.0f};
        bgfx::vertexPack(packed, false, bgfx::Attrib::Position, format, vertex_data.data(), uint32_t(v));
    }
    sdf_source_geometry extracted;
    check(extract_sdf_source_geometry(vertex_data.data(),
                                      uint32_t(geometry.positions.size()),
                                      format,
                                      geometry.indices.data(),
                                      geometry.get_triangle_count(),
                                      extracted),
          "geometry with junk triangles still extracts its real surface");
    const math::vec3 dimensions = extracted.bounds.get_dimensions();
    const float extent = math::max(dimensions.x, math::max(dimensions.y, dimensions.z));
    const math::vec3 clean_dimensions = clean_bounds.get_dimensions();
    const float clean_extent = math::max(clean_dimensions.x, math::max(clean_dimensions.y, clean_dimensions.z));
    std::printf("  %u triangles in, %u kept, %u discarded, extent %.3f (clean box is %.3f)\n",
                geometry.get_triangle_count(),
                extracted.get_triangle_count(),
                extracted.discarded_triangles,
                extent,
                clean_extent);
    check(extracted.discarded_triangles == 2, "both junk triangles are discarded");
    check(extracted.get_triangle_count() == clean_triangles, "every real triangle survives");
    check(std::isfinite(extent), "the bounds stay finite despite a NaN triangle");
    check(extent < clean_extent * 1.01f, "the bounds are the real surface's, not the sliver's");
    // The consequence the fix exists for: bounds set the voxel, so a sane field falls out.
    mesh_sdf_bake_settings settings;
    settings.resolution = 16;
    mesh_sdf sdf;
    check(bake_mesh_sdf(extracted, settings, sdf), "the cleaned geometry bakes");
    check(sdf.voxel_size < clean_extent, "the voxel is sized to the real surface");
    std::printf("  baked voxel %.4f, shell %.4f\n", sdf.voxel_size, sdf.two_sided_thickness);
}

/**
 * @brief Spheres spread along x, one per submesh, shaped like a real import.
 *
 * Separate from @ref make_multi_submesh_load_data because the LOD simplifier needs two things that
 * fixture cannot give it: enough triangles to actually simplify (a 12-triangle box reports "not
 * enough triangles" and produces no levels at all, so a test built on it silently falls back to the
 * base topology and asserts nothing about LODs), and vertex ATTRIBUTES, since the simplifier takes a
 * different code path when normals and UVs are present -- the path every shipped asset takes.
 */
auto make_spread_submesh_load_data(uint32_t submesh_count) -> mesh::load_data
{
    mesh::load_data data;
    data.vertex_format.begin(bgfx::RendererType::Noop)
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    const auto sphere = make_sphere(0.5f, 12, 16);
    const uint32_t sphere_vertices = uint32_t(sphere.positions.size());
    const uint32_t sphere_triangles = sphere.get_triangle_count();
    data.vertex_count = submesh_count * sphere_vertices;
    data.vertex_data.assign(size_t(data.vertex_count) * data.vertex_format.getStride(), 0u);
    data.triangle_data.reserve(size_t(submesh_count) * sphere_triangles);
    data.submeshes.reserve(submesh_count);
    data.bbox.reset();
    for(uint32_t s = 0; s < submesh_count; ++s)
    {
        const uint32_t vertex_offset = s * sphere_vertices;
        const math::vec3 offset(float(s) * submesh_spacing, 0.0f, 0.0f);
        for(uint32_t v = 0; v < sphere_vertices; ++v)
        {
            const math::vec3 local = sphere.positions[v];
            const math::vec3 p = local + offset;
            const float packed_position[4] = {p.x, p.y, p.z, 0.0f};
            bgfx::vertexPack(packed_position,
                             false,
                             bgfx::Attrib::Position,
                             data.vertex_format,
                             data.vertex_data.data(),
                             vertex_offset + v);
            // Radial normals and a crude planar UV. Exact shading values are irrelevant; the
            // simplifier only needs a real gradient to weigh collapses against.
            const math::vec3 n = math::normalize(local);
            const float packed_normal[4] = {n.x, n.y, n.z, 0.0f};
            bgfx::vertexPack(packed_normal,
                             false,
                             bgfx::Attrib::Normal,
                             data.vertex_format,
                             data.vertex_data.data(),
                             vertex_offset + v);
            const float packed_uv[4] = {local.x, local.y, 0.0f, 0.0f};
            bgfx::vertexPack(packed_uv,
                             false,
                             bgfx::Attrib::TexCoord0,
                             data.vertex_format,
                             data.vertex_data.data(),
                             vertex_offset + v);
            data.bbox.add_point(p);
        }
        auto& submesh = data.submeshes.emplace_back();
        submesh.data_group_id = s;
        submesh.vertex_start = int32_t(vertex_offset);
        submesh.vertex_count = sphere_vertices;
        submesh.face_start = int32_t(data.triangle_data.size());
        submesh.face_count = sphere_triangles;
        for(uint32_t t = 0; t < sphere_triangles; ++t)
        {
            auto& tri = data.triangle_data.emplace_back();
            tri.data_group_id = submesh.data_group_id;
            tri.indices[0] = sphere.indices[t * 3 + 0] + vertex_offset;
            tri.indices[1] = sphere.indices[t * 3 + 1] + vertex_offset;
            tri.indices[2] = sphere.indices[t * 3 + 2] + vertex_offset;
        }
    }
    data.triangle_count = uint32_t(data.triangle_data.size());
    return data;
}

/**
 * @brief Each submesh's LOD-extracted geometry must stay its own, through the REAL LOD generator.
 *
 * `test_submesh_extraction_selects_only_its_own_submesh` pins this for the base topology, and
 * `test_lod_extraction_clamps_rather_than_failing` pins level selection -- but that one builds its
 * LOD by hand, so the face ranges are correct by construction and it asserts nothing about bounds.
 * Neither covers the path the asset compiler actually uses: `generate_lods_for_load_data` writes the
 * ranges, and the SDF bakes from LOD 2 BY DEFAULT, so a mistake there reaches every imported model
 * while every existing test stays green.
 *
 * The symptom it guards is specific and severe. Bounds are what size a field: a submesh handed a
 * sibling's triangles gets bounds spanning the gap between them, and since the voxel is that extent
 * divided by a fixed count, the field becomes coarse enough that its geometry dilates into one solid
 * block. Compact parts scattered across a model -- street lamps, bolts, signage -- are the worst case.
 */
void test_lod_extraction_keeps_each_submesh_to_its_own_bounds()
{
    std::printf("test_lod_extraction_keeps_each_submesh_to_its_own_bounds\n");
    constexpr uint32_t submesh_count = 4;
    auto data = make_spread_submesh_load_data(submesh_count);
    // The production call, not a hand-built level.
    const auto lod_configs = mesh::generate_default_lod_configs(data, 0.01f);
    if(!lod_configs.empty())
    {
        mesh::generate_lods_for_load_data(data, lod_configs);
    }
    std::printf("  generated %zu LOD levels for %u submeshes spaced %.1f apart\n",
                data.lods.size(),
                submesh_count,
                submesh_spacing);
    // Every level the compiler could ask for, including LOD 0 and one past the last generated one,
    // since sdf.lod_index defaults to 2 and clamps.
    bool all_own_bounds = true;
    float worst_center_error = 0.0f;
    float worst_extent = 0.0f;
    for(uint32_t lod = 0; lod <= uint32_t(data.lods.size()) + 1u; ++lod)
    {
        for(uint32_t s = 0; s < submesh_count; ++s)
        {
            sdf_source_geometry g;
            if(!extract_sdf_source_geometry(data, lod, s, g))
            {
                all_own_bounds = false;
                continue;
            }
            const math::vec3 center = (g.bounds.min + g.bounds.max) * 0.5f;
            const math::vec3 dimensions = g.bounds.get_dimensions();
            const float extent = math::max(dimensions.x, math::max(dimensions.y, dimensions.z));
            const float center_error = std::fabs(center.x - float(s) * submesh_spacing);
            worst_center_error = math::max(worst_center_error, center_error);
            worst_extent = math::max(worst_extent, extent);
            // Sited at its own submesh, and no wider than one sphere. Either check alone can pass
            // while the geometry is wrong: bounds spanning two submeshes still centre correctly on
            // the middle one, and a correctly sized sphere can sit at the wrong place.
            all_own_bounds = all_own_bounds && center_error < 1e-3f && extent < submesh_spacing;
        }
    }
    std::printf("  worst centre error %.4f, worst extent %.3f (one submesh is 1.0, spacing %.1f)\n",
                worst_center_error,
                worst_extent,
                submesh_spacing);
    check(all_own_bounds, "every LOD of every submesh keeps its own bounds, not a sibling's");
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
    // 16x, from 16 x 24 to 64 x 96 rings and sectors.
    const double triangle_ratio = 16.0;
    std::printf("  %.0fx the triangles cost %.2fx the time\n", triangle_ratio, ratio);
    // Strongly sublinear in triangles. Stated loosely because it competes with whatever else the
    // machine is doing; the point is the ORDER -- if this ever approached 16x, the query would
    // have stopped pruning and the BVH would be the thing to fix, not the resolution.
    //
    // Expressed against the triangle multiplier rather than as a constant, because this is a RATIO
    // of two timings and both ends move when the query cost changes. Bounding each closest-point
    // query by a Lipschitz estimate from the previous voxel cut both measurements by about a
    // quarter, yet raised the ratio from ~3.8 to ~4.1: a shallow BVH has proportionally more of its
    // traversal to give up than a deep one. A constant just under the old number turned a 24%
    // speedup into a failure, which is the opposite of what this is here to detect.
    check(ratio < 0.5 * triangle_ratio, "bake time is sublinear in triangle count");
}

void test_bake_cost_estimate_reads_the_baked_grid()
{
    std::printf("test_bake_cost_estimate_reads_the_baked_grid\n");
    // The asset compiler starts its bakes in the order of this estimate, taken before any geometry is
    // extracted: from the bounds and the triangle count alone. Its voxel term has to be the grid the
    // bake itself sizes, through every rule that sizes one -- the voxel clamps, both caps, and the
    // thinner voxel a shell asks for.
    struct estimate_case
    {
        const char* name = "";
        sdf_source_geometry geometry;
        mesh_sdf_bake_settings settings;
    };
    std::vector<estimate_case> cases(5);
    cases[0].name = "sphere at the total cap";
    cases[0].geometry = make_sphere(1.0f, 16, 24);
    cases[1].name = "box at the voxel floor";
    cases[1].geometry = make_box(math::vec3(0.05f));
    cases[2].name = "long box past the per-axis cap";
    cases[2].geometry = make_box(math::vec3(5.0f, 0.1f, 0.1f));
    cases[2].settings.target_voxel_size = 0.02f;
    cases[3].name = "shell with a thinner voxel";
    cases[3].geometry = make_box(math::vec3(8.0f, 0.5f, 8.0f));
    cases[3].settings.two_sided = true;
    cases[4].name = "box under a tight budget";
    cases[4].geometry = make_box(math::vec3(2.0f, 1.0f, 0.5f));
    cases[4].settings.max_total_voxels = 16384;
    for(const estimate_case& entry : cases)
    {
        mesh_sdf field;
        const bool baked = bake_mesh_sdf(entry.geometry, entry.settings, field, sdf_bake_threading::serial);
        check(baked, std::string(entry.name) + " bakes");
        const uint32_t triangles = entry.geometry.get_triangle_count();
        const double grid_voxels = double(field.grid_dim.x) * double(field.grid_dim.y) * double(field.grid_dim.z);
        const double expected = grid_voxels * std::sqrt(double(triangles));
        const double estimate = estimate_mesh_sdf_bake_cost(entry.geometry.bounds, triangles, entry.settings);
        std::printf("  %-32s grid %ux%ux%u, estimate %.0f\n",
                    entry.name,
                    field.grid_dim.x,
                    field.grid_dim.y,
                    field.grid_dim.z,
                    estimate);
        check(estimate == expected, std::string(entry.name) + ": the estimate reads the grid the bake sized");
    }
    check(estimate_mesh_sdf_bake_cost(make_box(math::vec3(1.0f)).bounds, 0u, mesh_sdf_bake_settings{}) == 0.0,
          "geometry without triangles costs nothing");
}

void test_parallel_submesh_bake_matches_serial()
{
    std::printf("test_parallel_submesh_bake_matches_serial\n");
    // Mirrors the loop the asset compiler runs: submeshes in parallel, each pool worker pulling the
    // costliest submesh left, each individual bake serial. That nesting rule is invisible in the
    // output -- breaking it deadlocks the whole pool rather than producing a wrong field -- so it is
    // worth pinning here, where it runs in milliseconds, instead of discovering it on a model with
    // thousands of parts.
    constexpr uint32_t submesh_count = 128;
    const auto data = make_multi_submesh_load_data(submesh_count, 4);
    mesh_sdf_bake_settings settings;
    settings.resolution = 16;
    // Scrambled costs, so the bakes run in an order unrelated to their slots and a slot filled by the
    // wrong submesh, or by none, shows below. 37 is coprime with the count, so this is a permutation.
    constexpr uint32_t cost_stride = 37;
    std::vector<uint32_t> costs(submesh_count);
    for(uint32_t s = 0; s < submesh_count; ++s)
    {
        costs[s] = (s * cost_stride) % submesh_count;
    }
    const auto bake_all = [&](const char* label, bool parallel_submeshes, sdf_bake_threading threading)
        -> std::vector<mesh_sdf>
    {
        std::vector<mesh_sdf> fields(submesh_count);
        std::vector<std::atomic<uint32_t>> runs(submesh_count);
        const auto start = std::chrono::steady_clock::now();
        poolstl::for_each_costliest_first_par_if(
            parallel_submeshes,
            costs,
            [&](size_t i)
            {
                ++runs[i];
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
        const bool ran_each_once = std::all_of(runs.begin(),
                                               runs.end(),
                                               [](const std::atomic<uint32_t>& count)
                                               {
                                                   return count.load() == 1u;
                                               });
        check(ran_each_once, std::string(label) + " runs every submesh exactly once");
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

auto count_cards_facing(const lumen_mesh_cards& cards, uint32_t direction) -> uint32_t
{
    return uint32_t(std::count_if(cards.cards.begin(),
                                  cards.cards.end(),
                                  [&](const lumen_card& card)
                                  {
                                      return card.direction == direction;
                                  }));
}

/// A closed box gets one outer card per side, each facing its side and covering the face.
void test_lumen_cards_box()
{
    std::printf("test_lumen_cards_box\n");
    const sdf_source_geometry geometry = make_box(math::vec3(0.5f));
    lumen_mesh_cards cards;
    check(build_lumen_mesh_cards(geometry, false, 12, cards), "box cards build");
    check(cards.cards.size() == 6, "a box gets six cards");
    for(uint32_t direction = 0; direction < 6; ++direction)
    {
        check(count_cards_facing(cards, direction) == 1, "one card per side of a box");
    }
    for(const lumen_card& card : cards.cards)
    {
        math::vec3 normal(0.0f);
        normal[card.direction / 2] = (card.direction & 1) != 0 ? 1.0f : -1.0f;
        check(math::dot(card.axis_z, normal) > 0.999f, "a card faces its side");
        check(card.extent.x > 0.45f && card.extent.y > 0.45f, "a box card covers the whole face");
        // The card's front plane sits in front of (or on) the face it captures.
        const float front = math::dot(card.origin + card.axis_z * card.extent.z, normal);
        check(front >= 0.5f - 1e-4f, "the card's near plane is in front of its face");
    }
}

/// An open one-sided sheet is seen only from its front side; a two-sided one from both.
void test_lumen_cards_sheet()
{
    std::printf("test_lumen_cards_sheet\n");
    sdf_source_geometry geometry;
    add_quad(geometry, {-2.0f, 0.0f, 2.0f}, {2.0f, 0.0f, 2.0f}, {2.0f, 0.0f, -2.0f}, {-2.0f, 0.0f, -2.0f});
    recompute_bounds(geometry);
    const math::vec3 front = math::normalize(math::cross(geometry.positions[1] - geometry.positions[0],
                                                         geometry.positions[2] - geometry.positions[0]));
    const uint32_t front_direction = front.y > 0.0f ? 3u : 2u;
    lumen_mesh_cards one_sided;
    check(build_lumen_mesh_cards(geometry, false, 12, one_sided), "sheet cards build");
    check(one_sided.cards.size() == 1, "a one-sided sheet gets one card");
    check(count_cards_facing(one_sided, front_direction) == 1, "the one-sided sheet's card faces its front");
    lumen_mesh_cards two_sided;
    check(build_lumen_mesh_cards(geometry, true, 12, two_sided), "two-sided sheet cards build");
    check(two_sided.cards.size() == 2, "a two-sided sheet gets a card on each side");
    check(count_cards_facing(two_sided, 2u) == 1 && count_cards_facing(two_sided, 3u) == 1,
          "the two-sided sheet's cards face both sides");
}

void test_lumen_cards_doubled_sheet()
{
    std::printf("test_lumen_cards_doubled_sheet\n");
    // A sheet modelled as two coincident, oppositely wound sheets (the engine plane's render geometry) with a
    // one-sided material: a ray returns either triangle first, so the build must read the front face from each side
    // or half of each side gets no card.
    const sdf_source_geometry geometry = make_engine_plane_geometry();
    lumen_mesh_cards cards;
    check(build_lumen_mesh_cards(geometry, false, 12, cards), "doubled sheet cards build");
    const math::vec3 plane_extent = geometry.bounds.get_extents();
    std::printf("  plane extent (%.3f %.3f %.3f), %zu cards\n", plane_extent.x, plane_extent.y, plane_extent.z,
                cards.cards.size());
    for(const auto& card : cards.cards)
    {
        std::printf("  direction %u origin (%.3f %.3f %.3f) extent (%.3f %.3f %.3f) axis_z (%.2f %.2f %.2f)\n",
                    card.direction,
                    card.origin.x,
                    card.origin.y,
                    card.origin.z,
                    card.extent.x,
                    card.extent.y,
                    card.extent.z,
                    card.axis_z.x,
                    card.axis_z.y,
                    card.axis_z.z);
    }
    check(count_cards_facing(cards, 2u) == 1 && count_cards_facing(cards, 3u) == 1,
          "the doubled sheet gets one card facing each side");
    for(const auto& card : cards.cards)
    {
        check(card.extent.x > 0.95f * plane_extent.x && card.extent.y > 0.95f * plane_extent.z,
              "each doubled sheet card spans the whole sheet");
    }
}

/// Two boxes in a row along X: the far box's inner face is hidden from the outer near plane, so
/// each X side gets an interior layer card; the other sides see both boxes in one layer.
void test_lumen_cards_interior_layers()
{
    std::printf("test_lumen_cards_interior_layers\n");
    sdf_source_geometry geometry = make_box(math::vec3(0.4f));
    sdf_source_geometry second = make_box(math::vec3(0.4f));
    const uint32_t base = uint32_t(geometry.positions.size());
    for(math::vec3& p : geometry.positions)
    {
        p.x -= 1.0f;
    }
    for(const math::vec3& p : second.positions)
    {
        geometry.positions.push_back(p + math::vec3(1.0f, 0.0f, 0.0f));
    }
    for(uint32_t index : second.indices)
    {
        geometry.indices.push_back(base + index);
    }
    recompute_bounds(geometry);
    lumen_mesh_cards cards;
    check(build_lumen_mesh_cards(geometry, false, 12, cards), "two-box cards build");
    check(count_cards_facing(cards, 0u) == 2 && count_cards_facing(cards, 1u) == 2,
          "each X side has an outer and an interior layer card");
    for(uint32_t direction = 2; direction < 6; ++direction)
    {
        check(count_cards_facing(cards, direction) == 1, "the sides that see both boxes at once have one card");
    }
    lumen_mesh_cards limited;
    check(build_lumen_mesh_cards(geometry, false, 6, limited), "two-box cards build with a budget");
    check(limited.cards.size() == 6, "the card budget is respected");
    lumen_mesh_cards again;
    build_lumen_mesh_cards(geometry, false, 12, again);
    bool same = again.cards.size() == cards.cards.size();
    for(size_t i = 0; same && i < cards.cards.size(); ++i)
    {
        same = again.cards[i].origin == cards.cards[i].origin && again.cards[i].extent == cards.cards[i].extent;
    }
    check(same, "the card build is deterministic");
}

/// Cards are compiled into the mesh asset and read back at load time (UE FCardRepresentationData): every card and
/// the sidedness they were built for survive the round trip.
void test_lumen_cards_serialization_round_trip()
{
    std::printf("test_lumen_cards_serialization_round_trip\n");
    lumen_mesh_cards original;
    check(build_lumen_mesh_cards(make_box(math::vec3(0.5f, 1.0f, 2.0f)), true, 12, original), "box cards build");
    original.is_mostly_two_sided = true;
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    {
        ser20::oarchive_binary_t archive(stream);
        try_save(archive, ser20::make_nvp("cards", original));
    }
    lumen_mesh_cards restored;
    {
        ser20::iarchive_binary_t archive(stream);
        try_load(archive, ser20::make_nvp("cards", restored));
    }
    check(restored.cards.size() == original.cards.size() && !restored.cards.empty(), "every card survives");
    bool are_cards_equal = restored.cards.size() == original.cards.size();
    for(size_t i = 0; are_cards_equal && i < original.cards.size(); ++i)
    {
        const lumen_card& a = original.cards[i];
        const lumen_card& b = restored.cards[i];
        are_cards_equal = a.origin == b.origin && a.extent == b.extent && a.axis_x == b.axis_x &&
                          a.axis_y == b.axis_y && a.axis_z == b.axis_z && a.direction == b.direction;
    }
    check(are_cards_equal, "with its box, axes and direction");
    check(restored.bounds.min == original.bounds.min && restored.bounds.max == original.bounds.max, "bounds survive");
    check(restored.is_mostly_two_sided, "the sidedness the cards were built for survives");
}

/// A placement of one wall-sized card (16 m x 16 m face) for the surface cache scene tests.
auto make_wall_card_source() -> lumen_scene::source
{
    auto cards = std::make_shared<lumen_mesh_cards>();
    lumen_card card;
    card.extent = math::vec3(8.0f, 8.0f, 0.1f);
    cards->cards.push_back(card);
    lumen_scene::source source;
    source.identity = 1;
    source.instance_index = 0;
    source.cards = cards;
    return source;
}

/// Wall cards stacked 4 m apart along -z, one placement each, the first 1 m in front of a viewer at (0, 0, 1).
auto make_wall_card_stack(uint32_t card_count) -> std::vector<lumen_scene::source>
{
    std::vector<lumen_scene::source> sources;
    for(uint32_t i = 0; i < card_count; ++i)
    {
        lumen_scene::source source = make_wall_card_source();
        source.identity = i + 1u;
        source.instance_index = i;
        source.local_to_world = math::translate(math::mat4(1.0f), math::vec3(0.0f, 0.0f, -4.0f * float(i)));
        sources.push_back(source);
    }
    return sources;
}

/// UE maps a card's new mip only with physical room beside everything resident and room for every page in this
/// frame's capture atlas, and the new pages inherit the lighting of the previous allocation
/// (ProcessLumenSurfaceCacheRequests, bResampleLastLighting): lumen_scene lists that allocation for the resample.
void test_lumen_scene_reallocation_lists_the_previous_mip()
{
    std::printf("test_lumen_scene_reallocation_lists_the_previous_mip\n");
    const std::vector<lumen_scene::source> sources{make_wall_card_source()};
    const math::vec3 far_view(0.0f, 0.0f, 40.0f);
    const math::vec3 near_view(0.0f, 0.0f, 1.0f);
    lumen_scene scene;
    scene.init(lumen_scene::settings{});
    scene.update(sources, 1, far_view);
    const std::vector<lumen_scene::capture> first = scene.get_captures();
    check(first.size() == 1 && first.front().resample_card < 0, "a first allocation has no lighting to inherit");
    check(scene.get_resample_table().empty(), "and lists nothing to resample");
    const math::vec4 first_mip = scene.get_card_table()[4];
    std::printf("  far: %.0f x %.0f pages at res level %.0f x %.0f\n", first_mip.x, first_mip.y, first_mip.z, first_mip.w);
    scene.update(sources, 1, near_view);
    const auto& second = scene.get_captures();
    const math::vec4 second_mip = scene.get_card_table()[4];
    std::printf("  near: %.0f x %.0f pages at res level %.0f x %.0f, %zu captures\n",
                second_mip.x,
                second_mip.y,
                second_mip.z,
                second_mip.w,
                second.size());
    check(second_mip.z > first_mip.z, "the card is reallocated finer when the viewer comes close");
    check(second.size() == size_t(second_mip.x * second_mip.y), "every page of the new mip is captured at once");
    check(std::all_of(second.begin(),
                      second.end(),
                      [](const lumen_scene::capture& cap)
                      {
                          return cap.resample_card == 0;
                      }),
          "every new page inherits the previous allocation's lighting");
    const auto& table = scene.get_resample_table();
    check(scene.get_resample_page_base() == lumen_scene::card_stride && table.size() == lumen_scene::card_stride + 1u,
          "the resample table holds the previous card and its one page");
    check(table.size() > lumen_scene::card_stride && table[4] == first_mip, "with the previous mip");
    check(table.size() > lumen_scene::card_stride &&
              math::vec2(table[lumen_scene::card_stride]) == math::vec2(first.front().atlas_offset),
          "and the previous page's atlas texels");
    const auto& pages = scene.get_page_table();
    check(std::all_of(pages.begin(),
                      pages.end(),
                      [](const math::vec4& page)
                      {
                          return page.z > 0.0f;
                      }),
          "every page of a resident card is mapped");
    scene.update(sources, 1, near_view);
    // Settled, the card keeps its mip; its only captures are the refresh's (test_lumen_scene_refresh...).
    check(scene.get_stats().reallocated == 0 && scene.get_card_table()[4] == second_mip,
          "a settled card is not reallocated");
    check(scene.get_stats().refreshed == scene.get_captures().size(), "its only captures are the refresh's");
    // A capture atlas of one page cannot take the near mip's pages: the card keeps its far allocation and waits.
    lumen_scene::settings small_capture;
    small_capture.capture_atlas_size = lumen_scene::physical_page_size;
    lumen_scene waiting;
    waiting.init(small_capture);
    waiting.update(sources, 1, far_view);
    waiting.update(sources, 1, near_view);
    check(waiting.get_stats().refreshed == waiting.get_captures().size(),
          "a mip the capture atlas cannot take is not mapped");
    check(waiting.get_card_table()[4] == first_mip, "the card keeps its previous allocation meanwhile");
}

/// UE captures resident pages again, the longest-uncaptured first, within CardCaptureRefreshFraction of the frame's
/// page and texel budgets, so material changes reach the surface cache; a recaptured page resamples its own card's
/// lighting in place and the tables stay as they are (LumenSceneRendering.cpp SceneCardCaptureRefresh,
/// RecaptureCardPage, GetCardCaptureRefreshNumPages / NumTexels).
void test_lumen_scene_refresh_recaptures_the_oldest_pages()
{
    std::printf("test_lumen_scene_refresh_recaptures_the_oldest_pages\n");
    constexpr uint32_t card_count = 12;
    constexpr uint32_t settle_frames = 16;
    using page_key = std::pair<uint32_t, uint32_t>;
    const std::vector<lumen_scene::source> sources = make_wall_card_stack(card_count);
    const math::vec3 view(0.0f, 0.0f, 1.0f);
    lumen_scene scene;
    scene.init(lumen_scene::settings{});
    // Every capture since the first frame, allocations included: the frame each atlas position was last captured.
    std::map<page_key, uint32_t> last_capture;
    uint32_t frame = 0;
    const auto update = [&]()
    {
        scene.update(sources, card_count, view);
        ++frame;
    };
    const auto record_captures = [&]()
    {
        for(const auto& cap : scene.get_captures())
        {
            last_capture[{cap.atlas_offset.x, cap.atlas_offset.y}] = frame;
        }
    };
    for(uint32_t i = 0; i < settle_frames; ++i)
    {
        update();
        record_captures();
    }
    const lumen_scene::settings& s = scene.get_settings();
    const uint32_t budget_pages = std::clamp(uint32_t(float(s.max_captures_per_frame) * s.card_capture_refresh_fraction),
                                             1u,
                                             s.max_captures_per_frame);
    const uint64_t budget_texels =
        std::max(uint64_t(float(s.capture_atlas_size) * float(s.capture_atlas_size) * s.card_capture_refresh_fraction),
                 uint64_t(lumen_scene::physical_page_size) * lumen_scene::physical_page_size);
    const size_t page_count = scene.get_resident_pages().size();
    const uint64_t revision = scene.get_tables_revision();
    // The settled pages by atlas position: how often each is refreshed.
    std::map<page_key, uint32_t> refreshes;
    for(const auto& page : scene.get_resident_pages())
    {
        refreshes[{page.atlas_offset.x, page.atlas_offset.y}] = 0;
    }
    bool are_all_refreshes = true;
    bool is_within_budget = true;
    bool resamples_its_own_card = true;
    bool is_oldest_first = true;
    size_t most_in_a_frame = 0;
    uint32_t frames = 0;
    uint32_t refreshed = 0;
    std::vector<page_key> captured;
    // Every page twice over.
    while(refreshed < 2u * page_count && frames < 4096u)
    {
        update();
        ++frames;
        const auto& captures = scene.get_captures();
        are_all_refreshes = are_all_refreshes && scene.get_stats().refreshed == captures.size() && !captures.empty();
        uint64_t texels = 0;
        uint32_t newest_refreshed = 0;
        captured.clear();
        for(const auto& cap : captures)
        {
            texels += uint64_t(cap.size.x) * cap.size.y;
            const auto& resample = scene.get_resample_table();
            const size_t entry = size_t(std::max(cap.resample_card, 0)) * lumen_scene::card_stride + 4u;
            resamples_its_own_card = resamples_its_own_card && cap.resample_card >= 0 && entry < resample.size() &&
                                     resample[entry] == scene.get_card_table()[cap.card_index * lumen_scene::card_stride + 4u];
            const page_key key{cap.atlas_offset.x, cap.atlas_offset.y};
            captured.push_back(key);
            auto it = refreshes.find(key);
            is_oldest_first = is_oldest_first && it != refreshes.end();
            if(it != refreshes.end())
            {
                ++it->second;
            }
            newest_refreshed = std::max(newest_refreshed, last_capture[key]);
        }
        // Oldest first: no page left waiting was captured longer ago than a page refreshed now (ties in any order,
        // as UE's heap takes them).
        for(const auto& entry : refreshes)
        {
            const bool was_refreshed = std::find(captured.begin(), captured.end(), entry.first) != captured.end();
            is_oldest_first = is_oldest_first && (was_refreshed || last_capture[entry.first] >= newest_refreshed);
        }
        record_captures();
        refreshed += uint32_t(captures.size());
        most_in_a_frame = std::max(most_in_a_frame, captures.size());
        is_within_budget = is_within_budget && captures.size() <= budget_pages && texels <= budget_texels;
    }
    const uint32_t never = uint32_t(std::count_if(refreshes.begin(),
                                                  refreshes.end(),
                                                  [](const auto& entry)
                                                  {
                                                      return entry.second == 0u;
                                                  }));
    std::printf("  %zu resident pages, budget %u pages / %llu texels a frame: %u refreshed in %u frames, at most %zu "
                "in one\n",
                page_count,
                budget_pages,
                static_cast<unsigned long long>(budget_texels),
                refreshed,
                frames,
                most_in_a_frame);
    check(most_in_a_frame < page_count, "a frame refreshes only part of the scene's pages");
    check(are_all_refreshes, "a settled scene captures only refreshes, every frame");
    check(is_within_budget, "within the refresh's share of the page and texel budgets");
    check(never == 0u, "every resident page is captured again");
    check(is_oldest_first, "the longest-uncaptured first");
    check(resamples_its_own_card, "a refreshed page resamples its own card's allocation");
    check(scene.get_tables_revision() == revision, "and the tables stay as they are");
    // Fraction 0 turns the refresh off (UE returns no pages and no texels).
    lumen_scene::settings no_refresh;
    no_refresh.card_capture_refresh_fraction = 0.0f;
    lumen_scene still;
    still.init(no_refresh);
    for(uint32_t i = 0; i < settle_frames; ++i)
    {
        still.update(sources, card_count, view);
    }
    check(still.get_captures().empty() && still.get_stats().refreshed == 0u, "a fraction of 0 captures nothing settled");
}

/// UE's card lighting scheduler: the per-frame tile budgets, the priority buckets, and a scene with more resident
/// tiles than a frame's budget (R/LumenSceneLighting.cpp:98-126, S/LumenSceneLighting.usf:105-366).
void test_lumen_scene_lighting_schedule()
{
    std::printf("test_lumen_scene_lighting_schedule\n");
    check(lumen_scene::compute_lighting_tile_budget(4096, 32) == 8281, "Epic's 4096 atlas relights 8281 direct tiles");
    check(lumen_scene::compute_lighting_tile_budget(4096, 64) == 4096, "and 4096 radiosity tiles a frame");
    check(lumen_scene::compute_lighting_tile_budget(2048, 32) == 2116, "a 2048 atlas 2116 direct tiles");
    check(lumen_scene::compute_lighting_tile_budget(256, 1024) == 256, "never less than one full page");
    check(lumen_scene::compute_lighting_bucket(2048, 2.0f) == 1, "a never-lit page in the frustum ranks 1");
    check(lumen_scene::compute_lighting_bucket(2048, 1.0f) == 2, "out of it 2");
    check(lumen_scene::compute_lighting_bucket(1, 2.0f) == 12, "a page lit last frame in the frustum ranks 12");
    check(lumen_scene::compute_lighting_bucket(64, 2.0f) == 6, "64 frames later 6");
    check(lumen_scene::compute_lighting_bucket(100000, 1.0f) == 0, "the most urgent bucket is 0");
    // Twelve wall cards stacked away from the viewer: the near ones take 2 x 2 pages, far more tiles than a frame's
    // budget. The frustum is far away, so only distance sets the speeds.
    constexpr uint32_t card_count = 12;
    const std::vector<lumen_scene::source> sources = make_wall_card_stack(card_count);
    const math::vec3 view(0.0f, 0.0f, 1.0f);
    const math::frustum far_frustum(math::bbox(math::vec3(1000.0f), math::vec3(1001.0f)));
    lumen_scene scene;
    scene.init(lumen_scene::settings{});
    const uint32_t budget_direct = lumen_scene::compute_lighting_tile_budget(scene.get_settings().atlas_size, 32);
    const uint32_t budget_radiosity = lumen_scene::compute_lighting_tile_budget(scene.get_settings().atlas_size, 64);
    constexpr uint32_t frame_count = 120;
    std::vector<uint32_t> updates(card_count, 0u);
    std::vector<uint32_t> pages_of_card(card_count, 0u);
    bool within_budget = true;
    uint32_t first_frame_unlit = 0;
    for(uint32_t frame = 0; frame < frame_count; ++frame)
    {
        scene.update(sources, card_count, view);
        scene.schedule_lighting(view, far_frustum);
        within_budget = within_budget && scene.get_stats().lit_tiles[lumen_scene::lighting_direct] <= budget_direct &&
                        scene.get_stats().lit_tiles[lumen_scene::lighting_radiosity] <= budget_radiosity;
        const auto& pages = scene.get_resident_pages();
        if(frame == 0)
        {
            first_frame_unlit = uint32_t(pages.size() - scene.get_lit_pages(lumen_scene::lighting_radiosity).size());
        }
        if(frame + 1u < frame_count / 2u)
        {
            continue;
        }
        // The second half, once every card is resident: radiosity updates per card, per page.
        std::fill(pages_of_card.begin(), pages_of_card.end(), 0u);
        for(const auto& page : pages)
        {
            ++pages_of_card[page.card_index];
        }
        for(const auto& lit : scene.get_lit_pages(lumen_scene::lighting_radiosity))
        {
            ++updates[pages[lit.resident_page].card_index];
        }
    }
    const auto& pages = scene.get_resident_pages();
    uint32_t total_tiles = 0;
    for(const auto& page : pages)
    {
        total_tiles += (page.size.x / 8u) * (page.size.y / 8u);
    }
    std::printf("  %zu resident pages, %u tiles; budgets %u direct / %u radiosity; first frame left %u pages unlit\n",
                pages.size(),
                total_tiles,
                budget_direct,
                budget_radiosity,
                first_frame_unlit);
    for(uint32_t i = 0; i < card_count; ++i)
    {
        std::printf("  card %u (%.1f m): %u pages, %.2f radiosity updates per page per frame\n",
                    i,
                    0.9f + 4.0f * float(i),
                    pages_of_card[i],
                    pages_of_card[i] > 0 ? float(updates[i]) / float(pages_of_card[i]) / float(frame_count / 2u) : 0.0f);
    }
    check(total_tiles > budget_radiosity, "the scene holds more tiles than a frame's radiosity budget");
    check(within_budget, "no frame lights more tiles than its budgets");
    const auto rate = [&](uint32_t card) -> float
    {
        return pages_of_card[card] > 0 ? float(updates[card]) / float(pages_of_card[card]) : 0.0f;
    };
    check(std::all_of(updates.begin(), updates.end(), [](uint32_t n) { return n > 0u; }), "every card is relit");
    check(rate(0) > rate(card_count - 1u), "a page near the viewer is relit more often than a far one");
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

// ---------------------------------------------------------------------------------

auto run_gi_bake_suite(rtti::context& /*ctx*/) -> int
{
    test_sphere_accuracy();
    test_field_is_conservative();
    test_stored_voxels_match_brute_force();
    test_sign_correctness();
    test_conservative_empty_bricks();
    test_conservative_empty_bricks_in_a_shell();
    test_large_open_submesh_bakes_signed();
    test_brick_seam_continuity();
    test_two_sided_shell();
    test_thin_wall();
    test_inverted_winding_is_corrected();
    test_gpu_addressing_matches_cpu();
    test_bounds_entry_is_not_a_hit();
    test_trace_from_outside_hits_the_surface_not_the_bounds();
    test_open_mesh_does_not_produce_inside_regions();
    test_doubled_sheet_bakes_unsigned();
    test_engine_plane_primitive_bakes_flat();
    test_open_sheet_bakes_solid_below();
    test_serialization_round_trip();
    test_invalid_field_is_rejected();
    test_determinism();
    test_sampling_cost_does_not_scale_with_field_size();
    test_clipmap_is_conservative();
    test_gpu_composed_clipmap_keeps_no_cpu_copy();
    test_clipmap_bounds_stretched_boxes_per_axis();
    test_clipmap_rotated_room_matches_boxes();
    test_engine_plane_composes_a_hittable_sheet();
    test_instance_grid_never_misses_an_instance();
    test_instance_grid_shader_walk_matches_cpu();
    test_instance_grid_cell_clamping_covers_every_instance();
    test_instance_grid_walk_stops_past_the_nearest_hit();
    test_instance_grid_handles_degenerate_input();
    test_clipmap_partial_update_follows_moved_geometry();
    test_clipmap_continuous_edits_update_partially();
    test_clipmap_full_edit_coalescing();
    test_clipmap_partial_cadence_is_staggered();
    test_editing_scales_history_and_trace_budget();
    test_clipmap_compose_shader_transcription_matches_cpu();
    test_clipmap_culled_composition_matches_brute_force();
    test_clipmap_transition_is_continuous();
    test_clipmap_blend_stays_conservative();
    test_clipmap_is_world_stable();
    test_clipmap_scroll_copy_matches_recompose();
    test_clipmap_sees_offscreen_geometry();
    test_raw_buffer_extraction_matches_direct_geometry();
    test_submesh_extraction_selects_only_its_own_submesh();
    test_unmapped_submesh_reports_no_transforms();
    test_submesh_bake_pass_cost_is_linear();
    test_bake_grid_scales_with_world_size();
    test_voxel_size_is_honoured_and_scale_free();
    test_mip_chain_is_coarser_cheaper_and_conservative();
    test_a_coarse_mip_is_resident_where_the_finest_does_not_fit();
    test_coarsest_mip_answers_the_lumen_coverage_band();
    test_mip_chain_bakes_one_solid();
    test_total_voxel_budget_bounds_a_field();
    test_lod_extraction_clamps_rather_than_failing();
    test_lod_extraction_keeps_each_submesh_to_its_own_bounds();
    test_degenerate_triangles_do_not_size_the_field();
    test_scattered_parts_are_detected_and_cannot_be_resolved();
    test_surface_test_keeps_ordinary_tessellation();
    test_bake_cost_is_dominated_by_voxels_not_triangles();
    test_bake_cost_estimate_reads_the_baked_grid();
    test_parallel_submesh_bake_matches_serial();
    test_degenerate_inputs();
    test_lumen_cards_box();
    test_lumen_cards_sheet();
    test_lumen_cards_doubled_sheet();
    test_lumen_cards_interior_layers();
    test_lumen_cards_serialization_round_trip();
    test_lumen_scene_reallocation_lists_the_previous_mip();
    test_lumen_scene_refresh_recaptures_the_oldest_pages();
    test_lumen_scene_lighting_schedule();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

REGISTER_TEST_SUITE("gi bake / sdf / clipmap", run_gi_bake_suite)
