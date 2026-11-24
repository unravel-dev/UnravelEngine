/*
 * math::transform_t against math::transform_legacy_t, the class the engine used before it.
 *
 *   <build-dir>/bin/unravel-tests --suite "transform parity"            checks
 *   <build-dir>/bin/unravel-tests --suite "transform parity" --bench    checks and benchmarks
 *
 * Types compared:
 *   legacy  math::transform_legacy (transform_legacy_t<float>)
 *   tf      math::transform (transform_t<float>), float positions
 *   td      transform_t<float, double>, double positions, float rotation / scale
 *   tdd     transform_t<double, double>, everything double
 *
 * Checks: API parity (compile time), differential runs of random operation sequences on legacy
 * and tf / td, double oracles for every type, edge cases, and precision far from the origin.
 */

#include "../tests.h"

#include <math/math.h>
#include <math/transform_legacy.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

using namespace unravel::test;
using namespace unravel;

// Every member of every type compiles (members a test never calls included); transform.cpp
// instantiates transform_t<float>.
template class math::transform_legacy_t<float>;
template class math::transform_t<float, double>;
template class math::transform_t<double, double>;

namespace
{
using legacy_t = math::transform_legacy;
using tf = math::transform;
using td = math::transform_t<float, double>;
using tdd = math::transform_t<double, double>;

//---------------------------------------------------------------------------------------------
// Generic access
//---------------------------------------------------------------------------------------------
template<typename X>
using pos_of = std::decay_t<decltype(std::declval<const X&>().get_position())>;

template<typename X>
constexpr bool has_matrix = requires(const X& t) { t.get_matrix(); };

template<typename X>
constexpr bool has_matrix_relative = requires(const X& t) { t.get_matrix_relative(pos_of<X>{}); };

/// Whole matrix in double: the cached matrix when the type has one, else linear + position.
template<typename X>
auto dmatrix(const X& t) -> math::dmat4
{
    if constexpr(has_matrix<X>)
    {
        return math::dmat4(t.get_matrix());
    }
    else
    {
        math::dmat4 m(math::dmat3(t.get_linear()));
        m[3] = math::dvec4(math::dvec3(t.get_position()), 1.0);
        return m;
    }
}

template<typename X>
auto make(const math::dvec3& p, const math::quat& q, const math::vec3& s, const math::vec3& skew = math::vec3(0.0f))
    -> X
{
    using v3 = typename X::vec3_t;
    using qt = typename X::quat_t;
    X t;
    t.set_position(pos_of<X>(p));
    t.set_rotation(qt(q));
    t.set_scale(v3(s));
    if(skew != math::vec3(0.0f))
    {
        t.set_skew(v3(skew));
    }
    return t;
}

template<typename X>
auto from_matrix(const math::mat4& m) -> X
{
    return X(typename X::mat4_t(m));
}

/// The same transform, rebuilt from its matrix (matrix defined, components not yet decomposed).
template<typename X>
auto from_any(const X& t) -> X
{
    if constexpr(has_matrix<X>)
    {
        return X(t.get_matrix());
    }
    else
    {
        return X(dmatrix(t));
    }
}

//---------------------------------------------------------------------------------------------
// Double oracle: T * R * shear * scale, the composition glm_recompose uses.
//---------------------------------------------------------------------------------------------
auto oracle_matrix(const math::dvec3& p, const math::dquat& q, const math::dvec3& s, const math::dvec3& skew)
    -> math::dmat4
{
    math::dmat4 shear(1.0);
    shear[1][0] = skew.z;
    shear[2][0] = skew.y;
    shear[2][1] = skew.x;
    return math::translate(math::dmat4(1.0), p) * math::dmat4(math::mat3_cast(q)) * shear *
           math::scale(math::dmat4(1.0), s);
}

auto max_rel_diff(const math::dmat4& a, const math::dmat4& b) -> double
{
    double worst = 0.0;
    for(int c = 0; c < 4; ++c)
    {
        for(int r = 0; r < 4; ++r)
        {
            const double d = std::abs(a[c][r] - b[c][r]) / std::max(1.0, std::abs(b[c][r]));
            worst = std::max(worst, d);
        }
    }
    return worst;
}

auto rel_diff(const math::dvec3& a, const math::dvec3& b) -> double
{
    return math::length(a - b) / std::max(1.0, math::length(b));
}

auto rotation_angle(const math::dquat& a, const math::dquat& b) -> double
{
    const double d = std::min(1.0, std::abs(math::dot(math::normalize(a), math::normalize(b))));
    return 2.0 * std::acos(d);
}

//---------------------------------------------------------------------------------------------
// Random inputs
//---------------------------------------------------------------------------------------------
struct rng
{
    std::mt19937 engine;
    explicit rng(std::uint32_t seed)
        : engine(seed)
    {
    }
    auto uniform(float lo, float hi) -> float
    {
        return std::uniform_real_distribution<float>(lo, hi)(engine);
    }
    auto index(int n) -> int
    {
        return std::uniform_int_distribution<int>(0, n - 1)(engine);
    }
    auto chance(float p) -> bool
    {
        return uniform(0.0f, 1.0f) < p;
    }
    auto vec(float range) -> math::vec3
    {
        return {uniform(-range, range), uniform(-range, range), uniform(-range, range)};
    }
    auto quat() -> math::quat
    {
        std::normal_distribution<float> n(0.0f, 1.0f);
        math::quat q(n(engine), n(engine), n(engine), n(engine));
        if(math::dot(q, q) < 1e-6f)
        {
            return math::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }
        return math::normalize(q);
    }
    auto euler_degrees() -> math::vec3
    {
        return {uniform(-180.0f, 180.0f), uniform(-89.0f, 89.0f), uniform(-180.0f, 180.0f)};
    }
    /// Uniform, non-uniform or mirrored, between 0.5 and 2 in magnitude.
    auto scale() -> math::vec3
    {
        const int kind = index(4);
        if(kind == 0)
        {
            const float s = uniform(0.5f, 2.0f);
            return {s, s, s};
        }
        if(kind == 1)
        {
            return {uniform(0.5f, 2.0f), uniform(0.5f, 2.0f), uniform(0.5f, 2.0f)};
        }
        if(kind == 2)
        {
            const float s = -uniform(0.5f, 2.0f);
            return {s, s, s};
        }
        return {1.0f, 1.0f, 1.0f};
    }
    auto skew() -> math::vec3
    {
        return {uniform(-0.4f, 0.4f), uniform(-0.4f, 0.4f), uniform(-0.4f, 0.4f)};
    }
    auto trs_matrix() -> math::mat4
    {
        const math::vec3 s = scale();
        return math::translate(math::mat4(1.0f), vec(50.0f)) * math::mat4_cast(quat()) *
               math::scale(math::mat4(1.0f), s);
    }
    auto sheared_matrix() -> math::mat4
    {
        math::mat4 shear(1.0f);
        shear[1][0] = uniform(-0.4f, 0.4f);
        shear[2][1] = uniform(-0.4f, 0.4f);
        return math::translate(math::mat4(1.0f), vec(50.0f)) * math::mat4_cast(quat()) * shear *
               math::scale(math::mat4(1.0f), scale());
    }
};

//---------------------------------------------------------------------------------------------
// API parity: every call the engine makes on math::transform compiles on tf.
//---------------------------------------------------------------------------------------------
template<typename X>
concept transform_api = requires(X t,
                                 const X& c,
                                 typename X::vec3_t v,
                                 typename X::vec2_t v2,
                                 typename X::vec4_t v4,
                                 typename X::quat_t q,
                                 typename X::mat4_t m,
                                 float f) {
    X(m);
    { c.get_position() };
    { c.get_translation() };
    t.set_position(v);
    t.set_position(f, f, f);
    t.set_translation(v);
    t.reset_position();
    t.reset_translation();
    { c.get_rotation() };
    t.set_rotation(q);
    t.set_rotation(v, v, v);
    t.reset_rotation();
    { c.get_rotation_euler() };
    { c.get_rotation_euler_degrees() };
    { c.get_rotation_euler_degrees(v) };
    t.set_rotation_euler(v);
    t.set_rotation_euler_degrees(v);
    t.set_rotation_euler_degrees(f, f, f);
    { c.has_rotation_euler_hint() } -> std::same_as<bool>;
    c.restore_euler_angles_hint_degrees(v);
    { c.try_restore_euler_angles_hint_degrees(v) } -> std::same_as<bool>;
    { c.get_scale() };
    t.set_scale(v);
    t.set_scale(f, f, f);
    t.reset_scale();
    { c.get_skew() };
    t.set_skew(v);
    t.reset_skew();
    { c.get_perspective() };
    t.set_perspective(v4);
    t.reset_perspective();
    { c.x_axis() };
    { c.y_axis() };
    { c.z_axis() };
    { c.x_unit_axis() };
    { c.y_unit_axis() };
    { c.z_unit_axis() };
    t.rotate_axis(f, v);
    t.rotate(q);
    t.rotate(v);
    t.rotate_local(v);
    t.scale(v);
    t.translate(v);
    t.translate_local(v);
    { c.compare(c) } -> std::same_as<int>;
    { c.compare(c, f) } -> std::same_as<int>;
    { c.transform_coord(v) };
    { c.inverse_transform_coord(v) };
    { c.transform_normal(v) };
    { c.inverse_transform_normal(v) };
    { c.transform_coord(v2) };
    { c.transform_normal(v2) };
    { X::identity() };
    { X::scaling(v) };
    { X::rotation(q) };
    { X::rotation_euler(v) };
    { X::translation(v) };
    { c.get_matrix() };
    { static_cast<const typename X::mat4_t&>(c) };
    { static_cast<const float*>(c) };
    { c[0] };
    { c * v4 };
    { c * c };
    { c == c } -> std::same_as<bool>;
    { c != c } -> std::same_as<bool>;
    { inverse(c) };
    { c.is_skew_zero() } -> std::same_as<bool>;
    { c.is_perspective_identity() } -> std::same_as<bool>;
    { c.is_scale_uniform() } -> std::same_as<bool>;
};

static_assert(transform_api<legacy_t>, "the reference list matches the legacy class");
static_assert(transform_api<tf>, "transform_t answers every call made on the legacy class");
static_assert(!has_matrix<td>, "double positions have no absolute float matrix");
static_assert(has_matrix_relative<td> && has_matrix_relative<tf>, "every transform type has relative matrices");
static_assert(!std::is_convertible_v<math::mat4, td>, "a float matrix becomes a double-position transform only explicitly");

void test_sizes()
{
    std::printf("  sizeof: legacy %zu, tf %zu, td %zu, tdd %zu (alignof %zu, %zu, %zu, %zu)\n",
                sizeof(legacy_t),
                sizeof(tf),
                sizeof(td),
                sizeof(tdd),
                alignof(legacy_t),
                alignof(tf),
                alignof(td),
                alignof(tdd));
    check(sizeof(tf) <= sizeof(legacy_t), "transform_t is no larger than the legacy class");
}

//---------------------------------------------------------------------------------------------
// Differential runs: legacy and tf / td fed the same operations
//---------------------------------------------------------------------------------------------
struct diff_stats
{
    double matrix = 0.0;
    double position = 0.0;
    double rotation = 0.0;
    double scale = 0.0;
    double skew = 0.0;
    double coord = 0.0;
    double inverse_coord = 0.0;
    double normal = 0.0;
    double inverse_normal = 0.0;
    double axes = 0.0;
    double euler = 0.0;
    int hint_mismatch = 0;
    int shape_mismatch = 0;
    int compare_mismatch = 0;
    int compared = 0;
};

auto to_d(const math::vec3& v) -> math::dvec3
{
    return math::dvec3(v);
}

auto to_d(const math::quat& q) -> math::dquat
{
    return math::dquat(q);
}

template<typename B>
void compare_state(const legacy_t& a, const B& b, const math::vec3& probe_f, diff_stats& st)
{
    using bp = pos_of<B>;
    const bp probe(probe_f);
    ++st.compared;
    st.matrix = std::max(st.matrix, max_rel_diff(dmatrix(b), math::dmat4(a.get_matrix())));
    st.position = std::max(st.position, rel_diff(math::dvec3(b.get_position()), to_d(a.get_position())));
    if constexpr(has_matrix<B>)
    {
        // The matrix and the position agree exactly.
        if(math::vec3(b.get_matrix()[3]) != b.get_position())
        {
            st.position = std::max(st.position, 1.0);
        }
    }
    st.rotation = std::max(st.rotation, rotation_angle(to_d(b.get_rotation()), to_d(a.get_rotation())));
    st.scale = std::max(st.scale, rel_diff(to_d(b.get_scale()), to_d(a.get_scale())));
    st.skew = std::max(st.skew, math::length(to_d(b.get_skew()) - to_d(a.get_skew())));
    st.coord =
        std::max(st.coord, rel_diff(math::dvec3(b.transform_coord(probe)), to_d(a.transform_coord(probe_f))));
    st.inverse_coord = std::max(st.inverse_coord,
                                rel_diff(math::dvec3(b.inverse_transform_coord(probe)),
                                         to_d(a.inverse_transform_coord(probe_f))));
    st.normal = std::max(st.normal, rel_diff(to_d(b.transform_normal(probe_f)), to_d(a.transform_normal(probe_f))));
    st.inverse_normal = std::max(st.inverse_normal,
                                 rel_diff(to_d(b.inverse_transform_normal(probe_f)),
                                          to_d(a.inverse_transform_normal(probe_f))));
    st.axes = std::max(st.axes, rel_diff(to_d(b.x_axis()), to_d(a.x_axis())));
    st.axes = std::max(st.axes, rel_diff(to_d(b.y_axis()), to_d(a.y_axis())));
    st.axes = std::max(st.axes, rel_diff(to_d(b.z_axis()), to_d(a.z_axis())));

    if(a.has_rotation_euler_hint() != b.has_rotation_euler_hint())
    {
        ++st.hint_mismatch;
    }
    else if(a.has_rotation_euler_hint())
    {
        // Both hold Euler angles: authored ones are the same numbers; ones computed from the
        // quaternion near gimbal lock can differ as triples, so compare the orientations they name.
        const math::dvec3 ea = math::radians(math::dvec3(a.get_rotation_euler_degrees()));
        const math::dvec3 eb = math::radians(math::dvec3(b.get_rotation_euler_degrees()));
        st.euler = std::max(st.euler, rotation_angle(math::dquat(ea), math::dquat(eb)));
    }
    if(a.is_skew_zero() != b.is_skew_zero() || a.is_scale_uniform() != b.is_scale_uniform())
    {
        ++st.shape_mismatch;
    }
    if(a.compare(a, 1e-4f) != b.compare(b, 1e-4f))
    {
        ++st.compare_mismatch;
    }
}

/// One random operation with its inputs drawn up front, so both classes get identical values.
struct op
{
    int kind = 0;
    math::vec3 v{};
    math::quat q{};
    math::mat4 m{1.0f};
    float f = 0.0f;
    int other = 0;
};

constexpr int k_op_kinds = 23;

auto draw_op(rng& r, int pool_size) -> op
{
    op o;
    o.kind = r.index(k_op_kinds);
    o.q = r.quat();
    o.f = r.uniform(-3.0f, 3.0f);
    o.other = r.index(pool_size);
    switch(o.kind)
    {
        case 0:
        case 9:
        case 10:
            o.v = r.vec(50.0f);
            break;
        case 2:
        case 16:
            o.v = r.euler_degrees();
            break;
        case 3:
            o.v = r.scale();
            break;
        case 4:
            o.v = r.skew();
            break;
        case 7:
            o.v = r.vec(1.0f);
            break;
        case 8:
            o.v = r.vec(1.0f);
            break;
        case 11:
            o.v = math::abs(r.scale());
            break;
        case 12:
            o.m = r.chance(0.7f) ? r.trs_matrix() : r.sheared_matrix();
            break;
        default:
            break;
    }
    return o;
}

template<typename X>
void apply_op(X& t, const op& o, const std::vector<X>& pool)
{
    switch(o.kind)
    {
        case 0:
            t.set_position(o.v);
            break;
        case 1:
            t.set_rotation(o.q);
            break;
        case 2:
            t.set_rotation_euler_degrees(o.v);
            break;
        case 3:
            t.set_scale(o.v);
            break;
        case 4:
            t.set_skew(o.v);
            break;
        case 5:
            t.reset_skew();
            break;
        case 6:
            t.rotate(o.q);
            break;
        case 7:
            t.rotate_local(o.v);
            break;
        case 8:
            t.rotate_axis(o.f, o.v);
            break;
        case 9:
            t.translate(o.v * 0.1f);
            break;
        case 10:
            t.translate_local(o.v * 0.1f);
            break;
        case 11:
            t.scale(o.v);
            break;
        case 12:
            t = X(typename X::mat4_t(o.m));
            break;
        case 13:
            t = pool[o.other] * t;
            break;
        case 14:
            t = t * pool[o.other];
            break;
        case 15:
            t = inverse(t);
            break;
        case 16:
            t.restore_euler_angles_hint_degrees(o.v);
            break;
        case 17:
            (void)t.try_restore_euler_angles_hint_degrees(t.get_rotation_euler_degrees(o.v));
            break;
        case 18:
            (void)t.get_rotation_euler_degrees();
            break;
        case 19:
            t.reset_rotation();
            break;
        case 21:
            // The same orientation set again: the hint check runs and keeps the hint.
            t.set_rotation(t.get_rotation());
            break;
        case 22:
            // Rebuilt from its own matrix.
            t = from_any(t);
            break;
        default:
            t.set_scale(math::vec3(1.0f));
            t.set_position(o.v);
            break;
    }
}

/// Keeps a sequence's magnitudes in range: compose chains multiply scales.
template<typename X>
auto in_range(const X& t) -> bool
{
    const auto s = t.get_scale();
    const auto p = t.get_position();
    const float smax = std::max(std::abs(s.x), std::max(std::abs(s.y), std::abs(s.z)));
    const float smin = std::min(std::abs(s.x), std::min(std::abs(s.y), std::abs(s.z)));
    return smax < 64.0f && smin > 1.0f / 64.0f && math::length(p) < 5000.0f;
}

template<typename B>
void run_differential_sequences(const char* name)
{
    rng r(0x5eed1u);
    constexpr int pool_size = 32;
    std::vector<legacy_t> pool_a;
    std::vector<B> pool_b;
    std::vector<tdd> pool_exact;
    for(int i = 0; i < pool_size; ++i)
    {
        const math::vec3 skew = (i % 5 == 0) ? r.skew() : math::vec3(0.0f);
        const math::dvec3 p(r.vec(20.0f));
        const math::quat q = r.quat();
        const math::vec3 s = r.scale();
        pool_a.push_back(make<legacy_t>(p, q, s, skew));
        pool_b.push_back(make<B>(p, q, s, skew));
        pool_exact.push_back(make<tdd>(p, q, s, skew));
        if(i % 3 == 0)
        {
            (void)pool_a.back().get_matrix();
            (void)dmatrix(pool_b.back());
        }
    }

    // The same sequence in double is the reference for both float classes: two float classes
    // drift apart by the rounding a sequence amplifies, so each is also judged by its own distance
    // from the double result.
    diff_stats st;
    double worst_from_exact_a = 0.0;
    double worst_from_exact_b = 0.0;
    double sum_from_exact_a = 0.0;
    double sum_from_exact_b = 0.0;
    int ops = 0;
    int ops_by_kind[k_op_kinds] = {};
    constexpr int k_sequences = 6000;
    constexpr int k_steps = 24;
    for(int seq = 0; seq < k_sequences; ++seq)
    {
        legacy_t a;
        B b;
        tdd exact;
        for(int step = 0; step < k_steps; ++step)
        {
            const op o = draw_op(r, pool_size);
            apply_op(a, o, pool_a);
            apply_op(b, o, pool_b);
            apply_op(exact, o, pool_exact);
            ++ops;
            ++ops_by_kind[o.kind];
            if(!in_range(a))
            {
                break;
            }
            compare_state(a, b, r.vec(10.0f), st);
            const math::dmat4 reference = dmatrix(exact);
            const double from_exact_a = max_rel_diff(dmatrix(a), reference);
            const double from_exact_b = max_rel_diff(dmatrix(b), reference);
            worst_from_exact_a = std::max(worst_from_exact_a, from_exact_a);
            worst_from_exact_b = std::max(worst_from_exact_b, from_exact_b);
            sum_from_exact_a += from_exact_a;
            sum_from_exact_b += from_exact_b;
        }
    }
    const double mean_from_exact_a = sum_from_exact_a / std::max(1, st.compared);
    const double mean_from_exact_b = sum_from_exact_b / std::max(1, st.compared);

    std::printf("  differential legacy / %s: %d operations, %d states compared\n", name, ops, st.compared);
    std::printf("    worst relative: matrix %.2e, position %.2e, scale %.2e, coord %.2e, inverse coord %.2e\n",
                st.matrix,
                st.position,
                st.scale,
                st.coord,
                st.inverse_coord);
    std::printf("    worst: rotation %.2e rad, skew %.2e, normal %.2e, inverse normal %.2e, axes %.2e, euler %.2e rad\n",
                st.rotation,
                st.skew,
                st.normal,
                st.inverse_normal,
                st.axes,
                st.euler);
    std::printf("    mismatches: euler hint %d, shape flags %d, compare %d\n",
                st.hint_mismatch,
                st.shape_mismatch,
                st.compare_mismatch);
    std::printf("    matrix distance from the double sequence: legacy worst %.2e, mean %.2e; %s worst %.2e, mean %.2e\n",
                worst_from_exact_a,
                mean_from_exact_a,
                name,
                worst_from_exact_b,
                mean_from_exact_b);

    const std::string n = std::string(name) + ": ";
    // The legacy class is itself up to 1.1e-4 from the double sequence, so the agreement of the two
    // float classes is bounded by that; the distances from the double sequence are the measure.
    check(st.matrix < 3e-4, n + "matrices stay close to the legacy class");
    check(worst_from_exact_b <= worst_from_exact_a * 1.25 + 1e-6,
          n + "the worst matrix is no farther from the double sequence than in the legacy class");
    check(mean_from_exact_b <= mean_from_exact_a * 1.05,
          n + "matrices are on average no farther from the double sequence than in the legacy class");
    check(st.position < 1e-4, n + "positions agree with the legacy class, and the matrix holds its position");
    check(st.rotation < 1e-3, n + "rotations agree with the legacy class");
    check(st.scale < 1e-4, n + "scales agree with the legacy class");
    check(st.skew < 1e-3, n + "skews agree with the legacy class");
    check(st.coord < 1e-4 && st.inverse_coord < 1e-3, n + "coordinate transforms agree with the legacy class");
    check(st.normal < 1e-3 && st.inverse_normal < 1e-3, n + "normal transforms agree with the legacy class");
    check(st.axes < 1e-4, n + "axes agree with the legacy class");
    check(st.euler < 1e-4, n + "Euler angles name the same orientation as in the legacy class");
    check(st.hint_mismatch == 0, n + "the Euler hint is kept and dropped on the same steps");
    check(st.shape_mismatch == 0, n + "skew and uniform-scale classification agree");
    check(st.compare_mismatch == 0, n + "compare agrees");
    for(int k = 0; k < k_op_kinds; ++k)
    {
        check(ops_by_kind[k] > 1000, n + "operation kind " + std::to_string(k) + " was exercised");
    }
}

void test_differential_sequences()
{
    run_differential_sequences<tf>("tf");
    run_differential_sequences<td>("td");
}

void test_differential_matrices()
{
    // Matrices into both classes, every component read back.
    rng r(0x5eed2u);
    diff_stats st;
    for(int i = 0; i < 20000; ++i)
    {
        math::mat4 m;
        switch(i % 4)
        {
            case 0:
                m = r.trs_matrix();
                break;
            case 1:
                m = r.sheared_matrix();
                break;
            case 2:
                m = math::translate(math::mat4(1.0f), r.vec(100.0f)) * math::mat4_cast(r.quat());
                break;
            default:
                // Rotations by 180 degrees reach every branch of the quaternion extraction.
                m = math::mat4_cast(math::angleAxis(math::pi<float>(), math::normalize(r.vec(1.0f) + 0.01f)));
                break;
        }
        const legacy_t a(m);
        const tf b(m);
        compare_state(a, b, r.vec(10.0f), st);
    }
    std::printf("  from matrices: worst matrix %.2e, position %.2e, rotation %.2e rad, scale %.2e, skew %.2e\n",
                st.matrix,
                st.position,
                st.rotation,
                st.scale,
                st.skew);
    check(st.matrix == 0.0, "a matrix comes back unchanged");
    check(st.position == 0.0, "the position is the translation column, exactly");
    check(st.rotation < 1e-5 && st.scale < 1e-6 && st.skew < 1e-6,
          "the decompose matches the general decompose");
    check(st.shape_mismatch == 0, "shape flags from a matrix agree");
}

//---------------------------------------------------------------------------------------------
// Double oracles
//---------------------------------------------------------------------------------------------
auto max_abs(const math::dmat4& m) -> double
{
    double v = 0.0;
    for(int c = 0; c < 4; ++c)
    {
        for(int r = 0; r < 4; ++r)
        {
            v = std::max(v, std::abs(m[c][r]));
        }
    }
    return v;
}

/// Largest element difference over the magnitude of the computation that produced it.
auto scaled_diff(const math::dmat4& a, const math::dmat4& b, double magnitude) -> double
{
    double worst = 0.0;
    for(int c = 0; c < 4; ++c)
    {
        for(int r = 0; r < 4; ++r)
        {
            worst = std::max(worst, std::abs(a[c][r] - b[c][r]));
        }
    }
    return worst / std::max(1.0, magnitude);
}

/// The oracle of a transform: its stored components, composed in double.
template<typename X>
auto oracle_of(const X& t) -> math::dmat4
{
    const auto q = t.get_rotation();
    return oracle_matrix(math::dvec3(t.get_position()),
                         math::dquat(double(q.w), double(q.x), double(q.y), double(q.z)),
                         math::dvec3(t.get_scale()),
                         math::dvec3(t.get_skew()));
}

template<typename X>
void run_oracle(const char* name, double tolerance)
{
    rng r(0x0c1eu);
    double worst_build = 0.0;
    double worst_compose = 0.0;
    double worst_compose_general = 0.0;
    double worst_inverse = 0.0;
    double worst_coord = 0.0;
    double worst_roundtrip = 0.0;
    double worst_rebuild = 0.0;
    double worst_relative = 0.0;
    for(int i = 0; i < 20000; ++i)
    {
        const bool skewed = i % 4 == 3;
        const math::dvec3 pa(r.vec(100.0f));
        const math::vec3 ka = skewed ? r.skew() : math::vec3(0.0f);
        const X a = make<X>(pa, r.quat(), r.scale(), ka);
        const X b = make<X>(math::dvec3(r.vec(100.0f)), r.quat(), r.scale());
        if(i % 2 == 0)
        {
            (void)dmatrix(a);
            (void)dmatrix(b);
        }
        const math::dmat4 oa = oracle_of(a);
        const math::dmat4 ob = oracle_of(b);
        const double ma = max_abs(oa);
        const double mb = max_abs(ob);

        worst_build = std::max(worst_build, scaled_diff(dmatrix(a), oa, ma));

        const X ab = a * b;
        const double compose_err = scaled_diff(dmatrix(ab), oa * ob, 3.0 * ma * mb);
        const bool general = !a.is_skew_zero() || !a.is_scale_uniform();
        (general ? worst_compose_general : worst_compose) =
            std::max(general ? worst_compose_general : worst_compose, compose_err);

        const math::dmat4 oinv = glm::inverse(oa);
        worst_inverse = std::max(worst_inverse, scaled_diff(dmatrix(inverse(a)), oinv, max_abs(oinv)));

        const math::vec3 local = r.vec(10.0f);
        const math::dvec3 expected(oa * math::dvec4(math::dvec3(local), 1.0));
        const math::dvec3 world(a.transform_coord(pos_of<X>(local)));
        worst_coord = std::max(worst_coord, math::length(world - expected) / std::max(1.0, 3.0 * ma * 10.0));
        const auto back = a.inverse_transform_coord(a.transform_coord(pos_of<X>(local)));
        worst_roundtrip =
            std::max(worst_roundtrip, math::length(math::dvec3(back) - math::dvec3(local)) / std::max(1.0, ma * 30.0));

        // The matrix decomposes back into components that rebuild it.
        X from = [&]
        {
            if constexpr(has_matrix<X>)
            {
                return X(a.get_matrix());
            }
            else
            {
                return X(dmatrix(a));
            }
        }();
        const math::dmat4 rebuilt = oracle_of(from);
        worst_rebuild = std::max(worst_rebuild, scaled_diff(rebuilt, dmatrix(a), ma));

        if constexpr(has_matrix_relative<X>)
        {
            const math::dvec3 origin(pa + math::dvec3(r.vec(5.0f)));
            const math::dmat4 rel(a.get_matrix_relative(pos_of<X>(origin)));
            math::dmat4 expected_rel = oa;
            expected_rel[3] = math::dvec4(math::dvec3(oa[3]) - origin, 1.0);
            worst_relative = std::max(worst_relative, scaled_diff(rel, expected_rel, ma));
        }
    }
    std::printf("  oracle %-6s build %.1e, compose %.1e / general %.1e, inverse %.1e, coord %.1e, round trip %.1e, "
                "decompose %.1e, relative %.1e\n",
                name,
                worst_build,
                worst_compose,
                worst_compose_general,
                worst_inverse,
                worst_coord,
                worst_roundtrip,
                worst_rebuild,
                worst_relative);
    const std::string n(name);
    check(worst_build < tolerance, n + ": matrices match the double oracle");
    check(worst_compose < tolerance, n + ": component compose matches the double oracle");
    check(worst_compose_general < tolerance, n + ": general compose matches the double oracle");
    check(worst_inverse < tolerance * 10.0, n + ": inverse matches the double oracle");
    check(worst_coord < tolerance, n + ": transform_coord matches the double oracle");
    check(worst_roundtrip < tolerance * 10.0, n + ": inverse_transform_coord undoes transform_coord");
    check(worst_rebuild < tolerance * 10.0, n + ": a decompose rebuilds the matrix");
    check(worst_relative < tolerance, n + ": relative matrices match the double oracle");
}

void test_oracles()
{
    run_oracle<legacy_t>("legacy", 2e-6);
    run_oracle<tf>("tf", 2e-6);
    run_oracle<td>("td", 2e-6);
    run_oracle<tdd>("tdd", 1e-14);
}

//---------------------------------------------------------------------------------------------
// Edge cases
//---------------------------------------------------------------------------------------------
void test_edge_cases()
{
    {
        // set_position on a transform built from a matrix needs no decompose and keeps the rest.
        rng r(7);
        const math::mat4 m = r.sheared_matrix();
        tf t(m);
        t.set_position(math::vec3(1.0f, 2.0f, 3.0f));
        math::mat4 expected = m;
        expected[3] = math::vec4(1.0f, 2.0f, 3.0f, 1.0f);
        check(t.get_matrix() == expected, "set_position on a matrix-built transform changes only the translation");
        const legacy_t c(expected);
        check(rotation_angle(to_d(t.get_rotation()), to_d(c.get_rotation())) < 1e-6,
              "its rotation still decomposes from the original basis");
    }
    {
        // Zero and tiny scales.
        tf t;
        t.set_scale(0.0f, 0.0f, 0.0f);
        check(t.get_matrix()[0] == math::vec4(0.0f), "a zero uniform scale gives a zero basis");
        tf u;
        u.set_scale(-1e-7f, 1.0f, 1.0f);
        check(math::determinant(math::mat3(u.get_matrix())) < 0.0f, "a tiny negative scale keeps its handedness");
        check(u.inverse_transform_coord(u.transform_coord(math::vec3(0.0f, 1.0f, 0.0f))).y > 0.99f,
              "inverse_transform_coord stays finite through a tiny scale");
    }
    {
        // Mirrored matrices decompose into a negative scale and a proper rotation.
        const math::mat4 m = math::scale(math::mat4(1.0f), math::vec3(-2.0f, 2.0f, 2.0f));
        const tf t(m);
        check(t.get_scale().x < 0.0f || t.get_scale().y < 0.0f || t.get_scale().z < 0.0f,
              "a mirrored matrix decomposes into a negative scale");
        check(math::abs(math::length(t.get_rotation()) - 1.0f) < 1e-5f, "with a unit rotation");
        check(max_rel_diff(dmatrix(tf(t.get_matrix())), math::dmat4(m)) < 1e-6, "and recomposes into the matrix");
    }
    {
        // Projections: kept as the matrix; coordinate transforms divide by w.
        const math::mat4 proj = math::perspective(math::radians(60.0f), 1.5f, 0.1f, 100.0f);
        const tf p(proj);
        const legacy_t c(proj);
        check(!p.is_perspective_identity(), "a projection is recognized");
        check(p.get_matrix() == proj, "a projection keeps its matrix");
        const math::vec3 point(1.0f, 2.0f, 10.0f);
        check(math::length(p.transform_coord(point) - c.transform_coord(point)) < 1e-5f,
              "a projection divides by w as the legacy class does");
        const tf view(math::lookAt(math::vec3(3.0f, 4.0f, -5.0f), math::vec3(0.0f), math::vec3(0.0f, 1.0f, 0.0f)));
        const tf vp = p * view;
        const legacy_t cvp = c * legacy_t(view.get_matrix());
        check(max_rel_diff(dmatrix(vp), dmatrix(cvp)) < 1e-6, "projection * view is the matrix product");
        check(max_rel_diff(dmatrix(inverse(vp)), dmatrix(inverse(cvp))) < 1e-4, "its inverse matches");
        check(math::length(p.get_perspective() - c.get_perspective()) < 1e-5f, "its perspective row matches");
        tf edited = p;
        edited.set_position(math::vec3(0.5f, 0.0f, 0.0f));
        legacy_t cedited = c;
        cedited.set_position(math::vec3(0.5f, 0.0f, 0.0f));
        check(max_rel_diff(dmatrix(edited), dmatrix(cedited)) < 1e-4, "editing a projection's position recomposes it");
    }
    {
        // The Euler hint survives a decompose and a set_position.
        tf t;
        t.set_rotation_euler_degrees(10.0f, 89.9f, -30.0f);
        tf m(t.get_matrix());
        check(!m.has_rotation_euler_hint(), "a matrix-built transform has no hint");
        t.set_position(math::vec3(5.0f));
        check(t.get_rotation_euler_degrees() == math::vec3(10.0f, 89.9f, -30.0f),
              "the authored angles survive set_position");
    }
    {
        // With wider positions the cached matrix has a constant fourth column; a general inverse
        // (non-uniform scale) must write it too, or compare reads what the memory held.
        td t;
        t.set_position(math::dvec3(3.0, -2.0, 5.0));
        t.set_rotation(math::quat(math::vec3(0.3f, -0.8f, 1.1f)));
        t.set_scale(1.0f, 2.0f, 0.5f);
        const td inv = inverse(t);
        td rebuilt;
        rebuilt.set_position(inv.get_position());
        rebuilt.set_rotation(inv.get_rotation());
        rebuilt.set_scale(inv.get_scale());
        rebuilt.set_skew(inv.get_skew());
        check(inv.compare(rebuilt, 1e-4f) == 0,
              "a general inverse with double positions compares equal to the transform built from its components");
        check(inverse(t).compare(inverse(t)) == 0, "and two inverses of one transform compare equal");
    }
    {
        // A tiny non-uniform scale is floored when the matrix is built; every path that answers
        // from the components must answer the same, whether the matrix is built yet or not.
        tf dirty;
        dirty.set_rotation(math::quat(math::vec3(0.2f, 0.4f, -0.6f)));
        dirty.set_scale(1e-7f, 1.0f, 2.0f);
        tf built = dirty;
        (void)built.get_matrix();
        const math::vec3 point(3.0f, -1.0f, 2.0f);
        check(math::length(dirty.transform_coord(point) - built.transform_coord(point)) < 1e-6f,
              "transform_coord of a tiny non-uniform scale does not depend on the matrix being built");
        tf axes = dirty;
        const math::vec3 axis_before = axes.x_axis();
        (void)axes.get_matrix();
        check(axis_before == axes.x_axis(), "x_axis of a tiny non-uniform scale does not depend on it either");
    }
    {
        // Wider positions keep exact integers far out; the float class rounds them.
        td t;
        t.set_position(math::dvec3(12345678.25, -7654321.5, 0.125));
        check(t.get_position() == math::dvec3(12345678.25, -7654321.5, 0.125), "double positions are stored exactly");
        const math::mat4 rel = t.get_matrix_relative(math::dvec3(12345678.0, -7654321.0, 0.0));
        check(math::vec3(rel[3]) == math::vec3(0.25f, -0.5f, 0.125f), "the relative matrix subtracts in double");
    }
}

//---------------------------------------------------------------------------------------------
// Precision far from the origin
//---------------------------------------------------------------------------------------------
template<typename X>
auto rotation_of(const X& t) -> math::dquat
{
    const auto q = t.get_rotation();
    return math::dquat(double(q.w), double(q.x), double(q.y), double(q.z));
}

struct far_errors
{
    double compose = 0.0;
    double compose_dirty = 0.0;
    double vertex = 0.0;
    double relative = 0.0;
    double roundtrip = 0.0;
    double chain = 0.0;
    double path_spread = 0.0;
};

/// Errors against the true positions (in double) for transforms whose rotations are the stored
/// ones: what remains is what the class does to positions.
template<typename X>
auto measure_far(double distance, std::uint32_t seed) -> far_errors
{
    rng r(seed);
    far_errors e;
    for(int i = 0; i < 200; ++i)
    {
        const math::dvec3 dir = math::normalize(math::dvec3(r.vec(1.0f)) + math::dvec3(0.001));
        const math::dvec3 root_pos = dir * distance;
        const math::vec3 child_pos(1.234567f, -0.5f, 2.25f);

        const X root = make<X>(root_pos, r.quat(), math::vec3(1.0f));
        const X child = make<X>(math::dvec3(child_pos), r.quat(), math::vec3(1.0f));
        const math::dquat root_rot = rotation_of(root);
        const math::dquat child_rot = rotation_of(child);
        const math::dvec3 expected = root_pos + root_rot * math::dvec3(child_pos);

        X root_dirty = root;
        const X world_dirty = root_dirty * child;
        e.compose_dirty = std::max(e.compose_dirty, math::length(math::dvec3(world_dirty.get_position()) - expected));

        X root_clean = root;
        X child_clean = child;
        (void)dmatrix(root_clean);
        (void)dmatrix(child_clean);
        const X world = root_clean * child_clean;
        e.compose = std::max(e.compose, math::length(math::dvec3(world.get_position()) - expected));

        const math::vec3 vertex(0.5f, 0.25f, -0.75f);
        const math::dvec3 expected_vertex = expected + root_rot * (child_rot * math::dvec3(vertex));
        e.vertex =
            std::max(e.vertex, math::length(math::dvec3(world.transform_coord(pos_of<X>(vertex))) - expected_vertex));

        // The camera sits a few metres away; the GPU gets the offset from it.
        const math::dvec3 camera = expected + math::dvec3(3.0, 1.0, -2.0);
        math::dvec3 relative;
        if constexpr(has_matrix_relative<X>)
        {
            relative = math::dvec3(math::vec3(world.get_matrix_relative(pos_of<X>(camera))[3]));
        }
        else
        {
            math::mat4 m = world.get_matrix();
            glm::set_position_relative(m, math::vec3(camera));
            relative = math::dvec3(math::vec3(m[3]));
        }
        e.relative = std::max(e.relative, math::length(relative - (expected - camera)));

        // World to local through the inverse, with and without its matrix built first.
        const auto world_point = world.transform_coord(pos_of<X>(vertex));
        const X inv = inverse(world);
        X inv_built = inv;
        (void)dmatrix(inv_built);
        const auto back = inv.transform_coord(world_point);
        const auto back_built = inv_built.transform_coord(world_point);
        e.roundtrip = std::max(e.roundtrip, math::length(math::dvec3(back) - math::dvec3(vertex)));
        e.roundtrip = std::max(e.roundtrip, math::length(math::dvec3(back_built) - math::dvec3(vertex)));

        X leaf = root;
        math::dvec3 expected_leaf = root_pos;
        math::dquat expected_rot = root_rot;
        for(int depth = 0; depth < 6; ++depth)
        {
            const math::vec3 offset = r.vec(2.0f);
            const X link = make<X>(math::dvec3(offset), r.quat(), math::vec3(1.0f));
            leaf = leaf * link;
            expected_leaf += expected_rot * math::dvec3(offset);
            expected_rot = expected_rot * rotation_of(link);
        }
        e.chain = std::max(e.chain, math::length(math::dvec3(leaf.get_position()) - expected_leaf));

        // A far child under a parent near the origin: the same child must land on the same
        // coordinates whichever path computed it (parent matrix cached or not, components read
        // or not, transform_coord of the parent).
        const X group = make<X>(math::dvec3(r.vec(5.0f)), r.quat(), math::vec3(1.0f));
        const X far_child = make<X>(root_pos, r.quat(), math::vec3(1.0f));
        X group_clean = group;
        (void)dmatrix(group_clean);
        X group_built = from_any<X>(group);
        X group_read = group_built;
        (void)group_read.get_rotation();
        const math::dvec3 p0(math::dvec3((group * far_child).get_position()));
        const math::dvec3 p1(math::dvec3((group_clean * far_child).get_position()));
        const math::dvec3 p2(math::dvec3(group.transform_coord(far_child.get_position())));
        const math::dvec3 p3(math::dvec3((group_built * far_child).get_position()));
        const math::dvec3 p4(math::dvec3((group_read * far_child).get_position()));
        double spread = std::max(math::length(p1 - p0), math::length(p2 - p0));
        spread = std::max(spread, math::length(p4 - p3));
        e.path_spread = std::max(e.path_spread, spread);
    }
    return e;
}

void test_far_from_origin()
{
    std::printf("  far from the origin, worst error in metres over 200 placements\n");
    std::printf("    %-9s %-6s %10s %10s %10s %10s %10s %10s %10s\n",
                "distance",
                "type",
                "compose",
                "dirty",
                "vertex",
                "relative",
                "round trip",
                "chain x6",
                "paths");
    const double distances[] = {1e3, 1e4, 1e5, 1e6, 1e7};
    for(const double d : distances)
    {
        const far_errors c = measure_far<legacy_t>(d, 11);
        const far_errors f = measure_far<tf>(d, 11);
        const far_errors dd = measure_far<td>(d, 11);
        const far_errors a = measure_far<tdd>(d, 11);
        auto row = [d](const char* name, const far_errors& e)
        {
            std::printf("    %-9.0e %-6s %10.2e %10.2e %10.2e %10.2e %10.2e %10.2e %10.2e\n",
                        d,
                        name,
                        e.compose,
                        e.compose_dirty,
                        e.vertex,
                        e.relative,
                        e.roundtrip,
                        e.chain,
                        e.path_spread);
        };
        row("legacy", c);
        row("tf", f);
        row("td", dd);
        row("tdd", a);

        const std::string at = " at " + std::to_string(static_cast<long long>(d)) + " m";
        check(dd.compose < 1e-6 && dd.compose_dirty < 1e-6 && dd.vertex < 1e-6,
              "double positions place children and vertices within a micrometre" + at);
        check(dd.chain < 1e-5, "double positions keep a chain of six within 10 micrometres" + at);
        check(dd.relative < 1e-6, "relative matrices of double positions stay within a micrometre" + at);
        check(dd.roundtrip < 1e-6, "world to local through the inverse stays within a micrometre" + at);
        check(dd.path_spread == 0.0, "double positions land on the same coordinates on every path" + at);
        check(a.path_spread < 1e-6, "all-double transforms agree across paths" + at);
        check(f.compose <= c.compose * 1.5 + 1e-6, "transform_t is no less precise than the legacy class" + at);
    }
}

//---------------------------------------------------------------------------------------------
// Benchmarks
//---------------------------------------------------------------------------------------------
using bench_clock = std::chrono::steady_clock;
volatile double g_sink = 0.0;

template<typename Fn>
auto time_ns(Fn&& fn, int iters) -> double
{
    constexpr int k_reps = 7;
    double best = 1e30;
    for(int rep = 0; rep < k_reps; ++rep)
    {
        double acc = 0.0;
        const auto start = bench_clock::now();
        for(int i = 0; i < iters; ++i)
        {
            acc += fn(i);
        }
        const double ns = std::chrono::duration<double, std::nano>(bench_clock::now() - start).count();
        g_sink = g_sink + acc;
        best = std::min(best, ns);
    }
    return best / double(iters);
}

template<typename X>
auto matrix_x(const X& t) -> double
{
    if constexpr(has_matrix<X>)
    {
        const auto& m = t.get_matrix();
        return double(m[3].x) + double(m[0].x);
    }
    else
    {
        const auto m = t.get_matrix_relative(pos_of<X>{});
        return double(m[3].x) + double(m[0].x);
    }
}

template<typename X>
auto relative_x(const X& t, const math::dvec3& origin) -> double
{
    if constexpr(has_matrix_relative<X>)
    {
        return double(t.get_matrix_relative(pos_of<X>(origin))[3].x);
    }
    else
    {
        math::mat4 m = t.get_matrix();
        glm::set_position_relative(m, math::vec3(origin));
        return double(m[3].x);
    }
}

template<typename X>
struct bench_data
{
    static constexpr int n = 64;
    std::vector<X> parents;
    std::vector<X> locals;
    std::vector<X> non_uniform;
    std::vector<X> skewed;
    std::vector<typename X::mat4_t> mats;
    std::vector<typename X::mat4_t> sheared_mats;
    std::vector<pos_of<X>> points;
    std::vector<typename X::vec3_t> dirs;
    std::vector<typename X::quat_t> quats;

