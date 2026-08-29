/*
 * Validation suite for math::transform_t and the free rotation helpers.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite transform
 *
 * These pin the correctness fixes from the 2026-08 transform review:
 *   - detail::scale_fix collapsing tiny negative scales to a positive floor,
 *     flipping handedness in glm_recompose and inverse_transform_coord;
 *   - the Euler hint surviving unlimited incremental set_rotation drift because
 *     each step compared against the previous quaternion instead of the hint;
 *   - transform_normal / inverse_transform_normal flipping direction under a
 *     negative uniform scale and disagreeing in magnitude between the
 *     component and matrix branches;
 *   - rotate_axis passing a non-unit axis straight into glm::angleAxis;
 *   - the perspective divide in transform_coord producing NaNs at w = 0;
 *   - compare() overriding the caller's tolerance for one matrix element;
 *   - look_rotation / from_to_rotation NaN-ing on degenerate inputs.
 */

#include "../tests.h"

#include <math/math.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace unravel;

namespace
{

int g_checks = 0;
int g_failures = 0;

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
        std::printf("  FAIL: %s (got %.6f, expected %.6f +/- %.6f)\n", what.c_str(), actual, expected, tolerance);
    }
}

void check_near(const math::vec3& actual, const math::vec3& expected, float tolerance, const std::string& what)
{
    ++g_checks;
    if(!(math::length(actual - expected) <= tolerance))
    {
        ++g_failures;
        std::printf("  FAIL: %s (got (%.6f, %.6f, %.6f), expected (%.6f, %.6f, %.6f))\n",
                    what.c_str(),
                    actual.x,
                    actual.y,
                    actual.z,
                    expected.x,
                    expected.y,
                    expected.z);
    }
}

void check_finite(const math::vec3& v, const std::string& what)
{
    check(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z), what);
}

void check_finite(const math::quat& q, const std::string& what)
{
    check(std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w), what);
}

// ---------------------------------------------------------------------------------

void test_scale_fix_preserves_sign()
{
    std::printf("scale_fix sign preservation\n");

    float tiny_negative = -0.00005f;
    float tiny_positive = 0.00005f;
    float regular_negative = -0.5f;
    check(math::detail::scale_fix(tiny_negative) < 0.0f, "tiny negative scale stays negative");
    check(math::detail::scale_fix(tiny_positive) > 0.0f, "tiny positive scale stays positive");
    check_near(math::detail::scale_fix(regular_negative), -0.5f, 0.0f, "scales beyond the floor pass through untouched");

    // A mirrored near-zero scale must keep the mirror in the recomposed matrix.
    math::mat4 recomposed{1.0f};
    math::glm_recompose(recomposed,
                        math::vec3{-0.00005f, 1.0f, 1.0f},
                        glm::identity<math::quat>(),
                        math::vec3{0.0f},
                        math::vec3{0.0f},
                        math::vec4{0.0f, 0.0f, 0.0f, 1.0f});
    check(glm::determinant(math::mat3{recomposed}) < 0.0f,
          "glm_recompose keeps mirror handedness for a near-zero negative scale");

    // Same fix seen through the transform API: the inverse of a tiny negative
    // scale must have a negative sign, so a coord round trip keeps its sign.
    math::transform t;
    t.set_scale(-0.00005f, 1.0f, 1.0f);
    const math::vec3 world = t.transform_coord(math::vec3{1.0f, 0.0f, 0.0f});
    const math::vec3 local = t.inverse_transform_coord(world);
    check(local.x > 0.0f, "inverse_transform_coord keeps the sign through a tiny negative scale");
}

void test_euler_hint_follows_incremental_rotation()
{
    std::printf("euler hint vs incremental rotation\n");

    math::transform t;
    t.set_rotation_euler_degrees({0.0f, 10.0f, 0.0f});
    // 40 steps of 1 degree: each step is inside the hint-equivalence tolerance,
    // so comparing new-vs-previous would never invalidate the hint while the
    // actual orientation walks 40 degrees away.
    for(int i = 0; i < 40; ++i)
    {
        t.rotate_axis(math::radians(1.0f), math::vec3{0.0f, 1.0f, 0.0f});
    }

    const math::vec3 euler = t.get_rotation_euler_degrees();
    const math::quat from_euler = math::normalize(math::quat(math::radians(euler)));
    check(math::abs(math::dot(from_euler, t.get_rotation())) > 0.999f,
          "euler readback represents the actual orientation after incremental rotation");
    check_near(euler.y, 50.0f, 0.5f, "yaw readback tracks the accumulated rotation");

    // The tolerance exists so re-applying the same orientation keeps the typed
    // triple, including at gimbal lock where extraction would mangle it.
    math::transform t2;
    t2.set_rotation_euler_degrees({90.0f, 45.0f, 0.0f});
    t2.set_rotation(t2.get_rotation());
    check_near(t2.get_rotation_euler_degrees(), {90.0f, 45.0f, 0.0f}, 1e-4f,
               "re-applying the same quaternion keeps the authored euler triple");
}

void test_euler_hint_restore_after_decompose()
{
    std::printf("euler hint restore across a matrix round trip\n");

    math::transform src;
    src.set_rotation_euler_degrees({90.0f, 30.0f, 0.0f});
    math::transform dst{src.get_matrix()};
    dst.restore_euler_angles_hint_degrees({90.0f, 30.0f, 0.0f});
    check_near(dst.get_rotation_euler_degrees(), {90.0f, 30.0f, 0.0f}, 1e-4f,
               "restored hint survives decompose at gimbal lock");
}

void test_normals_under_negative_uniform_scale()
{
    std::printf("normals under negative uniform scale\n");

    math::transform t;
    t.set_scale(-2.0f, -2.0f, -2.0f);
    t.set_rotation_euler_degrees({0.0f, 90.0f, 0.0f});

    const math::vec3 v{1.0f, 0.0f, 0.0f};
    const math::vec3 n = t.transform_normal(v);

    // Ground truth straight from the normal matrix (inverse transpose).
    const math::mat3 linear{t.get_matrix()};
    const math::vec3 ref = math::normalize(glm::transpose(glm::inverse(linear)) * v);
    check(math::dot(math::normalize(n), ref) > 0.999f,
          "transform_normal direction matches the normal matrix under negative uniform scale");
    check_near(math::length(n), 1.0f, 1e-4f, "transform_normal preserves the input length");

    const math::vec3 back = t.inverse_transform_normal(n);
    check_near(back, v, 1e-3f, "inverse_transform_normal inverts transform_normal (negative uniform scale)");
}

