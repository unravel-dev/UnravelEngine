#pragma once
//-----------------------------------------------------------------------------
// transform_t: a transform kept as components with a cached matrix; math::transform is
// transform_t<float>.
//
// T is the scalar of the rotation, scale, skew and the cached matrix; P is the scalar of the
// position. With P == T the matrix carries the position (get_matrix() and the matrix
// conversions). With P wider than T (double positions) there is no absolute matrix in T: callers
// read get_matrix_relative(origin), which subtracts the origin in P before narrowing, or
// get_linear() and get_position().
//
// Storage: the components (position, rotation, scale, skew) and a cached matrix. The matrix's
// columns 0-2 hold the linear part (rotation * skew * scale); column 3 holds the position when
// P == T and (0, 0, 0, 1) otherwise. The position is always valid: a transform built from a matrix
// reads it from the translation column at once (over w for a projection), so only rotation, scale
// and skew wait for a decompose, and set_position never needs one.
//
// With P wider than T, every position computed through the linear part (compose, transform_coord,
// inverse) uses the stored linear part, built from the components when they were edited last: a
// point lands on the same coordinates whichever path computed it, and on the coordinates the GPU
// draws it at. Evaluating the rotation in double on one path and the float matrix on another would
// differ by the float rounding of the matrix times the offset, which is metres at 1e7 m.
//
// A matrix whose bottom row is not (0, 0, 0, 1) (a projection) is kept as the truth: its
// components come from the general decompose and every component edit recomposes it at once.
//
// What the code is shaped around:
//  - `a = b * c` writes the members of a in place. The compose is inlined, its result never has
//    its address taken on the affine path, and the flags of the result say on every path that its
//    matrix is built, so the compiler keeps the result in registers. A copy of the whole object
//    (144 bytes or more) is a call to memcpy.
//  - A member that was just written is read back as the register it was written as. A read of
//    two or three lanes of a four-lane member becomes one unaligned register that reaches into the
//    next member, and waits for both stores; the kernels here read whole members.
//  - position_, scale_ and skew_ are stored with a zero fourth lane (the padding of a vec3 held
//    in a register): arithmetic runs over all four lanes, and leftover bits there are often
//    denormal numbers, which cost tens of cycles per operation.
//  - Functions that read a built matrix test one flag and call out of line to build it.
//-----------------------------------------------------------------------------
#include "matrix_recompose.hpp"

#include <cstdint>
#include <string>
#include <type_traits>

