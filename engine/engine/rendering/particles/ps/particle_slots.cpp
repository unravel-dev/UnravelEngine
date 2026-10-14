#include "particle_slots.h"

#include <algorithm>

#include <bx/math.h>

namespace unravel
{
namespace ps_soa
{
namespace
{

constexpr uint32_t k_bits_per_word = 64;

auto later_death(const particle_death_queue::entry& a, const particle_death_queue::entry& b) -> bool
{
    return a.death_tick > b.death_tick;
}

auto lowest_clear_bit(uint64_t word) -> uint32_t
{
    return uint32_t(bx::countTrailingZeros(~word));
}

} // namespace

void particle_slot_allocator::reset(uint32_t capacity)
{
    capacity_ = capacity;
    words_.assign((capacity + k_bits_per_word - 1) / k_bits_per_word, 0u);
    used_count_ = 0;
    high_water_ = 0;
    first_free_word_ = 0;
}

auto particle_slot_allocator::allocate() -> uint32_t
{
    const uint32_t word_count = uint32_t(words_.size());
    for(uint32_t word = first_free_word_; word < word_count; ++word)
    {
        if(words_[word] == ~uint64_t(0))
        {
            continue;
        }
        const uint32_t slot = word * k_bits_per_word + lowest_clear_bit(words_[word]);
        first_free_word_ = word;
        if(slot >= capacity_)
        {
            break;
        }
        words_[word] |= uint64_t(1) << (slot % k_bits_per_word);
        ++used_count_;
        high_water_ = std::max(high_water_, slot + 1);
        return slot;
    }
    first_free_word_ = word_count;
    return capacity_;
}

void particle_slot_allocator::release(uint32_t slot)
{
    if(slot >= capacity_ || !is_used(slot))
    {
        return;
    }
    const uint32_t word = slot / k_bits_per_word;
    words_[word] &= ~(uint64_t(1) << (slot % k_bits_per_word));
    --used_count_;
    first_free_word_ = std::min(first_free_word_, word);
    while(high_water_ > 0 && !is_used(high_water_ - 1))
    {
        --high_water_;
    }
}

auto particle_slot_allocator::is_used(uint32_t slot) const -> bool
{
    return (words_[slot / k_bits_per_word] >> (slot % k_bits_per_word)) & 1u;
}

auto particle_slot_allocator::get_high_water() const -> uint32_t
{
    return high_water_;
}

auto particle_slot_allocator::get_capacity() const -> uint32_t
{
    return capacity_;
}

auto particle_slot_allocator::get_used_count() const -> uint32_t
{
    return used_count_;
}

void particle_death_queue::clear()
{
    heap_.clear();
    last_death_tick_ = 0;
}

void particle_death_queue::push(uint64_t death_tick, uint32_t slot)
{
    heap_.push_back({death_tick, slot});
    std::push_heap(heap_.begin(), heap_.end(), later_death);
    last_death_tick_ = std::max(last_death_tick_, death_tick);
}

void particle_death_queue::pop_front()
{
    std::pop_heap(heap_.begin(), heap_.end(), later_death);
    heap_.pop_back();
    if(heap_.empty())
    {
        last_death_tick_ = 0;
    }
}

auto particle_death_queue::size() const -> uint32_t
{
    return uint32_t(heap_.size());
}

auto particle_death_queue::empty() const -> bool
{
    return heap_.empty();
}

auto particle_death_queue::get_last_death_tick() const -> uint64_t
{
    return last_death_tick_;
}

auto particle_death_queue::get_entries() const -> const std::vector<entry>&
{
    return heap_;
}

} // namespace ps_soa
} // namespace unravel
