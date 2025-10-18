#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include "color.h"

namespace math
{
using namespace glm;

    
template<typename T>
auto gradient_lerp(const T& start, const T& end, float progress) -> T;
// {
//     static_assert(false, "gradient_lerp is not implemented for this type");
//     return T();
// }

enum class gradient_interpolation_mode_t
{
    linear,
    constant
};

template<typename T>
struct gradient_point
{
    float progress = 0.0;
    T element{};
};

template<typename T>
inline auto operator<(const gradient_point<T>& lhs, const gradient_point<T>& rhs) noexcept -> bool
{
    return lhs.progress < rhs.progress;
}

template<typename T>
inline auto operator==(const gradient_point<T>& lhs, const gradient_point<T>& rhs) noexcept -> bool
{
    return math::epsilonEqual(lhs.progress, rhs.progress, math::epsilon<float>()) &&
           lhs.element == rhs.element;
}

template<typename T>
class gradient
{
public:
    using point_t = gradient_point<T>;
    using points_t = std::vector<point_t>;

    auto add_point(const T& element, float progress) -> size_t;
    void remove_point(int index);

    void set_points(const points_t& points);
    auto get_points() const noexcept -> const points_t&;

    void set_progress(int index, float progress);
    auto get_progress(int index) -> float;

    void set_element(int index, const T& element);
    auto get_element(int index) -> T;

    void reverse();

    auto is_valid() const noexcept -> bool;
    auto sample(float progress) const -> T;

    void set_interpolation_mode(gradient_interpolation_mode_t mode);
    auto get_interpolation_mode() const noexcept -> gradient_interpolation_mode_t;

    // LUT functionality
    void generate_lut(size_t lut_size = 256);
    void clear_lut();
    bool has_lut() const noexcept { return !lut_.empty(); }

    auto operator==(const gradient<T>& other) const -> bool;
private:
    points_t points_{};
    gradient_interpolation_mode_t interpolation_mode_ = gradient_interpolation_mode_t::linear;
    
    // LUT cache for O(1) sampling
    mutable std::vector<T> lut_;
    mutable size_t lut_size_ = 0;
    mutable bool lut_dirty_ = true;
    
    // Internal methods
    void regenerate_lut_if_needed() const;
    T sample_from_lut(float progress) const;
    T sample_original(float progress) const;
    void mark_lut_dirty() const { lut_dirty_ = true; }
};

} // namespace math

#include "gradient.hpp"
