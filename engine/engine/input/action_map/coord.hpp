#pragma once

namespace input
{
struct coord
{
    int x;
    int y;

    coord(int x = 0, int y = 0) : x(x), y(y)
    {
    }
};

inline auto operator==(const coord& p1, const coord& p2) -> bool
{
    return p1.x == p2.x && p1.y == p2.y;
}

inline auto operator!=(const coord& p1, const coord& p2) -> bool
{
    return p1.x != p2.x || p1.y != p2.y;
}
} // namespace input
