#pragma once

#include <engine/engine_export.h>

#include <entt/entity/handle.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace unravel
{

/**
 * @struct contact_links
 * @brief Head of one body's intrusive contact list, plus the counters that gate removal.
 *
 * Lives on whatever per-body component the backend already keeps, so resolving it from
 * an entity is a single sparse-set probe.
 */
struct contact_links
{
    static constexpr uint32_t npos = 0xFFFFFFFFu;

    /// First slot in this body's list.
    uint32_t head{npos};
    /// Slots in this body's list.
    uint16_t tracked{0};
    /// Slots that owe an exit event if this body is removed. THE destroy fast path:
    /// a body with nothing owed is recognised by one integer compare.
    uint16_t flush_pending{0};
};

/**
 * @class contact_graph
 * @brief Pooled contact pairs, each linked into both participants' lists.
 *
 * The shape Box2D uses for its contact graph, for the same reason: a per-body question
 * ("what is this body touching", "does removing it owe anyone an event") has to be
 * answerable without scanning the whole contact set, because it is asked on the destroy
 * path of every body in the scene.
 *
 * Complexity, against the sorted-vector map this replaced:
 *   insert           O(n) memmove -> O(1) amortised
 *   erase            O(n) memmove -> O(1) unlink
 *   "is B touching"  O(n) scan    -> one compare
 *   "B's contacts"   O(n) scan    -> O(k) list walk
 *
 * Backend-agnostic and payload-generic so it can be exercised on its own; the pointer
 * surgery below is the part most worth testing directly.
 *
 * @tparam Payload Per-pair data the backend cares about. Never inspected here, and
 *                 never destroyed on erase, so a recycled slot keeps any capacity its
 *                 payload had allocated.
 */
template<typename Payload>
class contact_graph
{
public:
    static constexpr uint32_t npos = contact_links::npos;

    /**
     * @struct slot
     * @brief One tracked pair, linked into both participants' lists.
     */
    struct slot
    {
        Payload payload{};

        entt::handle a{};
        entt::handle b{};

        uint32_t next_a{npos};
        uint32_t prev_a{npos};
        uint32_t next_b{npos};
        uint32_t prev_b{npos};

        /// Position in the dense live list, for O(1) removal.
        uint32_t live_index{npos};
        /// Bumped on erase so a queued reference can detect a recycled slot.
        uint32_t generation{1};

        bool in_use{false};
        /// Seen in the current step. Anything left false has separated.
        bool active_this_frame{false};
        /// Precomputed from both sides' policy when the pair is inserted, so the
        /// destroy path never reads a component.
        bool flush_on_destroy{false};
        /// Whether the enter actually reached gameplay. A removal must not answer an
        /// undelivered enter with an exit.
        bool enter_dispatched{false};
    };

    /// Resolves a participant's link header. Returns null for entities the backend
    /// does not track (an object with no body component).
    using links_resolver = contact_links* (*)(entt::handle);

    explicit contact_graph(links_resolver resolve) noexcept : resolve_(resolve)
    {
    }

    /**
     * @brief Inserts a pair and links it into both participants' lists.
     * @return The new slot id, or npos if either participant has no link header.
     */
    auto insert(entt::handle a, entt::handle b, bool flush_on_destroy) -> uint32_t
    {
        auto* links_a = resolve_(a);
        auto* links_b = resolve_(b);
        if(links_a == nullptr || links_b == nullptr)
        {
            return npos;
        }

        const uint32_t id = allocate();

        auto& s = slots_[id];
        s.a = a;
        s.b = b;
        // Read by link_one for the counters, so it has to be set before linking.
        s.flush_on_destroy = flush_on_destroy;

        link_one(id, *links_a, a);
        link_one(id, *links_b, b);

        return id;
    }

    /**
     * @brief Unlinks a pair from both participants and returns it to the pool.
     */
    void erase(uint32_t id)
    {
        if(id >= slots_.size() || !slots_[id].in_use)
        {
            return;
        }

        auto& s = slots_[id];
        unlink_one(id, s.a);
        unlink_one(id, s.b);

        // Swap-and-pop out of the dense live list.
        const uint32_t last = live_.back();
        live_[s.live_index] = last;
        slots_[last].live_index = s.live_index;
        live_.pop_back();

        s.in_use = false;
        s.live_index = npos;
        ++s.generation;

        free_.push_back(id);
    }

    /**
     * @brief Walks one participant's list.
     *
     * The successor is captured before each call, so @p fn may erase the slot it was
     * handed - which is exactly what a removal flush does.
     *
     * @param fn Callable as bool(uint32_t id, slot&). Returning true stops the walk.
     * @return The slot the walk stopped on, or npos if it ran to the end.
     */
    template<typename Fn>
    auto visit(entt::handle owner, Fn&& fn) -> uint32_t
    {
        auto* links = resolve_(owner);
        if(links == nullptr)
        {
            return npos;
        }

        uint32_t id = links->head;
        while(id != npos)
        {
            auto& s = slots_[id];
            const uint32_t next = (s.a == owner) ? s.next_a : s.next_b;

            if(fn(id, s))
            {
                return id;
            }

            id = next;
        }

        return npos;
    }

    /**
     * @brief Re-applies a pair's removal policy, keeping both counters in step.
     */
    void set_flush_on_destroy(uint32_t id, bool value)
    {
        auto& s = slots_[id];
        if(!s.in_use || s.flush_on_destroy == value)
        {
            return;
        }

        s.flush_on_destroy = value;
        adjust_flush_pending(s.a, value);
        adjust_flush_pending(s.b, value);
    }

    /**
     * @brief Drops every pair without notifying anyone. For bulk teardown.
     */
    void clear()
    {
        for(const uint32_t id : live_)
        {
            auto& s = slots_[id];
            if(auto* links_a = resolve_(s.a))
            {
                *links_a = {};
            }
            if(auto* links_b = resolve_(s.b))
            {
                *links_b = {};
            }
        }

        slots_.clear();
        free_.clear();
        live_.clear();
    }

    auto get(uint32_t id) -> slot&
    {
        return slots_[id];
    }

    auto get(uint32_t id) const -> const slot&
    {
        return slots_[id];
    }

    /// Dense list of in-use slot ids, so a per-step sweep is O(live) rather than
    /// O(pool capacity).
    auto live() const -> const std::vector<uint32_t>&
    {
        return live_;
    }

    auto is_live(uint32_t id, uint32_t generation) const -> bool
    {
        return id < slots_.size() && slots_[id].in_use && slots_[id].generation == generation;
    }

private:
    auto allocate() -> uint32_t
    {
        uint32_t id{};
        if(!free_.empty())
        {
            id = free_.back();
            free_.pop_back();
        }
        else
        {
            id = static_cast<uint32_t>(slots_.size());
            slots_.emplace_back();
        }

        auto& s = slots_[id];
        s.in_use = true;
        s.active_this_frame = true;
        s.enter_dispatched = false;
        s.flush_on_destroy = false;
        s.next_a = s.prev_a = s.next_b = s.prev_b = npos;

        s.live_index = static_cast<uint32_t>(live_.size());
        live_.push_back(id);

        return id;
    }

    void link_one(uint32_t id, contact_links& links, entt::handle owner)
    {
        auto& s = slots_[id];
        const bool as_a = (s.a == owner);

        (as_a ? s.prev_a : s.prev_b) = npos;
        (as_a ? s.next_a : s.next_b) = links.head;

        if(links.head != npos)
        {
            auto& head = slots_[links.head];
            (head.a == owner ? head.prev_a : head.prev_b) = id;
        }

        links.head = id;
        ++links.tracked;
        if(s.flush_on_destroy)
        {
            ++links.flush_pending;
        }
    }

    void unlink_one(uint32_t id, entt::handle owner)
    {
        auto* links = resolve_(owner);
        if(links == nullptr)
        {
            return;
        }

        auto& s = slots_[id];
        const bool as_a = (s.a == owner);
        const uint32_t next = as_a ? s.next_a : s.next_b;
        const uint32_t prev = as_a ? s.prev_a : s.prev_b;

        if(prev != npos)
        {
            auto& prev_slot = slots_[prev];
            (prev_slot.a == owner ? prev_slot.next_a : prev_slot.next_b) = next;
        }
        else
        {
            links->head = next;
        }

        if(next != npos)
        {
            auto& next_slot = slots_[next];
            (next_slot.a == owner ? next_slot.prev_a : next_slot.prev_b) = prev;
        }

        if(links->tracked > 0)
        {
            --links->tracked;
        }
        if(s.flush_on_destroy && links->flush_pending > 0)
        {
            --links->flush_pending;
        }
    }

    void adjust_flush_pending(entt::handle owner, bool increment)
    {
        auto* links = resolve_(owner);
        if(links == nullptr)
        {
            return;
        }

        if(increment)
        {
            ++links->flush_pending;
        }
        else if(links->flush_pending > 0)
        {
            --links->flush_pending;
        }
    }

    links_resolver resolve_{};
    std::vector<slot> slots_;
    std::vector<uint32_t> free_;
    std::vector<uint32_t> live_;
};

} // namespace unravel
