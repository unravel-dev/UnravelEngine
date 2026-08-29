#pragma once
//-----------------------------------------------------------------------------
// transform Header Includes
//-----------------------------------------------------------------------------
#include "matrix_recompose.hpp"
#include <string>

namespace math
{
using namespace glm;

// The `inline` keyword matters here: transform_t is explicitly instantiated
// (extern template below), which suppresses implicit instantiation - without
// `inline` every call from other TUs must go through the out-of-line symbols in
// transform.cpp and small hot getters cannot be inlined (short of LTO).
#ifdef _MSC_VER
#define TRANSFORM_INLINE inline
#elif defined(__GNUC__) || defined(__clang__)
#define TRANSFORM_INLINE inline
#else
#define TRANSFORM_INLINE inline
#endif

// Big cold bodies (the WebKit matrix decompose, general-path normal math) are
// pinned out of line: letting them inline into hot loops measurably regressed
// RelWithDebInfo (decompose 66 -> 93 ns/op in the transform suite bench), while
// the thin dirty-flag checks around them benefit from inlining.
#if defined(_MSC_VER) && !defined(__clang__)
#define TRANSFORM_NOINLINE __declspec(noinline)
#else
#define TRANSFORM_NOINLINE __attribute__((noinline))
#endif

/// Absolute tolerance for treating decomposed skew as zero. Float decompose of a
/// genuine TRS matrix leaves skew residue up to ~1e-6; glm::epsilon (~1.19e-7) sits
/// below that noise floor, which permanently knocked every matrix-roundtripped
/// transform off the simplified fast paths and made recompose multiply noise shear
/// matrices back in.
template<typename T>
inline constexpr T k_skew_zero_tolerance = static_cast<T>(1e-5);

/// Relative tolerance for scale uniformity. Decompose noise on the extracted scale
/// grows with its magnitude, so an absolute epsilon misclassifies large scales;
/// the comparison floors the reference at 1 so tiny scales use it absolutely.
template<typename T>
inline constexpr T k_scale_uniform_tolerance = static_cast<T>(1e-5);

/**
 * @brief General purpose transformation class designed to maintain each component of
 * the transformation separate (translation, rotation, scale and shear) whilst
 * providing much of the same functionality provided by standard matrices.
 */
template<typename T, precision Q = defaultp>
class transform_t
{
public:
    using mat3_t = mat<3, 3, T, Q>;
    using mat4_t = mat<4, 4, T, Q>;
    using vec2_t = vec<2, T, Q>;
    using vec3_t = vec<3, T, Q>;
    using vec4_t = vec<4, T, Q>;
    using quat_t = qua<T, Q>;
    using length_t = typename mat4_t::length_type;
    using col_t = typename mat4_t::col_type;

    //-------------------------------------------------------------------------
    // Constructors & Destructors
    //-------------------------------------------------------------------------
    /**
     * @brief Default constructor.
     */
    transform_t() = default;
    ~transform_t() = default;

    /**
     * @brief Copy constructor.
     */
    transform_t(const transform_t& t) noexcept = default;

    /**
     * @brief Move constructor.
     */
    transform_t(transform_t&& t) noexcept = default;

    /**
     * @brief Copy assignment operator.
     */
    auto operator=(const transform_t& m) noexcept -> transform_t& = default;

    /**
     * @brief Move assignment operator.
     */
    auto operator=(transform_t&& m) noexcept -> transform_t& = default;

    /**
     * @brief Constructor from a 4x4 matrix.
     */
    transform_t(const mat4_t& m) noexcept;

    //-------------------------------------------------------------------------
    // Translation Methods
    //-------------------------------------------------------------------------

    /**
     * @brief Get the position component.
     * @return A constant reference to the position vector.
     */
    auto get_position() const noexcept -> const vec3_t&;

    /**
     * @brief Get the translation component.
     * @return A constant reference to the translation vector.
     */
    auto get_translation() const noexcept -> const vec3_t&;

    /**
     * @brief Set the translation component.
     * @param position The new translation vector.
     */
    void set_translation(const vec3_t& position) noexcept;

    /**
     * @brief Set the translation component.
     * @param x The X component.
     * @param y The Y component.
     * @param z The Z component.
     */
    void set_translation(T x, T y, T z = T(0)) noexcept;

    /**
     * @brief Reset the translation component to zero.
     */
    void reset_translation() noexcept;

    /**
     * @brief Set the position component.
     * @param position The new position vector.
     */
    void set_position(const vec3_t& position) noexcept;

    /**
     * @brief Set the position component.
     * @param x The X component.
     * @param y The Y component.
     * @param z The Z component.
     */
    void set_position(T x, T y, T z = T(0)) noexcept;

    /**
     * @brief Reset the position component to zero.
     */
    void reset_position() noexcept;

    //-------------------------------------------------------------------------
    // Rotation Methods
    //-------------------------------------------------------------------------

    /**
     * @brief Get the rotation component as Euler angles (radians).
     * Prefers the stored Euler hint when it still matches the quaternion.
     */
    auto get_rotation_euler() const noexcept -> vec3_t;

    /**
     * @brief Get the rotation component as Euler angles in degrees.
     *
     * Uses a Unity-style Euler hint so inspector/script edits keep the typed
     * axes stable across frames (quaternion decomposition is not unique).
     */
    auto get_rotation_euler_degrees() const noexcept -> vec3_t;

    /**
     * @brief Get Euler degrees closest to @p hint (360-wrap sync only).
     * @param hint Preferred Euler degrees for continuity.
     */
    auto get_rotation_euler_degrees(vec3_t hint) const noexcept -> vec3_t;

    /**
     * @brief Set the rotation from Euler angles (radians) and update the Euler hint.
     * @param euler_angles The Euler angles to set.
     */
    void set_rotation_euler(const vec3_t& euler_angles) noexcept;

    /**
     * @brief Set the rotation component using Euler angles.
     * @param x The X component.
     * @param y The Y component.
     * @param z The Z component.
     */
    void set_rotation_euler(T x, T y, T z) noexcept;

    /**
     * @brief Set the rotation component using Euler angles in degrees.
     * @param euler_angles The Euler angles in degrees to set.
     */
    void set_rotation_euler_degrees(const vec3_t& euler_angles) noexcept;

    /**
     * @brief Set the rotation component using Euler angles in degrees.
     * @param x The X component.
     * @param y The Y component.
     * @param z The Z component.
     */
    void set_rotation_euler_degrees(T x, T y, T z) noexcept;