namespace math
{
using namespace glm;

#if defined(_MSC_VER) && !defined(__clang__)
#define TRANSFORM_NOINLINE __declspec(noinline)
#define TRANSFORM_FORCEINLINE __forceinline
#else
#define TRANSFORM_NOINLINE __attribute__((noinline))
#define TRANSFORM_FORCEINLINE inline __attribute__((always_inline))
#endif

/// Absolute tolerance below which a decomposed skew counts as zero (float decompose noise of a
/// TRS matrix stays under it).
template<typename T>
inline constexpr T k_skew_zero_tolerance = static_cast<T>(1e-5);

/// Relative tolerance for scale uniformity; the reference is floored at 1.
template<typename T>
inline constexpr T k_scale_uniform_tolerance = static_cast<T>(1e-5);

namespace transform_kernels
{
/// True when a vec3 of T has the storage of a vec4: four lanes, the fourth unused.
template<typename T, qualifier Q>
inline constexpr bool k_vec3_has_fourth_lane = requires(vec<3, T, Q>& r, const vec<4, T, Q>& v) { r.data = v.data; };

/// True when that storage is one SIMD register (not an array of four scalars).
template<typename T, qualifier Q>
inline constexpr bool k_vec3_in_register = requires(vec<3, T, Q>& r, const vec<4, T, Q>& v) {
    r.data = v.data;
    requires !std::is_class_v<std::remove_cvref_t<decltype(v.data)>>;
};

/// The xyz of @p v. A vec3 held in a register is copied as the whole register, so it is stored and
/// loaded back as one vector (its fourth lane holds v.w). A vec3 held as four scalars is copied
/// scalar by scalar, with a zero in the fourth.
template<typename T, qualifier Q>
TRANSFORM_FORCEINLINE auto vec3_of(const vec<4, T, Q>& v) noexcept -> vec<3, T, Q>
{
    if constexpr(k_vec3_in_register<T, Q>)
    {
        vec<3, T, Q> r;
        r.data = v.data;
        return r;
    }
    else if constexpr(k_vec3_has_fourth_lane<T, Q>)
    {
        const vec<4, T, Q> lanes(v.x, v.y, v.z, T(0));
        vec<3, T, Q> r;
        r.data = lanes.data;
        return r;
    }
    else
    {
        return vec<3, T, Q>(v);
    }
}

/// @p v with a zero in its fourth lane, where a vec3 has one. The members are stored this way: a
/// vec3 written member by member keeps whatever the lane held, the compiler and glm run vector
/// arithmetic over all four lanes, and a leftover denormal there costs tens of cycles.
template<typename T, qualifier Q>
TRANSFORM_FORCEINLINE auto clear_padding(const vec<3, T, Q>& v) noexcept -> vec<3, T, Q>
{
    if constexpr(k_vec3_has_fourth_lane<T, Q>)
    {
        return vec3_of(vec<4, T, Q>(v, T(0)));
    }
    else
    {
        return v;
    }
}

/// a * b for two affine matrices, b given by its columns. The bottom rows are (0, 0, 0, 1): the
/// fourth row of b multiplies nothing and the translation of a is added once.
template<typename T, qualifier Q>
TRANSFORM_FORCEINLINE auto mul_affine(const mat<4, 4, T, Q>& a,
                       const vec<4, T, Q>& b0,
                       const vec<4, T, Q>& b1,
                       const vec<4, T, Q>& b2,
                       const vec<4, T, Q>& b3) noexcept -> mat<4, 4, T, Q>
{
    const vec<4, T, Q> a0 = a[0];
    const vec<4, T, Q> a1 = a[1];
    const vec<4, T, Q> a2 = a[2];
    const vec<4, T, Q> a3 = a[3];
    mat<4, 4, T, Q> r;
    r[0] = a0 * b0.x + a1 * b0.y + a2 * b0.z;
    r[1] = a0 * b1.x + a1 * b1.y + a2 * b1.z;
    r[2] = a0 * b2.x + a1 * b2.y + a2 * b2.z;
    r[3] = a0 * b3.x + a1 * b3.y + a2 * b3.z + a3;
    return r;
}

/// The linear parts (columns 0-2) of a * b, b given by its columns, with the fourth column
/// (0, 0, 0, 1).
template<typename T, qualifier Q>
TRANSFORM_FORCEINLINE auto mul_linear_parts(const mat<4, 4, T, Q>& a,
                             const vec<4, T, Q>& b0,
                             const vec<4, T, Q>& b1,
                             const vec<4, T, Q>& b2) noexcept -> mat<4, 4, T, Q>
{
    const vec<4, T, Q> a0 = a[0];
    const vec<4, T, Q> a1 = a[1];
    const vec<4, T, Q> a2 = a[2];
    mat<4, 4, T, Q> r;
    r[0] = a0 * b0.x + a1 * b0.y + a2 * b0.z;
    r[1] = a0 * b1.x + a1 * b1.y + a2 * b1.z;
    r[2] = a0 * b2.x + a1 * b2.y + a2 * b2.z;
    r[3] = vec<4, T, Q>(T(0), T(0), T(0), T(1));
    return r;
}

/// q * v for a unit quaternion, v + 2w (u x v) + 2 u x (u x v), in scalar arithmetic of S.
template<typename S, typename T, qualifier Q>
TRANSFORM_FORCEINLINE auto rotate(const qua<T, Q>& q, const vec<3, S, Q>& v) noexcept -> vec<3, S, Q>
{
    const S qx = S(q.x);
    const S qy = S(q.y);
    const S qz = S(q.z);
    const S qw = S(q.w);
    const S tx = S(2) * (qy * v.z - qz * v.y);
    const S ty = S(2) * (qz * v.x - qx * v.z);
    const S tz = S(2) * (qx * v.y - qy * v.x);
    return vec<3, S, Q>(v.x + qw * tx + (qy * tz - qz * ty),
                        v.y + qw * ty + (qz * tx - qx * tz),
                        v.z + qw * tz + (qx * ty - qy * tx));
}

/// The inverse rotation of a unit quaternion.
template<typename T, qualifier Q>
TRANSFORM_FORCEINLINE auto conjugate(const qua<T, Q>& q) noexcept -> qua<T, Q>
{
    qua<T, Q> r;
    r.x = -q.x;
    r.y = -q.y;
    r.z = -q.z;
    r.w = q.w;
    return r;
}

/// How far a quaternion's squared length may be from 1 for Newton steps of 1/sqrt to bring it to
/// unit length without a square root and a divide.
template<typename T>
inline constexpr T k_unit_step_tolerance = static_cast<T>(3e-4);

/// 1 / sqrt(n2) for n2 within k_unit_step_tolerance of 1, by Newton steps from 1. A step
/// leaves 3/8 of the squared distance: under half a float's last place after one step, and under
/// a double's after two (one when n2 is already within 1e-8).
template<typename T>
TRANSFORM_FORCEINLINE auto unit_scale(T n2) noexcept -> T
{
    T k = T(1.5) - T(0.5) * n2;
    if constexpr(sizeof(T) > sizeof(float))
    {
        if(abs(n2 - T(1)) > T(1e-8))
        {
            k = k * (T(1.5) - T(0.5) * n2 * k * k);
        }
    }
    return k;
}

/// q at unit length. A quaternion that is unit to rounding (one that was normalized before, or a
/// product of such) takes the Newton step; any other the square root and divide.
template<typename T, qualifier Q>
TRANSFORM_FORCEINLINE auto normalize_unit(const qua<T, Q>& q) noexcept -> qua<T, Q>
{
    const T n2 = (q.x * q.x + q.y * q.y) + (q.z * q.z + q.w * q.w);
    if(abs(n2 - T(1)) > k_unit_step_tolerance<T>) [[unlikely]]
    {
        return glm::normalize(q);
    }
    const T k = unit_scale(n2);
    qua<T, Q> r;
    r.x = q.x * k;
    r.y = q.y * k;
    r.z = q.z * k;
    r.w = q.w * k;
    return r;
}

/// a * b for unit quaternions, brought back to unit length by one Newton step of 1/sqrt(|ab|^2):
/// the product is unit to rounding, so the step restores it without a square root and a divide.
/// Written as four-lane products, each component of a times a signed permutation of b.
template<typename T, qualifier Q>
TRANSFORM_FORCEINLINE auto mul_unit(const qua<T, Q>& a, const qua<T, Q>& b) noexcept -> qua<T, Q>
{
    const vec<4, T, Q> b_w(b.x, b.y, b.z, b.w);
    const vec<4, T, Q> b_x(b.w, -b.z, b.y, -b.x);
    const vec<4, T, Q> b_y(b.z, b.w, -b.x, -b.y);
    const vec<4, T, Q> b_z(-b.y, b.x, b.w, -b.z);
    vec<4, T, Q> r = b_w * a.w + b_x * a.x + b_y * a.y + b_z * a.z;
    const vec<4, T, Q> squares = r * r;
    const T n2 = (squares.x + squares.y) + (squares.z + squares.w);
    qua<T, Q> result;
    if(abs(n2 - T(1)) > k_unit_step_tolerance<T>) [[unlikely]]
    {
        result.x = r.x;
        result.y = r.y;
        result.z = r.z;
        result.w = r.w;
        return glm::normalize(result);
    }
    r *= unit_scale(n2);
    result.x = r.x;
    result.y = r.y;
    result.z = r.z;
    result.w = r.w;
    return result;
}

/// The columns of the rotation matrix of unit quaternion q, each times s, with zero w lanes.
/// Four-lane arithmetic: (q + q) s carries the factor 2 s into every product, the diagonal is
/// s minus two squares, and the off-diagonal pairs are one sum and one difference of products.
template<typename T, qualifier Q>
TRANSFORM_FORCEINLINE void scaled_rotation_columns(const qua<T, Q>& q,
                                    T s,
                                    vec<4, T, Q>& c0,
                                    vec<4, T, Q>& c1,
                                    vec<4, T, Q>& c2) noexcept
{
    const vec<4, T, Q> v(q.x, q.y, q.z, q.w);
    const vec<4, T, Q> d = (v + v) * s;
    const vec<4, T, Q> sq = v * d;
    const vec<4, T, Q> diag =
        vec<4, T, Q>(s) - vec<4, T, Q>(sq.y, sq.x, sq.x, sq.w) - vec<4, T, Q>(sq.z, sq.z, sq.y, sq.w);
    // (xz, xy, yz) and (wy, wz, wx), each times 2 s.
    const vec<4, T, Q> a = vec<4, T, Q>(v.x, v.x, v.y, v.w) * vec<4, T, Q>(d.z, d.y, d.z, d.w);
    const vec<4, T, Q> b = vec<4, T, Q>(v.w) * vec<4, T, Q>(d.y, d.z, d.x, d.w);
    const vec<4, T, Q> sum = a + b;
    const vec<4, T, Q> dif = a - b;
    c0 = vec<4, T, Q>(diag.x, sum.y, dif.x, T(0));
    c1 = vec<4, T, Q>(dif.y, diag.y, sum.z, T(0));
    c2 = vec<4, T, Q>(sum.x, dif.z, diag.z, T(0));
}

/// A scale kept away from zero: magnitudes under 0.0001 become 0.0001 with the scale's sign, so
/// a mirrored scale keeps its handedness. The floor glm_recompose applies.
template<typename T>
TRANSFORM_FORCEINLINE auto floor_scale(T s) noexcept -> T
{
    const T lowest = static_cast<T>(0.0001);
    return abs(s) < lowest ? copysign(lowest, s) : s;
}

/// The rotation whose matrix has the orthonormal columns a, b, c.
template<typename T>
inline auto quat_from_axes(T ax, T ay, T az, T bx, T by, T bz, T cx, T cy, T cz) noexcept -> qua<T, defaultp>
{
    qua<T, defaultp> q;
    const T trace = ax + by + cz;
    if(trace > T(0))
    {
        T root = sqrt(trace + T(1));
        q.w = T(0.5) * root;
        root = T(0.5) / root;
        q.x = root * (bz - cy);
        q.y = root * (cx - az);
        q.z = root * (ay - bx);
    }
    else if(ax >= by && ax >= cz)
    {
        T root = sqrt(ax - by - cz + T(1));
        q.x = T(0.5) * root;
        root = T(0.5) / root;
        q.y = root * (ay + bx);
        q.z = root * (az + cx);
        q.w = root * (bz - cy);
    }
    else if(by >= cz)
    {
        T root = sqrt(by - cz - ax + T(1));
        q.y = T(0.5) * root;
        root = T(0.5) / root;
        q.z = root * (bz + cy);
        q.x = root * (bx + ay);
        q.w = root * (cx - az);
    }
    else
    {
        T root = sqrt(cz - ax - by + T(1));
        q.z = T(0.5) * root;
        root = T(0.5) / root;
        q.x = root * (cx + az);
        q.y = root * (cy + bz);
        q.w = root * (ay - bx);
    }
    return q;
}

/// The linear part of @p m (columns 0-2) applied to @p v in the arithmetic of S.
template<typename S, typename T, qualifier Q>
TRANSFORM_FORCEINLINE auto mul_columns(const mat<4, 4, T, Q>& m, const vec<3, S, Q>& v) noexcept -> vec<3, S, Q>
{
    return vec<3, S, Q>(S(m[0].x) * v.x + S(m[1].x) * v.y + S(m[2].x) * v.z,
                        S(m[0].y) * v.x + S(m[1].y) * v.y + S(m[2].y) * v.z,
                        S(m[0].z) * v.x + S(m[1].z) * v.y + S(m[2].z) * v.z);
}

/// @p m applied to @p v in the arithmetic of S.
template<typename S, typename T, qualifier Q>
TRANSFORM_FORCEINLINE auto mul3(const mat<3, 3, T, Q>& m, const vec<3, S, Q>& v) noexcept -> vec<3, S, Q>
{
    return vec<3, S, Q>(S(m[0].x) * v.x + S(m[1].x) * v.y + S(m[2].x) * v.z,
                        S(m[0].y) * v.x + S(m[1].y) * v.y + S(m[2].y) * v.z,
                        S(m[0].z) * v.x + S(m[1].z) * v.y + S(m[2].z) * v.z);
}
} // namespace transform_kernels

/**
 * @brief A transform kept as separate components (position, rotation, scale, skew) with a
 * cached matrix, whose position may be stored wider than the rest.
 */
template<typename T, typename P = T, precision Q = defaultp>
class transform_t
{
public:
    using value_type = T;
    using position_value_type = P;
    using mat3_t = mat<3, 3, T, Q>;
    using mat4_t = mat<4, 4, T, Q>;
    using vec2_t = vec<2, T, Q>;
    using vec3_t = vec<3, T, Q>;
    using vec4_t = vec<4, T, Q>;
    using quat_t = qua<T, Q>;
    using position_t = vec<3, P, Q>;
    using translation_t = position_t;
    using position_mat4_t = mat<4, 4, P, Q>;
    using length_t = typename mat4_t::length_type;
    using col_t = typename mat4_t::col_type;
    /// The Euler hint's storage: three scalars with no fourth lane (it is only ever copied).
    using hint_t = vec<3, T, packed_highp>;

    /// True when the cached matrix carries the position (P == T): get_matrix() exists.
    static constexpr bool k_matrix_holds_position = std::is_same_v<T, P>;

    transform_t() noexcept
        : matrix_(glm::identity<mat4_t>())
        , position_(glm::zero<position_t>())
        , rotation_(glm::identity<quat_t>())
        , scale_(glm::one<vec3_t>())
        , skew_(glm::zero<vec3_t>())
        , euler_hint_(T(0))
        , flags_(0)
    {
    }
    ~transform_t() = default;
    transform_t(const transform_t&) noexcept = default;
    transform_t(transform_t&&) noexcept = default;
    auto operator=(const transform_t&) noexcept -> transform_t& = default;
    auto operator=(transform_t&&) noexcept -> transform_t& = default;

    /**
     * @brief Builds a transform from a matrix. The position is read at once; rotation, scale and
     * skew are decomposed when first read. Implicit when the matrix carries the position (P == T).
     */
    explicit(!k_matrix_holds_position) transform_t(const mat4_t& m) noexcept;

    /**
     * @brief Builds a transform from a matrix in the position's precision (P wider than T).
     */
    explicit transform_t(const position_mat4_t& m) noexcept
        requires(!k_matrix_holds_position)
        : skew_(glm::zero<vec3_t>())
        , euler_hint_(T(0))
        , flags_(0)
    {
        matrix_[0] = vec4_t(m[0]);
        matrix_[1] = vec4_t(m[1]);
        matrix_[2] = vec4_t(m[2]);
        matrix_[3] = vec4_t(T(0), T(0), T(0), T(1));
        store_pos(position_t(m[3]));
        flags_ = k_components_dirty | k_matrix_defined;
    }

