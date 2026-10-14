#pragma once

#include "color.h"

// range<T> / frange_t. Header-only with no link dependency, and already on this target's include
// path, so the traits for every value type a curve can hold stay in one place.
#include <base/basetypes.hpp>

#include <array>
#include <cstddef>
#include <vector>

namespace math
{
using namespace glm;

/**
 * @brief How the segment leaving a key reaches the next one.
 */
enum class curve_interpolation
{
    /// Holds the key's value until the next key, stepping there.
    constant,
    /// Straight line to the next key.
    linear,
    /// Cubic Hermite through both keys, shaped by their tangents.
    smooth
};

/**
 * @brief Where a smooth key's tangents come from.
 */
enum class curve_tangent_mode
{
    /// Both tangents follow the slope through the neighbouring keys, and are recomputed whenever
    /// a key moves.
    automatic,
    /// Both tangents are zero, so the curve arrives and leaves level.
    flat,
    /// The tangents are authored, in and out independently.
    free
};

/**
 * @brief One key of one channel.
 *
 * @ref position is in the curve's own domain, which is 0..1 for anything sampled over a lifetime
 * but is not constrained to it.
 */
struct curve_key
{
    float position{};
    float value{};
    /// Slope entering the key, in value per unit of position.
    float in_tangent{};
    /// Slope leaving the key.
    float out_tangent{};
    curve_interpolation interpolation{curve_interpolation::linear};
    curve_tangent_mode tangent_mode{curve_tangent_mode::automatic};

    auto operator==(const curve_key& other) const -> bool;
};

inline auto operator<(const curve_key& lhs, const curve_key& rhs) noexcept -> bool
{
    return lhs.position < rhs.position;
}

/**
 * @brief One scalar track: keys sorted by position, sampled on its own.
 *
 * A curve is a fixed set of these, one per channel of its value type, so a channel can be keyed
 * without disturbing the others.
 */
class curve_channel
{
public:
    using keys_t = std::vector<curve_key>;

    /**
     * @brief Inserts a key and returns the index it ended up at.
     */
    auto add_key(float position, float value) -> size_t;
    void remove_key(size_t index);
    void clear();

    auto get_keys() const noexcept -> const keys_t&;
    /// Replaces every key; the result is sorted whatever order the keys arrive in.
    void set_keys(keys_t keys);

    auto get_key_count() const noexcept -> size_t;
    /// The key at @p index, or a default-constructed key when the index is out of range.
    auto get_key(size_t index) const -> curve_key;
    /**
     * @brief Writes the key, holding it between its neighbours.
     *
     * A key cannot be moved past the keys either side of it, so the order never changes and an
     * index stays valid for as long as a drag lasts. Reordering goes through @ref remove_key and
     * @ref add_key.
     */
    void set_key(size_t index, const curve_key& key);

    auto is_empty() const noexcept -> bool;
    /// Lowest and highest key position. An empty channel has no domain and reports 0..0.
    auto get_domain() const -> frange_t;
    /// Whether any key steps. A stepping channel cannot be read from an interpolated table.
    auto has_constant_key() const -> bool;

    /**
     * @brief The value at @p position. Outside the key range the end values hold.
     */
    auto sample(float position) const -> float;

    /// Recomputes the tangents of every key whose mode is not `free`. Run after any key moves.
    void refresh_automatic_tangents();

    /// Mirrors every key position about the middle of [@p domain_min, @p domain_max], swapping
    /// the tangents so the shape is mirrored rather than just reordered.
    void reverse(float domain_min, float domain_max);

