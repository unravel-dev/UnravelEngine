#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace serialization
{
/**
 * @brief Small indices by name hash (ser20::name_hash): what a binary document's dictionary and the loaders that
 *        read many names look a name up in, comparing numbers, never text.
 *
 * Open addressing over a power-of-two table kept at most half full, so a lookup is a probe or two.
 */
class name_index
{
public:
    /// What find returns for a hash the index does not hold.
    static constexpr uint32_t none = 0xFFFFFFFFu;

    /// Makes room for count entries without growing.
    void reserve(size_t count)
    {
        size_t size = 16;
        while(size < count * 2)
        {
            size *= 2;
        }
        if(size > slots_.size())
        {
            rebuild(size);
        }
    }

    /// Adds hash with its index; a hash already held keeps the index it was added with.
    void insert(uint64_t hash, uint32_t index)
    {
        if((count_ + 1) * 2 > slots_.size())
        {
            rebuild(slots_.empty() ? 16 : slots_.size() * 2);
        }
        if(place(hash, index))
        {
            ++count_;
        }
    }

    /// The index added with hash, or none.
    auto find(uint64_t hash) const noexcept -> uint32_t
    {
        if(slots_.empty())
        {
            return none;
        }
        const auto mask = slots_.size() - 1;
        for(auto at = static_cast<size_t>(hash) & mask; slots_[at].index != none; at = (at + 1) & mask)
        {
            if(slots_[at].hash == hash)
            {
                return slots_[at].index;
            }
        }
        return none;
    }

private:
    struct slot
    {
        uint64_t hash = 0;
        uint32_t index = none;
    };

    /// Puts an entry in the first free slot from its home; false when its hash is already held.
    auto place(uint64_t hash, uint32_t index) -> bool
    {
        const auto mask = slots_.size() - 1;
        auto at = static_cast<size_t>(hash) & mask;
        for(; slots_[at].index != none; at = (at + 1) & mask)
        {
            if(slots_[at].hash == hash)
            {
                return false;
            }
        }
        slots_[at] = {hash, index};
        return true;
    }

    void rebuild(size_t size)
    {
        auto old = std::move(slots_);
        slots_.assign(size, slot{});
        for(const auto& entry : old)
        {
            if(entry.index != none)
            {
                place(entry.hash, entry.index);
            }
        }
    }

    std::vector<slot> slots_;
    size_t count_ = 0;
};
} // namespace serialization