void test_normal_branch_consistency()
{
    std::printf("normal transform branch consistency\n");

    // Uniform positive scale: TransformDirection semantics, length preserved.
    math::transform t;
    t.set_scale(2.0f, 2.0f, 2.0f);
    t.set_rotation_euler_degrees({0.0f, 90.0f, 0.0f});
    const math::vec3 d = t.transform_normal(math::vec3{1.0f, 0.0f, 0.0f});
    check_near(d, {0.0f, 0.0f, -1.0f}, 1e-4f, "uniform scale does not leak into transform_normal");

    // Skewed matrix: the matrix branch must share the same length contract and
    // invert exactly through inverse_transform_normal.
    math::mat4 shear{1.0f};
    shear[1][0] = 0.35f;
    const math::mat4 m = math::mat4_cast(math::quat(math::radians(math::vec3{20.0f, 30.0f, 40.0f}))) * shear *
                         math::scale(math::mat4{1.0f}, math::vec3{1.0f, 2.0f, 3.0f});
    const math::transform ts{m};
    const math::vec3 v = math::normalize(math::vec3{0.3f, -0.7f, 0.2f});
    const math::vec3 out = ts.transform_normal(v);
    check_near(math::length(out), 1.0f, 1e-4f, "skewed transform_normal preserves the input length");
    const math::vec3 back = ts.inverse_transform_normal(out);
    check_near(back, v, 1e-3f, "inverse_transform_normal inverts transform_normal (skewed matrix)");
}

void test_rotate_axis_axis_handling()
{
    std::printf("rotate_axis axis handling\n");

    math::transform t;
    t.rotate_axis(math::radians(90.0f), math::vec3{0.0f, 2.0f, 0.0f});
    const math::vec3 r = t.get_rotation() * math::vec3{1.0f, 0.0f, 0.0f};
    check_near(r, {0.0f, 0.0f, -1.0f}, 1e-4f, "a non-unit axis rotates by the requested angle");

    const math::quat before = t.get_rotation();
    t.rotate_axis(1.0f, math::vec3{0.0f, 0.0f, 0.0f});
    check_finite(t.get_rotation(), "a zero axis does not poison the rotation");
    check(math::abs(math::dot(before, t.get_rotation())) > 0.9999f, "a zero axis leaves the rotation unchanged");
}

void test_perspective_divide_guard()
{
    std::printf("perspective divide guard\n");

    math::transform t;
    t.set_perspective(0.0f, 0.0f, -1.0f, 0.0f);
    // The origin lands on the eye plane of this perspective (w = 0).
    check_finite(t.transform_coord(math::vec3{0.0f, 0.0f, 0.0f}),
                 "transform_coord stays finite when the perspective divide degenerates");
}

void test_compare_honors_tolerance()
{
    std::printf("compare tolerance\n");

    math::transform a;
    math::transform b;
    b.set_position(0.0005f, 0.0f, 0.0f);
    check(a.compare(b, 0.001f) == 0, "difference inside the tolerance compares equal");
    b.set_position(0.002f, 0.0f, 0.0f);
    check(a.compare(b, 0.001f) == 1, "difference outside the tolerance compares unequal");
}

