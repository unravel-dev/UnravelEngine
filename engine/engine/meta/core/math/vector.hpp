#pragma once

#include <math/math.h>
#include <ser20/types/array.hpp>
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
inline void SERIALIZE_FUNCTION_NAME(Archive& ar, math::curve_point<T>& obj)
{
    try_serialize(ar, ser20::make_nvp("progress", obj.progress));
    try_serialize(ar, ser20::make_nvp("element", obj.element));
}

template<typename Archive>
inline void SERIALIZE_FUNCTION_NAME(Archive& ar, math::curve_key& obj)
{
    try_serialize(ar, ser20::make_nvp("position", obj.position));
    try_serialize(ar, ser20::make_nvp("value", obj.value));
    try_serialize(ar, ser20::make_nvp("in_tangent", obj.in_tangent));
    try_serialize(ar, ser20::make_nvp("out_tangent", obj.out_tangent));
    try_serialize(ar, ser20::make_nvp("interpolation", obj.interpolation));
    try_serialize(ar, ser20::make_nvp("tangent_mode", obj.tangent_mode));
}

template<typename Archive>
inline void SAVE_FUNCTION_NAME(Archive& ar, const math::curve_channel& obj)
{
    try_save(ar, ser20::make_nvp("keys", obj.get_keys()));
}

template<typename Archive>
inline void LOAD_FUNCTION_NAME(Archive& ar, math::curve_channel& obj)
{
    math::curve_channel::keys_t keys;
    if(try_load(ar, ser20::make_nvp("keys", keys)))
    {
        obj.set_keys(std::move(keys));
    }
}

/// Written as one key list per channel. Curves saved before channels existed carry a single
/// `points` list instead, which @ref LOAD_FUNCTION_NAME reads as a linked curve.
template<typename Archive, typename T>
inline void SAVE_FUNCTION_NAME(Archive& ar, const math::curve<T>& obj)
{
    try_save(ar, ser20::make_nvp("linked", obj.is_linked()));
    try_save(ar, ser20::make_nvp("channels", obj.get_channels()));
}

template<typename Archive, typename T>
inline void LOAD_FUNCTION_NAME(Archive& ar, math::curve<T>& obj)
{
    typename math::curve<T>::channels_t channels;
    if(try_load(ar, ser20::make_nvp("channels", channels)))
    {
        obj.set_channels(channels);
        bool linked = true;
        try_load(ar, ser20::make_nvp("linked", linked));
        // Only unlinking is applied: re-linking here would re-key the channels that were just
        // read, and a curve saved as linked already is.
        if(!linked)
        {
            obj.set_linked(false);
        }
        return;
    }

    // The shape curves were saved in before channels existed: one key of every channel at the
    // same position, always interpolating linearly.
    std::vector<typename math::curve<T>::point_t> points;
    if(try_load(ar, ser20::make_nvp("points", points)))
    {
        obj.set_points(points);
        obj.set_interpolation(math::curve_interpolation::linear);
    }
}


} // namespace ser20
