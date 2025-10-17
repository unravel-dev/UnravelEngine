#pragma once

#include <math/math.h>
#include <serialization/serialization.h>
#include <serialization/types/vector.hpp>

namespace ser20
{
template<typename Archive, typename T, math::qualifier P>
inline void SERIALIZE_FUNCTION_NAME(Archive& ar, math::tvec2<T, P>& obj)
{
    try_serialize(ar, ser20::make_nvp("x", obj.x));
    try_serialize(ar, ser20::make_nvp("y", obj.y));
}

template<typename Archive, typename T, math::qualifier P>
inline void SERIALIZE_FUNCTION_NAME(Archive& ar, math::tvec3<T, P>& obj)
{
    try_serialize(ar, ser20::make_nvp("x", obj.x));
    try_serialize(ar, ser20::make_nvp("y", obj.y));
    try_serialize(ar, ser20::make_nvp("z", obj.z));
}

template<typename Archive, typename T, math::qualifier P>
inline void SERIALIZE_FUNCTION_NAME(Archive& ar, math::tvec4<T, P>& obj)
{
    try_serialize(ar, ser20::make_nvp("x", obj.x));
    try_serialize(ar, ser20::make_nvp("y", obj.y));
    try_serialize(ar, ser20::make_nvp("z", obj.z));
    try_serialize(ar, ser20::make_nvp("w", obj.w));
}

template<typename Archive>
inline void SERIALIZE_FUNCTION_NAME(Archive& ar, math::color& obj)
{
    try_serialize(ar, ser20::make_nvp("r", obj.value.r));
    try_serialize(ar, ser20::make_nvp("g", obj.value.g));
    try_serialize(ar, ser20::make_nvp("b", obj.value.b));
    try_serialize(ar, ser20::make_nvp("a", obj.value.a));
}


template<typename Archive, typename T>
inline void SERIALIZE_FUNCTION_NAME(Archive& ar, math::gradient_point<T>& obj)
{
    try_serialize(ar, ser20::make_nvp("progress", obj.progress));
    try_serialize(ar, ser20::make_nvp("element", obj.element));
}


template<typename Archive, typename T>
inline void SAVE_FUNCTION_NAME(Archive& ar, const math::gradient<T>& obj)
{
    const auto& points = obj.get_points();
    try_save(ar, ser20::make_nvp("points", points));
}
template<typename Archive, typename T>
inline void LOAD_FUNCTION_NAME(Archive& ar, math::gradient<T>& obj)
{
    std::vector<typename math::gradient<T>::point_t> points;
    if(try_load(ar, ser20::make_nvp("points", points)))
    {
        obj.set_points(points);
    }
}


} // namespace ser20