    auto operator==(const curve_channel& other) const -> bool;

private:
    keys_t keys_{};
};

/**
 * @brief How a value type maps onto the scalar channels a curve keys independently.
 *
 * Specialize this to hold a new value type in a curve; nothing else has to change. The editor
 * reads the channel count, the names and the colors from here, so a new type arrives in the curve
 * editor with no widget code at all.
 *
 * A specialization provides:
 *   static constexpr size_t channel_count;
 *   static constexpr bool   is_color;                 // present the curve as a gradient strip
 *   static auto channel_name(size_t) -> const char*;
 *   static auto channel_color(size_t) -> color;
 *   static auto get(const T&, size_t) -> float;
 *   static void set(T&, size_t, float);
 */
template<typename T>
struct curve_traits;

namespace detail
{
/// X and R red, Y and G green, Z and B blue, W and A grey: the axis colors the editor plots a
/// channel in, shared by the vector and color traits so they never drift apart.
auto get_curve_channel_color(size_t channel) -> color;
/// "X", "Y", "Z", "W".
auto get_vector_channel_name(size_t channel) -> const char*;
/// "R", "G", "B", "A".
auto get_color_channel_name(size_t channel) -> const char*;

/// Shared body of the glm vector traits: a vector exposes its components in order.
template<typename T, size_t N>
struct vector_curve_traits
{
    static constexpr size_t channel_count = N;
    static constexpr bool is_color = false;

    static auto channel_name(size_t channel) -> const char*
    {
        return get_vector_channel_name(channel);
    }

    static auto channel_color(size_t channel) -> color
    {
        return get_curve_channel_color(channel);
    }

    static auto get(const T& value, size_t channel) -> float
    {
        return value[int(channel)];
    }

    static void set(T& value, size_t channel, float channel_value)
    {
        value[int(channel)] = channel_value;
    }
};
} // namespace detail

template<>
struct curve_traits<float>
{
    static constexpr size_t channel_count = 1;
    static constexpr bool is_color = false;

    static auto channel_name(size_t /*channel*/) -> const char*
    {
        return "Value";
    }

    static auto channel_color(size_t /*channel*/) -> color
    {
        return color::white();
    }

    static auto get(const float& value, size_t /*channel*/) -> float
    {
        return value;
    }

    static void set(float& value, size_t /*channel*/, float channel_value)
    {
        value = channel_value;
    }
};

template<>
struct curve_traits<vec2> : detail::vector_curve_traits<vec2, 2>
{
};

template<>
struct curve_traits<vec3> : detail::vector_curve_traits<vec3, 3>
{
};

template<>
struct curve_traits<vec4> : detail::vector_curve_traits<vec4, 4>
{
};

template<>
struct curve_traits<color>
{
    static constexpr size_t channel_count = 4;
    static constexpr bool is_color = true;

    static auto channel_name(size_t channel) -> const char*
    {
        return detail::get_color_channel_name(channel);
    }

    static auto channel_color(size_t channel) -> color
    {
        return detail::get_curve_channel_color(channel);
    }

    static auto get(const color& value, size_t channel) -> float
    {
        return value.value[int(channel)];
    }

    static void set(color& value, size_t channel, float channel_value)
    {
        value.value[int(channel)] = channel_value;
    }
};

/// A range is two independent tracks, which is what a min/max curve means: the two ends move on
/// their own and the editor plots them as two lines.
template<>
struct curve_traits<frange_t>
{
    static constexpr size_t channel_count = 2;
    static constexpr bool is_color = false;

    static auto channel_name(size_t channel) -> const char*
    {
        return channel == 0 ? "Min" : "Max";
    }

    static auto channel_color(size_t channel) -> color
    {
        return detail::get_curve_channel_color(channel);
    }

    static auto get(const frange_t& value, size_t channel) -> float
    {
        return channel == 0 ? value.min : value.max;
    }

