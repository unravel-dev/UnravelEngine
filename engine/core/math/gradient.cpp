#include "gradient.h"

namespace math
{
 

template<>
auto gradient_lerp(const vec4& start, const vec4& end, float progress) -> vec4
{
    return glm::lerp(start, end, vec4(progress));
}

template<>
auto gradient_lerp(const vec3& start, const vec3& end, float progress) -> vec3
{
    return glm::lerp(start, end, vec3(progress));
}

template<>
auto gradient_lerp(const vec2& start, const vec2& end, float progress) -> vec2
{
    return glm::lerp(start, end, vec2(progress));
}


template<>
auto gradient_lerp(const float& start, const float& end, float progress) -> float
{
    return glm::lerp(start, end, progress);
}

template<>
auto gradient_lerp(const color& start, const color& end, float progress) -> color
{
    return color(gradient_lerp(start.value, end.value, progress));
}
auto to_string(gradient_interpolation_mode_t mode) -> std::string
{
    switch(mode)
    {
        case gradient_interpolation_mode_t::constant:
            return "constant";
        case gradient_interpolation_mode_t::linear:
            return "linear";
        default:
            return "constant";
    }

}
auto interpolation_mode_from_string(const std::string& mode) -> gradient_interpolation_mode_t
{
    if(mode == "constant")
    {
        return gradient_interpolation_mode_t::constant;
    }
    else if(mode == "linear")
    {
        return gradient_interpolation_mode_t::linear;
    }

    return gradient_interpolation_mode_t::constant;
}



} // namespace math
