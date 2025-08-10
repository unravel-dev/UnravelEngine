#pragma once

#include "point.hpp"
#include <cmath>
#include <functional>

namespace input
{
//  Deadzone filter functions for a single value
using deadzone_float_filter = std::function<float(const float, const float)>;

//  Deadzone filter functions for two values (XY)
using deadzone_point_filter = std::function<point(const float, const float, const float)>;

//  ----------------------------------------------------------------------------
static auto no_deadzone(const float deadzone, const float value) -> float
{
    return value;
}

//  ----------------------------------------------------------------------------
static auto basic_deadzone(const float deadzone, const float value) -> float
{
    if(std::abs(value) >= deadzone)
    {
        return value;
    }

    return 0.0f;
}

//  ----------------------------------------------------------------------------
static auto no_deadzone(const float deadzone, const float x, const float y) -> point
{
    return {x, y};
}

//  ----------------------------------------------------------------------------
static auto radial_deadzone(const float deadzone, const float x, const float y) -> point
{
    //  Calculate vector length
    const double length = std::sqrt(x * x + y * y);

    //  Normalize if needed
    if(length > 1.0f)
    {
        const float nx = x / length;
        const float ny = y / length;
        return radial_deadzone(deadzone, nx, ny);
    }

    if(length >= deadzone)
    {
        return {x, y};
    }

    return {0.0f, 0.0f};
}
} // namespace input