    static void set(frange_t& value, size_t channel, float channel_value)
    {
        (channel == 0 ? value.min : value.max) = channel_value;
    }
};

/**
 * @brief One key of every channel at the same position.
 *
 * The view a linked curve presents: the shape a curve had before channels existed, and the shape
 * it is written to disk and handed to tooling in. Member names follow the serialized names.
 */
template<typename T>
struct curve_point
{
    float progress = 0.0f;
    T element{};
};

template<typename T>
inline auto operator<(const curve_point<T>& lhs, const curve_point<T>& rhs) noexcept -> bool
{
    return lhs.progress < rhs.progress;
}

template<typename T>
inline auto operator==(const curve_point<T>& lhs, const curve_point<T>& rhs) noexcept -> bool
{
    return math::epsilonEqual(lhs.progress, rhs.progress, math::epsilon<float>()) && lhs.element == rhs.element;
}

/**
 * @brief A value of type T keyed over a range, one independently keyed channel per component.
 *
 * Channels are `linked` by default, meaning every channel carries a key at the same positions.
 * That is what a gradient is, and it is what the key-list half of this interface reads and writes.
 * Unlinking lets one channel be keyed on its own - separate alpha keys on a color, a size range
 * whose two ends move differently.
 *
 * Sampling is direct unless @ref bake has been called, which trades memory for a table lookup.
 */
template<typename T>
class curve
{
public:
    using traits = curve_traits<T>;
    static constexpr size_t channel_count = traits::channel_count;

    using channels_t = std::array<curve_channel, channel_count>;
    using point_t = curve_point<T>;
    using points_t = std::vector<point_t>;

    auto get_channel(size_t index) const -> const curve_channel&;
    auto get_channel(size_t index) -> curve_channel&;
    auto get_channels() const noexcept -> const channels_t&;
    void set_channels(const channels_t& channels);

    /// Whether every channel carries a key at the same positions.
    auto is_linked() const noexcept -> bool;
    /// Linking re-keys every channel onto the union of their key positions, sampling the value
    /// each channel had there, so the curve keeps its shape.
    void set_linked(bool linked);

    auto is_valid() const noexcept -> bool;
    /// Lowest and highest key position across every channel.
    auto get_domain() const -> frange_t;

    auto sample(float position) const -> T;

    /**
     * @brief Pre-samples the curve so later sampling is a table lookup.
     *
     * The table spans the curve's own domain, and is read without interpolating when any channel
     * steps, so baking never changes what the curve means. Any edit drops the table; call this
     * again once the curve settles.
     */
    void bake(size_t resolution = 256);
    void clear_bake();
    auto is_baked() const noexcept -> bool;

    /// Mirrors every key about the middle of the curve's domain.
    void reverse();
    void clear();

    auto operator==(const curve<T>& other) const -> bool;

    // ---------------------------------------------------------------------------------
    // The key-list view. A key here is one key of every channel at the same position, so these
    // read and write a linked curve; on an unlinked one they report what the channels sample.

    auto add_point(const T& element, float progress) -> size_t;
    void remove_point(int index);
    void set_points(const points_t& points);
    /**
     * @brief set_points that keeps the shape of the keys it keeps: a key left at the position of one of the curve's
     * keys keeps that key's interpolation and tangents, so writing back the points get_points read changes nothing.
     * Keys at new positions are linear with automatic tangents.
     */
    void set_points_keeping_shape(const points_t& points);
    /**
     * @brief The keys as one list.
     *
     * By value: the channels hold the keys, so there is no single list to lend out. Binding the
     * result to a `const auto&` extends its lifetime and reads as it always did.
     */
    auto get_points() const -> points_t;

    void set_progress(int index, float progress);
    auto get_progress(int index) const -> float;
    void set_element(int index, const T& element);
    auto get_element(int index) const -> T;

    /// Sets the interpolation of every key at once.
    void set_interpolation(curve_interpolation interpolation);
    /// The interpolation shared by every key, or `linear` on a curve whose keys disagree.
    auto get_interpolation() const -> curve_interpolation;

private:
    /// Sampling that reads the channels, whatever the table holds.
    auto sample_direct(float position) const -> T;
    /// The positions at which any channel has a key, in order and without duplicates.
    auto collect_key_positions() const -> std::vector<float>;
    void mark_bake_dirty();

    channels_t channels_{};
    bool linked_{true};

    std::vector<T> bake_table_{};
    frange_t bake_domain_{};
    /// False when a channel steps: the table is then read without interpolating, so the step
    /// survives at the table's resolution instead of becoming a one-cell ramp.
    bool bake_interpolates_{true};
};

} // namespace math

#include "curve.hpp"
