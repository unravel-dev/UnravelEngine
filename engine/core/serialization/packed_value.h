#pragma once
#include "serialization.h"

#include <array>
#include <cstddef>
#include <type_traits>

namespace ser20
{
/**
 * @brief Values made of a few numbers - a vector, a quaternion, a color - that a binary document stores as
 *        one packed value instead of an object of named numbers. JSON keeps the object.
 *
 * Specialized to true beside a type's serialization, which goes through serialization::serialize_components.
 */
template<class T>
struct is_packed_value : std::false_type
{
};

template<class T>
inline constexpr bool is_packed_value_v = is_packed_value<std::remove_cv_t<T>>::value;
} // namespace ser20

namespace serialization
{
/// Archives that store packed values: the binary archive (serialization/binary_archive.h).
template<typename Archive>
concept stores_packed_values = Archive::stores_packed_values;

/// Whether the path filter in force, if any, keeps every one of the components named.
template<std::size_t N>
auto keeps_every_component(const std::array<ser20::literal_name, N>& names) -> bool
{
    const auto* path = get_path_context();
    if(path == nullptr || !path->is_recording())
    {
        return true;
    }
    for(const auto& name : names)
    {
        if(!serialize_check(name.text,
                            []() -> bool
                            {
                                return true;
                            }))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Serializes a value made of a few numbers as its named components.
 *
 * JSON writes and reads them as members of an object, as it always has. A binary document holds them as one
 * packed value, and reads the object form too (a document converted from JSON). A filtered load (a prefab
 * override path such as "position/x") still takes each component by its name, so an override of one
 * component keeps working on a packed value; a filtered save that leaves a component out writes the object
 * form with the components it keeps, as JSON does.
 */
template<typename Archive, typename T, std::size_t N>
void serialize_components(Archive& ar,
                          const std::array<T*, N>& components,
                          const std::array<ser20::literal_name, N>& names)
{
    if constexpr(stores_packed_values<Archive>)
    {
        if constexpr(is_loading_archive<Archive>())
        {
            if(ar.isPackedValueNext())
            {
                T values[N]{};
                ar.loadPackedValue(values, N);
                for(std::size_t index = 0; index < N; ++index)
                {
                    serialize_check(names[index].text,
                                    [&]() -> bool
                                    {
                                        *components[index] = values[index];
                                        return true;
                                    });
                }
                return;
            }
            ar.startNode();
            for(std::size_t index = 0; index < N; ++index)
            {
                try_serialize(ar, ser20::make_nvp(names[index], *components[index]));
            }
            ar.finishNode();
        }
        else if(keeps_every_component(names))
        {
            T values[N]{};
            for(std::size_t index = 0; index < N; ++index)
            {
                values[index] = *components[index];
            }
            ar.savePackedValue(values, N);
        }
        else
        {
            ar.startNode();
            for(std::size_t index = 0; index < N; ++index)
            {
                try_serialize(ar, ser20::make_nvp(names[index], *components[index]));
            }
            ar.finishNode();
        }
    }
    else
    {
        for(std::size_t index = 0; index < N; ++index)
        {
            try_serialize(ar, ser20::make_nvp(names[index], *components[index]));
        }
    }
}

/// Component names of the packed values.
inline constexpr std::array<ser20::literal_name, 2> xy_names{ser20::literal_name("x"), ser20::literal_name("y")};
inline constexpr std::array<ser20::literal_name, 3> xyz_names{ser20::literal_name("x"),
                                                              ser20::literal_name("y"),
                                                              ser20::literal_name("z")};
inline constexpr std::array<ser20::literal_name, 4> xyzw_names{ser20::literal_name("x"),
                                                               ser20::literal_name("y"),
                                                               ser20::literal_name("z"),
                                                               ser20::literal_name("w")};
inline constexpr std::array<ser20::literal_name, 4> rgba_names{ser20::literal_name("r"),
                                                               ser20::literal_name("g"),
                                                               ser20::literal_name("b"),
                                                               ser20::literal_name("a")};
} // namespace serialization