    /**
     * @brief Builds a transform from its components.
     */
    transform_t(const position_t& position, const quat_t& rotation, const vec3_t& scale) noexcept;

    //-------------------------------------------------------------------------
    // Position
    //-------------------------------------------------------------------------
    auto get_position() const noexcept -> const position_t&;
    auto get_translation() const noexcept -> const position_t&;
    void set_position(const position_t& position) noexcept;
    void set_position(P x, P y, P z = P(0)) noexcept;
    void set_translation(const position_t& position) noexcept;
    void set_translation(P x, P y, P z = P(0)) noexcept;
    void reset_position() noexcept;
    void reset_translation() noexcept;

    //-------------------------------------------------------------------------
    // Rotation
    //-------------------------------------------------------------------------
    auto get_rotation() const noexcept -> const quat_t&;
    void set_rotation(const quat_t& rotation) noexcept;
    void set_rotation(const vec3_t& x, const vec3_t& y, const vec3_t& z) noexcept;
    void reset_rotation() noexcept;

    auto get_rotation_euler() const noexcept -> vec3_t;
    auto get_rotation_euler_degrees() const noexcept -> vec3_t;
    auto get_rotation_euler_degrees(vec3_t hint) const noexcept -> vec3_t;
    void set_rotation_euler(const vec3_t& euler_angles) noexcept;
    void set_rotation_euler(T x, T y, T z) noexcept;
    void set_rotation_euler_degrees(const vec3_t& euler_angles) noexcept;
    void set_rotation_euler_degrees(T x, T y, T z) noexcept;

    auto has_rotation_euler_hint() const noexcept -> bool;
    void restore_euler_angles_hint_degrees(const vec3_t& hint) const noexcept;
    auto try_restore_euler_angles_hint_degrees(const vec3_t& hint) const noexcept -> bool;

    //-------------------------------------------------------------------------
    // Scale, skew, perspective
    //-------------------------------------------------------------------------
    auto get_scale() const noexcept -> const vec3_t&;
    void set_scale(const vec3_t& scale) noexcept;
    void set_scale(T x, T y, T z = T(1)) noexcept;
    void reset_scale() noexcept;

    auto get_skew() const noexcept -> const vec3_t&;
    void set_skew(const vec3_t& skew) noexcept;
    void set_skew(T x, T y, T z) noexcept;
    void reset_skew() noexcept;

    /// The perspective row; (0, 0, 0, 1) for every transform that is not a projection.
    auto get_perspective() const noexcept -> vec4_t;
    void set_perspective(const vec4_t& perspective) noexcept;
    void set_perspective(T x, T y, T z, T w) noexcept;
    void reset_perspective() noexcept;

    //-------------------------------------------------------------------------
    // Axes
    //-------------------------------------------------------------------------
    auto x_axis() const noexcept -> vec3_t;
    auto y_axis() const noexcept -> vec3_t;
    auto z_axis() const noexcept -> vec3_t;
    auto x_unit_axis() const noexcept -> vec3_t;
    auto y_unit_axis() const noexcept -> vec3_t;
    auto z_unit_axis() const noexcept -> vec3_t;

    //-------------------------------------------------------------------------
    // Edits relative to the current state
    //-------------------------------------------------------------------------
    void rotate_axis(T a, const vec3_t& v) noexcept;
    void rotate(const quat_t& q) noexcept;
    void rotate(T x, T y, T z) noexcept;
    void rotate(const vec3_t& v) noexcept;
    void rotate_local(T x, T y, T z) noexcept;
    void rotate_local(const vec3_t& v) noexcept;
    void scale(T x, T y, T z = T(1)) noexcept;
    void scale(const vec3_t& v) noexcept;
    void translate(T x, T y, T z = T(0)) noexcept;
    void translate(const vec3_t& v) noexcept;
    void translate_local(T x, T y, T z = T(0)) noexcept;
    void translate_local(const vec3_t& v) noexcept;

    //-------------------------------------------------------------------------
    // Comparison
    //-------------------------------------------------------------------------
    /// 0 when every matrix element (and, with wider positions, every position axis) is within
    /// epsilon, 1 otherwise.
    auto compare(const transform_t& rhs) const noexcept -> int;
    auto compare(const transform_t& rhs, T tolerance) const noexcept -> int;

    //-------------------------------------------------------------------------
    // Applying the transform
    //-------------------------------------------------------------------------
    auto transform_coord(const position_t& v) const noexcept -> position_t;
    auto inverse_transform_coord(const position_t& v) const noexcept -> position_t;
    auto transform_normal(const vec3_t& v) const noexcept -> vec3_t;
    auto inverse_transform_normal(const vec3_t& v) const noexcept -> vec3_t;
    /// @p v as a vector (an offset between two points): rotation, scale and skew apply, the position does not.
    auto transform_vector(const vec3_t& v) const noexcept -> vec3_t;
    /// The vector that transform_vector maps to @p v.
    auto inverse_transform_vector(const vec3_t& v) const noexcept -> vec3_t;

    auto transform_coord(const vec2_t& v) const noexcept -> vec2_t
        requires k_matrix_holds_position
    {
        return vec2_t(transform_coord(vec3_t(v, T(0))));
    }
    auto inverse_transform_coord(const vec2_t& v) const noexcept -> vec2_t
        requires k_matrix_holds_position
    {
        return vec2_t(inverse_transform_coord(vec3_t(v, T(0))));
    }
    auto transform_normal(const vec2_t& v) const noexcept -> vec2_t;
    auto inverse_transform_normal(const vec2_t& v) const noexcept -> vec2_t;

    //-------------------------------------------------------------------------
    // Construction helpers
    //-------------------------------------------------------------------------
    static auto identity() noexcept -> const transform_t&;
    static auto scaling(const vec2_t& scale) noexcept -> transform_t;
    static auto scaling(const vec3_t& scale) noexcept -> transform_t;
    static auto rotation(const quat_t& rotation) noexcept -> transform_t;
    static auto rotation_euler(const vec3_t& euler_angles) noexcept -> transform_t;
    static auto translation(const vec2_t& trans) noexcept -> transform_t;
    static auto translation(const position_t& trans) noexcept -> transform_t;

    //-------------------------------------------------------------------------
    // Matrices
    //-------------------------------------------------------------------------
    /// The matrix; only when it carries the position (P == T).
    auto get_matrix() const noexcept -> const mat4_t&
        requires k_matrix_holds_position
    {
        ensure_matrix();
        return matrix_;
    }

    /// The matrix with the translation measured from @p origin, the subtraction done in P before
    /// narrowing to T.
    auto get_matrix_relative(const position_t& origin) const noexcept -> mat4_t;

    /// The linear part (rotation * skew * scale).
    auto get_linear() const noexcept -> mat3_t;

    operator const mat4_t&() const noexcept
        requires k_matrix_holds_position
    {
        return get_matrix();
    }
    operator const mat4_t*() const noexcept
        requires k_matrix_holds_position
    {
        return &get_matrix();
    }
    operator const T*() const noexcept
        requires k_matrix_holds_position
    {
        return value_ptr(get_matrix());
    }
    auto operator[](length_t i) const noexcept -> const col_t&
        requires k_matrix_holds_position
    {
        return get_matrix()[i];
    }
    auto operator*(const vec4_t& v) const noexcept -> vec4_t
        requires k_matrix_holds_position
    {
        return get_matrix() * v;
    }

    auto operator*(const transform_t& t) const noexcept -> transform_t;
    auto operator==(const transform_t& t) const noexcept -> bool;
    auto operator!=(const transform_t& t) const noexcept -> bool;

    auto is_skew_zero() const noexcept -> bool;
    auto is_perspective_identity() const noexcept -> bool;
    auto is_scale_uniform() const noexcept -> bool;

private:
    enum flag : std::uint32_t
    {
        /// The matrix's linear part (and the position column when P == T) must be rebuilt.
        k_matrix_dirty = 1u << 0,
        /// rotation_, scale_ and skew_ must be decomposed from the matrix.
        k_components_dirty = 1u << 1,
        /// skew_ is non-zero (meaningful while the components are valid).
        k_has_skew = 1u << 2,
        /// scale_ is not uniform (meaningful while the components are valid).
        k_non_uniform = 1u << 3,
        /// The matrix is a projection; it is the truth and is never dirty.
        k_projective = 1u << 4,
        /// euler_hint_ holds the authored Euler angles.
        k_euler_hint = 1u << 5,
        /// The matrix defines the transform (built from a matrix, a general compose or inverse);
        /// cleared by a component edit. Reading the components does not change it.
        k_matrix_defined = 1u << 6,
    };

    /// Components valid, no skew, uniform scale: the quaternion paths apply.
    static constexpr std::uint32_t k_not_simple = k_components_dirty | k_has_skew | k_non_uniform | k_projective;
    /// Components valid, no skew (scale may differ per axis).
    static constexpr std::uint32_t k_not_simple_any_scale = k_components_dirty | k_has_skew | k_projective;
    /// The component paths for positions: chosen by cache state when P == T, by what defines the
    /// transform when positions are wider, so the result never depends on what was read before.
    static constexpr std::uint32_t k_position_path_blocked =
        k_matrix_holds_position ? k_not_simple : static_cast<std::uint32_t>(k_not_simple | k_matrix_defined);
    static constexpr std::uint32_t k_position_path_blocked_any_scale =
        k_matrix_holds_position ? k_not_simple_any_scale
                                : static_cast<std::uint32_t>(k_not_simple_any_scale | k_matrix_defined);