    /**
     * @brief Get the rotation component.
     * @return A constant reference to the quaternion rotation.
     */
    auto get_rotation() const noexcept -> const quat_t&;

    /**
     * @brief Set the rotation component.
     * @param rotation The quaternion rotation to set.
     */
    void set_rotation(const quat_t& rotation) noexcept;

    /**
     * @brief Set the rotation component using basis vectors.
     * @param x The X basis vector.
     * @param y The Y basis vector.
     * @param z The Z basis vector.
     */
    void set_rotation(const vec3_t& x, const vec3_t& y, const vec3_t& z) noexcept;

    /**
     * @brief Reset the rotation component to identity.
     */
    void reset_rotation() noexcept;

    /**
     * @brief True when an authored Euler hint is currently valid.
     */
    auto has_rotation_euler_hint() const noexcept -> bool;

    /**
     * @brief Restores an authoring Euler hint after loading a quaternion.
     * Always trusts the provided hint (needed near gimbal lock / serialization).
     */
    void restore_euler_angles_hint_degrees(const vec3_t& hint) const noexcept;

    /**
     * @brief Restores @p hint only when it still represents the current orientation.
     * @return True if the hint was accepted.
     */
    auto try_restore_euler_angles_hint_degrees(const vec3_t& hint) const noexcept -> bool;

    //-------------------------------------------------------------------------
    // Scale Methods
    //-------------------------------------------------------------------------

    /**
     * @brief Get the scale component.
     * @return A constant reference to the scale vector.
     */
    auto get_scale() const noexcept -> const vec3_t&;

    /**
     * @brief Set the scale component.
     * @param scale The new scale vector.
     */
    void set_scale(const vec3_t& scale) noexcept;

    /**
     * @brief Set the scale component.
     * @param x The X component.
     * @param y The Y component.
     * @param z The Z component.
     */
    void set_scale(T x, T y, T z = T(1)) noexcept;

    /**
     * @brief Reset the scale component to one.
     */
    void reset_scale() noexcept;

    //-------------------------------------------------------------------------
    // Skew Methods
    //-------------------------------------------------------------------------

    /**
     * @brief Get the skew component.
     * @return A constant reference to the skew vector.
     */
    auto get_skew() const noexcept -> const vec3_t&;

    /**
     * @brief Set the skew component.
     * @param skew The new skew vector.
     */
    void set_skew(const vec3_t& skew) noexcept;

    /**
     * @brief Set the skew component.
     * @param x The X component.
     * @param y The Y component.
     * @param z The Z component.
     */
    void set_skew(T x, T y, T z) noexcept;

    /**
     * @brief Reset the skew component to zero.
     */
    void reset_skew() noexcept;

    //-------------------------------------------------------------------------
    // Perspective Methods
    //-------------------------------------------------------------------------

    /**
     * @brief Get the perspective component.
     * @return A constant reference to the perspective vector.
     */
    auto get_perspective() const noexcept -> const vec4_t&;

    /**
     * @brief Set the perspective component.
     * @param perspective The new perspective vector.
     */
    void set_perspective(const vec4_t& perspective) noexcept;

    /**
     * @brief Set the perspective component.
     * @param x The X component.
     * @param y The Y component.
     * @param z The Z component.
     * @param w The W component.
     */
    void set_perspective(T x, T y, T z, T w) noexcept;

    /**
     * @brief Reset the perspective component to default.
     */
    void reset_perspective() noexcept;

    //-------------------------------------------------------------------------
    // Axis Methods
    //-------------------------------------------------------------------------

    /**
     * @brief Get the X axis of the transform.
     * @return The X axis vector.
     */
    auto x_axis() const noexcept -> vec3_t;

    /**
     * @brief Get the Y axis of the transform.
     * @return The Y axis vector.
     */
    auto y_axis() const noexcept -> vec3_t;

    /**
     * @brief Get the Z axis of the transform.
     * @return The Z axis vector.
     */
    auto z_axis() const noexcept -> vec3_t;

    /**
     * @brief Get the unit X axis of the transform.
     * @return The unit X axis vector.
     */
    auto x_unit_axis() const noexcept -> vec3_t;

    /**
     * @brief Get the unit Y axis of the transform.
     * @return The unit Y axis vector.
     */
    auto y_unit_axis() const noexcept -> vec3_t;

    /**
     * @brief Get the unit Z axis of the transform.
     * @return The unit Z axis vector.
     */
    auto z_unit_axis() const noexcept -> vec3_t;

    //-------------------------------------------------------------------------
    // Transformation Methods
    //-------------------------------------------------------------------------

    /**
     * @brief Rotate the transform around an axis.
     * @param a The angle to rotate.
     * @param v The axis to rotate around.
     */
    void rotate_axis(T a, const vec3_t& v) noexcept;

    /**
     * @brief Rotate the transform using a quaternion.
     * @param q The quaternion to rotate by.
     */
    void rotate(const quat_t& q) noexcept;

    /**
     * @brief Rotate the transform using Euler angles.
     * @param x The X component.
     * @param y The Y component.
     * @param z The Z component.
     */
    void rotate(T x, T y, T z) noexcept;

    /**
     * @brief Rotate the transform using Euler angles.
     * @param v The Euler angles.
     */
    void rotate(const vec3_t& v) noexcept;

    /**
     * @brief Rotate the transform locally using Euler angles.
     * @param x The X component.
     * @param y The Y component.
     * @param z The Z component.
     */
    void rotate_local(T x, T y, T z) noexcept;

    /**
     * @brief Rotate the transform locally using Euler angles.
     * @param v The Euler angles.
     */
    void rotate_local(const vec3_t& v) noexcept;

    /**
     * @brief Scale the transform.
     * @param x The X component.
     * @param y The Y component.
     * @param z The Z component.
     */
    void scale(T x, T y, T z = T(1)) noexcept;

    /**
     * @brief Scale the transform.
     * @param v The scale vector.
     */
    void scale(const vec3_t& v) noexcept;

    /**
     * @brief Translate the transform.
     * @param x The X component.
     * @param y The Y component.
     * @param z The Z component.
     */
    void translate(T x, T y, T z = T(0)) noexcept;

    /**
     * @brief Translate the transform.
     * @param v The translation vector.
     */
    void translate(const vec3_t& v) noexcept;

    /**
     * @brief Translate the transform locally.
     * @param x The X component.
     * @param y The Y component.
     * @param z The Z component.
     */
    void translate_local(T x, T y, T z = T(0)) noexcept;

    /**
     * @brief Translate the transform locally.
     * @param v The translation vector.
     */
    void translate_local(const vec3_t& v) noexcept;

