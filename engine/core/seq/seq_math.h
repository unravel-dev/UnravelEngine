#pragma once
#include "seq_common.h"
#include "seq_ease.h"
#include <algorithm>

/**
 * @namespace seq
 * @brief Provides mathematical utilities for interpolation, scaling, and easing functions.
 */
namespace seq
{

/**
 * @brief Linearly interpolates between two values based on progress.
 * @tparam T The type of the values to interpolate.
 * @param start The starting value.
 * @param end The ending value.
 * @param progress The progress factor (0.0f to 1.0f).
 * @param ease_func The easing function to apply (default is linear easing).
 * @return The interpolated value.
 */
template<typename T>
auto lerp(const T& start, const T& end, float progress, const ease_t& ease_func = ease::linear) -> T;

/**
 * @brief Maps a value from one range to another range, with optional easing.
 * @tparam InType The type of the input range values.
 * @tparam OutType The type of the output range values.
 * @param in The input value.
 * @param in_start The start of the input range.
 * @param in_end The end of the input range.
 * @param out_start The start of the output range.
 * @param out_end The end of the output range.
 * @param ease_func The easing function to apply (default is linear easing).
 * @return The mapped value in the output range.
 */
template<typename InType, typename OutType>
auto range_map(const InType& in,
               const decltype(in)& in_start,
               const decltype(in)& in_end,
               const OutType& out_start,
               const decltype(out_start)& out_end,
               const ease_t& ease_func = ease::linear) -> OutType;

/**
 * @brief Clamps a value to lie between a minimum and maximum.
 * @tparam T The type of the value.
 * @param value The value to clamp.
 * @param min The minimum bound.
 * @param max The maximum bound.
 * @return The clamped value.
 */
template<typename T>
auto clamp(const T& value, const T& min, const T& max) -> T
{
    return std::min<T>(std::max<T>(value, min), max);
}

/**
 * @brief Computes the square of a number raised to a power.
 * @param x The base value.
 * @param n The exponent.
 * @return The squared value.
 */
auto square(float x, int n) -> float;

/**
 * @brief Flips a normalized value (1.0 becomes 0.0, 0.0 becomes 1.0).
 * @param x The normalized value to flip.
 * @return The flipped value.
 */
auto flip(float x) -> float;

/**
 * @brief Mixes two values based on a weighted progress factor.
 * @param a The first value.
 * @param b The second value.
 * @param weight The weight of the mix.
 * @param t The progress factor (0.0f to 1.0f).
 * @return The mixed value.
 */
auto mix(float a, float b, float weight, float t) -> float;

/**
 * @brief Creates a crossfade effect between two values based on progress.
 * @param a The first value.
 * @param b The second value.
 * @param t The progress factor (0.0f to 1.0f).
 * @return The crossfaded value.
 */
auto crossfade(float a, float b, float t) -> float;

/**
 * @brief Scales a value by a factor.
 * @param a The value to scale.
 * @param t The scaling factor.
 * @return The scaled value.
 */
auto scale(float a, float t) -> float;

/**
 * @brief Scales a value in reverse by a factor.
 * @param a The value to scale.
 * @param t The scaling factor.
 * @return The reverse-scaled value.
 */
auto reverse_scale(float a, float t) -> float;

/**
 * @brief Computes an arch effect (parabolic curve) based on progress.
 * @param t The progress factor (0.0f to 1.0f).
 * @return The arch value.
 */
auto arch(float t) -> float;

} // namespace seq

#include "seq_math.hpp"
