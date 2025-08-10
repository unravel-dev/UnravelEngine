#pragma once

namespace input
{
struct point
{
    float x;
    float y;

    point(float x = 0, float y = 0) : x(x), y(y)
    {
    }
};

inline auto operator==(const point& p1, const point& p2) -> bool
{
    return p1.x == p2.x && p1.y == p2.y;
}

inline auto operator!=(const point& p1, const point& p2) -> bool
{
    return p1.x != p2.x || p1.y != p2.y;
}
} // namespace input
