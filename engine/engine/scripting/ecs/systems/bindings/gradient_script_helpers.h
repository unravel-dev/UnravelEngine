#pragma once

#include "../script_interop.h"

#include <math/math.h>

#include <algorithm>
#include <vector>

namespace unravel
{

// Gradients cross to scripts as arrays of keys. Managed code passes and receives them as raw bytes
// (byte[] <-> the key structs below), which carry any blittable layout.

/// Managed GradientKey.
struct gradient_key
{
    float time{};
    dotnetpp_backend::managed_interface::color color{};
};

/// Managed CurveKey.
struct curve_value_key
{
    float time{};
    float value{};
};

/// @p base with the points of @p keys: keys that stay where @p base has keys keep their interpolation and
/// tangents, so a curve read by a script and written back is unchanged.
template<typename T, typename Key, typename ToElement>
auto make_script_gradient(const std::vector<Key>& keys, ToElement&& to_element, const math::gradient<T>& base)
    -> math::gradient<T>
{
    typename math::gradient<T>::points_t points;
    points.reserve(keys.size());
    for(const Key& key : keys)
    {
        points.push_back({key.time, to_element(key)});
    }
    std::sort(points.begin(), points.end());
    math::gradient<T> gradient = base;
    gradient.set_points_keeping_shape(points);
    return gradient;
}

inline auto to_value_keys(const math::gradient<float>& curve) -> std::vector<curve_value_key>
{
    std::vector<curve_value_key> keys;
    for(const auto& point : curve.get_points())
    {
        keys.push_back({point.progress, point.element});
    }
    return keys;
}

inline auto to_value_curve(const std::vector<curve_value_key>& keys, const math::gradient<float>& base)
    -> math::gradient<float>
{
    return make_script_gradient<float>(keys,
                                       [](const curve_value_key& key)
                                       {
                                           return key.value;
                                       },
                                       base);
}

inline auto to_color_keys(const math::gradient<math::color>& gradient) -> std::vector<gradient_key>
{
    using converter = dotnet::managed_interface::converter;
    std::vector<gradient_key> keys;
    for(const auto& point : gradient.get_points())
    {
        keys.push_back({point.progress,
                        converter::convert<math::color, dotnetpp_backend::managed_interface::color>(point.element)});
    }
    return keys;
}

inline auto to_color_gradient(const std::vector<gradient_key>& keys, const math::gradient<math::color>& base)
    -> math::gradient<math::color>
{
    using converter = dotnet::managed_interface::converter;
    return make_script_gradient<math::color>(keys,
                                             [](const gradient_key& key)
                                             {
                                                 return converter::convert<dotnetpp_backend::managed_interface::color,
                                                                           math::color>(key.color);
                                             },
                                             base);
}

} // namespace unravel
