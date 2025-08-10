#include "seq_math.h"

namespace seq
{
auto square(float x, int n) -> float
{
    for(int i = 0; i < n; i++)
    {
        x *= x;
    }
    return x;
}

auto flip(float x) -> float
{
    return 1.0f - x;
}

auto mix(float a, float b, float weight, float t) -> float
{
    const float mix = ((1.0f - weight) * a) + ((1.0f - weight) * b);
    return mix * t;
}

auto crossfade(float a, float b, float t) -> float
{
    return ((1.0f - t) * a) + (t * b);
}

auto scale(float a, float t) -> float
{
    return a * t;
}

auto reverse_scale(float a, float t) -> float
{
    return a * (1.0f - t);
}

auto arch(float t) -> float
{
    return t * (1.0f - t);
}

} // namespace seq