void test_from_to_rotation()
{
    std::printf("from_to_rotation\n");

    const math::quat q = math::from_to_rotation({1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    check_near(q * math::vec3{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 1e-4f, "rotates from onto to");

    const math::quat opposite = math::from_to_rotation({1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f});
    check_finite(opposite, "antipodal vectors give a finite quaternion");
    check_near(opposite * math::vec3{1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, 1e-4f, "antipodal vectors rotate by pi");

    const math::quat degenerate = math::from_to_rotation({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f});
    check_finite(degenerate, "a zero-length input gives a finite quaternion");
    check(degenerate.w > 0.999f, "a zero-length input gives identity");

    const math::vec3 f{1.0f, 0.0f, 0.0f};
    const math::vec3 nearly = math::normalize(math::vec3{1.0f, 2e-4f, 0.0f});
    const math::quat tiny = math::from_to_rotation(f, nearly);
    check_near(tiny * f, nearly, 1e-5f, "a tiny angle is represented accurately");
}

void test_look_rotation_matches_lookat_reference()
{
    std::printf("look_rotation equivalence vs inverse(lookAtLH)\n");

    // The pre-2026-08 implementation was quat_cast(inverse(lookAt(0, forward, up))),
    // which resolves to lookAtLH under this engine's GLM_FORCE_LEFT_HANDED config.
    // The direct basis construction must agree with it on every non-degenerate input.
    const math::vec3 up{0.0f, 1.0f, 0.0f};
    float min_agreement = 1.0f;
    int compared = 0;
    for(int xi = -2; xi <= 2; ++xi)
    {
        for(int yi = -2; yi <= 2; ++yi)
        {
            for(int zi = -2; zi <= 2; ++zi)
            {
                const math::vec3 fwd{0.5f * static_cast<float>(xi),
                                     0.5f * static_cast<float>(yi),
                                     0.5f * static_cast<float>(zi)};
                if(math::dot(fwd, fwd) < 1e-6f)
                {
                    continue;
                }
                // The reference NaNs when forward is collinear with up - that case is
                // covered by the dedicated degenerate checks below.
                if(math::abs(math::dot(math::normalize(fwd), up)) > 0.999f)
                {
                    continue;
                }
                const math::quat ref =
                    glm::quat_cast(glm::inverse(glm::lookAtLH(math::vec3{0.0f}, fwd, up)));
                const math::quat got = math::look_rotation(fwd, up);
                min_agreement = std::min(min_agreement, math::abs(math::dot(ref, got)));
                ++compared;
            }
        }
    }
    check(compared > 50, "direction sweep covered a useful sample");
    check_near(min_agreement, 1.0f, 1e-5f, "look_rotation matches the old inverse(lookAtLH) path everywhere");
}

void test_from_to_rotation_matches_acos_reference()
{
    std::printf("from_to_rotation equivalence vs acos/angleAxis\n");

    // The pre-2026-08 implementation used angleAxis(acos(dot), normalize(cross)).
    // The half-vector construction must agree with it across the generic range,
    // and the rotation must actually map f onto t.
    std::vector<math::vec3> dirs;
    for(int xi = -1; xi <= 1; ++xi)
    {
        for(int yi = -1; yi <= 1; ++yi)
        {
            for(int zi = -1; zi <= 1; ++zi)
            {
                const math::vec3 v{static_cast<float>(xi), static_cast<float>(yi), static_cast<float>(zi)};
                if(math::dot(v, v) > 0.0f)
                {
                    dirs.push_back(math::normalize(v));
                }
            }
        }
    }
    float min_agreement = 1.0f;
    float max_map_error = 0.0f;
    int compared = 0;
    for(const math::vec3& f : dirs)
    {
        for(const math::vec3& t : dirs)
        {
            const float d = math::clamp(math::dot(f, t), -1.0f, 1.0f);
            const math::quat got = math::from_to_rotation(f, t);
            if(d > -0.999f)
            {
                max_map_error = std::max(max_map_error, math::length(got * f - t));
            }
            if(d > -0.999f && d < 0.999f)
            {
                const math::quat ref = glm::angleAxis(math::acos(d), math::normalize(math::cross(f, t)));
                min_agreement = std::min(min_agreement, math::abs(math::dot(ref, got)));
                ++compared;
            }
        }
    }
    check(compared > 200, "direction-pair sweep covered a useful sample");
    check_near(min_agreement, 1.0f, 1e-5f, "from_to_rotation matches the old acos/angleAxis path mid-range");
    check(max_map_error < 1e-4f, "from_to_rotation maps f onto t for every direction pair");
}

void test_look_rotation()
{
    std::printf("look_rotation\n");

    const math::quat identity_like = math::look_rotation({0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f});
    check_near(identity_like * math::vec3{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, 1e-4f, "+Z forward is identity");

    const math::quat toward_x = math::look_rotation({1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    check_near(toward_x * math::vec3{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, 1e-4f, "local +Z maps onto forward");
    check_near(toward_x * math::vec3{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 1e-4f, "up is respected");

    const math::quat collinear = math::look_rotation({0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    check_finite(collinear, "forward parallel to up stays finite");
    check_near(collinear * math::vec3{0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, 1e-4f,
               "forward parallel to up still looks along forward");

    const math::quat zero_forward = math::look_rotation({0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    check_finite(zero_forward, "a zero forward stays finite");
    check(zero_forward.w > 0.999f, "a zero forward gives identity");
}

void test_operator_multiply_matches_matrix_reference()
{
    std::printf("operator* vs matrix-product reference\n");

    // Left operands: fast-path eligible (skew/perspective free, uniform scale,
    // including negative-uniform) plus fallback shapes (non-uniform scale).
    std::vector<math::transform> lefts;
    {
        math::transform t;
        lefts.push_back(t); // identity
        t.set_position(3.0f, -2.0f, 8.0f);
        t.set_rotation_euler_degrees({25.0f, -40.0f, 10.0f});
        t.set_scale(2.0f, 2.0f, 2.0f);
        lefts.push_back(t);
        t.set_scale(-1.5f, -1.5f, -1.5f); // negative uniform
        lefts.push_back(t);
        t.set_scale(1.0f, 2.0f, 3.0f); // non-uniform -> must take the matrix path
        lefts.push_back(t);
    }
    // Right operands: simplified, including non-uniform scale (legal for the
    // fast path as long as the LEFT scale is uniform).
    std::vector<math::transform> rights;
    {
        math::transform t;
        t.set_position(-5.0f, 1.0f, 2.5f);
        t.set_rotation_euler_degrees({0.0f, 70.0f, -15.0f});
        rights.push_back(t);
        t.set_scale(0.5f, 4.0f, 1.25f); // non-uniform right scale
        rights.push_back(t);
        t.set_position(10.0f, -8.0f, 3.0f);
        t.set_rotation_euler_degrees({89.0f, 0.0f, 45.0f});
        t.set_scale(3.0f, 3.0f, 3.0f);
        rights.push_back(t);
    }

    float max_matrix_diff = 0.0f;
    float max_position_diff = 0.0f;
    for(const auto& a : lefts)
    {
        for(const auto& b : rights)
        {
            const math::transform product = a * b;
            const math::mat4 ref = a.get_matrix() * b.get_matrix();
            const math::mat4& got = product.get_matrix();
            for(int c = 0; c < 4; ++c)
            {
                for(int r = 0; r < 4; ++r)
                {
                    max_matrix_diff = std::max(max_matrix_diff, std::fabs(got[c][r] - ref[c][r]));
                }
            }
            // Components must agree with what decomposing the reference yields.
            const math::transform ref_t{ref};
            max_position_diff = std::max(max_position_diff, math::length(product.get_position() - ref_t.get_position()));
            check(math::abs(math::dot(product.get_rotation(), ref_t.get_rotation())) > 0.9999f,
                  "operator* rotation matches the decomposed matrix product");
        }
    }
    check(max_matrix_diff < 1e-3f, "operator* matrix matches the explicit matrix product");
    check(max_position_diff < 1e-3f, "operator* position matches the decomposed matrix product");

    // Dirty right operand: the fast path must defer the matrix and still agree.
    math::transform a = lefts[1];
    math::transform b = rights[1];
    b.set_position(7.0f, 7.0f, -7.0f); // dirties b's matrix
    const math::transform lazy = a * b;
    const math::mat4 lazy_ref = a.get_matrix() * b.get_matrix();
    float lazy_diff = 0.0f;
    for(int c = 0; c < 4; ++c)
    {
        for(int r = 0; r < 4; ++r)
        {
            lazy_diff = std::max(lazy_diff, std::fabs(lazy.get_matrix()[c][r] - lazy_ref[c][r]));
        }
    }
    check(lazy_diff < 1e-3f, "operator* with a dirty operand recomposes to the matrix product");

    // Chain drift: 64 composed steps stay glued to the pure mat4 chain.
    math::transform acc;
    math::mat4 acc_m{1.0f};
    math::transform step;
    step.set_position(0.1f, -0.05f, 0.2f);
    step.set_rotation_euler_degrees({1.0f, 2.0f, 0.5f});
    for(int i = 0; i < 64; ++i)
    {
        acc = acc * step;
        acc_m = acc_m * step.get_matrix();
    }
    check(math::length(acc.get_position() - math::vec3{acc_m[3]}) < 1e-3f,
          "64-deep operator* chain position stays glued to the mat4 chain");
    const math::transform acc_ref{acc_m};
    check(math::abs(math::dot(acc.get_rotation(), acc_ref.get_rotation())) > 0.9999f,
          "64-deep operator* chain rotation stays glued to the mat4 chain");
}

void test_inverse_and_axis_fast_paths()
{
    std::printf("inverse and axis getters vs matrix reference\n");

    std::vector<math::transform> shapes;
    {
        math::transform t;
        t.set_position(4.0f, -6.0f, 9.0f);
        t.set_rotation_euler_degrees({30.0f, -50.0f, 20.0f});
        t.set_scale(2.0f, 2.0f, 2.0f);
        shapes.push_back(t); // uniform TRS -> component inverse fast path
        t.set_scale(-1.5f, -1.5f, -1.5f);
        shapes.push_back(t); // negative uniform
        t.set_scale(1.0f, 2.0f, 3.0f);
        shapes.push_back(t); // non-uniform -> affine inverse path
    }
    {
        math::mat4 shear{1.0f};
        shear[1][0] = 0.4f;
        shapes.emplace_back(math::mat4{math::mat4_cast(math::quat(math::radians(math::vec3{10.0f, 20.0f, 30.0f}))) * shear});
    }

    for(const auto& t : shapes)
    {
        // inverse() must agree with the explicit 4x4 inverse.
        const math::mat4 ref = glm::inverse(math::mat4{t.get_matrix()});
        const math::mat4& got = math::inverse(t).get_matrix();
        float diff = 0.0f;
        for(int c = 0; c < 4; ++c)
        {
            for(int r = 0; r < 4; ++r)
            {
                diff = std::max(diff, std::fabs(got[c][r] - ref[c][r]));
            }
        }
        check(diff < 1e-3f, "inverse(transform) matches the 4x4 matrix inverse");

        // The axis getters must match the matrix columns on every path.
        const math::mat4& m = t.get_matrix();
        check(math::length(t.x_axis() - math::vec3{m[0]}) < 1e-4f, "x_axis matches matrix column 0");
        check(math::length(t.y_axis() - math::vec3{m[1]}) < 1e-4f, "y_axis matches matrix column 1");
        check(math::length(t.z_axis() - math::vec3{m[2]}) < 1e-4f, "z_axis matches matrix column 2");

        // Round trip through the inverse recovers the original point.
        const math::vec3 p{1.5f, -2.5f, 0.75f};
        const math::vec3 there = t.transform_coord(p);
        check(math::length(math::inverse(t).transform_coord(there) - p) < 1e-3f,
              "inverse(transform) round-trips transform_coord");
        check(math::length(t.inverse_transform_coord(there) - p) < 1e-3f,
              "inverse_transform_coord round-trips transform_coord");
    }
}

void test_recompose_matches_reference()
{
    std::printf("simplified recompose vs T*R*S reference\n");

    // The simplified update_matrix path writes scaled rotation columns directly;
    // it must equal the explicit translate * mat4_cast * scale product.
    const math::vec3 positions[] = {{0.0f, 0.0f, 0.0f}, {12.5f, -3.0f, 7.25f}, {-100.0f, 50.0f, 0.001f}};
    const math::vec3 eulers[] = {{0.0f, 0.0f, 0.0f}, {30.0f, -45.0f, 60.0f}, {90.0f, 15.0f, -5.0f}};
    const math::vec3 scales[] = {{1.0f, 1.0f, 1.0f}, {2.5f, 2.5f, 2.5f}, {1.0f, 2.0f, 3.0f}, {-2.0f, -2.0f, -2.0f}};

    float max_diff = 0.0f;
    for(const auto& p : positions)
    {
        for(const auto& e : eulers)
        {
            for(const auto& s : scales)
            {
                math::transform t;
                t.set_position(p);
                t.set_rotation_euler_degrees(e);
                t.set_scale(s);
                const math::mat4& m = t.get_matrix();

                const math::mat4 identity{1.0f};
                const math::mat4 ref = math::translate(identity, p) *
                                       math::mat4_cast(math::normalize(math::quat(math::radians(e)))) *
                                       math::scale(identity, s);
                for(int c = 0; c < 4; ++c)
                {
                    for(int r = 0; r < 4; ++r)
                    {
                        max_diff = std::max(max_diff, std::fabs(m[c][r] - ref[c][r]));
                    }
                }
            }
        }
    }
    check(max_diff < 1e-4f, "simplified recompose equals translate * mat4_cast * scale");
}

// ---------------------------------------------------------------------------------
// Randomized property tests against a double-precision oracle.
//
// Every reference is computed in double with raw glm (independent of transform_t),
// so these measure the float implementation against ground truth rather than one
// float branch against another. Each property aggregates the worst observed error
// over its whole random sweep, prints it, and asserts a bound - so a pass carries
// the actual numerical margin, not just a boolean.

struct deterministic_rng
{
    std::uint64_t state{0x123456789ABCDEF0ull};

    auto next01() -> float
    {
        state += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return static_cast<float>(z >> 40) * (1.0f / 16777216.0f);
    }

    auto range(float a, float b) -> float
    {
        return a + (b - a) * next01();
    }
};

auto widen(const math::vec3& v) -> glm::dvec3
{
    return {static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)};
}

auto widen(const math::quat& q) -> glm::dquat
{
    return glm::dquat(static_cast<double>(q.w),
                      static_cast<double>(q.x),
                      static_cast<double>(q.y),
                      static_cast<double>(q.z));
}

auto widen(const math::mat4& m) -> glm::dmat4
{
    glm::dmat4 result{};
    for(int c = 0; c < 4; ++c)
    {
        for(int r = 0; r < 4; ++r)
        {
            result[c][r] = static_cast<double>(m[c][r]);
        }
    }
    return result;
}

/// Independent double-precision reconstruction of a simplified (skew/perspective
/// free) transform: T * R * S from the exact stored float components.
auto trs_oracle(const math::transform& t) -> glm::dmat4
{
    return glm::translate(glm::dmat4{1.0}, widen(t.get_position())) *
           glm::mat4_cast(glm::normalize(widen(t.get_rotation()))) *
           glm::scale(glm::dmat4{1.0}, widen(t.get_scale()));
}

auto rel_err(const math::mat4& got, const glm::dmat4& ref) -> double
{
    double max_ref = 1.0;
    double max_diff = 0.0;
    for(int c = 0; c < 4; ++c)
    {
        for(int r = 0; r < 4; ++r)
        {
            max_ref = std::max(max_ref, std::fabs(ref[c][r]));
            max_diff = std::max(max_diff, std::fabs(static_cast<double>(got[c][r]) - ref[c][r]));
        }
    }
    return max_diff / max_ref;
}

auto rel_err_d(const glm::dmat4& a, const glm::dmat4& b) -> double
{
    double max_ref = 1.0;
    double max_diff = 0.0;
    for(int c = 0; c < 4; ++c)
    {
        for(int r = 0; r < 4; ++r)
        {
            max_ref = std::max(max_ref, std::fabs(b[c][r]));
            max_diff = std::max(max_diff, std::fabs(a[c][r] - b[c][r]));
        }
    }
    return max_diff / max_ref;
}

auto rel_err_v(const math::vec3& got, const glm::dvec3& ref) -> double
{
    return glm::length(widen(got) - ref) / std::max(1.0, glm::length(ref));
}

auto random_unit_quat(deterministic_rng& rng) -> math::quat
{
    math::vec3 axis{rng.range(-1.0f, 1.0f), rng.range(-1.0f, 1.0f), rng.range(-1.0f, 1.0f)};
    const float len2 = math::dot(axis, axis);
    axis = len2 < 1e-4f ? math::vec3{1.0f, 0.0f, 0.0f} : axis / std::sqrt(len2);
    return glm::angleAxis(rng.range(-math::pi<float>(), math::pi<float>()), axis);
}

enum class scale_shape
{
    uniform,
    negative_uniform,
    non_uniform
};

auto random_simple_transform(deterministic_rng& rng, scale_shape shape) -> math::transform
{
    math::transform t;
    t.set_position(rng.range(-50.0f, 50.0f), rng.range(-50.0f, 50.0f), rng.range(-50.0f, 50.0f));
    t.set_rotation(random_unit_quat(rng));
    switch(shape)
    {
        case scale_shape::uniform:
        {
            const float s = rng.range(0.2f, 4.0f);
            t.set_scale(s, s, s);
            break;
        }
        case scale_shape::negative_uniform:
        {
            const float s = -rng.range(0.2f, 4.0f);
            t.set_scale(s, s, s);
            break;
        }
        case scale_shape::non_uniform:
        {
            t.set_scale(rng.range(0.2f, 4.0f), rng.range(0.2f, 4.0f), rng.range(0.2f, 4.0f));
            break;
        }
    }
    return t;
}

auto random_skewed_transform(deterministic_rng& rng) -> math::transform
{
    // Build a genuinely sheared matrix in double, then narrow to float: the
    // transform's own matrix is the source of truth for these shapes.
    glm::dmat4 shear{1.0};
    shear[1][0] = static_cast<double>(rng.range(-0.5f, 0.5f));
    shear[2][0] = static_cast<double>(rng.range(-0.5f, 0.5f));
    shear[2][1] = static_cast<double>(rng.range(-0.5f, 0.5f));
    const glm::dmat4 dm = trs_oracle(random_simple_transform(rng, scale_shape::non_uniform)) * shear;
    math::mat4 m{};
    for(int c = 0; c < 4; ++c)
    {
        for(int r = 0; r < 4; ++r)
        {
            m[c][r] = static_cast<float>(dm[c][r]);
        }
    }
    return math::transform{m};
}

void test_prop_recompose_oracle()
{
    std::printf("property: update_matrix vs double T*R*S oracle\n");

    deterministic_rng rng;
    double worst = 0.0;
    constexpr int k_cases = 2000;
    for(int i = 0; i < k_cases; ++i)
    {
        const auto shape = static_cast<scale_shape>(i % 3);
        const math::transform t = random_simple_transform(rng, shape);
        worst = std::max(worst, rel_err(t.get_matrix(), trs_oracle(t)));
    }
    std::printf("  max relative error over %d cases: %.3g\n", k_cases, worst);
    check(worst < 1e-5, "simplified recompose stays within float budget of the double oracle");
}

void test_prop_operator_multiply_oracle()
{
    std::printf("property: operator* vs double oracle (matrix AND components)\n");

    deterministic_rng rng;
    double worst_matrix = 0.0;
    double worst_position = 0.0;
    double worst_rotation = 0.0;
    double worst_scale = 0.0;
    double worst_theorem = 0.0;
    constexpr int k_cases = 1500;
    for(int i = 0; i < k_cases; ++i)
    {
        // Left: fast-path eligible (uniform or negative-uniform scale).
        // Right: any simplified shape, including non-uniform scale.
        const auto left_shape = (i % 2) == 0 ? scale_shape::uniform : scale_shape::negative_uniform;
        const auto right_shape = static_cast<scale_shape>(i % 3);
        const math::transform a = random_simple_transform(rng, left_shape);
        const math::transform b = random_simple_transform(rng, right_shape);

        const glm::dmat4 oracle = trs_oracle(a) * trs_oracle(b);
        const math::transform product = a * b;

        worst_matrix = std::max(worst_matrix, rel_err(product.get_matrix(), oracle));

        // Component references computed directly in double - no decompose involved.
        const glm::dvec3 dp1 = widen(a.get_position());
        const glm::dvec3 dp2 = widen(b.get_position());
        const glm::dvec3 ds1 = widen(a.get_scale());
        const glm::dvec3 ds2 = widen(b.get_scale());
        const glm::dquat dq1 = glm::normalize(widen(a.get_rotation()));
        const glm::dquat dq2 = glm::normalize(widen(b.get_rotation()));
        const glm::dvec3 pos_ref = dp1 + dq1 * (ds1 * dp2);
        const glm::dquat rot_ref = glm::normalize(dq1 * dq2);
        const glm::dvec3 scale_ref = ds1 * ds2;

        worst_position = std::max(worst_position, rel_err_v(product.get_position(), pos_ref));
        // Normalize the widened float quat so 1-|dot| measures pure angular error
        // rather than the float quat's ~5e-8 norm deficit.
        worst_rotation =
            std::max(worst_rotation, 1.0 - std::fabs(glm::dot(glm::normalize(widen(product.get_rotation())), rot_ref)));
        worst_scale = std::max(worst_scale, rel_err_v(product.get_scale(), scale_ref));

        // The commutation theorem itself, executed in double: with a uniform left
        // scale, T(pos_ref) R(rot_ref) S(scale_ref) must equal Ma * Mb to
        // double-precision roundoff. This proves the fast-path algebra, not just
        // the float implementation of it.
        const glm::dmat4 recombined = glm::translate(glm::dmat4{1.0}, pos_ref) * glm::mat4_cast(rot_ref) *
                                      glm::scale(glm::dmat4{1.0}, scale_ref);
        worst_theorem = std::max(worst_theorem, rel_err_d(recombined, oracle));
    }
    std::printf("  matrix %.3g  position %.3g  rotation(1-|dot|) %.3g  scale %.3g  theorem %.3g over %d cases\n",
                worst_matrix,
                worst_position,
                worst_rotation,
                worst_scale,
                worst_theorem,
                k_cases);
    check(worst_matrix < 1e-5, "operator* matrix stays within float budget of the double oracle");
    check(worst_position < 1e-5, "operator* position matches the double component formula");
    check(worst_rotation < 1e-11, "operator* rotation matches the double quaternion product");
    check(worst_scale < 1e-6, "operator* scale matches the double component product");
    check(worst_theorem < 1e-12, "commutation theorem holds to double precision (algebra is exact)");
}

void test_prop_operator_multiply_laws()
{
    std::printf("property: operator* identity and associativity laws\n");

    deterministic_rng rng;
    const math::transform& identity = math::transform::identity();
    double worst_identity = 0.0;
    double worst_assoc = 0.0;
    constexpr int k_cases = 600;
    for(int i = 0; i < k_cases; ++i)
    {
        // Mix eligible and fallback shapes so the laws hold ACROSS branch choices.
        const math::transform a = random_simple_transform(rng, static_cast<scale_shape>(i % 3));
        const math::transform b = (i % 5) == 0 ? random_skewed_transform(rng)
                                               : random_simple_transform(rng, static_cast<scale_shape>((i + 1) % 3));
        const math::transform c = random_simple_transform(rng, static_cast<scale_shape>((i + 2) % 3));

        worst_identity = std::max(worst_identity, rel_err((identity * a).get_matrix(), widen(a.get_matrix())));
        worst_identity = std::max(worst_identity, rel_err((a * identity).get_matrix(), widen(a.get_matrix())));

        const glm::dmat4 reference = widen(a.get_matrix()) * widen(b.get_matrix()) * widen(c.get_matrix());
        worst_assoc = std::max(worst_assoc, rel_err(((a * b) * c).get_matrix(), reference));
        worst_assoc = std::max(worst_assoc, rel_err((a * (b * c)).get_matrix(), reference));
    }
    std::printf("  identity %.3g  associativity %.3g over %d cases\n", worst_identity, worst_assoc, k_cases);
    check(worst_identity < 1e-6, "identity is a two-sided unit for operator*");
    check(worst_assoc < 1e-4, "operator* associates across fast/fallback branch mixes");
}

void test_prop_coord_and_normal_oracle()
{
    std::printf("property: transform_coord / normals vs double oracle + round trips\n");

    deterministic_rng rng;
    double worst_coord = 0.0;
    double worst_coord_trip = 0.0;
    double worst_normal_dir = 0.0;
    double worst_normal_len = 0.0;
    double worst_normal_trip = 0.0;
    constexpr int k_cases = 1500;
    for(int i = 0; i < k_cases; ++i)
    {
        const bool skewed = (i % 4) == 3;
        const math::transform t =
            skewed ? random_skewed_transform(rng) : random_simple_transform(rng, static_cast<scale_shape>(i % 3));
        const glm::dmat4 dm = skewed ? widen(t.get_matrix()) : trs_oracle(t);
        const math::vec3 v{rng.range(-10.0f, 10.0f), rng.range(-10.0f, 10.0f), rng.range(-10.0f, 10.0f)};

        // Coordinates.
        const glm::dvec4 mapped = dm * glm::dvec4(widen(v), 1.0);
        worst_coord = std::max(worst_coord, rel_err_v(t.transform_coord(v), glm::dvec3(mapped)));
        worst_coord_trip =
            std::max(worst_coord_trip, rel_err_v(t.inverse_transform_coord(t.transform_coord(v)), widen(v)));

        // Normals: direction from the double inverse-transpose, length preserved.
        const float v_len = math::length(v);
        if(v_len > 1e-3f)
        {
            const glm::dmat3 normal_matrix = glm::transpose(glm::inverse(glm::dmat3(dm)));
            const glm::dvec3 dir_ref = glm::normalize(normal_matrix * widen(v));
            const math::vec3 n = t.transform_normal(v);
            worst_normal_dir = std::max(worst_normal_dir, glm::length(glm::normalize(widen(n)) - dir_ref));
            worst_normal_len =
                std::max(worst_normal_len, std::fabs(static_cast<double>(math::length(n) - v_len)) / v_len);
            worst_normal_trip = std::max(worst_normal_trip, rel_err_v(t.inverse_transform_normal(n), widen(v)));
        }
    }
    std::printf("  coord %.3g  coord-trip %.3g  normal-dir %.3g  normal-len %.3g  normal-trip %.3g over %d cases\n",
                worst_coord,
                worst_coord_trip,
                worst_normal_dir,
                worst_normal_len,
                worst_normal_trip,
                k_cases);
    check(worst_coord < 1e-5, "transform_coord matches the double matrix application");
    check(worst_coord_trip < 1e-4, "inverse_transform_coord round-trips transform_coord");
    check(worst_normal_dir < 1e-4, "transform_normal direction matches the double inverse-transpose");
    check(worst_normal_len < 1e-4, "transform_normal preserves input length");
    check(worst_normal_trip < 1e-4, "inverse_transform_normal round-trips transform_normal");
}

void test_prop_inverse_and_axes_oracle()
{
    std::printf("property: inverse() and axis getters vs double oracle\n");

    deterministic_rng rng;
    double worst_inverse = 0.0;
    double worst_identity = 0.0;
    double worst_axes = 0.0;
    constexpr int k_cases = 1200;
    for(int i = 0; i < k_cases; ++i)
    {
        const bool skewed = (i % 4) == 3;
        const math::transform t =
            skewed ? random_skewed_transform(rng) : random_simple_transform(rng, static_cast<scale_shape>(i % 3));
        const glm::dmat4 dm = skewed ? widen(t.get_matrix()) : trs_oracle(t);

        worst_inverse = std::max(worst_inverse, rel_err(math::inverse(t).get_matrix(), glm::inverse(dm)));
        worst_identity = std::max(worst_identity, rel_err((math::inverse(t) * t).get_matrix(), glm::dmat4{1.0}));

        worst_axes = std::max(worst_axes, rel_err_v(t.x_axis(), glm::dvec3(dm[0])));
        worst_axes = std::max(worst_axes, rel_err_v(t.y_axis(), glm::dvec3(dm[1])));
        worst_axes = std::max(worst_axes, rel_err_v(t.z_axis(), glm::dvec3(dm[2])));
    }
    std::printf("  inverse %.3g  inverse*t vs I %.3g  axes %.3g over %d cases\n",
                worst_inverse,
                worst_identity,
                worst_axes,
                k_cases);
    check(worst_inverse < 1e-4, "inverse(transform) matches the double matrix inverse");
    check(worst_identity < 1e-3, "inverse(t) * t recovers identity");
    check(worst_axes < 1e-5, "axis getters match the double matrix columns");
}

void test_prop_decompose_recompose_roundtrip()
{
    std::printf("property: decompose -> recompose round trip (incl. shear and mirrors)\n");

    deterministic_rng rng;
    double worst = 0.0;
    constexpr int k_cases = 1200;
    for(int i = 0; i < k_cases; ++i)
    {
        math::transform source = (i % 2) == 0 ? random_skewed_transform(rng)
                                              : random_simple_transform(rng, static_cast<scale_shape>(i % 3));
        if(i % 5 == 0)
        {
            // Mirror: negate one column so the determinant flips.
            math::mat4 m = source.get_matrix();
            m[0] = -m[0];
            source = math::transform{m};
        }
        const math::mat4 original = source.get_matrix();
        math::transform t{original};
        (void)t.get_position();          // force decompose
        t.set_position(t.get_position()); // dirty the matrix without changing anything
        worst = std::max(worst, rel_err(t.get_matrix(), widen(original)));
    }
    std::printf("  max relative error over %d cases: %.3g\n", k_cases, worst);
    check(worst < 1e-4, "recompose(decompose(M)) reproduces M for TRS, sheared and mirrored matrices");
}

void test_prop_rotation_helpers()
{
    std::printf("property: from_to_rotation / look_rotation randomized sweeps\n");

    deterministic_rng rng;
    double worst_from_to = 0.0;
    double worst_antipodal = 0.0;
    double worst_look = 0.0;
    int up_violations = 0;
    constexpr int k_cases = 2000;
    for(int i = 0; i < k_cases; ++i)
    {
        const math::vec3 f = math::normalize(math::vec3{rng.range(-1.0f, 1.0f) + 1e-4f,
                                                        rng.range(-1.0f, 1.0f),
                                                        rng.range(-1.0f, 1.0f)});
        const math::vec3 to = math::normalize(math::vec3{rng.range(-1.0f, 1.0f),
                                                         rng.range(-1.0f, 1.0f) + 1e-4f,
                                                         rng.range(-1.0f, 1.0f)});
        const float d = math::dot(f, to);
        const math::quat q = math::from_to_rotation(f, to);
        const double err = static_cast<double>(math::length(q * f - to));
        if(d > -0.999f)
        {
            worst_from_to = std::max(worst_from_to, err);
        }
        // Antipodal band: any axis orthogonal to f is legal, but f must still map onto to.
        const math::quat qa = math::from_to_rotation(f, -f * (1.0f - 1e-7f));
        worst_antipodal = std::max(worst_antipodal, static_cast<double>(math::length(qa * f + f)));

        const math::vec3 up = math::normalize(math::vec3{rng.range(-1.0f, 1.0f),
                                                         rng.range(-1.0f, 1.0f) + 1e-4f,
                                                         rng.range(-1.0f, 1.0f)});
        if(math::abs(math::dot(f, up)) < 0.999f)
        {
            const math::quat lq = math::look_rotation(f, up);
            worst_look = std::max(worst_look, static_cast<double>(math::length(lq * math::vec3{0.0f, 0.0f, 1.0f} - f)));
            // The rotated +Y must land in the half plane of `up` (positive dot with
            // the component of up orthogonal to forward).
            const math::vec3 up_orth = up - f * math::dot(up, f);
            if(math::dot(lq * math::vec3{0.0f, 1.0f, 0.0f}, up_orth) <= 0.0f)
            {
                ++up_violations;
            }
        }
    }
    std::printf("  from_to %.3g  antipodal %.3g  look %.3g  up-violations %d over %d cases\n",
                worst_from_to,
                worst_antipodal,
                worst_look,
                up_violations,
                k_cases);
    check(worst_from_to < 1e-5, "from_to_rotation maps f onto t across the sphere");
    check(worst_antipodal < 1e-3, "from_to_rotation handles the antipodal band");
    check(worst_look < 1e-5, "look_rotation maps local +Z onto forward across the sphere");
    check(up_violations == 0, "look_rotation respects the up half-plane");
}

void test_prop_euler_hint_invariants()
{
    std::printf("property: euler readback invariants\n");

    deterministic_rng rng;
    int exact_violations = 0;
    double worst_representation = 0.0;
    constexpr int k_cases = 2000;
    for(int i = 0; i < k_cases; ++i)
    {
        // Typed euler triples read back exactly, including at gimbal lock.
        math::vec3 euler{rng.range(-360.0f, 360.0f), rng.range(-360.0f, 360.0f), rng.range(-360.0f, 360.0f)};
        if(i % 7 == 0)
        {
            euler.x = 90.0f; // gimbal lock
        }
        math::transform t;
        t.set_rotation_euler_degrees(euler);
        if(t.get_rotation_euler_degrees() != euler)
        {
            ++exact_violations;
        }

        // Extracted euler (dirty hint) must represent the actual orientation.
        math::transform u;
        u.set_rotation(random_unit_quat(rng));
        const math::quat from_euler = math::normalize(math::quat(u.get_rotation_euler()));
        worst_representation =
            std::max(worst_representation,
                     1.0 - std::fabs(glm::dot(glm::normalize(widen(from_euler)), glm::normalize(widen(u.get_rotation())))));
    }
    std::printf("  exact-readback violations %d  representation(1-|dot|) %.3g over %d cases\n",
                exact_violations,
                worst_representation,
                k_cases);
    check(exact_violations == 0, "authored euler triples read back bit-exactly");
    check(worst_representation < 1e-9, "extracted euler triples represent the stored orientation");
}

void run_property_tests()
{
    test_prop_recompose_oracle();
    test_prop_operator_multiply_oracle();
    test_prop_operator_multiply_laws();
    test_prop_coord_and_normal_oracle();
    test_prop_inverse_and_axes_oracle();
    test_prop_decompose_recompose_roundtrip();
    test_prop_rotation_helpers();
    test_prop_euler_hint_invariants();
}

// ---------------------------------------------------------------------------------
// Benchmarks (--bench / --bench-only).
//
// operator* is the hottest path in the engine and currently lowers to glm's SSE
// mat4 multiply, so optimization candidates are judged against these rows in
// RelWithDebInfo (primary) and Debug (sanity) - never by flop counting. The
// "matmul compose" rows pin the pre-optimization semantics explicitly
// (get_matrix() * get_matrix()) so the A/B survives changes to operator* itself.

using bench_clock = std::chrono::steady_clock;

volatile float g_bench_sink = 0.0f;

template<typename Fn>
void run_bench(const char* name, Fn&& fn)
{
    constexpr int k_reps = 5;
    constexpr int k_iters = 200000;
    double best_ms = 1e30;
    for(int rep = 0; rep < k_reps; ++rep)
    {
        float acc = 0.0f;
        const auto start = bench_clock::now();
        for(int i = 0; i < k_iters; ++i)
        {
            acc += fn(i);
        }
        const double ms = std::chrono::duration<double, std::milli>(bench_clock::now() - start).count();
        // The volatile store keeps every iteration's result live.
        g_bench_sink = g_bench_sink + acc;
        best_ms = std::min(best_ms, ms);
    }
    std::printf("  %-48s %9.2f ns/op\n", name, best_ms * 1e6 / double(k_iters));
}

struct bench_fixture
{
    std::vector<math::transform> parents;  // simplified TRS, uniform scale, matrix resolved
    std::vector<math::transform> locals;   // simplified TRS, matrix resolved
    std::vector<math::mat4> parent_mats;
    std::vector<math::mat4> local_mats;
    std::vector<math::mat4> composed_mats; // parent * local, for isolated decompose timing
    std::vector<math::vec3> points;
    math::transform skewed; // general-path transform (non-zero skew)
};

auto make_bench_fixture() -> bench_fixture
{
    bench_fixture f;
    constexpr int n = 64;
    for(int i = 0; i < n; ++i)
    {
        const float fi = static_cast<float>(i);
        math::transform p;
        p.set_position(fi * 0.5f, fi * -0.25f, 10.0f + fi);
        p.set_rotation_euler_degrees({fi * 3.0f, fi * 7.0f, fi * 1.5f});
        const float us = 1.0f + static_cast<float>(i % 4) * 0.25f;
        p.set_scale(us, us, us);
        (void)p.get_matrix(); // steady state: components and matrix both valid
        f.parents.push_back(p);

        math::transform l;
        l.set_position(-fi, 2.0f, fi * 0.1f);
        l.set_rotation_euler_degrees({0.0f, fi * 11.0f, 0.0f});
        l.set_scale(1.0f, 1.0f, 1.0f);
        (void)l.get_matrix();
        f.locals.push_back(l);

        f.parent_mats.push_back(p.get_matrix());
        f.local_mats.push_back(l.get_matrix());
        f.composed_mats.push_back(p.get_matrix() * l.get_matrix());
        f.points.emplace_back(fi * 0.1f, 1.0f - fi * 0.02f, fi * 0.05f);
    }
    math::mat4 shear{1.0f};
    shear[1][0] = 0.3f;
    f.skewed = math::transform{math::mat4_cast(math::quat(math::radians(math::vec3{15.0f, 25.0f, 35.0f}))) * shear};
    (void)f.skewed.get_position(); // resolve components
    return f;
}

void run_transform_benches()
{
    std::printf("transform benchmarks (best of 5 x 200k iterations)\n");
    bench_fixture f = make_bench_fixture();

    run_bench("raw mat4 * mat4 (simd reference)",
              [&](int i) -> float
              {
                  const int k = i & 63;
                  const math::mat4 m = f.parent_mats[k] * f.local_mats[k];
                  return m[3].x;
              });

    run_bench("matmul compose -> matrix (pinned old op*)",
              [&](int i) -> float
              {
                  const int k = i & 63;
                  const math::transform r{f.parent_mats[k] * f.local_mats[k]};
                  return r.get_matrix()[3].x;
              });

    run_bench("matmul compose -> position+rotation (pinned)",
              [&](int i) -> float
              {
                  const int k = i & 63;
                  const math::transform r{f.parent_mats[k] * f.local_mats[k]};
                  return r.get_position().x + r.get_rotation().w;
              });

    run_bench("op* -> matrix",
              [&](int i) -> float
              {
                  const int k = i & 63;
                  const math::transform r = f.parents[k] * f.locals[k];
                  return r.get_matrix()[3].x;
              });

    run_bench("op* -> position+rotation",
              [&](int i) -> float
              {
                  const int k = i & 63;
                  const math::transform r = f.parents[k] * f.locals[k];
                  return r.get_position().x + r.get_rotation().w;
              });

    run_bench("op* -> matrix+position",
              [&](int i) -> float
              {
                  const int k = i & 63;
                  const math::transform r = f.parents[k] * f.locals[k];
                  return r.get_matrix()[3].x + r.get_position().x;
              });

    run_bench("decompose (mat4 ctor + get_position)",
              [&](int i) -> float
              {
                  const int k = i & 63;
                  const math::transform r{f.composed_mats[k]};
                  return r.get_position().x;
              });

    {
        math::transform t = f.parents[0];
        run_bench("recompose (set_position + get_matrix)",
                  [&, t](int i) mutable -> float
                  {
                      const int k = i & 63;
                      t.set_position(f.points[k]);
                      return t.get_matrix()[3].x;
                  });
    }

    run_bench("get_position (cached)",
              [&](int i) -> float
              {
                  return f.parents[i & 63].get_position().x;
              });

    run_bench("x_unit_axis",
              [&](int i) -> float
              {
                  return f.parents[i & 63].x_unit_axis().x;
              });

    run_bench("transform_coord (component path)",
              [&](int i) -> float
              {
                  const int k = i & 63;
                  return f.parents[k].transform_coord(f.points[k]).x;
              });

    run_bench("inverse_transform_coord (skewed, matrix path)",
              [&](int i) -> float
              {
                  const int k = i & 63;
                  return f.skewed.inverse_transform_coord(f.points[k]).x;
              });

    run_bench("inverse(transform) -> position",
              [&](int i) -> float
              {
                  const int k = i & 63;
                  return math::inverse(f.parents[k]).get_position().x;
              });

    run_bench("transform_normal (skewed, matrix path)",
              [&](int i) -> float
              {
                  const int k = i & 63;
                  return f.skewed.transform_normal(f.points[k]).x;
              });

    run_bench("transform_normal (component path)",
              [&](int i) -> float
              {
                  const int k = i & 63;
                  return f.parents[k].transform_normal(f.points[k]).x;
              });

    run_bench("inverse_transform_coord (component path)",
              [&](int i) -> float
              {
                  const int k = i & 63;
                  return f.parents[k].inverse_transform_coord(f.points[k]).x;
              });

    {
        // Fallback compose: non-uniform LEFT scale forces the matrix path.
        std::vector<math::transform> non_uniform_lefts = f.parents;
        for(size_t i = 0; i < non_uniform_lefts.size(); ++i)
        {
            const float fi = static_cast<float>(i);
            non_uniform_lefts[i].set_scale(1.0f, 2.0f + fi * 0.01f, 3.0f);
            (void)non_uniform_lefts[i].get_matrix();
        }
        run_bench("op* fallback (non-uniform left) -> matrix",
                  [&](int i) -> float
                  {
                      const int k = i & 63;
                      const math::transform r = non_uniform_lefts[k] * f.locals[k];
                      return r.get_matrix()[3].x;
                  });
    }

    {
        // A freshly-edited local (dirty matrix): the fast path defers the result
        // matrix, so this row pays compose + one direct recompose.
        math::transform local = f.locals[0];
        run_bench("op* (dirty local) -> matrix",
                  [&, local](int i) mutable -> float
                  {
                      const int k = i & 63;
                      local.set_position(f.points[k]);
                      const math::transform r = f.parents[k] * local;
                      return r.get_matrix()[3].x;
                  });
    }

    run_bench("op* chain x8 -> matrix+position",
              [&](int i) -> float
              {
                  const int k = i & 63;
                  math::transform acc = f.parents[k];
                  for(int j = 0; j < 8; ++j)
                  {
                      acc = acc * f.locals[(k + j) & 63];
                  }
                  return acc.get_matrix()[3].x + acc.get_position().x;
              });

    {
        math::transform t = f.parents[0];
        const math::quat q = t.get_rotation();
        run_bench("set_rotation (same rotation, hint kept)",
                  [&, t, q](int i) mutable -> float
                  {
                      t.set_rotation(q);
                      return t.get_rotation().w + static_cast<float>(i & 1);
                  });
    }

    run_bench("get_rotation_euler_degrees (cached hint)",
              [&](int i) -> float
              {
                  return f.parents[i & 63].get_rotation_euler_degrees().y;
              });

    run_bench("compare (equal transforms)",
              [&](int i) -> float
              {
                  const int k = i & 63;
                  return static_cast<float>(f.parents[k].compare(f.parents[k], 1e-4f));
              });
}

} // namespace

// ---------------------------------------------------------------------------------

auto run_transform_math_suite(rtti::context& /*ctx*/) -> int
{
    const bool bench_only = tests::wants("bench-only");
    const bool bench = bench_only || tests::wants("bench");

    if(!bench_only)
    {
        test_scale_fix_preserves_sign();
        test_euler_hint_follows_incremental_rotation();
        test_euler_hint_restore_after_decompose();
        test_normals_under_negative_uniform_scale();
        test_normal_branch_consistency();
        test_rotate_axis_axis_handling();
        test_perspective_divide_guard();
        test_compare_honors_tolerance();
        test_from_to_rotation();
        test_from_to_rotation_matches_acos_reference();
        test_look_rotation();
        test_look_rotation_matches_lookat_reference();
        test_recompose_matches_reference();
        test_operator_multiply_matches_matrix_reference();
        test_inverse_and_axis_fast_paths();
        run_property_tests();
    }

    if(bench)
    {
        run_transform_benches();
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

REGISTER_TEST_SUITE("transform math", run_transform_math_suite)
