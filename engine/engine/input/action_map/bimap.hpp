#pragma once

#include <map>

namespace input
{
template<typename K, typename V>
class bimap
{
    std::map<V, K> key_by_value_;
    std::map<K, V> values_by_key_;

public:
    void clear()
    {
        key_by_value_.clear();
        values_by_key_.clear();
    }

    auto get_key(const V value) const -> K
    {
        return key_by_value_.at(value);
    }

    auto get_key(const V value, const K defaultKey) const -> K
    {
        const auto& find = key_by_value_.find(value);
        if(find == key_by_value_.end())
        {
            return defaultKey;
        }

        return find->second;
    }

    auto get_value(const K key) const -> V
    {
        return values_by_key_.at(key);
    }

    auto get_value(const K key, const V defaultValue) const -> V
    {
        const auto& find = values_by_key_.find(key);
        if(find == values_by_key_.end())
        {
            return defaultValue;
        }

        return find->second;
    }

    void map(const K key, const V value)
    {
        key_by_value_.emplace(value, key);
        values_by_key_.emplace(key, value);
    }
};
} // namespace InputLib
