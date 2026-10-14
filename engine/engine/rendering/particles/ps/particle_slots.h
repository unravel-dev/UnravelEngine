#pragma once

#include <cstdint>
#include <vector>

namespace unravel
{
namespace ps_soa
{

/**
 * @brief Hands out an emitter's particle slots, always the lowest free one.
 *
 * Keeping the live slots packed at the bottom keeps the high-water mark - the range the GPU walks every
 * frame - close to the live count, and means a free slot below the mark never stays free for longer
 * than the oldest particle above it lives.
 */
class particle_slot_allocator
{
public:
    void reset(uint32_t capacity);
    /// @return the slot, or capacity when every slot is in use.
    auto allocate() -> uint32_t;
    void release(uint32_t slot);
    auto is_used(uint32_t slot) const -> bool;
    /// One past the highest slot in use.
    auto get_high_water() const -> uint32_t;
    auto get_capacity() const -> uint32_t;
    auto get_used_count() const -> uint32_t;

private:
    std::vector<uint64_t> words_;
    uint32_t capacity_ = 0;
    uint32_t used_count_ = 0;
    uint32_t high_water_ = 0;
    /// No word below this one has a free bit.
    uint32_t first_free_word_ = 0;
};

/**
 * @brief The live particles of an emitter ordered by death time: what dies next is always on top.
 */
class particle_death_queue
{
public:
    struct entry
    {
        uint64_t death_tick = 0;
        uint32_t slot = 0;
    };

    void clear();
    void push(uint64_t death_tick, uint32_t slot);
    /// Removes and returns, in death order, every particle dead at @p now_tick.
    template<typename Visitor>
    void pop_dead(uint64_t now_tick, Visitor&& visit)
    {
        while(!heap_.empty() && heap_.front().death_tick <= now_tick)
        {
            const entry dead = heap_.front();
            pop_front();
            visit(dead);
        }
    }
    auto size() const -> uint32_t;
    auto empty() const -> bool;
    /// Latest death tick of the live particles (0 when empty).
    auto get_last_death_tick() const -> uint64_t;
    auto get_entries() const -> const std::vector<entry>&;

private:
    void pop_front();

    std::vector<entry> heap_;
    uint64_t last_death_tick_ = 0;
};

} // namespace ps_soa
} // namespace unravel
