#pragma once
#include "../serialization.h"

#include <ser20/types/vector.hpp>

#include <vector>

namespace serialization
{
/**
 * @brief A list of dense elements of one shape - a mesh's triangles, an animation's keys - that a binary
 *        document stores as a packed list (rows of bare values, archives/tagged_binary.hpp) and JSON as an
 *        ordinary array of objects.
 *
 * Made by packed(); saved and loaded like the vector it wraps:
 *
 *     try_save(ar, ser20::make_nvp("triangle_data", serialization::packed(obj.triangle_data)));
 *     try_load(ar, ser20::make_nvp("triangle_data", serialization::packed(obj.triangle_data)));
 *
 * For compiled data: rows are read back by the code that wrote them, so an element's load must read every
 * value its save wrote, and the asset's format version changes with that code. A prefab override path
 * does not reach inside the list.
 */
template<typename V>
struct packed_list
{
    V* values = nullptr;
};

/// The vector as a packed list.
template<typename T, typename A>
auto packed(std::vector<T, A>& values) -> packed_list<std::vector<T, A>>
{
    return {&values};
}

/// The vector as a packed list, for a save.
template<typename T, typename A>
auto packed(const std::vector<T, A>& values) -> packed_list<const std::vector<T, A>>
{
    return {&values};
}

} // namespace serialization

namespace ser20
{
/// A packed list in an archive without packed lists: the vector it wraps, in the node its name opened.
template<class Archive, class V>
inline void SER20_SAVE_FUNCTION_NAME(Archive& ar, const serialization::packed_list<V>& list)
{
    SER20_SAVE_FUNCTION_NAME(ar, *list.values);
}

template<class Archive, class V>
inline void SER20_LOAD_FUNCTION_NAME(Archive& ar, serialization::packed_list<V>& list)
{
    SER20_LOAD_FUNCTION_NAME(ar, *list.values);
}
} // namespace ser20
