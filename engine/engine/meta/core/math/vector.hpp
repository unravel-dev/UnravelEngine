#pragma once

#include <math/math.h>
#include <ser20/types/array.hpp>
#include <serialization/packed_value.h>
#include <serialization/serialization.h>
#include <serialization/types/vector.hpp>

namespace ser20
{
// Vectors and colors are one packed value in a binary document; JSON keeps their named members.
template<typename T, math::qualifier P>
struct is_packed_value<math::tvec2<T, P>> : std::true_type
{
};
template<typename T, math::qualifier P>
struct is_packed_value<math::tvec3<T, P>> : std::true_type
{
};
template<typename T, math::qualifier P>
struct is_packed_value<math::tvec4<T, P>> : std::true_type
{
};
template<>
struct is_packed_value<math::color> : std::true_type
{
};

template<typename Archive, typename T, math::qualifier P>
inline void SERIALIZE_FUNCTION_NAME(Archive& ar, math::tvec2<T, P>& obj)
{
    serialization::serialize_components(ar, std::array<T*, 2>{&obj.x, &obj.y}, serialization::xy_names);
}

template<typename Archive, typename T, math::qualifier P>
inline void SERIALIZE_FUNCTION_NAME(Archive& ar, math::tvec3<T, P>& obj)
{
    serialization::serialize_components(ar, std::array<T*, 3>{&obj.x, &obj.y, &obj.z}, serialization::xyz_names);
}

template<typename Archive, typename T, math::qualifier P>
inline void SERIALIZE_FUNCTION_NAME(Archive& ar, math::tvec4<T, P>& obj)
{
    serialization::serialize_components(ar,
                                        std::array<T*, 4>{&obj.x, &obj.y, &obj.z, &obj.w},
                                        serialization::xyzw_names);
}

template<typename Archive>
inline void SERIALIZE_FUNCTION_NAME(Archive& ar, math::color& obj)
{
    serialization::serialize_components(ar,
                                        std::array<float*, 4>{&obj.value.r, &obj.value.g, &obj.value.b, &obj.value.a},
                                        serialization::rgba_names);
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
    // Read over the live keys: a load that lists some of them, or some of their members, keeps the others.
    math::curve_channel::keys_t keys = obj.get_keys();
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
    bool linked = true;
    try_load(ar, ser20::make_nvp("linked", linked));
    typename math::curve<T>::channels_t channels = obj.get_channels();
    if(try_load(ar, ser20::make_nvp("channels", channels)))
    {
        obj.set_channels(channels);
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
