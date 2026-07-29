#pragma once
#include "quaternion.hpp"
#include "vector.hpp"

namespace ser20
{
template<typename Archive, typename T, math::qualifier P>
inline void SERIALIZE_FUNCTION_NAME(Archive& ar, math::tmat2x2<T, P>& obj)
{
    try_serialize(ar, ser20::make_nvp("col_0", obj[0]));
    try_serialize(ar, ser20::make_nvp("col_1", obj[1]));
}

template<typename Archive, typename T, math::qualifier P>
inline void SERIALIZE_FUNCTION_NAME(Archive& ar, math::tmat2x3<T, P>& obj)
{
    try_serialize(ar, ser20::make_nvp("col_0", obj[0]));
    try_serialize(ar, ser20::make_nvp("col_1", obj[1]));
    try_serialize(ar, ser20::make_nvp("col_2", obj[2]));
}
template<typename Archive, typename T, math::qualifier P>
inline void SERIALIZE_FUNCTION_NAME(Archive& ar, math::tmat2x4<T, P>& obj)
{
    try_serialize(ar, ser20::make_nvp("col_0", obj[0]));
    try_serialize(ar, ser20::make_nvp("col_1", obj[1]));
    try_serialize(ar, ser20::make_nvp("col_2", obj[2]));
    try_serialize(ar, ser20::make_nvp("col_3", obj[3]));
}

template<typename Archive, typename T, math::qualifier P>
inline void SERIALIZE_FUNCTION_NAME(Archive& ar, math::tmat3x2<T, P>& obj)
{
    try_serialize(ar, ser20::make_nvp("col_0", obj[0]));
    try_serialize(ar, ser20::make_nvp("col_1", obj[1]));
}

template<typename Archive, typename T, math::qualifier P>
inline void SERIALIZE_FUNCTION_NAME(Archive& ar, math::tmat4x2<T, P>& obj)
{
    try_serialize(ar, ser20::make_nvp("col_0", obj[0]));
    try_serialize(ar, ser20::make_nvp("col_1", obj[1]));
}

template<typename Archive, typename T, math::qualifier P>
inline void SERIALIZE_FUNCTION_NAME(Archive& ar, math::tmat3x3<T, P>& obj)
{
    try_serialize(ar, ser20::make_nvp("col_0", obj[0]));
    try_serialize(ar, ser20::make_nvp("col_1", obj[1]));
    try_serialize(ar, ser20::make_nvp("col_2", obj[2]));
}

template<typename Archive, typename T, math::qualifier P>
inline void SERIALIZE_FUNCTION_NAME(Archive& ar, math::tmat3x4<T, P>& obj)
{
    try_serialize(ar, ser20::make_nvp("col_0", obj[0]));
    try_serialize(ar, ser20::make_nvp("col_1", obj[1]));
    try_serialize(ar, ser20::make_nvp("col_2", obj[2]));
    try_serialize(ar, ser20::make_nvp("col_3", obj[3]));
}

template<typename Archive, typename T, math::qualifier P>
inline void SERIALIZE_FUNCTION_NAME(Archive& ar, math::tmat4x3<T, P>& obj)
{
    try_serialize(ar, ser20::make_nvp("col_0", obj[0]));
    try_serialize(ar, ser20::make_nvp("col_1", obj[1]));
    try_serialize(ar, ser20::make_nvp("col_2", obj[2]));
}

template<typename Archive, typename T, math::qualifier P>
inline void SERIALIZE_FUNCTION_NAME(Archive& ar, math::tmat4x4<T, P>& obj)
{
    try_serialize(ar, ser20::make_nvp("col_0", obj[0]));
    try_serialize(ar, ser20::make_nvp("col_1", obj[1]));
    try_serialize(ar, ser20::make_nvp("col_2", obj[2]));
    try_serialize(ar, ser20::make_nvp("col_3", obj[3]));
}

template<typename Archive, typename T, math::qualifier P>
inline void SERIALIZE_FUNCTION_NAME(Archive& ar, math::transform_t<T, P>& obj)
{
    // Seed from current values first. try_serialize may skip fields (prefab overrides);
    // on failure the locals keep the current object state so apply is a no-op for those.
    auto pos = obj.get_position();
    auto rotation_euler = obj.get_rotation_euler_degrees();
    auto rotation = obj.get_rotation();
    auto scale = obj.get_scale();
    auto skew = obj.get_skew();
    auto euler_hint = rotation_euler;

    try_serialize(ar, ser20::make_nvp("position", pos));

    if constexpr(is_loading_archive<Archive>())
    {
        bool has_rotation_euler = false;
        bool has_rotation = false;

        if constexpr(is_binary_archive<Archive>())
        {
            has_rotation_euler = try_serialize(ar, ser20::make_nvp("rotation_euler", rotation_euler));
        }
        else
        {
            // Associative: prefer authored Euler, fall back to legacy quat / euler_hint.
            has_rotation_euler = try_serialize(ar, ser20::make_nvp("rotation_euler", rotation_euler));
            has_rotation = try_serialize(ar, ser20::make_nvp("rotation", rotation));
        }

        try_serialize(ar, ser20::make_nvp("scale", scale));
        try_serialize(ar, ser20::make_nvp("skew", skew));

        obj.set_position(pos);
        obj.set_scale(scale);
        obj.set_skew(skew);

        if(has_rotation_euler)
        {
            obj.set_rotation_euler_degrees(rotation_euler);
        }
        else if(has_rotation)
        {
            obj.set_rotation(rotation);
        }
    }
    else
    {
        try_serialize(ar, ser20::make_nvp("rotation_euler", rotation_euler));
        try_serialize(ar, ser20::make_nvp("scale", scale));
        try_serialize(ar, ser20::make_nvp("skew", skew));
    }
}
} // namespace ser20