    //-------------------------------------------------------------------------
    // Comparison Methods
    //-------------------------------------------------------------------------

    /**
     * @brief Compare this transform with another.
     * @param t The transform to compare with.
     * @return 0 if equal within the default epsilon, 1 otherwise.
     */
    auto compare(const transform_t& rhs) const noexcept -> int;

    /**
     * @brief Compare this transform with another within a tolerance.
     * @param t The transform to compare with.
     * @param tolerance The per-element tolerance value.
     * @return 0 if equal within the tolerance, 1 otherwise.
     */
    auto compare(const transform_t& rhs, T tolerance) const noexcept -> int;

    //-------------------------------------------------------------------------
    // Coordinate Transformation Methods
    //-------------------------------------------------------------------------

    /**
     * @brief Transform a 2D coordinate.
     * @param v The 2D vector to transform.
     * @return The transformed 2D coordinate.
     */
    auto transform_coord(const vec2_t& v) const noexcept -> vec2_t;

    /**
     * @brief Inverse transform a 2D coordinate.
     * @param v The 2D vector to inverse transform.
     * @return The inverse transformed 2D coordinate.
     */
    auto inverse_transform_coord(const vec2_t& v) const noexcept -> vec2_t;

    /**
     * @brief Transform a 2D normal.
     * @param v The 2D normal to transform.
     * @return The transformed 2D normal.
     */
    auto transform_normal(const vec2_t& v) const noexcept -> vec2_t;

    /**
     * @brief Inverse transform a 2D normal.
     * @param v The 2D normal to inverse transform.
     * @return The inverse transformed 2D normal.
     */
    auto inverse_transform_normal(const vec2_t& v) const noexcept -> vec2_t;

    /**
     * @brief Transform a 3D coordinate.
     * @param v The 3D vector to transform.
     * @return The transformed 3D coordinate.
     */
    auto transform_coord(const vec3_t& v) const noexcept -> vec3_t;

    /**
     * @brief Inverse transform a 3D coordinate.
     * @param v The 3D vector to inverse transform.
     * @return The inverse transformed 3D coordinate.
     */
    auto inverse_transform_coord(const vec3_t& v) const noexcept -> vec3_t;

    /**
     * @brief Transform a 3D normal.
     * @param v The 3D normal to transform.
     * @return The transformed 3D normal.
     */
    auto transform_normal(const vec3_t& v) const noexcept -> vec3_t;

    /**
     * @brief Inverse transform a 3D normal.
     * @param v The 3D normal to inverse transform.
     * @return The inverse transformed 3D normal.
     */
    auto inverse_transform_normal(const vec3_t& v) const noexcept -> vec3_t;

    //-------------------------------------------------------------------------
    // Identity and Factory Methods
    //-------------------------------------------------------------------------

    /**
     * @brief Get the identity transform.
     * @return A constant reference to the identity transform.
     */
    static auto identity() noexcept -> const transform_t&;

    /**
     * @brief Create a scaling transform.
     * @param scale The scale vector.
     * @return The scaling transform.
     */
    static auto scaling(const vec2_t& scale) noexcept -> transform_t;

    /**
     * @brief Create a scaling transform.
     * @param scale The scale vector.
     * @return The scaling transform.
     */
    static auto scaling(const vec3_t& scale) noexcept -> transform_t;

    /**
     * @brief Create a rotation transform.
     * @param rotation The quaternion rotation.
     * @return The rotation transform.
     */
    static auto rotation(const quat_t& rotation) noexcept -> transform_t;

    /**
     * @brief Create a rotation transform from Euler angles.
     * @param euler_angles The Euler angles.
     * @return The rotation transform.
     */
    static auto rotation_euler(const vec3_t& euler_angles) noexcept -> transform_t;

    /**
     * @brief Create a translation transform.
     * @param trans The translation vector.
     * @return The translation transform.
     */
    static auto translation(const vec2_t& trans) noexcept -> transform_t;

    /**
     * @brief Create a translation transform.
     * @param trans The translation vector.
     * @return The translation transform.
     */
    static auto translation(const vec3_t& trans) noexcept -> transform_t;

    //-------------------------------------------------------------------------
    // Public Operator Overloads
    //-------------------------------------------------------------------------

    /**
     * @brief Implicit conversion to a constant reference to the transformation matrix.
     *
     * Allows the transform to be used wherever a constant reference to a mat4_t is expected.
     *
     * @return A constant reference to the transformation matrix.
     */
    operator const mat4_t&() const noexcept;

    /**
     * @brief Implicit conversion to a constant pointer to the transformation matrix.
     *
     * Allows the transform to be used wherever a constant pointer to a mat4_t is expected.
     *
     * @return A constant pointer to the transformation matrix.
     */
    operator const mat4_t*() const noexcept;

    /**
     * @brief Implicit conversion to a constant pointer to the transformation matrix data.
     *
     * Allows the transform to be used wherever a constant pointer to the underlying data is expected,
     * facilitating interfacing with APIs that require raw matrix data.
     *
     * @return A constant pointer to the transformation matrix data.
     */
    operator const T*() const noexcept;

    /**
     * @brief Multiply two transforms.
     * @param t The transform to multiply by.
     * @return The result of the multiplication.
     */
    auto operator*(const transform_t& t) const noexcept -> transform_t;

    /**
     * @brief Compare two transforms for equality.
     * @param t The transform to compare with.
     * @return True if equal, false otherwise.
     */
    auto operator==(const transform_t& t) const noexcept -> bool;

    /**
     * @brief Compare two transforms for inequality.
     * @param t The transform to compare with.
     * @return True if not equal, false otherwise.
     */
    auto operator!=(const transform_t& t) const noexcept -> bool;

    /**
     * @brief Get a column of the transform matrix.
     * @param i The column index.
     * @return The column of the matrix.
     */
    auto operator[](length_t i) const noexcept -> const col_t&;

    /**
     * @brief Transform a 4D vector.
     * @param v The vector to transform.
     * @return The transformed 4D vector.
     */
    auto operator*(const vec4_t& v) const noexcept -> vec4_t;

    /**
     * @brief Get the transform matrix.
     * @return A constant reference to the transform matrix.
     */
    auto get_matrix() const noexcept -> const mat4_t&;

private:
    /**
     * @brief Updates the transformation components from the transformation matrix.
     *
     * This function decomposes the current transformation matrix into its constituent components:
     * position, rotation, scale, skew, and perspective, and updates the corresponding member variables.
     * This is typically called after setting the transformation matrix directly.
     */
    void update_components() const noexcept;

