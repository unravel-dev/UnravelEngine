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
#include <cmath>
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

} // namespace

// ---------------------------------------------------------------------------------

auto run_transform_math_suite(rtti::context& /*ctx*/) -> int
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

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

REGISTER_TEST_SUITE("transform math", run_transform_math_suite)
