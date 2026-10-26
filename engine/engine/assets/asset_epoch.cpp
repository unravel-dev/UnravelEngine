#include "asset_epoch.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <thread>

namespace asset_detail
{
namespace
{
/// Set when the thread's slot was given back at its end; a read after that keeps its new slot.
thread_local bool is_thread_slot_released = false;

/// Gives the thread's slot back when the thread ends, so a later thread can take it.
struct thread_slot_release
{
    void touch()
    {
    }

    ~thread_slot_release();
};

thread_local thread_slot_release local_thread_slot_release;
} // namespace

thread_slot_release::~thread_slot_release()
{
    if(local_epoch_slot != nullptr)
    {
        epoch_domain::release_thread_slot(local_epoch_slot);
    }
    is_thread_slot_released = true;
}

void epoch_domain::release_thread_slot(epoch_thread_slot* slot)
{
    slot->is_held_past_thread_end = false;
    local_epoch_slot = nullptr;
    slot->is_taken.store(false, std::memory_order_release);
}

auto epoch_domain::acquire_thread_slot() -> epoch_thread_slot*
{
    epoch_thread_slot* slot = nullptr;
    for(auto* candidate = slots_.load(std::memory_order_acquire); candidate != nullptr && slot == nullptr;
        candidate = candidate->next)
    {
        bool is_taken = false;
        if(!candidate->is_taken.load(std::memory_order_relaxed) &&
           candidate->is_taken.compare_exchange_strong(is_taken, true, std::memory_order_acq_rel))
        {
            slot = candidate;
        }
    }
    if(slot == nullptr)
    {
        // Slots are never freed: there are as many as threads ever read at once. Sequentially consistent with the
        // scan in take_freeable: a writer that scans after this thread announced its epoch sees the slot.
        slot = new epoch_thread_slot();
        slot->next = slots_.load(std::memory_order_relaxed);
        while(!slots_.compare_exchange_weak(slot->next, slot, std::memory_order_seq_cst, std::memory_order_relaxed))
        {
        }
        slot_count_.fetch_add(1, std::memory_order_relaxed);
    }
    local_epoch_slot = slot;
    slot->is_held_past_thread_end = is_thread_slot_released;
    if(!is_thread_slot_released)
    {
        // The first use constructs the thread's releaser, which registers its end-of-thread run.
        local_thread_slot_release.touch();
    }
    return slot;
}

void epoch_domain::retire(void* object, void (*destroy)(void*))
{
    std::vector<retired_object> freeable;
    {
        std::lock_guard<std::mutex> lock(retired_mutex_);
        // Readers that load the epoch after this advance read after the replacement.
        const uint64_t stamp = epoch_.fetch_add(1, std::memory_order_seq_cst);
        retired_.push_back({object, destroy, stamp});
        take_freeable(freeable);
    }
    destroy_all(freeable);
}

void epoch_domain::collect()
{
    if(retired_count_.load(std::memory_order_acquire) == 0)
    {
        return;
    }
    std::vector<retired_object> freeable;
    {
        std::lock_guard<std::mutex> lock(retired_mutex_);
        take_freeable(freeable);
    }
    destroy_all(freeable);
}

void epoch_domain::collect_all()
{
    // Inside a read the caller would wait for itself.
    assert(local_epoch_slot == nullptr || local_epoch_slot->depth == 0);
    collect();
    while(retired_count_.load(std::memory_order_acquire) != 0)
    {
        std::this_thread::yield();
        collect();
    }
}

auto epoch_domain::get_retired_count() const -> size_t
{
    return retired_count_.load(std::memory_order_acquire);
}

auto epoch_domain::get_thread_slot_count() const -> size_t
{
    return slot_count_.load(std::memory_order_relaxed);
}

void epoch_domain::take_freeable(std::vector<retired_object>& freeable)
{
    uint64_t oldest = std::numeric_limits<uint64_t>::max();
    for(auto* slot = slots_.load(std::memory_order_seq_cst); slot != nullptr; slot = slot->next)
    {
        const uint64_t epoch = slot->epoch.load(std::memory_order_seq_cst);
        if(epoch != QUIESCENT)
        {
            oldest = std::min(oldest, epoch);
        }
    }
    // A reader that announced an epoch above an object's stamp began after its replacement.
    const auto due = std::partition(retired_.begin(),
                                    retired_.end(),
                                    [oldest](const retired_object& retired)
                                    {
                                        return retired.epoch >= oldest;
                                    });
    freeable.assign(due, retired_.end());
    retired_.erase(due, retired_.end());
    retired_count_.store(retired_.size(), std::memory_order_release);
}

void epoch_domain::destroy_all(const std::vector<retired_object>& objects)
{
    for(const auto& retired : objects)
    {
        retired.destroy(retired.object);
    }
}

} // namespace asset_detail
