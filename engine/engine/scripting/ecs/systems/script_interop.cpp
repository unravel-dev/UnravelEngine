#include "script_interop.h"

namespace mono
{
namespace managed_interface
{

template<>
auto converter::convert(const math::vec2& v) -> vector2
{
    return {v.x, v.y};
}

template<>
auto converter::convert(const vector2& v) -> math::vec2
{
    return {v.x, v.y};
}

template<>
auto converter::convert(const math::vec3& v) -> vector3
{
    return {v.x, v.y, v.z};
}

template<>
auto converter::convert(const vector3& v) -> math::vec3
{
    return {v.x, v.y, v.z};
}

template<>
auto converter::convert(const math::vec4& v) -> vector4
{
    return {v.x, v.y, v.z, v.w};
}

template<>
auto converter::convert(const vector4& v) -> math::vec4
{
    return {v.x, v.y, v.z, v.w};
}

template<>
auto converter::convert(const math::quat& q) -> quaternion
{
    return {q.x, q.y, q.z, q.w};
}

template<>
auto converter::convert(const quaternion& q) -> math::quat
{
    return math::quat::wxyz(q.w, q.x, q.y, q.z);
}

template<>
auto converter::convert(const math::color& v) -> color
{
    return {v.value.r, v.value.g, v.value.b, v.value.a};
}

template<>
auto converter::convert(const color& v) -> math::color
{
    return {v.r, v.g, v.b, v.a};
}

template<>
auto converter::convert(const math::bbox& v) -> bbox
{
    return {{v.min.x, v.min.y, v.min.z}, {v.max.x, v.max.y, v.max.z}};
}

template<>
auto converter::convert(const bbox& v) -> math::bbox
{
    return {{v.min.x, v.min.y, v.min.z}, {v.max.x, v.max.y, v.max.z}};
}
}
}