    /// A result that starts with nothing written: the caller writes the flags and every member
    /// those flags declare valid.
    struct result_tag
    {
    };
    explicit transform_t(result_tag) noexcept
    {
    }

    void ensure_components() const noexcept;
    TRANSFORM_NOINLINE void compute_euler_hint() const noexcept;
    auto pos() const noexcept -> const position_t&
    {
        return position_;
    }
    void store_pos(const position_t& p) const noexcept
    {
        position_ = transform_kernels::clear_padding(p);
    }
    void ensure_matrix() const noexcept;
    void decompose() const noexcept;
    void decompose_affine() const noexcept;
    TRANSFORM_NOINLINE void decompose_affine_floored() const noexcept;
    void decompose_projective(vec4_t* perspective_out) const noexcept;
    void build_matrix() const noexcept;
    TRANSFORM_NOINLINE void build_matrix_sheared() const noexcept;
    void update_shape_flags() const noexcept;
    void update_uniform_flag() const noexcept;
    void update_skew_flag() const noexcept;
    auto mul_linear(const position_t& v) const noexcept -> position_t;
    static auto rotate_position(const quat_t& q, const position_t& v) noexcept -> position_t;
    static auto bottom_row_is_affine(const mat4_t& m) noexcept -> bool;

    template<typename Edit>
    void edit_components(Edit&& edit) noexcept;
    template<typename Edit>
    TRANSFORM_NOINLINE void edit_projective(Edit&& edit) noexcept;

    TRANSFORM_NOINLINE auto compose_projective(const transform_t& t) const noexcept -> transform_t;
    TRANSFORM_NOINLINE void build_matrix_out_of_line() const noexcept;
    /// The columns of the matrix for the right operand of a compose. A dirty matrix of a uniform
    /// TRS is built into the cache here and returned from the registers it was built in.
    void matrix_columns(vec4_t& c0, vec4_t& c1, vec4_t& c2, vec4_t& c3) const noexcept;
    auto translation_column() const noexcept -> vec4_t;
    /// The scale with each axis kept away from zero, as a sheared or non-uniform matrix is built.
    auto floored_scale() const noexcept -> vec3_t
    {
        return transform_kernels::vec3_of(vec4_t(transform_kernels::floor_scale(scale_.x),
                                                    transform_kernels::floor_scale(scale_.y),
                                                    transform_kernels::floor_scale(scale_.z),
                                                    T(0)));
    }
    TRANSFORM_NOINLINE auto transform_normal_general(const vec3_t& v) const noexcept -> vec3_t;
    TRANSFORM_NOINLINE auto inverse_transform_normal_general(const vec3_t& v) const noexcept -> vec3_t;

    static auto rotations_equivalent(const quat_t& a, const quat_t& b) noexcept -> bool;
    static auto sync_euler_degrees_to_hint(vec3_t angles, const vec3_t& hint) noexcept -> vec3_t;

    // No default member initializers: the constructors decide what is written, so a compose or
    // inverse result is not filled with identity values it overwrites at once. The Euler hint has
    // no fourth lane: 144 bytes with float positions.
    mutable mat4_t matrix_;
    mutable position_t position_;
    mutable quat_t rotation_;
    mutable vec3_t scale_;
    mutable vec3_t skew_;
    mutable hint_t euler_hint_;
    mutable std::uint32_t flags_;