    /**
     * @brief Updates the transformation matrix from the transformation components.
     *
     * This function recomposes the transformation matrix from the current values of the position,
     * rotation, scale, skew, and perspective components. It is marked as const to allow it to be called
     * from const member functions, and it only performs the update if the dirty flag is set.
     */
    void update_matrix() const noexcept;

    /**
     * @brief Marks the transformation matrix as needing an update.
     *
     * This function sets the dirty flag to true, indicating that the transformation matrix needs
     * to be recomposed from the current transformation components before it can be used again.
     * This is typically called whenever any of the transformation components are modified.
     */
    void make_matrix_dirty() noexcept;
    void make_components_dirty() noexcept;

    /**
     * @brief Out-of-line body of update_components: full matrix decompose plus
     * cache/hint bookkeeping. Only ever called with components_need_recompute_ set.
     */
    void decompose_matrix() const noexcept;

    /**
     * @brief Out-of-line general (skew/perspective/non-uniform) branch of transform_normal.
     */
    auto transform_normal_general(const vec3_t& v) const noexcept -> vec3_t;

    /**
     * @brief Out-of-line general branch of inverse_transform_normal.
     */
    auto inverse_transform_normal_general(const vec3_t& v) const noexcept -> vec3_t;

    // Helper functions to check if skew and perspective are zero/identity
    auto is_skew_zero() const noexcept -> bool;
    auto is_perspective_identity() const noexcept -> bool;
    auto is_scale_uniform() const noexcept -> bool;

    auto can_use_simplified_calculations() const noexcept -> bool;
    auto can_use_simplified_calculations_without_uniform_scale() const noexcept -> bool;
    /**
     * @brief The transformation matrix, computed from the translation, rotation, scale, skew, and perspective
     * components.
     *
     * This matrix is marked as mutable to allow its modification even in const member functions,
     * enabling lazy updates when the transformation components change.
     */
    mutable mat4_t matrix_ = glm::identity<mat4_t>();

    /**
     * @brief The position (translation) component of the transform.
     *
     * This vector represents the translation part of the transform, specifying the position of the transform
     * in the 3D space.
     */
    mutable vec3_t position_ = glm::zero<vec3_t>();

    /**
     * @brief The rotation component of the transform, represented as a quaternion.
     *
     * This quaternion represents the rotational part of the transform, specifying the orientation
     * of the transform in the 3D space.
     */
    mutable quat_t rotation_ = glm::identity<quat_t>();

    /**
     * @brief The scale component of the transform.
     *
     * This vector represents the scaling part of the transform, specifying the scale factors along
     * the X, Y, and Z axes.
     */
    mutable vec3_t scale_ = glm::one<vec3_t>();

    /**
     * @brief The skew (shear) component of the transform.
     *
     * This vector represents the skew part of the transform, specifying the shear factors along
     * the X, Y, and Z axes.
     */
    mutable vec3_t skew_ = glm::zero<vec3_t>();

    /**
     * @brief The perspective component of the transform.
     *
     * This vector represents the perspective part of the transform, typically used for representing
     * perspective projection parameters.
     */
    mutable vec4_t perspective_ = vec4_t(0, 0, 0, 1);

    /**
     * @brief A flag indicating whether the transformation matrix needs to be recomputed.
     *
     * This flag is set to true whenever any of the transformation components (position, rotation, scale, skew, or
     * perspective) are modified, signaling that the matrix_ needs to be updated before it can be used.
     */
    mutable bool matrix_needs_recompute_ = false;

    /**
     * @brief A flag indicating whether the transformation components needs to be recomputed.
     *
     * This flag is set to true whenever the matrix_ is modified, signaling that the transformation
     * components (position, rotation, scale, skew, or perspective)
     * needs to be updated before it can be used.
     */
    mutable bool components_need_recompute_ = false;

    mutable bool is_skew_zero_cached_ = true;
    mutable bool is_perspective_identity_cached_ = true;
    mutable bool is_scale_uniform_cached_ = true;

    /**
     * @brief Authoring Euler degrees matching the last set_rotation_euler* call.
     * Valid while euler_hint_dirty_ is false and the hint still maps to rotation_.
     */
    mutable vec3_t euler_angles_hint_ = glm::zero<vec3_t>();

    /**
     * @brief When true, euler_angles_hint_ must be rebuilt from rotation_.
     */
    mutable bool euler_hint_dirty_ = true;

    /**
     * @brief True when @p a and @p b represent the same orientation (including q vs -q).
     */
    static auto rotations_equivalent(const quat_t& a, const quat_t& b) noexcept -> bool;

    /**
     * @brief Wrap @p angles toward @p hint by 360 degrees per axis.
     */
    static auto sync_euler_degrees_to_hint(vec3_t angles, const vec3_t& hint) noexcept -> vec3_t;

    /**
     * @brief Marks the Euler hint invalid so the next get recomputes from the quaternion.
     */
    void make_euler_hint_dirty() const noexcept;

