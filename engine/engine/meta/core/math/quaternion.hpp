#pragma once

#include <math/math.h>
#include <serialization/packed_value.h>
#include <serialization/serialization.h>

namespace ser20
{
// A quaternion is one packed value in a binary document; JSON keeps its named members.
template<typename T, math::qualifier P>
struct is_packed_value<math::tquat<T, P>> : std::true_type
{
};

template<typename Archive, typename T, math::qualifier P>
inline void SERIALIZE_FUNCTION_NAME(Archive& ar, math::tquat<T, P>& obj)
{
    serialization::serialize_components(ar,
                                        std::array<T*, 4>{&obj.x, &obj.y, &obj.z, &obj.w},
                                        serialization::xyzw_names);
}
} // namespace ser20
