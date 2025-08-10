#pragma once
#include <cmath>

namespace input
{
enum class input_type
{
    axis,
    button,
    key,
};

inline auto epsilon_not_equal(float x, float y) -> bool
{
    constexpr float epsilon = 0.0001f;
    return std::abs(x - y) >= epsilon;
}
}