    template<typename T2, typename P2, precision Q2>
    friend auto inverse(transform_t<T2, P2, Q2> const& t) noexcept -> transform_t<T2, P2, Q2>;
    template<typename T2, typename P2, precision Q2>
    friend class transform_t;
};

//-----------------------------------------------------------------------------
// Implementation
//-----------------------------------------------------------------------------

template<typename T, typename P, precision Q>
inline transform_t<T, P, Q>::transform_t(const mat4_t& m) noexcept
    : matrix_(m)
    , skew_(glm::zero<vec3_t>())
    , euler_hint_(T(0))
    , flags_(0)
{
    if(bottom_row_is_affine(m))
    {
        store_pos(position_t(vec3_t(m[3])));
        if constexpr(!k_matrix_holds_position)
        {
            matrix_[3] = vec4_t(T(0), T(0), T(0), T(1));
        }
        flags_ = k_components_dirty | k_matrix_defined;
    }
    else
    {
        // The translation the general decompose reads: the fourth column over w (1 when w is zero).
        const T w = abs(m[3][3]) < epsilon<T>() ? T(1) : m[3][3];
        store_pos(position_t(vec3_t(m[3]) / w));
        flags_ = k_components_dirty | k_projective | k_matrix_defined;
    }
}

template<typename T, typename P, precision Q>
inline transform_t<T, P, Q>::transform_t(const position_t& position,
                                               const quat_t& rotation,
                                               const vec3_t& scale) noexcept
    : position_(transform_kernels::clear_padding(position))
    , rotation_(transform_kernels::normalize_unit(rotation))
    , scale_(transform_kernels::clear_padding(scale))
    , skew_(glm::zero<vec3_t>())
    , euler_hint_(T(0))
    , flags_(0)
{
    flags_ = k_matrix_dirty;
    update_shape_flags();
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::bottom_row_is_affine(const mat4_t& m) noexcept -> bool
{
    // The tolerance the general decompose uses to call a matrix perspective-free.
    return abs(m[0][3]) <= epsilon<T>() && abs(m[1][3]) <= epsilon<T>() && abs(m[2][3]) <= epsilon<T>() &&
           abs(m[3][3] - T(1)) <= epsilon<T>();
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::ensure_components() const noexcept
{
    if(flags_ & k_components_dirty)
    {
        decompose();
    }
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::ensure_matrix() const noexcept
{
    // Out of line: the functions that only read a built matrix stay small.
    if(flags_ & k_matrix_dirty)
    {
        build_matrix_out_of_line();
    }
}

template<typename T, typename P, precision Q>
TRANSFORM_NOINLINE void transform_t<T, P, Q>::decompose() const noexcept
{
    if(flags_ & k_projective)
    {
        decompose_projective(nullptr);
        return;
    }
    decompose_affine();
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::decompose_affine() const noexcept
{
    // Gram-Schmidt on the basis columns, the factorization glm_decompose uses (without its
    // perspective solve and the divide by m[3][3], which is 1 for an affine matrix). A decompose
    // takes as long as its chain of square roots and divides, so they wait on each other as
    // little as possible: the second axis is made orthogonal to the first with a ratio of two dot
    // products (no unit first axis needed), and the third axis is the cross product of the first
    // two, so two normalizations are in sequence instead of three. The third scale, signed, is
    // that axis dotted with the third column: negative for a mirrored basis.
    const T lowest = static_cast<T>(0.0001);
    const T m0x = matrix_[0].x, m0y = matrix_[0].y, m0z = matrix_[0].z;
    const T m1x = matrix_[1].x, m1y = matrix_[1].y, m1z = matrix_[1].z;
    const T m2x = matrix_[2].x, m2y = matrix_[2].y, m2z = matrix_[2].z;

    const T g00 = m0x * m0x + m0y * m0y + m0z * m0z;
    const T g01 = m0x * m1x + m0y * m1y + m0z * m1z;
    const T g02 = m0x * m2x + m0y * m2y + m0z * m2z;
    const T t01 = g01 / g00;
    const T ux = m1x - m0x * t01;
    const T uy = m1y - m0y * t01;
    const T uz = m1z - m0z * t01;
    const T h11 = ux * ux + uy * uy + uz * uz;
    // An axis shorter than the scale floor (or not a number) takes the floored path.
    if(!(g00 >= lowest * lowest && h11 >= lowest * lowest)) [[unlikely]]
    {
        decompose_affine_floored();
        return;
    }
    const T len_x = sqrt(g00);
    const T len_y = sqrt(h11);
    // The unit axes; the fourth lane is the reciprocal of the length.
    const vec4_t a4 = vec4_t(m0x, m0y, m0z, T(1)) / len_x;
    const vec4_t b4 = vec4_t(ux, uy, uz, T(1)) / len_y;
    T ax = a4.x, ay = a4.y, az = a4.z;
    T bx = b4.x, by = b4.y, bz = b4.z;
    const T cx = ay * bz - az * by;
    const T cy = az * bx - ax * bz;
    const T cz = ax * by - ay * bx;
    const T signed_z = cx * m2x + cy * m2y + cz * m2z;
    const T len_z = abs(signed_z);
    if(!(len_z >= lowest)) [[unlikely]]
    {
        decompose_affine_floored();
        return;
    }
    // The shears: XY from the first two columns, XZ and YZ from the third column, YZ after the
    // first axis is taken out of it.
    const T dot_a_m2 = g02 * a4.w;
    const T rx = m2x - ax * dot_a_m2;
    const T ry = m2y - ay * dot_a_m2;
    const T rz = m2z - az * dot_a_m2;
    const T inv_z = T(1) / len_z;
    const T kz = g01 * a4.w * b4.w;
    const T ky = dot_a_m2 * inv_z;
    const T kx = (bx * rx + by * ry + bz * rz) * inv_z;

    // A mirrored basis keeps its handedness in the scale: all three negative, the first two axes
    // turned, the third (their cross product) unchanged.
    T sx = len_x;
    T sy = len_y;
    if(signed_z < T(0))
    {
        sx = -sx;
        sy = -sy;
        ax = -ax;
        ay = -ay;
        az = -az;
        bx = -bx;
        by = -by;
        bz = -bz;
    }

    rotation_ = transform_kernels::quat_from_axes(ax, ay, az, bx, by, bz, cx, cy, cz);
    scale_ = transform_kernels::vec3_of(vec4_t(sx, sy, signed_z, T(0)));
    skew_ = transform_kernels::vec3_of(vec4_t(kx, ky, kz, T(0)));
    flags_ &= static_cast<std::uint32_t>(~k_components_dirty);
    update_shape_flags();
}

template<typename T, typename P, precision Q>
TRANSFORM_NOINLINE void transform_t<T, P, Q>::decompose_affine_floored() const noexcept
{
    // The same factorization one axis after the other, each length floored at 0.0001 as in
    // glm_decompose: what a matrix with a collapsed axis decomposes into.
    const T lowest = static_cast<T>(0.0001);
    T ax = matrix_[0].x, ay = matrix_[0].y, az = matrix_[0].z;
    T bx = matrix_[1].x, by = matrix_[1].y, bz = matrix_[1].z;
    T cx = matrix_[2].x, cy = matrix_[2].y, cz = matrix_[2].z;

    T sx = max(lowest, sqrt(ax * ax + ay * ay + az * az));
    T inv = T(1) / sx;
    ax *= inv;
    ay *= inv;
    az *= inv;

    T kz = ax * bx + ay * by + az * bz;
    bx -= ax * kz;
    by -= ay * kz;
    bz -= az * kz;
    T sy = max(lowest, sqrt(bx * bx + by * by + bz * bz));
    inv = T(1) / sy;
    bx *= inv;
    by *= inv;
    bz *= inv;
    kz *= inv;

    T ky = ax * cx + ay * cy + az * cz;
    cx -= ax * ky;
    cy -= ay * ky;
    cz -= az * ky;
    T kx = bx * cx + by * cy + bz * cz;
    cx -= bx * kx;
    cy -= by * kx;
    cz -= bz * kx;
    T sz = max(lowest, sqrt(cx * cx + cy * cy + cz * cz));
    inv = T(1) / sz;
    cx *= inv;
    cy *= inv;
    cz *= inv;
    ky *= inv;
    kx *= inv;

    // A mirrored basis keeps its handedness in the scale.
    const T det = ax * (by * cz - bz * cy) + ay * (bz * cx - bx * cz) + az * (bx * cy - by * cx);
    if(det < T(0))
    {
        sx = -sx;
        sy = -sy;
        sz = -sz;
        ax = -ax;
        ay = -ay;
        az = -az;
        bx = -bx;
        by = -by;
        bz = -bz;
        cx = -cx;
        cy = -cy;
        cz = -cz;
    }

    rotation_ = transform_kernels::quat_from_axes(ax, ay, az, bx, by, bz, cx, cy, cz);
    scale_ = transform_kernels::vec3_of(vec4_t(sx, sy, sz, T(0)));
    skew_ = transform_kernels::vec3_of(vec4_t(kx, ky, kz, T(0)));
    flags_ &= static_cast<std::uint32_t>(~k_components_dirty);
    update_shape_flags();
}

template<typename T, typename P, precision Q>
TRANSFORM_NOINLINE void transform_t<T, P, Q>::decompose_projective(vec4_t* perspective_out) const noexcept
{
    vec3_t scale;
    vec3_t skew;
    vec3_t translation;
    vec4_t perspective;
    glm_decompose(matrix_, scale, rotation_, translation, skew, perspective);
    scale_ = transform_kernels::clear_padding(scale);
    skew_ = transform_kernels::clear_padding(skew);
    store_pos(position_t(translation));
    if(perspective_out)
    {
        *perspective_out = perspective;
    }
    flags_ &= static_cast<std::uint32_t>(~k_components_dirty);
    update_shape_flags();
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::update_uniform_flag() const noexcept
{
    const T ax = abs(scale_.x);
    const T ay = abs(scale_.y);
    const T az = abs(scale_.z);
    const T tolerance = k_scale_uniform_tolerance<T> * max(max(max(ax, ay), az), T(1));
    const bool uniform = abs(scale_.x - scale_.y) <= tolerance && abs(scale_.y - scale_.z) <= tolerance &&
                         abs(scale_.x - scale_.z) <= tolerance;
    flags_ = static_cast<std::uint32_t>(uniform ? (flags_ & ~k_non_uniform) : (flags_ | k_non_uniform));
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::update_skew_flag() const noexcept
{
    const T tolerance = k_skew_zero_tolerance<T>;
    const bool zero = abs(skew_.x) <= tolerance && abs(skew_.y) <= tolerance && abs(skew_.z) <= tolerance;
    flags_ = static_cast<std::uint32_t>(zero ? (flags_ & ~k_has_skew) : (flags_ | k_has_skew));
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::update_shape_flags() const noexcept
{
    update_uniform_flag();
    update_skew_flag();
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::build_matrix() const noexcept
{
    // Affine only: a projection is never dirty.
    if(flags_ & (k_has_skew | k_non_uniform))
    {
        build_matrix_sheared();
        return;
    }
    // Uniform scale, no skew: the rotation columns times the one scale.
    transform_kernels::scaled_rotation_columns(rotation_, scale_.x, matrix_[0], matrix_[1], matrix_[2]);
    matrix_[3] = translation_column();
    flags_ &= static_cast<std::uint32_t>(~k_matrix_dirty);
}

template<typename T, typename P, precision Q>
TRANSFORM_NOINLINE void transform_t<T, P, Q>::build_matrix_sheared() const noexcept
{
    // R * shear * scale with the upper unitriangular shear of glm_recompose: column 1 is
    // (skew.z, 1, 0), column 2 is (skew.y, skew.x, 1). Tiny scales are floored as there.
    vec4_t c0;
    vec4_t c1;
    vec4_t c2;
    transform_kernels::scaled_rotation_columns(rotation_, T(1), c0, c1, c2);
    const T sx = transform_kernels::floor_scale(scale_.x);
    const T sy = transform_kernels::floor_scale(scale_.y);
    const T sz = transform_kernels::floor_scale(scale_.z);
    matrix_[0] = c0 * sx;
    matrix_[1] = (c0 * skew_.z + c1) * sy;
    matrix_[2] = (c0 * skew_.y + c1 * skew_.x + c2) * sz;
    matrix_[3] = translation_column();
    flags_ &= static_cast<std::uint32_t>(~k_matrix_dirty);
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::mul_linear(const position_t& v) const noexcept -> position_t
{
    if constexpr(k_matrix_holds_position)
    {
        return vec3_t(matrix_[0] * v.x + matrix_[1] * v.y + matrix_[2] * v.z);
    }
    else
    {
        ensure_matrix();
        return transform_kernels::mul_columns<P>(matrix_, v);
    }
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::rotate_position(const quat_t& q, const position_t& v) noexcept -> position_t
{
    return transform_kernels::rotate<P>(q, v);
}

template<typename T, typename P, precision Q>
template<typename Edit>
inline void transform_t<T, P, Q>::edit_components(Edit&& edit) noexcept
{
    if(flags_ & (k_projective | k_components_dirty))
    {
        if(flags_ & k_projective)
        {
            edit_projective(edit);
            return;
        }
        decompose();
    }
    edit();
    flags_ = static_cast<std::uint32_t>((flags_ & ~k_matrix_defined) | k_matrix_dirty);
}

template<typename T, typename P, precision Q>
template<typename Edit>
TRANSFORM_NOINLINE void transform_t<T, P, Q>::edit_projective(Edit&& edit) noexcept
{
    vec4_t perspective;
    decompose_projective(&perspective);
    edit();
    glm_recompose(matrix_, scale_, rotation_, vec3_t(pos()), skew_, perspective);
    if(bottom_row_is_affine(matrix_))
    {
        flags_ &= static_cast<std::uint32_t>(~k_projective);
        if constexpr(!k_matrix_holds_position)
        {
            matrix_[3] = vec4_t(T(0), T(0), T(0), T(1));
        }
    }
    update_shape_flags();
}

//-----------------------------------------------------------------------------
// Position
//-----------------------------------------------------------------------------
template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::get_position() const noexcept -> const position_t&
{
    return position_;
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::get_translation() const noexcept -> const position_t&
{
    return get_position();
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::set_position(const position_t& position) noexcept
{
    if(flags_ & k_projective)
    {
        edit_projective([&] { store_pos(position); });
        return;
    }
    if constexpr(k_matrix_holds_position)
    {
        // The linear part is untouched, so the matrix only needs its translation column (a dirty
        // matrix gets the column again when it is built). One register feeds both stores.
        const vec4_t column(position, T(1));
        matrix_[3] = column;
        position_ = transform_kernels::vec3_of(column);
    }
    else
    {
        store_pos(position);
    }
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::set_position(P x, P y, P z) noexcept
{
    set_position(position_t(x, y, z));
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::set_translation(const position_t& position) noexcept
{
    set_position(position);
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::set_translation(P x, P y, P z) noexcept
{
    set_position(position_t(x, y, z));
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::reset_position() noexcept
{
    set_position(glm::zero<position_t>());
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::reset_translation() noexcept
{
    reset_position();
}

//-----------------------------------------------------------------------------
// Rotation
//-----------------------------------------------------------------------------
template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::rotations_equivalent(const quat_t& a, const quat_t& b) noexcept -> bool
{
    constexpr T k_rotation_dot_tolerance = T(1) - T(1e-4);
    return abs(dot(a, b)) >= k_rotation_dot_tolerance;
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::sync_euler_degrees_to_hint(vec3_t angles, const vec3_t& hint) noexcept -> vec3_t
{
    auto repeat_working = [](T t, T length) -> T
    {
        return (t - (floor(t / length) * length));
    };
    angles.x = repeat_working(angles.x - hint.x + T(180), T(360)) + hint.x - T(180);
    angles.y = repeat_working(angles.y - hint.y + T(180), T(360)) + hint.y - T(180);
    angles.z = repeat_working(angles.z - hint.z + T(180), T(360)) + hint.z - T(180);
    return angles;
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::get_rotation() const noexcept -> const quat_t&
{
    ensure_components();
    return rotation_;
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::set_rotation(const quat_t& rotation) noexcept
{
    edit_components(
        [&]
        {
            const quat_t normalized = transform_kernels::normalize_unit(rotation);
            // The authored Euler angles stay while they still describe the new orientation. A
            // quaternion from Euler angles is unit by construction (products of sines and cosines).
            if(flags_ & k_euler_hint)
            {
                const quat_t from_hint(radians(vec3_t(euler_hint_)));
                if(!rotations_equivalent(from_hint, normalized))
                {
                    flags_ &= static_cast<std::uint32_t>(~k_euler_hint);
                }
            }
            rotation_ = normalized;
        });
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::set_rotation(const vec3_t& x, const vec3_t& y, const vec3_t& z) noexcept
{
    set_rotation(quat_cast(mat3_t(x, y, z)));
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::reset_rotation() noexcept
{
    set_rotation_euler_degrees(glm::zero<vec3_t>());
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::get_rotation_euler() const noexcept -> vec3_t
{
    return radians(get_rotation_euler_degrees());
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::get_rotation_euler_degrees() const noexcept -> vec3_t
{
    if((flags_ & (k_components_dirty | k_euler_hint)) != k_euler_hint)
    {
        compute_euler_hint();
    }
    return vec3_t(euler_hint_);
}

template<typename T, typename P, precision Q>
TRANSFORM_NOINLINE void transform_t<T, P, Q>::compute_euler_hint() const noexcept
{
    ensure_components();
    if(!(flags_ & k_euler_hint))
    {
        euler_hint_ = hint_t(degrees(eulerAngles(rotation_)));
        flags_ |= k_euler_hint;
    }
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::get_rotation_euler_degrees(vec3_t hint) const noexcept -> vec3_t
{
    ensure_components();
    return sync_euler_degrees_to_hint(degrees(eulerAngles(rotation_)), hint);
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::set_rotation_euler(const vec3_t& euler_angles) noexcept
{
    edit_components(
        [&]
        {
            rotation_ = transform_kernels::normalize_unit(quat_t(euler_angles));
            euler_hint_ = hint_t(degrees(euler_angles));
            flags_ |= k_euler_hint;
        });
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::set_rotation_euler(T x, T y, T z) noexcept
{
    set_rotation_euler(vec3_t(x, y, z));
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::set_rotation_euler_degrees(const vec3_t& euler_angles) noexcept
{
    edit_components(
        [&]
        {
            rotation_ = transform_kernels::normalize_unit(quat_t(radians(euler_angles)));
            euler_hint_ = hint_t(euler_angles);
            flags_ |= k_euler_hint;
        });
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::set_rotation_euler_degrees(T x, T y, T z) noexcept
{
    set_rotation_euler_degrees(vec3_t(x, y, z));
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::has_rotation_euler_hint() const noexcept -> bool
{
    return (flags_ & k_euler_hint) != 0;
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::restore_euler_angles_hint_degrees(const vec3_t& hint) const noexcept
{
    euler_hint_ = hint_t(hint);
    flags_ |= k_euler_hint;
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::try_restore_euler_angles_hint_degrees(const vec3_t& hint) const noexcept
    -> bool
{
    ensure_components();
    const quat_t from_hint = transform_kernels::normalize_unit(quat_t(radians(hint)));
    if(!rotations_equivalent(from_hint, rotation_))
    {
        return false;
    }
    euler_hint_ = hint_t(hint);
    flags_ |= k_euler_hint;
    return true;
}

//-----------------------------------------------------------------------------
// Scale, skew, perspective
//-----------------------------------------------------------------------------
template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::get_scale() const noexcept -> const vec3_t&
{
    ensure_components();
    return scale_;
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::set_scale(const vec3_t& scale) noexcept
{
    edit_components(
        [&]
        {
            scale_ = transform_kernels::clear_padding(scale);
            update_uniform_flag();
        });
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::set_scale(T x, T y, T z) noexcept
{
    set_scale(vec3_t(x, y, z));
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::reset_scale() noexcept
{
    set_scale(T(1), T(1), T(1));
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::get_skew() const noexcept -> const vec3_t&
{
    ensure_components();
    return skew_;
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::set_skew(const vec3_t& skew) noexcept
{
    edit_components(
        [&]
        {
            skew_ = transform_kernels::clear_padding(skew);
            update_skew_flag();
        });
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::set_skew(T x, T y, T z) noexcept
{
    set_skew(vec3_t(x, y, z));
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::reset_skew() noexcept
{
    set_skew(T(0), T(0), T(0));
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::get_perspective() const noexcept -> vec4_t
{
    if(!(flags_ & k_projective))
    {
        return vec4_t(T(0), T(0), T(0), T(1));
    }
    vec4_t perspective;
    decompose_projective(&perspective);
    return perspective;
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::set_perspective(const vec4_t& perspective) noexcept
{
    const bool identity =
        glm::all(glm::epsilonEqual(perspective, vec4_t(T(0), T(0), T(0), T(1)), glm::epsilon<T>()));
    if(identity && !(flags_ & k_projective))
    {
        return;
    }
    // Leave the affine representation: decompose, recompose with the new perspective row.
    vec4_t ignored;
    if(flags_ & k_projective)
    {
        decompose_projective(&ignored);
    }
    else
    {
        ensure_components();
    }
    glm_recompose(matrix_, scale_, rotation_, vec3_t(pos()), skew_, perspective);
    if(bottom_row_is_affine(matrix_))
    {
        flags_ = static_cast<std::uint32_t>((flags_ & ~(k_projective | k_matrix_dirty)) | k_matrix_defined);
        if constexpr(!k_matrix_holds_position)
        {
            matrix_[3] = vec4_t(T(0), T(0), T(0), T(1));
        }
    }
    else
    {
        flags_ = static_cast<std::uint32_t>((flags_ & ~k_matrix_dirty) | k_projective | k_matrix_defined);
    }
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::set_perspective(T x, T y, T z, T w) noexcept
{
    set_perspective(vec4_t(x, y, z, w));
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::reset_perspective() noexcept
{
    set_perspective(T(0), T(0), T(0), T(1));
}

//-----------------------------------------------------------------------------
// Axes
//-----------------------------------------------------------------------------
template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::x_axis() const noexcept -> vec3_t
{
    ensure_matrix();
    return transform_kernels::vec3_of(matrix_[0]);
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::y_axis() const noexcept -> vec3_t
{
    ensure_matrix();
    return transform_kernels::vec3_of(matrix_[1]);
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::z_axis() const noexcept -> vec3_t
{
    ensure_matrix();
    return transform_kernels::vec3_of(matrix_[2]);
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::x_unit_axis() const noexcept -> vec3_t
{
    return normalize(x_axis());
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::y_unit_axis() const noexcept -> vec3_t
{
    return normalize(y_axis());
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::z_unit_axis() const noexcept -> vec3_t
{
    return normalize(z_axis());
}

//-----------------------------------------------------------------------------
// Relative edits
//-----------------------------------------------------------------------------
template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::rotate(const quat_t& q) noexcept
{
    set_rotation(q * get_rotation());
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::rotate_axis(T a, const vec3_t& v) noexcept
{
    const T axis_len2 = dot(v, v);
    if(axis_len2 <= epsilon<T>())
    {
        return;
    }
    set_rotation(glm::angleAxis(a, v / sqrt(axis_len2)) * get_rotation());
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::rotate(T x, T y, T z) noexcept
{
    rotate(vec3_t(x, y, z));
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::rotate(const vec3_t& v) noexcept
{
    set_rotation(quat_t(v) * get_rotation());
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::rotate_local(T x, T y, T z) noexcept
{
    rotate_local(vec3_t(x, y, z));
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::rotate_local(const vec3_t& v) noexcept
{
    set_rotation(get_rotation() * quat_t(v));
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::scale(T x, T y, T z) noexcept
{
    scale(vec3_t(x, y, z));
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::scale(const vec3_t& v) noexcept
{
    set_scale(get_scale() * v);
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::translate(T x, T y, T z) noexcept
{
    translate(vec3_t(x, y, z));
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::translate(const vec3_t& v) noexcept
{
    set_position(get_position() + position_t(v));
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::translate_local(T x, T y, T z) noexcept
{
    translate_local(vec3_t(x, y, z));
}

template<typename T, typename P, precision Q>
inline void transform_t<T, P, Q>::translate_local(const vec3_t& v) noexcept
{
    // The scalar rotation: glm's q * v reads three lanes of the stored rotation as one register
    // that reaches into the member behind it, and waits when the rotation was just written.
    ensure_components();
    translate(transform_kernels::rotate<T>(rotation_, v));
}

//-----------------------------------------------------------------------------
// Comparison
//-----------------------------------------------------------------------------
template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::compare(const transform_t& rhs) const noexcept -> int
{
    return compare(rhs, epsilon<T>());
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::compare(const transform_t& rhs, T tolerance) const noexcept -> int
{
    ensure_matrix();
    rhs.ensure_matrix();
    for(length_t i = 0; i < 4; ++i)
    {
        const vec4_t diff = matrix_[i] - rhs.matrix_[i];
        if(!glm::all(glm::epsilonEqual(diff, glm::zero<vec4_t>(), tolerance)))
        {
            return 1;
        }
    }
    if constexpr(!k_matrix_holds_position)
    {
        const position_t diff = get_position() - rhs.get_position();
        if(!glm::all(glm::epsilonEqual(diff, glm::zero<position_t>(), P(tolerance))))
        {
            return 1;
        }
    }
    return 0;
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::operator==(const transform_t& t) const noexcept -> bool
{
    return compare(t, epsilon<T>()) == 0;
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::operator!=(const transform_t& t) const noexcept -> bool
{
    return compare(t, epsilon<T>()) != 0;
}

//-----------------------------------------------------------------------------
// Applying the transform
//-----------------------------------------------------------------------------
template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::transform_coord(const position_t& v) const noexcept -> position_t
{
    // Always through the matrix (built on first use), so a point lands on the same coordinates
    // whatever was cached before.
    if(flags_ & k_projective) [[unlikely]]
    {
        const vec4_t result = matrix_ * vec4_t(vec3_t(v), T(1));
        if(abs(result.w) <= epsilon<T>())
        {
            return position_t(vec3_t(result));
        }
        return position_t(vec3_t(result) / result.w);
    }
    ensure_matrix();
    return pos() + mul_linear(v);
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::inverse_transform_coord(const position_t& v) const noexcept -> position_t
{
    if(!(flags_ & k_position_path_blocked_any_scale))
    {
        const vec3_t inv_scale = T(1) / floored_scale();
        if constexpr(k_matrix_holds_position)
        {
            // glm's SIMD rotation: the operands are already vectors in registers.
            return (glm::conjugate(rotation_) * (v - pos())) * inv_scale;
        }
        else
        {
            return rotate_position(transform_kernels::conjugate(rotation_), v - pos()) * position_t(inv_scale);
        }
    }
    if(flags_ & k_projective)
    {
        const vec4_t result = glm::inverse(matrix_) * vec4_t(vec3_t(v), T(1));
        if(abs(result.w) <= epsilon<T>())
        {
            return position_t(vec3_t(result));
        }
        return position_t(vec3_t(result) / result.w);
    }
    ensure_matrix();
    const mat3_t inv = glm::inverse(mat3_t(matrix_));
    const position_t d = v - pos();
    if constexpr(k_matrix_holds_position)
    {
        return inv * d;
    }
    else
    {
        return transform_kernels::mul3<P>(inv, d);
    }
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::transform_normal(const vec3_t& v) const noexcept -> vec3_t
{
    if(!(flags_ & k_not_simple))
    {
        // Uniform scale s: the normal matrix is (1/s) * R; keep the input length.
        const vec3_t rotated = transform_kernels::rotate<T>(rotation_, v);
        return scale_.x < T(0) ? -rotated : rotated;
    }
    return transform_normal_general(v);
}

template<typename T, typename P, precision Q>
TRANSFORM_NOINLINE auto transform_t<T, P, Q>::transform_normal_general(const vec3_t& v) const noexcept -> vec3_t
{
    // The inverse transpose is the matrix of cofactors (the cross products of the columns) over
    // the determinant. The result is brought back to the input's length, so only the sign of the
    // determinant matters and nothing is divided by it; a singular matrix gives no infinities.
    ensure_matrix();
    const vec3_t m0 = transform_kernels::vec3_of(matrix_[0]);
    const vec3_t m1 = transform_kernels::vec3_of(matrix_[1]);
    const vec3_t m2 = transform_kernels::vec3_of(matrix_[2]);
    const vec3_t c0 = cross(m1, m2);
    vec3_t result = c0 * v.x + cross(m2, m0) * v.y + cross(m0, m1) * v.z;
    const vec3_t volume = m0 * c0;
    if(volume.x + volume.y + volume.z < T(0))
    {
        result = -result;
    }
    const T result_len2 = dot(result, result);
    const T input_len2 = dot(v, v);
    if(result_len2 > T(0) && input_len2 > T(0))
    {
        result *= sqrt(input_len2 / result_len2);
    }
    return result;
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::inverse_transform_normal(const vec3_t& v) const noexcept -> vec3_t
{
    if(!(flags_ & k_not_simple))
    {
        const vec3_t rotated = transform_kernels::rotate<T>(transform_kernels::conjugate(rotation_), v);
        return scale_.x < T(0) ? -rotated : rotated;
    }
    return inverse_transform_normal_general(v);
}

template<typename T, typename P, precision Q>
TRANSFORM_NOINLINE auto transform_t<T, P, Q>::inverse_transform_normal_general(const vec3_t& v) const noexcept
    -> vec3_t
{
    ensure_matrix();
    vec3_t result = glm::transpose(mat3_t(matrix_)) * v;
    const T result_len2 = dot(result, result);
    const T input_len2 = dot(v, v);
    if(result_len2 > T(0) && input_len2 > T(0))
    {
        result *= sqrt(input_len2 / result_len2);
    }
    return result;
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::transform_vector(const vec3_t& v) const noexcept -> vec3_t
{
    // Through the matrix, as transform_coord, so a vector lands where the difference of two points would.
    ensure_matrix();
    return transform_kernels::vec3_of(matrix_[0] * v.x + matrix_[1] * v.y + matrix_[2] * v.z);
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::inverse_transform_vector(const vec3_t& v) const noexcept -> vec3_t
{
    if(!(flags_ & k_position_path_blocked_any_scale))
    {
        const vec3_t inv_scale = T(1) / floored_scale();
        return transform_kernels::rotate<T>(transform_kernels::conjugate(rotation_), v) * inv_scale;
    }
    ensure_matrix();
    return glm::inverse(mat3_t(matrix_)) * v;
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::transform_normal(const vec2_t& v) const noexcept -> vec2_t
{
    return vec2_t(transform_normal(vec3_t(v, T(0))));
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::inverse_transform_normal(const vec2_t& v) const noexcept -> vec2_t
{
    return vec2_t(inverse_transform_normal(vec3_t(v, T(0))));
}

//-----------------------------------------------------------------------------
// Construction helpers
//-----------------------------------------------------------------------------
template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::identity() noexcept -> const transform_t&
{
    static const transform_t value;
    return value;
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::scaling(const vec2_t& scale) noexcept -> transform_t
{
    return scaling(vec3_t(scale, T(1)));
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::scaling(const vec3_t& scale) noexcept -> transform_t
{
    transform_t result;
    result.set_scale(scale);
    return result;
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::rotation(const quat_t& rotation) noexcept -> transform_t
{
    transform_t result;
    result.set_rotation(rotation);
    return result;
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::rotation_euler(const vec3_t& euler_angles) noexcept -> transform_t
{
    transform_t result;
    result.set_rotation_euler(euler_angles);
    return result;
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::translation(const vec2_t& trans) noexcept -> transform_t
{
    return translation(position_t(vec3_t(trans, T(0))));
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::translation(const position_t& trans) noexcept -> transform_t
{
    transform_t result;
    result.set_position(trans);
    return result;
}

//-----------------------------------------------------------------------------
// Matrices
//-----------------------------------------------------------------------------
template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::get_matrix_relative(const position_t& origin) const noexcept -> mat4_t
{
    if(flags_ & k_projective)
    {
        return glm::translate(glm::identity<mat4_t>(), -vec3_t(origin)) * matrix_;
    }
    ensure_matrix();
    mat4_t result = matrix_;
    result[3] = vec4_t(vec3_t(pos() - origin), T(1));
    return result;
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::get_linear() const noexcept -> mat3_t
{
    ensure_matrix();
    return mat3_t(matrix_);
}

template<typename T, typename P, precision Q>
TRANSFORM_FORCEINLINE auto transform_t<T, P, Q>::operator*(const transform_t& t) const noexcept
    -> transform_t
{
    // Scene-graph world = parent * local, always as the product of the two matrices, so the result
    // never depends on what either operand had cached. The components come along when they compose
    // directly (no skew or pending decompose on either side, uniform parent scale:
    // T1 R1 (s I) T2 R2 S2 = T(p1 + R1 s p2) (R1 R2) (s S2)); otherwise they wait for a decompose.
    //
    // Inlined into the caller, and the result's address is never taken here, so `a = b * c` writes
    // the members of a directly instead of filling a temporary and copying it. On every path the
    // result's flags say its matrix is built, so reading that matrix needs no call either.
    //
    // The right operand is usually a local that was just edited: its matrix is built here and
    // multiplied from the registers it was built in (a matrix read back right after it is stored
    // waits for the stores).
    if((flags_ | t.flags_) & k_projective) [[unlikely]]
    {
        const transform_t projected = compose_projective(t);
        transform_t copy(projected);
        copy.flags_ &= static_cast<std::uint32_t>(~k_matrix_dirty);
        return copy;
    }
    if(flags_ & k_matrix_dirty) [[unlikely]]
    {
        build_matrix_out_of_line();
    }
    vec4_t b0;
    vec4_t b1;
    vec4_t b2;
    vec4_t b3;
    t.matrix_columns(b0, b1, b2, b3);
    transform_t result{result_tag{}};
    if constexpr(k_matrix_holds_position)
    {
        result.matrix_ = transform_kernels::mul_affine(matrix_, b0, b1, b2, b3);
        result.position_ = transform_kernels::vec3_of(result.matrix_[3]);
    }
    else
    {
        result.matrix_ = transform_kernels::mul_linear_parts(matrix_, b0, b1, b2);
        result.store_pos(pos() + transform_kernels::mul_columns<P>(matrix_, t.pos()));
    }
    if(((flags_ | t.flags_) & (k_components_dirty | k_has_skew)) == 0 && !(flags_ & k_non_uniform))
    {
        result.rotation_ = transform_kernels::mul_unit(rotation_, t.rotation_);
        result.scale_ = scale_ * t.scale_;
        result.skew_ = glm::zero<vec3_t>();
        result.flags_ = (t.flags_ & k_non_uniform) | k_matrix_defined;
    }
    else
    {
        result.flags_ = k_components_dirty | k_matrix_defined;
    }
    return result;
}

template<typename T, typename P, precision Q>
TRANSFORM_NOINLINE void transform_t<T, P, Q>::build_matrix_out_of_line() const noexcept
{
    build_matrix();
}

template<typename T, typename P, precision Q>
TRANSFORM_FORCEINLINE void transform_t<T, P, Q>::matrix_columns(vec4_t& c0,
                                                                     vec4_t& c1,
                                                                     vec4_t& c2,
                                                                     vec4_t& c3) const noexcept
{
    // Affine only. The fourth column is the position when the matrix carries it, else (0, 0, 0, 1).
    if(flags_ & k_matrix_dirty)
    {
        if(!(flags_ & (k_has_skew | k_non_uniform)))
        {
            transform_kernels::scaled_rotation_columns(rotation_, scale_.x, c0, c1, c2);
            c3 = translation_column();
            matrix_[0] = c0;
            matrix_[1] = c1;
            matrix_[2] = c2;
            matrix_[3] = c3;
            flags_ &= static_cast<std::uint32_t>(~k_matrix_dirty);
            return;
        }
        build_matrix_sheared();
    }
    c0 = matrix_[0];
    c1 = matrix_[1];
    c2 = matrix_[2];
    c3 = matrix_[3];
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::translation_column() const noexcept -> vec4_t
{
    if constexpr(k_matrix_holds_position)
    {
        return vec4_t(pos(), T(1));
    }
    else
    {
        return vec4_t(T(0), T(0), T(0), T(1));
    }
}

template<typename T, typename P, precision Q>
TRANSFORM_NOINLINE auto transform_t<T, P, Q>::compose_projective(const transform_t& t) const noexcept
    -> transform_t
{
    // Projections stay full 4x4 products of the matrices, translation included.
    auto full = [](const transform_t& x) -> mat4_t
    {
        x.ensure_matrix();
        if constexpr(k_matrix_holds_position)
        {
            return x.matrix_;
        }
        else
        {
            if(x.flags_ & k_projective)
            {
                return x.matrix_;
            }
            mat4_t m = x.matrix_;
            m[3] = vec4_t(vec3_t(x.pos()), T(1));
            return m;
        }
    };
    return transform_t(full(*this) * full(t));
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::is_skew_zero() const noexcept -> bool
{
    ensure_components();
    return !(flags_ & k_has_skew);
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::is_perspective_identity() const noexcept -> bool
{
    return !(flags_ & k_projective);
}

template<typename T, typename P, precision Q>
inline auto transform_t<T, P, Q>::is_scale_uniform() const noexcept -> bool
{
    ensure_components();
    return !(flags_ & k_non_uniform);
}

template<typename T, typename P, precision Q>
auto inverse(transform_t<T, P, Q> const& t) noexcept -> transform_t<T, P, Q>
{
    using type = transform_t<T, P, Q>;
    using position_t = typename type::position_t;
    using vec3_t = typename type::vec3_t;
    using vec4_t = typename type::vec4_t;
    using mat3_t = typename type::mat3_t;

    if(!(t.flags_ & type::k_position_path_blocked))
    {
        // Uniform TRS: the component inverse.
        type result{typename type::result_tag{}};
        const T inv_scale = T(1) / transform_kernels::floor_scale(t.scale_.x);
        result.rotation_ = transform_kernels::conjugate(t.rotation_);
        result.scale_ = vec3_t(inv_scale);
        result.skew_ = glm::zero<vec3_t>();
        if constexpr(type::k_matrix_holds_position)
        {
            result.store_pos(-type::rotate_position(result.rotation_, t.pos() * P(inv_scale)));
            result.flags_ = type::k_matrix_dirty;
        }
        else
        {
            // The position goes through the stored linear part: built here, in registers.
            result.flags_ = type::k_matrix_dirty;
            result.build_matrix();
            result.store_pos(-result.mul_linear(t.pos()));
        }
        return result;
    }
    if(t.flags_ & type::k_projective)
    {
        return type(glm::inverse(t.matrix_));
    }
    t.ensure_matrix();
    const mat3_t inv = glm::inverse(mat3_t(t.matrix_));
    type result{typename type::result_tag{}};
    result.matrix_[0] = vec4_t(inv[0], T(0));
    result.matrix_[1] = vec4_t(inv[1], T(0));
    result.matrix_[2] = vec4_t(inv[2], T(0));
    if constexpr(type::k_matrix_holds_position)
    {
        result.store_pos(-(inv * t.pos()));
        result.matrix_[3] = vec4_t(result.pos(), T(1));
    }
    else
    {
        result.store_pos(-transform_kernels::mul3<P>(inv, t.pos()));
        result.matrix_[3] = vec4_t(T(0), T(0), T(0), T(1));
    }
    result.flags_ = type::k_components_dirty | type::k_matrix_defined;
    return result;
}

using transform = transform_t<float>;

extern template class transform_t<float, float, defaultp>;

template<typename T>
inline auto to_string(const T& v) -> std::string
{
    return glm::to_string(v);
}
} // namespace math

namespace glm
{
namespace detail
{
template<typename T, typename P, qualifier Q>
struct compute_to_string<math::transform_t<T, P, Q>>
{
    GLM_FUNC_QUALIFIER static auto call(math::transform_t<T, P, Q> const& x) -> std::string
    {
        char const* prefix_str = prefix<T>::value();
        return detail::format("%stransform((translation:%s, scale:%s, rotation:%s/rotation_euler:%s, skew:%s))",
                              prefix_str,
                              to_string(x.get_position()).c_str(),
                              to_string(x.get_scale()).c_str(),
                              to_string(x.get_rotation()).c_str(),
                              to_string(x.get_rotation_euler()).c_str(),
                              to_string(x.get_skew()).c_str());
    }
};
} // namespace detail

// Rotation that turns +Z onto forward and +Y toward upwards (approx); backs the script Quaternion.LookRotation.
inline auto look_rotation(const glm::vec3& forward, const glm::vec3& upwards) -> glm::quat
{
    // Build the basis directly: +Z = forward by construction, independent of the
    // GLM_FORCE_LEFT_HANDED configuration that glm::lookAt depends on, and safe for
    // the degenerate inputs (zero forward, forward parallel to upwards) that made
    // the lookAt-based version produce NaNs.
    constexpr float k_degenerate_len2 = 1e-12f;
    const float forward_len2 = glm::dot(forward, forward);
    if(forward_len2 < k_degenerate_len2)
    {
        return glm::identity<glm::quat>();
    }
    const glm::vec3 f = forward / glm::sqrt(forward_len2);
    glm::vec3 right = glm::cross(upwards, f);
    float right_len2 = glm::dot(right, right);
    if(right_len2 < k_degenerate_len2)
    {
        // upwards is collinear with forward (or zero): substitute any axis
        // orthogonal to f so the basis stays well defined.
        const glm::vec3 orth =
            glm::abs(f.x) > glm::abs(f.z) ? glm::vec3(-f.y, f.x, 0.0f) : glm::vec3(0.0f, -f.z, f.y);
        right = glm::cross(orth, f);
        right_len2 = glm::dot(right, right);
    }
    right /= glm::sqrt(right_len2);
    const glm::vec3 up = glm::cross(f, right);
    return glm::quat_cast(glm::mat3(right, up, f));
}

inline auto from_to_rotation(const glm::vec3& from, const glm::vec3& to) -> glm::quat
{
    // 1. Guard degenerate inputs (a zero vector has no direction) and normalize.
    constexpr float k_degenerate_len2 = 1e-12f;
    const float from_len2 = glm::dot(from, from);
    const float to_len2 = glm::dot(to, to);
    if(from_len2 < k_degenerate_len2 || to_len2 < k_degenerate_len2)
    {
        return glm::identity<glm::quat>();
    }
    const glm::vec3 f = from / glm::sqrt(from_len2);
    const glm::vec3 t = to / glm::sqrt(to_len2);

    const float dot_product = glm::dot(f, t);

    // 2. Nearly opposite: a 180-degree turn about any axis orthogonal to 'from'.
    // The half-vector construction below degenerates to a zero quaternion here.
    if(dot_product <= -1.0f + 1e-6f)
    {
        glm::vec3 orth = glm::abs(f.x) > glm::abs(f.z) ? glm::vec3(-f.y, f.x, 0.0f) : glm::vec3(0.0f, -f.z, f.y);
        orth = glm::normalize(orth);
        return glm::angleAxis(glm::pi<float>(), orth);
    }

    // 3. Half-vector construction: normalize(w = 1 + dot, xyz = cross(f, t)) rotates
    // f onto t. Trig-free and more accurate near dot = +/-1 than acos + angleAxis
    // (also exact for the nearly-parallel case, which needs no special handling).
    const glm::quat q(1.0f + dot_product, glm::cross(f, t));
    return glm::normalize(q);
}

inline void set_position_relative(glm::mat4& matrix, const glm::vec3& camera_position)
{
    matrix[3] = glm::vec4(glm::vec3(matrix[3]) - camera_position, 1.0f);
}
} // namespace glm