    bench_data()
    {
        rng r(0xbe7cu);
        for(int i = 0; i < n; ++i)
        {
            const float fi = static_cast<float>(i);
            const float us = 1.0f + static_cast<float>(i % 4) * 0.25f;
            parents.push_back(make<X>(math::dvec3(fi * 0.5, fi * -0.25, 10.0 + fi), r.quat(), math::vec3(us)));
            locals.push_back(make<X>(math::dvec3(-fi, 2.0, fi * 0.1), r.quat(), math::vec3(1.0f)));
            non_uniform.push_back(make<X>(math::dvec3(r.vec(20.0f)), r.quat(), math::vec3(1.0f, 2.0f + fi * 0.01f, 3.0f)));
            skewed.push_back(make<X>(math::dvec3(r.vec(20.0f)), r.quat(), math::vec3(1.0f), math::vec3(0.0f, 0.0f, 0.3f)));
            (void)dmatrix(parents.back());
            (void)dmatrix(locals.back());
            (void)dmatrix(non_uniform.back());
            (void)dmatrix(skewed.back());
            (void)parents.back().get_rotation();
            (void)skewed.back().get_rotation();
            mats.push_back(typename X::mat4_t(r.trs_matrix()));
            sheared_mats.push_back(typename X::mat4_t(r.sheared_matrix()));
            points.push_back(pos_of<X>(r.vec(10.0f)));
            dirs.push_back(typename X::vec3_t(r.vec(1.0f)));
            quats.push_back(typename X::quat_t(r.quat()));
        }
    }
};

template<typename X>
auto build_from(const typename X::mat4_t& m) -> X
{
    if constexpr(has_matrix<X>)
    {
        return X(m);
    }
    else
    {
        return X(m);
    }
}

/// Scene graph frame: some locals edited, every global recomposed, every matrix read.
template<typename X>
auto scene_graph_ns(float non_uniform_share) -> double
{
    constexpr int k_nodes = 4096;
    rng r(0x5ce7eu);
    std::vector<int> parent(k_nodes, -1);
    std::vector<X> locals(k_nodes);
    std::vector<X> globals(k_nodes);
    for(int i = 0; i < k_nodes; ++i)
    {
        // Roots every 64 nodes; the rest hang under an earlier node, depth up to about 6.
        parent[i] = (i % 64 == 0) ? -1 : std::max(i - 1 - r.index(std::min(i % 64, 8)), (i / 64) * 64);
        const bool stretched = r.chance(non_uniform_share);
        locals[i] = make<X>(math::dvec3(r.vec(10.0f)), r.quat(), stretched ? math::vec3(1.0f, 2.0f, 0.5f) : math::vec3(1.0f));
    }
    std::vector<int> edits;
    for(int i = 0; i < k_nodes / 10; ++i)
    {
        edits.push_back(r.index(k_nodes));
    }
    std::vector<math::vec3> positions;
    std::vector<math::quat> rotations;
    for(int i = 0; i < 64; ++i)
    {
        positions.push_back(r.vec(10.0f));
        rotations.push_back(r.quat());
    }

    int frame = 0;
    auto run_frame = [&](int) -> double
    {
        ++frame;
        for(size_t e = 0; e < edits.size(); ++e)
        {
            X& l = locals[edits[e]];
            l.set_position(pos_of<X>(positions[(e + frame) & 63]));
            l.set_rotation(typename X::quat_t(rotations[(e + frame) & 63]));
        }
        double acc = 0.0;
        for(int i = 0; i < k_nodes; ++i)
        {
            globals[i] = parent[i] < 0 ? locals[i] : globals[parent[i]] * locals[i];
            acc += matrix_x(globals[i]);
            if((i & 3) == 0)
            {
                acc += double(globals[i].get_position().x);
            }
        }
        return acc;
    };
    return time_ns(run_frame, 40) / double(k_nodes);
}

struct bench_row
{
    std::string name;
    double ns[4] = {};
};

template<int Index, typename X>
void bench_type(std::vector<bench_row>& rows)
{
    bench_data<X> f;
    const auto origin = math::dvec3(1000.0, -20.0, 300.0);
    int row = 0;
    auto put = [&](const char* name, double ns)
    {
        if(static_cast<int>(rows.size()) <= row)
        {
            rows.push_back(bench_row{name});
        }
        rows[row].ns[Index] = ns;
        ++row;
    };
    constexpr int it = 200000;

    put("mat4 -> transform -> position",
        time_ns([&](int i) { return double(build_from<X>(f.mats[i & 63]).get_position().x); }, it));
    put("mat4 -> transform -> rotation",
        time_ns([&](int i) { return double(build_from<X>(f.mats[i & 63]).get_rotation().w); }, it));
    put("mat4 -> transform -> all components",
        time_ns(
            [&](int i)
            {
                const X t = build_from<X>(f.mats[i & 63]);
                return double(t.get_position().x) + double(t.get_rotation().w) + double(t.get_scale().y) +
                       double(t.get_skew().z);
            },
            it));
    put("sheared mat4 -> transform -> all components",
        time_ns(
            [&](int i)
            {
                const X t = build_from<X>(f.sheared_mats[i & 63]);
                return double(t.get_position().x) + double(t.get_rotation().w) + double(t.get_scale().y) +
                       double(t.get_skew().z);
            },
            it));
    put("mat4 -> transform -> set_position -> matrix",
        time_ns(
            [&](int i)
            {
                X t = build_from<X>(f.mats[i & 63]);
                t.set_position(f.points[i & 63]);
                return matrix_x(t);
            },
            it));
    {
        X t = f.parents[0];
        put("set_position -> matrix",
            time_ns(
                [&](int i)
                {
                    t.set_position(f.points[i & 63]);
                    return matrix_x(t);
                },
                it));
    }
    {
        X t = f.parents[0];
        put("set_rotation -> matrix",
            time_ns(
                [&](int i)
                {
                    t.set_rotation(f.quats[i & 63]);
                    return matrix_x(t);
                },
                it));
    }
    {
        X t = f.parents[0];
        put("set position, rotation, scale -> matrix",
            time_ns(
                [&](int i)
                {
                    t.set_position(f.points[i & 63]);
                    t.set_rotation(f.quats[i & 63]);
                    t.set_scale(typename X::vec3_t(1.0f + float(i & 3)));
                    return matrix_x(t);
                },
                it));
    }
    {
        X t = f.parents[0];
        put("set non-uniform scale -> matrix",
            time_ns(
                [&](int i)
                {
                    t.set_scale(typename X::vec3_t(1.0f, 2.0f, 1.0f + float(i & 3)));
                    return matrix_x(t);
                },
                it));
    }
    put("op* clean -> matrix", time_ns([&](int i) { return matrix_x(f.parents[i & 63] * f.locals[i & 63]); }, it));
    put("op* clean -> position + rotation",
        time_ns(
            [&](int i)
            {
                const X r = f.parents[i & 63] * f.locals[i & 63];
                return double(r.get_position().x) + double(r.get_rotation().w);
            },
            it));
    {
        X local = f.locals[0];
        put("op* dirty local -> matrix",
            time_ns(
                [&](int i)
                {
                    local.set_position(f.points[i & 63]);
                    return matrix_x(f.parents[i & 63] * local);
                },
                it));
    }
    put("op* non-uniform parent -> matrix",
        time_ns([&](int i) { return matrix_x(f.non_uniform[i & 63] * f.locals[i & 63]); }, it));
    put("op* non-uniform parent -> position + rotation",
        time_ns(
            [&](int i)
            {
                const X r = f.non_uniform[i & 63] * f.locals[i & 63];
                return double(r.get_position().x) + double(r.get_rotation().w);
            },
            it));
    put("op* skewed parent -> matrix",
        time_ns([&](int i) { return matrix_x(f.skewed[i & 63] * f.locals[i & 63]); }, it));
    put("op* chain x8 -> matrix + position",
        time_ns(
            [&](int i)
            {
                X acc = f.parents[i & 63];
                for(int j = 0; j < 8; ++j)
                {
                    acc = acc * f.locals[(i + j) & 63];
                }
                return matrix_x(acc) + double(acc.get_position().x);
            },
            it / 4));
    put("inverse -> position", time_ns([&](int i) { return double(inverse(f.parents[i & 63]).get_position().x); }, it));
    put("inverse non-uniform -> matrix", time_ns([&](int i) { return matrix_x(inverse(f.non_uniform[i & 63])); }, it));
    put("transform_coord (matrix valid)",
        time_ns([&](int i) { return double(f.parents[i & 63].transform_coord(f.points[i & 63]).x); }, it));
    {
        std::vector<X> dirty = f.parents;
        for(auto& t : dirty)
        {
            t.set_rotation(t.get_rotation());
        }
        put("transform_coord (matrix dirty)",
            time_ns([&](int i) { return double(dirty[i & 63].transform_coord(f.points[i & 63]).x); }, it));
    }
    put("inverse_transform_coord",
        time_ns([&](int i) { return double(f.parents[i & 63].inverse_transform_coord(f.points[i & 63]).x); }, it));
    put("inverse_transform_coord (skewed)",
        time_ns([&](int i) { return double(f.skewed[i & 63].inverse_transform_coord(f.points[i & 63]).x); }, it));
    put("transform_normal (uniform)",
        time_ns([&](int i) { return double(f.parents[i & 63].transform_normal(f.dirs[i & 63]).x); }, it));
    put("transform_normal (non-uniform)",
        time_ns([&](int i) { return double(f.non_uniform[i & 63].transform_normal(f.dirs[i & 63]).x); }, it));
    put("x_unit_axis", time_ns([&](int i) { return double(f.parents[i & 63].x_unit_axis().x); }, it));
    put("get_position (valid)", time_ns([&](int i) { return double(f.parents[i & 63].get_position().x); }, it));
    put("get_rotation_euler_degrees (cached)",
        time_ns([&](int i) { return double(f.parents[i & 63].get_rotation_euler_degrees().y); }, it));
    {
        X t = f.parents[0];
        const auto q = t.get_rotation();
        put("set_rotation (same, hint kept)",
            time_ns(
                [&](int i)
                {
                    t.set_rotation(q);
                    return double(t.get_rotation().w) + double(i & 1);
                },
                it));
    }
    put("compare (equal)",
        time_ns([&](int i) { return double(f.parents[i & 63].compare(f.parents[i & 63], 1e-4f)); }, it));
    put("relative matrix (origin subtract)", time_ns([&](int i) { return relative_x(f.parents[i & 63], origin); }, it));
    put("copy", time_ns(
                    [&](int i)
                    {
                        const X c = f.parents[i & 63];
                        return double(c.get_position().y);
                    },
                    it));
    put("scene graph frame, uniform (per node)", scene_graph_ns<X>(0.0f));
    put("scene graph frame, 30% stretched (per node)", scene_graph_ns<X>(0.3f));
    {
        // An animated node: its local gets a new rotation, then its global is recomposed.
        X local = f.locals[0];
        put("set_rotation on local -> op* -> matrix",
            time_ns(
                [&](int i)
                {
                    local.set_rotation(f.quats[i & 63]);
                    return matrix_x(f.parents[i & 63] * local);
                },
                it));
    }
    {
        // An edit, then one point through the edited transform.
        X t = f.parents[0];
        put("set_rotation -> transform_coord",
            time_ns(
                [&](int i)
                {
                    t.set_rotation(f.quats[i & 63]);
                    return double(t.transform_coord(f.points[i & 63]).x);
                },
                it));
    }
    {
        // The same, with the matrix read afterwards as a compose or a draw would.
        X t = f.parents[0];
        put("set_rotation -> transform_coord -> matrix",
            time_ns(
                [&](int i)
                {
                    t.set_rotation(f.quats[i & 63]);
                    return double(t.transform_coord(f.points[i & 63]).x) + matrix_x(t);
                },
                it));
    }
    {
        X t = f.parents[0];
        put("set_rotation -> inverse_transform_coord",
            time_ns(
                [&](int i)
                {
                    t.set_rotation(f.quats[i & 63]);
                    return double(t.inverse_transform_coord(f.points[i & 63]).x);
                },
                it));
    }
    {
        X t = f.parents[0];
        put("set_rotation -> transform_normal",
            time_ns(
                [&](int i)
                {
                    t.set_rotation(f.quats[i & 63]);
                    return double(t.transform_normal(f.dirs[i & 63]).x);
                },
                it));
    }
    {
        X t = f.parents[0];
        put("set_rotation -> translate_local",
            time_ns(
                [&](int i)
                {
                    t.set_rotation(f.quats[i & 63]);
                    t.translate_local(f.dirs[i & 63]);
                    return double(t.get_position().x);
                },
                it));
    }
    {
        X t = f.parents[0];
        put("set_rotation -> z_unit_axis",
            time_ns(
                [&](int i)
                {
                    t.set_rotation(f.quats[i & 63]);
                    return double(t.z_unit_axis().x);
                },
                it));
    }
    {
        X local = f.locals[0];
        X global = f.locals[0];
        put("set position + rotation on local -> global = op*",
            time_ns(
                [&](int i)
                {
                    local.set_position(f.points[i & 63]);
                    local.set_rotation(f.quats[i & 63]);
                    global = f.parents[i & 63] * local;
                    return matrix_x(global);
                },
                it));
    }
}

void run_kernel_benches()
{
    // The building blocks transform_t is made of, against glm's versions.
    rng r(0x6e7u);
    std::vector<math::quat> q;
    std::vector<math::vec3> v;
    std::vector<math::dvec3> dv;
    for(int i = 0; i < 64; ++i)
    {
        q.push_back(r.quat());
        v.push_back(r.vec(10.0f));
        dv.push_back(math::dvec3(r.vec(10.0f)) * 1e6);
    }
    constexpr int it = 400000;
    namespace k = math::transform_kernels;
    std::printf("transform kernels, ns per operation (best of 7)\n");
    auto row = [](const char* name, double ns) { std::printf("  %-46s %8.2f\n", name, ns); };
    row("rotate float, glm q * v (aligned SIMD)", time_ns([&](int i) { return double((q[i & 63] * v[i & 63]).x); }, it));
    row("rotate float, scalar", time_ns([&](int i) { return double(k::rotate<float>(q[i & 63], v[i & 63]).x); }, it));
    row("rotate double, glm dquat * dvec3",
        time_ns([&](int i) { return (math::dquat(q[i & 63]) * dv[i & 63]).x; }, it));
    row("rotate double, scalar", time_ns([&](int i) { return k::rotate<double>(q[i & 63], dv[i & 63]).x; }, it));
    row("rotate chain float x8, glm",
        time_ns(
            [&](int i)
            {
                math::vec3 x = v[i & 63];
                for(int j = 0; j < 8; ++j)
                {
                    x = q[(i + j) & 63] * x;
                }
                return double(x.x);
            },
            it / 4));
    row("rotate chain float x8, scalar",
        time_ns(
            [&](int i)
            {
                math::vec3 x = v[i & 63];
                for(int j = 0; j < 8; ++j)
                {
                    x = k::rotate<float>(q[(i + j) & 63], x);
                }
                return double(x.x);
            },
            it / 4));
    row("quat product + glm::normalize",
        time_ns([&](int i) { return double(math::normalize(q[i & 63] * q[(i + 1) & 63]).w); }, it));
    row("quat product + Newton step (mul_unit)",
        time_ns([&](int i) { return double(k::mul_unit(q[i & 63], q[(i + 1) & 63]).w); }, it));
    row("quat chain x8, glm::normalize",
        time_ns(
            [&](int i)
            {
                math::quat x = q[i & 63];
                for(int j = 0; j < 8; ++j)
                {
                    x = math::normalize(x * q[(i + j) & 63]);
                }
                return double(x.w);
            },
            it / 4));
    row("quat chain x8, Newton step",
        time_ns(
            [&](int i)
            {
                math::quat x = q[i & 63];
                for(int j = 0; j < 8; ++j)
                {
                    x = k::mul_unit(x, q[(i + j) & 63]);
                }
                return double(x.w);
            },
            it / 4));
    {
        // Unit length after 10000 chained products: the Newton step does not drift.
        math::quat a = q[0];
        math::quat b = q[0];
        for(int j = 0; j < 10000; ++j)
        {
            a = k::mul_unit(a, q[j & 63]);
            b = math::normalize(b * q[j & 63]);
        }
        std::printf("  after 10000 products: |q| - 1 = %.2e (Newton), %.2e (normalize), angle between %.2e rad\n",
                    double(math::length(a)) - 1.0,
                    double(math::length(b)) - 1.0,
                    rotation_angle(to_d(a), to_d(b)));
    }
    const math::mat4 m = r.trs_matrix();
    row("decompose, glm_decompose (legacy class)",
        time_ns(
            [&](int i)
            {
                math::vec3 sc, tr, sk;
                math::quat ro;
                math::vec4 pe;
                math::mat4 mm = m;
                mm[3].x += float(i & 7);
                math::glm_decompose(mm, sc, ro, tr, sk, pe);
                return double(ro.w) + double(tr.x);
            },
            it));
}

void run_benches()
{
    run_kernel_benches();
    std::vector<bench_row> rows;
    bench_type<0, legacy_t>(rows);
    bench_type<1, tf>(rows);
    bench_type<2, td>(rows);
    bench_type<3, tdd>(rows);
    std::printf("transform benchmarks, ns per operation (best of 7)\n");
    std::printf("  %-46s %8s %8s %8s %8s %8s\n", "operation", "legacy", "tf", "td", "tdd", "tf/legacy");
    for(const auto& r : rows)
    {
        std::printf("  %-46s %8.2f %8.2f %8.2f %8.2f %7.2fx\n",
                    r.name.c_str(),
                    r.ns[0],
                    r.ns[1],
                    r.ns[2],
                    r.ns[3],
                    r.ns[1] / std::max(r.ns[0], 1e-9));
    }
}

void test_api_parity()
{
    check(transform_api<tf>, "transform_t answers every call made on the legacy class");
}

} // namespace

void run_transform_parity_suite(rtti::context& /*ctx*/)
{
    const bool bench_only = tests::wants("bench-only");
    const bool bench = bench_only || tests::wants("bench");
    if(!bench_only)
    {
        RUN_CASE(test_api_parity());
        RUN_CASE(test_sizes());
        RUN_CASE(test_differential_matrices());
        RUN_CASE(test_differential_sequences());
        RUN_CASE(test_oracles());
        RUN_CASE(test_edge_cases());
        RUN_CASE(test_far_from_origin());
    }
    if(bench)
    {
        run_benches();
    }
}

REGISTER_TEST_SUITE("transform parity", run_transform_parity_suite)
