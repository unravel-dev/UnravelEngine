#pragma once

#include <hpp/string_view.hpp>

#include <algorithm>
#include <vector>

namespace unravel
{

/// The entry of @p values named @p name (each value has a `name`), const as @p values is; null for none.
template<typename Values>
auto find_named(Values& values, hpp::string_view name) -> decltype(values.data())
{
    const auto found = std::find_if(values.begin(),
                                    values.end(),
                                    [&](const auto& value)
                                    {
                                        return value.name == name;
                                    });
    return found == values.end() ? nullptr : &*found;
}

/**
 * @brief What the entries that differ between @p values and @p previous change: the union of @p aspects_of(name) over
 * each one added, dropped or changed.
 */
template<typename Values, typename AspectsOf>
auto get_named_changes(const Values& values, const Values& previous, AspectsOf&& aspects_of)
    -> decltype(aspects_of(values.front().name))
{
    decltype(aspects_of(values.front().name)) changes{};
    for(const auto& entry : values)
    {
        const auto* match = find_named(previous, entry.name);
        if(match == nullptr || !(*match == entry))
        {
            changes |= aspects_of(entry.name);
        }
    }
    for(const auto& entry : previous)
    {
        if(find_named(values, entry.name) == nullptr)
        {
            changes |= aspects_of(entry.name);
        }
    }
    return changes;
}

/// Drops the entries of @p values named @p name; false when there was none.
template<typename Value>
auto erase_named(std::vector<Value>& values, hpp::string_view name) -> bool
{
    return std::erase_if(values,
                         [&](const Value& value)
                         {
                             return value.name == name;
                         }) > 0;
}

} // namespace unravel