    // inverse() builds the component-space inverse directly for uniform TRS
    // transforms, which needs member access.
    template<typename T2, precision Q2>
    friend auto inverse(transform_t<T2, Q2> const& t) noexcept -> transform_t<T2, Q2>;
};

template<typename T, precision Q>
auto inverse(transform_t<T, Q> const& t) noexcept -> transform_t<T, Q>
{
    // Uniform TRS: exact component inverse. No 4x4 inverse, and the result's
    // components are already valid, so no decompose is deferred onto the caller.
    if(t.can_use_simplified_calculations())
    {
        transform_t<T, Q> result;
        const T inv_scale = T(1) / detail::scale_fix(t.scale_.x);
        result.rotation_ = glm::conjugate(t.rotation_);
        result.scale_ = typename transform_t<T, Q>::vec3_t(inv_scale, inv_scale, inv_scale);
        result.position_ = -(result.rotation_ * (t.position_ * inv_scale));
        result.matrix_needs_recompute_ = true;
        return result;
    }

    const auto& m = t.get_matrix();
    if(!t.components_need_recompute_ && t.is_perspective_identity_cached_)
    {
        // Affine: cheaper and better conditioned than the general inverse.
        return glm::affineInverse(m);
    }
    return glm::inverse(m);
}

template<typename T, precision Q>
auto transpose(transform_t<T, Q> const& t) noexcept -> transform_t<T, Q>
{
    const auto& m = t.get_matrix();
    return glm::transpose(m);
}

template<typename T, precision Q>
TRANSFORM_INLINE transform_t<T, Q>::transform_t(const mat4_t& m) noexcept
    : matrix_(m)
    , is_skew_zero_cached_(false)
    , is_perspective_identity_cached_(false)
    , is_scale_uniform_cached_(false)
{
    make_components_dirty();
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::get_position() const noexcept -> const typename transform_t<T, Q>::vec3_t&
{
    update_components();
    return position_;
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::get_translation() const noexcept -> const typename transform_t<T, Q>::vec3_t&
{
    return get_position();
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::set_translation(const vec3_t& position) noexcept
{
    set_position(position);
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::set_translation(T x, T y, T z) noexcept
{
    set_position(x, y, z);
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::reset_translation() noexcept
{
    set_translation(0, 0, 0);
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::set_position(const vec3_t& position) noexcept
{
    update_components();
    position_ = position;
    make_matrix_dirty();
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::set_position(T x, T y, T z) noexcept
{
    set_position({x, y, z});
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::reset_position() noexcept
{
    set_position(0, 0, 0);
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::rotations_equivalent(const quat_t& a, const quat_t& b) noexcept -> bool
{
    // Orientation equality: q and -q are the same rotation. Tolerance is looser than
    // machine epsilon so euler->quat->hint validation stays stable while editing.
    constexpr T k_rotation_dot_tolerance = T(1) - T(1e-4);
    return abs(dot(a, b)) >= k_rotation_dot_tolerance;
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::sync_euler_degrees_to_hint(vec3_t angles, const vec3_t& hint) noexcept
    -> vec3_t
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

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::make_euler_hint_dirty() const noexcept
{
    euler_hint_dirty_ = true;
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::get_rotation_euler() const noexcept -> typename transform_t<T, Q>::vec3_t
{
    return radians(get_rotation_euler_degrees());
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::get_rotation_euler_degrees() const noexcept ->
    typename transform_t<T, Q>::vec3_t
{
    update_components();
    // Trust the authoring hint until set_rotation / matrix decompose dirties it.
    // Re-validating hint->quat near gimbal lock (pitch ~ +/-90) is numerically
    // unstable and causes X/Z to flip between equivalent Euler triples.
    if(!euler_hint_dirty_)
    {
        return euler_angles_hint_;
    }
    euler_angles_hint_ = degrees(eulerAngles(rotation_));
    euler_hint_dirty_ = false;
    return euler_angles_hint_;
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::get_rotation_euler_degrees(vec3_t hint) const noexcept ->
    typename transform_t<T, Q>::vec3_t
{
    update_components();
    vec3_t angles = degrees(eulerAngles(rotation_));
    return sync_euler_degrees_to_hint(angles, hint);
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::set_rotation_euler(const vec3_t& euler_angles) noexcept
{
    update_components();
    rotation_ = glm::normalize(quat_t(euler_angles));
    euler_angles_hint_ = degrees(euler_angles);
    euler_hint_dirty_ = false;
    make_matrix_dirty();
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::set_rotation_euler(T x, T y, T z) noexcept
{
    set_rotation_euler({x, y, z});
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::set_rotation_euler_degrees(const vec3_t& euler_angles) noexcept
{
    update_components();
    rotation_ = glm::normalize(quat_t(radians(euler_angles)));
    euler_angles_hint_ = euler_angles;
    euler_hint_dirty_ = false;
    make_matrix_dirty();
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::set_rotation_euler_degrees(T x, T y, T z) noexcept
{
    set_rotation_euler_degrees({x, y, z});
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::get_scale() const noexcept -> const typename transform_t<T, Q>::vec3_t&
{
    update_components();
    return scale_;
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::set_scale(const vec3_t& scale) noexcept
{
    update_components();
    scale_ = scale;
    is_scale_uniform_cached_ = is_scale_uniform();

    make_matrix_dirty();
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::set_scale(T x, T y, T z) noexcept
{
    set_scale({x, y, z});
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::reset_scale() noexcept
{
    set_scale(T(1), T(1), T(1));
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::set_skew(const vec3_t& skew) noexcept
{
    update_components();
    skew_ = skew;

    is_skew_zero_cached_ = is_skew_zero();
    make_matrix_dirty();
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::set_skew(T x, T y, T z) noexcept
{
    set_skew({x, y, z});
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::reset_skew() noexcept
{
    set_skew(0, 0, 0);
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::set_perspective(const vec4_t& perspective) noexcept
{
    update_components();
    perspective_ = perspective;
    is_perspective_identity_cached_ = is_perspective_identity();

    make_matrix_dirty();
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::set_perspective(T x, T y, T z, T w) noexcept
{
    set_perspective({x, y, z, w});
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::reset_perspective() noexcept
{
    set_perspective(0, 0, 0, 1);
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::get_rotation() const noexcept -> const typename transform_t<T, Q>::quat_t&
{
    update_components();
    return rotation_;
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::get_skew() const noexcept -> const typename transform_t<T, Q>::vec3_t&
{
    update_components();
    return skew_;
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::get_perspective() const noexcept -> const typename transform_t<T, Q>::vec4_t&
{
    update_components();
    return perspective_;
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::set_rotation(const quat_t& rotation) noexcept
{
    update_components();
    const quat_t normalized = glm::normalize(rotation);
    // Undo/property actions re-apply the same orientation via set_rotation after
    // set_rotation_euler*. Keep the typed Euler hint only while it still maps to the
    // NEW orientation. Comparing new-vs-previous instead would let many small
    // incremental rotations (each within tolerance of the last) drift arbitrarily
    // far from a hint that is never invalidated.
    if(!euler_hint_dirty_)
    {
        const quat_t from_hint = glm::normalize(quat_t(radians(euler_angles_hint_)));
        if(!rotations_equivalent(from_hint, normalized))
        {
            make_euler_hint_dirty();
        }
    }
    rotation_ = normalized;
    make_matrix_dirty();
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::set_rotation(const vec3_t& x, const vec3_t& y, const vec3_t& z) noexcept
{
    quat_t quat = quat_cast(mat3_t(x, y, z));
    set_rotation(quat);
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::reset_rotation() noexcept
{
    set_rotation_euler_degrees(glm::zero<vec3_t>());
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::has_rotation_euler_hint() const noexcept -> bool
{
    return !euler_hint_dirty_;
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::restore_euler_angles_hint_degrees(const vec3_t& hint) const noexcept
{
    // Trust the authored hint. Near gimbal lock, quat(hint) may not compare equal to the
    // stored quaternion even when it is the intended authoring triple.
    euler_angles_hint_ = hint;
    euler_hint_dirty_ = false;
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::try_restore_euler_angles_hint_degrees(const vec3_t& hint) const noexcept
    -> bool
{
    update_components();
    const quat_t from_hint = glm::normalize(quat_t(radians(hint)));
    if(!rotations_equivalent(from_hint, rotation_))
    {
        return false;
    }
    euler_angles_hint_ = hint;
    euler_hint_dirty_ = false;
    return true;
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::x_axis() const noexcept -> typename transform_t<T, Q>::vec3_t
{
    if(can_use_simplified_calculations_without_uniform_scale())
    {
        // First column of mat3_cast(rotation), scaled: ~12 flops instead of a
        // generic ~30-flop quat * vec3 rotation of a sparse vector.
        const quat_t& q = rotation_;
        return vec3_t(T(1) - T(2) * (q.y * q.y + q.z * q.z),
                      T(2) * (q.x * q.y + q.w * q.z),
                      T(2) * (q.x * q.z - q.w * q.y)) *
               scale_.x;
    }

    return get_matrix()[0];
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::y_axis() const noexcept -> typename transform_t<T, Q>::vec3_t
{
    if(can_use_simplified_calculations_without_uniform_scale())
    {
        // Second column of mat3_cast(rotation), scaled.
        const quat_t& q = rotation_;
        return vec3_t(T(2) * (q.x * q.y - q.w * q.z),
                      T(1) - T(2) * (q.x * q.x + q.z * q.z),
                      T(2) * (q.y * q.z + q.w * q.x)) *
               scale_.y;
    }

    return get_matrix()[1];
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::z_axis() const noexcept -> typename transform_t<T, Q>::vec3_t
{
    if(can_use_simplified_calculations_without_uniform_scale())
    {
        // Third column of mat3_cast(rotation), scaled.
        const quat_t& q = rotation_;
        return vec3_t(T(2) * (q.x * q.z + q.w * q.y),
                      T(2) * (q.y * q.z - q.w * q.x),
                      T(1) - T(2) * (q.x * q.x + q.y * q.y)) *
               scale_.z;
    }

    return get_matrix()[2];
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::x_unit_axis() const noexcept -> typename transform_t<T, Q>::vec3_t
{
    return normalize(x_axis());
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::y_unit_axis() const noexcept -> typename transform_t<T, Q>::vec3_t
{
    return normalize(y_axis());
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::z_unit_axis() const noexcept -> typename transform_t<T, Q>::vec3_t
{
    return normalize(z_axis());
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::rotate(const quat_t& q) noexcept
{
    quat_t result = q * get_rotation();
    set_rotation(result);
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::rotate_axis(T a, const vec3_t& v) noexcept
{
    // glm::angleAxis requires a unit axis; normalize so arbitrary caller input rotates
    // by the requested angle. A degenerate axis has no defined rotation - do nothing
    // rather than poison the quaternion with NaNs.
    const T axis_len2 = dot(v, v);
    if(axis_len2 <= epsilon<T>())
    {
        return;
    }
    quat_t q = glm::angleAxis(a, v / sqrt(axis_len2)) * get_rotation();
    set_rotation(q);
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::rotate(T x, T y, T z) noexcept
{
    rotate({x, y, z});
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::rotate(const vec3_t& v) noexcept
{
    // Convert Euler angles to quaternion
    quat_t delta_rotation(v);
    set_rotation(delta_rotation * get_rotation());
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::rotate_local(T x, T y, T z) noexcept
{
    rotate_local({x, y, z});
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::rotate_local(const vec3_t& v) noexcept
{
    // Convert Euler angles to quaternion
    quat_t delta_rotation(v);
    set_rotation(get_rotation() * delta_rotation);
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::scale(T x, T y, T z) noexcept
{
    scale({x, y, z});
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::scale(const vec3_t& v) noexcept
{
    set_scale(get_scale() * v);
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::translate(T x, T y, T z) noexcept
{
    translate({x, y, z});
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::translate(const vec3_t& v) noexcept
{
    set_position(get_position() + v);
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::translate_local(T x, T y, T z) noexcept
{
    translate_local({x, y, z});
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::translate_local(const vec3_t& v) noexcept
{
    // Transform local translation vector by current rotation
    vec3_t world_translation = get_rotation() * v;
    translate(world_translation);
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::compare(const transform_t& rhs) const noexcept -> int
{
    return compare(rhs, epsilon<T>());
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::compare(const transform_t& rhs, T tolerance) const noexcept -> int
{
    // bool comps_valid = !components_need_recompute_;
    // bool rhs_comps_valid = !rhs.components_need_recompute_;

    // if(comps_valid && rhs_comps_valid)
    // {
    //     if(!math::all(math::epsilonEqual(get_position(), rhs.get_position(), tolerance)))
    //     {
    //         return 1;
    //     }
    //     if(!math::all(math::epsilonEqual(get_scale(), rhs.get_scale(), tolerance)))
    //     {
    //         return 1;
    //     }

    //     // Compare rotation quaternions
    //     T dot = math::dot(get_rotation(), rhs.get_rotation());
    //     T angle_diff = math::abs(dot);
    //     if(angle_diff < (1 - tolerance))
    //     {
    //         return 1;
    //     }

    //     if(!math::all(math::epsilonEqual(get_skew(), rhs.get_skew(), tolerance)))
    //     {
    //         return 1;
    //     }
    //     if(!math::all(math::epsilonEqual(get_perspective(), rhs.get_perspective(), tolerance)))
    //     {
    //         return 1;
    //     }
    //     return 0;
    // }

    // otherwise do a matrix compare

    const auto& m1 = get_matrix();
    const auto& m2 = rhs.get_matrix();

    // Compare matrices; every element honors the caller's tolerance.
    for(length_t i = 0; i < 4; ++i)
    {
        const vec4_t diff = m1[i] - m2[i];
        if(!glm::all(glm::epsilonEqual(diff, glm::zero<vec4_t>(), tolerance)))
        {
            return 1;
        }
    }

    return 0;
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::transform_coord(const vec2_t& v) const noexcept ->
    typename transform_t<T, Q>::vec2_t
{
    return transform_coord(vec3_t(v, T(0)));
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::inverse_transform_coord(const vec2_t& v) const noexcept ->
    typename transform_t<T, Q>::vec2_t
{
    return inverse_transform_coord(vec3_t(v, T(0)));
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::transform_normal(const vec2_t& v) const noexcept ->
    typename transform_t<T, Q>::vec2_t
{
    return transform_normal(vec3_t(v, T(0)));
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::inverse_transform_normal(const vec2_t& v) const noexcept ->
    typename transform_t<T, Q>::vec2_t
{
    return inverse_transform_normal(vec3_t(v, T(0)));
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::transform_coord(const vec3_t& v) const noexcept ->
    typename transform_t<T, Q>::vec3_t
{
    if(can_use_simplified_calculations_without_uniform_scale())
    {
        // Direct transformation using components
        return get_position() + (get_rotation() * (get_scale() * v));
    }

    // Use matrix multiplication
    vec4_t result = get_matrix() * vec4_t(v, T(1));
    if(!components_need_recompute_ && is_perspective_identity_cached_)
    {
        // Affine matrix: w is exactly 1, skip the divide.
        return vec3_t(result);
    }
    if(abs(result.w) <= epsilon<T>())
    {
        // Point on the eye plane of a perspective transform: no finite projection.
        return vec3_t(result);
    }
    return vec3_t(result) / result.w;
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::inverse_transform_coord(const vec3_t& v) const noexcept ->
    typename transform_t<T, Q>::vec3_t
{
    if(can_use_simplified_calculations_without_uniform_scale())
    {
        auto scale = detail::scale_fix(get_scale());
        // Compute inverse scale
        vec3_t inv_scale = T(1) / scale;

        // Compute inverse rotation
        // Using conjugate
        quat_t inv_rotation = glm::conjugate(get_rotation()); // Efficient and correct for unit quaternions

        // Using inverse
        // quat_t inv_rotation = glm::inverse(get_rotation());   // Also correct but less efficient for unit quaternions

        // Apply inverse transformations
        vec3_t result = inv_rotation * (v - get_position());
        result *= inv_scale;

        return result;
    }

    // Use matrix multiplication
    if(!components_need_recompute_ && is_perspective_identity_cached_)
    {
        // Affine matrix: the affine inverse is cheaper and w stays exactly 1.
        vec4_t result = glm::affineInverse(get_matrix()) * vec4_t(v, T(1));
        return vec3_t(result);
    }
    vec4_t result = glm::inverse(get_matrix()) * vec4_t(v, T(1));
    if(abs(result.w) <= epsilon<T>())
    {
        // Point on the eye plane of a perspective transform: no finite projection.
        return vec3_t(result);
    }
    return vec3_t(result) / result.w;
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::transform_normal(const vec3_t& v) const noexcept ->
    typename transform_t<T, Q>::vec3_t
{
    // Contract: the direction of the inverse-transpose (normal matrix) with the input's
    // length preserved. Backs Unity-style TransformDirection, so length must not pick up
    // the object's scale, and both branches must agree.
    if(can_use_simplified_calculations())
    {
        // Uniform scale s: the normal matrix is (1/s) * R, so the direction is
        // sign(s) * (R * v) and the length of v is already preserved.
        const vec3_t rotated = get_rotation() * v;
        return get_scale().x < T(0) ? -rotated : rotated;
    }

    return transform_normal_general(v);
}

template<typename T, precision Q>
TRANSFORM_NOINLINE auto transform_t<T, Q>::transform_normal_general(const vec3_t& v) const noexcept ->
    typename transform_t<T, Q>::vec3_t
{
    // Use matrix multiplication
    // Extract the linear (rotation, scaling, skew) part of the transformation matrix
    mat3_t linear_matrix = mat3_t(get_matrix());

    // Compute the inverse transpose of the linear matrix
    mat3_t normal_matrix = glm::transpose(glm::inverse(linear_matrix));

    // Transform the normal vector using the normal matrix
    vec3_t result = normal_matrix * v;

    // Rescale to the input length so both branches share one magnitude contract.
    const T result_len2 = dot(result, result);
    const T input_len2 = dot(v, v);
    if(result_len2 > T(0) && input_len2 > T(0))
    {
        result *= sqrt(input_len2 / result_len2);
    }
    return result;
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::inverse_transform_normal(const vec3_t& v) const noexcept ->
    typename transform_t<T, Q>::vec3_t
{
    // Exact directional inverse of transform_normal (the transpose of the linear part
    // inverts the inverse-transpose), with the input's length preserved - see the
    // contract note in transform_normal.
    if(can_use_simplified_calculations())
    {
        // Uniform scale s: the transpose is s * R^T, so the direction is
        // sign(s) * (R^T * v) and the length of v is already preserved.
        const vec3_t rotated = glm::conjugate(get_rotation()) * v;
        return get_scale().x < T(0) ? -rotated : rotated;
    }

    return inverse_transform_normal_general(v);
}

template<typename T, precision Q>
TRANSFORM_NOINLINE auto transform_t<T, Q>::inverse_transform_normal_general(const vec3_t& v) const noexcept ->
    typename transform_t<T, Q>::vec3_t
{
    // Use transpose of the linear transformation matrix
    mat3_t linear_matrix = mat3_t(get_matrix());
    mat3_t normal_matrix = glm::transpose(linear_matrix);

    // Transform the normal vector using the normal matrix
    vec3_t result = normal_matrix * v;

    // Rescale to the input length so both branches share one magnitude contract.
    const T result_len2 = dot(result, result);
    const T input_len2 = dot(v, v);
    if(result_len2 > T(0) && input_len2 > T(0))
    {
        result *= sqrt(input_len2 / result_len2);
    }
    return result;
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::identity() noexcept -> const transform_t<T, Q>&
{
    static transform_t identity;
    return identity;
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::scaling(const vec2_t& scale) noexcept -> transform_t<T, Q>
{
    return scaling(vec3_t(scale, T(1)));
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::scaling(const vec3_t& scale) noexcept -> transform_t<T, Q>
{
    transform_t result{};
    result.set_scale(scale);
    return result;
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::rotation(const quat_t& rotation) noexcept -> transform_t<T, Q>
{
    transform_t result{};
    result.set_rotation(rotation);
    return result;
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::rotation_euler(const vec3_t& euler_angles) noexcept -> transform_t<T, Q>
{
    transform_t result{};
    result.set_rotation_euler(euler_angles);
    return result;
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::translation(const vec2_t& trans) noexcept -> transform_t<T, Q>
{
    return translation(vec3_t(trans, T(0)));
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::translation(const vec3_t& trans) noexcept -> transform_t<T, Q>
{
    transform_t result{};
    result.set_position(trans);
    return result;
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::operator*(const transform_t& t) const noexcept -> transform_t<T, Q>
{
    // Hottest path in the engine (scene-graph world = parent * local).
    //
    // Component-space compose is legal when both operands are skew/perspective
    // free with valid components and OUR scale is uniform: S1 = s*I commutes
    // with R2, so T1 R1 S1 T2 R2 S2 = T(p1 + R1 s p2) (R1 R2) (s * S2). The
    // right-hand scale may be non-uniform. This keeps the result's components
    // exact, so a later get_position()/get_rotation() costs nothing - the
    // matrix fallback instead defers a full WebKit decompose (~10x the cost of
    // the multiply) to the first component read.
    if(can_use_simplified_calculations() && t.can_use_simplified_calculations_without_uniform_scale())
    {
        transform_t result;
        result.position_ = position_ + rotation_ * (scale_ * t.position_);
        result.rotation_ = glm::normalize(rotation_ * t.rotation_);
        result.scale_ = scale_ * t.scale_;
        // Our scale is uniform, so the product's uniformity is the right-hand's.
        result.is_scale_uniform_cached_ = t.is_scale_uniform_cached_;
        // Skew/perspective stay identity-initialized; the euler hint stays dirty.
        if(!matrix_needs_recompute_ && !t.matrix_needs_recompute_)
        {
            // Both operand matrices are already resolved (steady-state scene
            // graph): the SIMD multiply is cheaper than a deferred recompose.
            result.matrix_ = matrix_ * t.matrix_;
        }
        else
        {
            result.matrix_needs_recompute_ = true;
        }
        return result;
    }

    // Fallback to matrix multiplication and decomposition
    return get_matrix() * t.get_matrix();
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::operator==(const transform_t& t) const noexcept -> bool
{
    return compare(t, math::epsilon<T>()) == 0;
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::operator!=(const transform_t& t) const noexcept -> bool
{
    return compare(t, math::epsilon<T>()) != 0;
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::get_matrix() const noexcept -> const mat4_t&
{
    update_matrix();
    return matrix_;
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::update_components() const noexcept
{
    if(components_need_recompute_)
    {
        decompose_matrix();
    }
}

template<typename T, precision Q>
TRANSFORM_NOINLINE void transform_t<T, Q>::decompose_matrix() const noexcept
{
    const bool had_euler_hint = !euler_hint_dirty_;
    const vec3_t saved_euler_hint = euler_angles_hint_;
    glm_decompose(get_matrix(), scale_, rotation_, position_, skew_, perspective_);

    components_need_recompute_ = false;
    // Keep authored Euler across matrix decompose. Re-validating hint->quat near
    // gimbal lock is unstable and is exactly what makes inspector XYZ jump after edit.
    if(had_euler_hint)
    {
        euler_angles_hint_ = saved_euler_hint;
        euler_hint_dirty_ = false;
    }
    else
    {
        make_euler_hint_dirty();
    }

    is_perspective_identity_cached_ = is_perspective_identity();
    is_skew_zero_cached_ = is_skew_zero();
    is_scale_uniform_cached_ = is_scale_uniform();
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::update_matrix() const noexcept
{
    if(matrix_needs_recompute_)
    {
        if(can_use_simplified_calculations())
        {
            // Direct T*R*S compose: write the scaled rotation columns straight into
            // the matrix. The previous translate(I) * mat4_cast(q) * scale(I) form
            // paid two full mat4 multiplies for the same result.
            const mat3_t rotation_matrix = glm::mat3_cast(rotation_);
            matrix_[0] = vec4_t(rotation_matrix[0] * scale_.x, T(0));
            matrix_[1] = vec4_t(rotation_matrix[1] * scale_.y, T(0));
            matrix_[2] = vec4_t(rotation_matrix[2] * scale_.z, T(0));
            matrix_[3] = vec4_t(position_, T(1));
        }
        else
        {
            glm_recompose(matrix_, get_scale(), get_rotation(), get_position(), get_skew(), get_perspective());
        }

        matrix_needs_recompute_ = false;
    }
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::make_matrix_dirty() noexcept
{
    matrix_needs_recompute_ = true;
}

template<typename T, precision Q>
TRANSFORM_INLINE void transform_t<T, Q>::make_components_dirty() noexcept
{
    components_need_recompute_ = true;
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::is_skew_zero() const noexcept -> bool
{
    return glm::all(glm::epsilonEqual(get_skew(), glm::zero<vec3_t>(), k_skew_zero_tolerance<T>));
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::is_perspective_identity() const noexcept -> bool
{
    return glm::all(glm::epsilonEqual(get_perspective(), vec4_t(0, 0, 0, 1), glm::epsilon<T>()));
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::is_scale_uniform() const noexcept -> bool
{
    const auto& scale = get_scale();
    const T max_axis = max(max(abs(scale.x), abs(scale.y)), abs(scale.z));
    const T tolerance = k_scale_uniform_tolerance<T> * max(max_axis, T(1));
    return abs(scale.x - scale.y) <= tolerance && abs(scale.y - scale.z) <= tolerance &&
           abs(scale.x - scale.z) <= tolerance;
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::can_use_simplified_calculations() const noexcept -> bool
{
    return can_use_simplified_calculations_without_uniform_scale() && is_scale_uniform_cached_;
}

template<typename T, precision Q>

TRANSFORM_INLINE auto transform_t<T, Q>::can_use_simplified_calculations_without_uniform_scale() const noexcept -> bool
{
    return !components_need_recompute_ && is_skew_zero_cached_ && is_perspective_identity_cached_;
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::operator[](length_t i) const noexcept -> const col_t&
{
    return get_matrix()[i];
}

template<typename T, precision Q>
TRANSFORM_INLINE auto transform_t<T, Q>::operator*(const vec4_t& v) const noexcept -> vec4_t
{
    vec4_t result = get_matrix() * v;
    return result;
}

template<typename T, precision Q>
TRANSFORM_INLINE transform_t<T, Q>::operator const T*() const noexcept
{
    return value_ptr(get_matrix());
}

template<typename T, precision Q>
TRANSFORM_INLINE transform_t<T, Q>::operator const typename transform_t<T, Q>::mat4_t *() const noexcept
{
    return &get_matrix();
}

template<typename T, precision Q>
TRANSFORM_INLINE transform_t<T, Q>::operator const typename transform_t<T, Q>::mat4_t &() const noexcept
{
    return get_matrix();
}

using transform = transform_t<float>;

extern template class transform_t<float, defaultp>;

template<typename T>
TRANSFORM_INLINE auto to_string(const T& v) -> std::string
{
    return glm::to_string(v);
}
} // namespace math

namespace glm
{
namespace detail
{
template<typename T, qualifier Q>
struct compute_to_string<math::transform_t<T, Q>>
{
    GLM_FUNC_QUALIFIER static auto call(math::transform_t<T, Q> const& x) -> std::string
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

// Unity-like LookRotation: +Z = forward, +Y = upwards (approx).
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
