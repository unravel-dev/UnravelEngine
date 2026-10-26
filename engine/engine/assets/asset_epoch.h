#pragma once

#include <engine/engine_export.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace asset_detail
{

/// A thread's announcement of the epoch it reads in; one cache line, written only by its thread.
struct alignas(64) epoch_thread_slot
{
    /// The epoch the thread's outermost read began in, or 0 while it reads nothing.
    std::atomic<uint64_t> epoch{0};
    /// Owned by a live thread; a slot whose thread ended is reused by the next new thread.
    std::atomic<bool> is_taken{true};
    /// Guards open on the owning thread.
    uint32_t depth{};
    /// Taken by a read after its thread's end gave the thread's slot back: given back again when that read ends.
    bool is_held_past_thread_end{};
    epoch_thread_slot* next{};
};

/// The calling thread's slot, null before its first read. A plain pointer, so a read during thread
/// teardown (after the slot was given back) takes a new slot instead of touching a destroyed object.
inline thread_local epoch_thread_slot* local_epoch_slot = nullptr;

/**
 * @brief Frees objects that many threads read without locks once no reader can still reach them.
 *
 * Epoch-based reclamation. A reader announces the epoch it reads in on its own cache line for the
 * length of a read (guard). A writer that replaced an object retires it: the object is stamped with
 * the epoch it was replaced in and the epoch advances. A retired object is freed once every thread
 * inside a read entered it in a later epoch - such a reader came after the replacement and reads
 * the new object. Readers write nothing other threads read, so any number of them read one object
 * without contending.
 *
 * Never wait inside a guard: it holds back every free that is due (collect_all waits for it).
 */
class ENGINE_EXPORT epoch_domain
{
public:
    /// The process' domain, shared by every asset handle.
    static auto instance() -> epoch_domain&
    {
        static epoch_domain domain;
        return domain;
    }

    /// One read: pointers loaded while a guard lives stay valid until it ends. Guards nest.
    class guard
    {
    public:
        guard() : slot_(get_thread_slot())
        {
            if(slot_->depth++ == 0)
            {
                // Announced before any protected pointer is loaded; the exchange is the full fence
                // a reader needs against a writer that scans the slots after replacing an object.
                slot_->epoch.exchange(instance().epoch_.load(std::memory_order_seq_cst), std::memory_order_seq_cst);
            }
        }

        ~guard()
        {
            if(--slot_->depth == 0)
            {
                // Release: the guard's reads happen before a writer that sees the slot quiescent frees what they read.
                slot_->epoch.store(QUIESCENT, std::memory_order_release);
                if(slot_->is_held_past_thread_end)
                {
                    release_thread_slot(slot_);
                }
            }
        }

        guard(const guard&) = delete;
        auto operator=(const guard&) -> guard& = delete;
        guard(guard&&) = delete;
        auto operator=(guard&&) -> guard& = delete;

    private:
        epoch_thread_slot* slot_;
    };

    epoch_domain() = default;
    /// Objects still retired at process exit are not freed: their destructors can need systems (the renderer) that
    /// are gone by then. The asset manager frees everything it retired with collect_all() at its deinit.
    ~epoch_domain() = default;
    epoch_domain(const epoch_domain&) = delete;
    auto operator=(const epoch_domain&) -> epoch_domain& = delete;

    /**
     * @brief Frees `object` with `destroy` once no reader can reach it, and every older retired
     * object that became unreachable. Call after the last shared pointer to it was replaced.
     * `destroy` runs on the calling thread, or on whichever thread later frees it.
     */
    void retire(void* object, void (*destroy)(void*));

    /// Frees every retired object no reader can reach. Returns at once when nothing is retired.
    void collect();

    /// Waits until the reads that could reach a retired object have ended, then frees everything
    /// retired (shutdown). Reads that start meanwhile do not hold it back.
    void collect_all();

    /// Objects retired and not freed yet.
    auto get_retired_count() const -> size_t;

    /// Thread slots ever created (threads reading at once, at most).
    auto get_thread_slot_count() const -> size_t;

    /// Gives the calling thread's slot back for a later thread to take: at the thread's end, or when a read during
    /// its teardown ends.
    static void release_thread_slot(epoch_thread_slot* slot);

private:
    /// A slot's epoch while its thread reads nothing; real epochs start at 1.
    static constexpr uint64_t QUIESCENT = 0;

    struct retired_object
    {
        void* object;
        void (*destroy)(void*);
        uint64_t epoch;
    };

    /// The calling thread's slot, taken on its first read.
    static auto get_thread_slot() -> epoch_thread_slot*
    {
        if(epoch_thread_slot* slot = local_epoch_slot)
        {
            return slot;
        }
        return instance().acquire_thread_slot();
    }

    auto acquire_thread_slot() -> epoch_thread_slot*;
    /// Moves every retired object no reader can reach to `freeable`; retired_mutex_ held.
    void take_freeable(std::vector<retired_object>& freeable);
    static void destroy_all(const std::vector<retired_object>& objects);

    std::atomic<uint64_t> epoch_{1};
    std::atomic<epoch_thread_slot*> slots_{nullptr};
    std::atomic<size_t> slot_count_{0};
    std::atomic<size_t> retired_count_{0};
    mutable std::mutex retired_mutex_;
    std::vector<retired_object> retired_;
};

} // namespace asset_detail
