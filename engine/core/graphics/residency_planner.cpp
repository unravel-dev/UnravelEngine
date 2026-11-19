#include "residency_planner.h"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace gfx::residency
{
namespace
{

/// A change the planner considers, with the bytes it frees or allocates.
struct planned_change
{
    std::uint32_t index{0};
    std::uint8_t level{0};
    bool evict{false};
    std::uint64_t bytes{0};
};

auto frames_since(std::uint64_t frame, std::uint64_t stamp) -> std::uint64_t
{
    return frame > stamp ? frame - stamp : 0;
}

/// Frames a resource goes unread before it may drop: the minimum age, plus the gap between reads it
/// showed when a read last needed what a drop took. A resource read on a cycle then stays resident.
auto unread_frames_to_drop(const candidate& c, const request& req) -> std::uint64_t
{
    return std::uint64_t(req.min_age_frames) + c.reuse_frames;
}

auto is_idle(const candidate& c, const request& req) -> bool
{
    if(req.ignore_age)
    {
        return true;
    }
    const std::uint64_t idle = frames_since(req.frame, c.last_use);
    if(idle < unread_frames_to_drop(c, req))
    {
        return false;
    }
    if(req.order == strategy::age_ttl)
    {
        return req.max_idle_frames != 0 && idle > req.max_idle_frames;
    }
    return true;
}

/// What an idle resource gives up: its levels down to the tail, or its whole copy without a tail.
auto get_idle_drop(const candidate& c, std::uint32_t index, planned_change& change) -> bool
{
    if(!c.has_handle)
    {
        return false;
    }
    if(c.tail_level == 0)
    {
        change = {index, 0, true, c.bytes[c.level]};
        return true;
    }
    if(c.level >= c.tail_level)
    {
        return false;
    }
    const std::uint64_t now = c.bytes[c.level];
    const std::uint64_t tail = c.bytes[c.tail_level];
    change = {index, c.tail_level, false, now > tail ? now - tail : 0};
    return true;
}

/// Whether a texture in use may lose a level: it has one above its tail and no read needed every
/// mip for as long as an idle resource goes unread before it drops.
auto can_drop_in_use(const candidate& c, const request& req) -> bool
{
    return c.has_handle && c.tail_level != 0 && c.level < c.tail_level &&
           frames_since(req.frame, c.last_full_use) >= unread_frames_to_drop(c, req);
}

auto is_wanted(const candidate& c, const request& req) -> bool
{
    return c.has_handle && c.level > 0 && frames_since(req.frame, c.last_use) <= req.wanted_frames;
}

auto may_rise(const candidate& c, const request& req) -> bool
{
    return c.last_drop == 0 || frames_since(req.frame, c.last_drop) >= req.min_age_frames;
}

void order_idle_drops(std::vector<planned_change>& changes,
                      const std::vector<candidate>& candidates,
                      strategy order)
{
    switch(order)
    {
        case strategy::lru:
        case strategy::age_ttl:
            std::stable_sort(changes.begin(),
                             changes.end(),
                             [&](const planned_change& a, const planned_change& b)
                             {
                                 return candidates[a.index].last_use < candidates[b.index].last_use;
                             });
            return;
        case strategy::lfu:
            std::stable_sort(changes.begin(),
                             changes.end(),
                             [&](const planned_change& a, const planned_change& b)
                             {
                                 return candidates[a.index].use_count < candidates[b.index].use_count;
                             });
            return;
        case strategy::largest_first:
            std::stable_sort(changes.begin(),
                             changes.end(),
                             [](const planned_change& a, const planned_change& b)
                             {
                                 return a.bytes > b.bytes;
                             });
            return;
    }
}

/// Tracks how far the drops of a pass have got.
class drop_budget
{
public:
    explicit drop_budget(const request& req)
        : goal_(req.order == strategy::age_ttl || req.ignore_age ? std::numeric_limits<std::uint64_t>::max()
                                                                  : req.drop_bytes)
        , max_drops_(req.max_drops)
    {
    }

    [[nodiscard]] auto is_done(const plan_result& result) const -> bool
    {
        return result.dropped_bytes >= goal_ || (max_drops_ != 0 && result.drops >= max_drops_);
    }

private:
    std::uint64_t goal_;
    std::uint32_t max_drops_;
};

void add_drop(plan_result& result, const planned_change& change)
{
    result.actions.push_back({change.index, change.level, change.evict});
    result.dropped_bytes += change.bytes;
    ++result.drops;
}

void plan_idle_drops(const std::vector<candidate>& candidates,
                     const request& req,
                     const drop_budget& budget,
                     std::vector<bool>& changed,
                     plan_result& result)
{
    std::vector<planned_change> changes;
    for(std::uint32_t i = 0; i < candidates.size(); ++i)
    {
        planned_change change;
        if(is_idle(candidates[i], req) && get_idle_drop(candidates[i], i, change))
        {
            changes.push_back(change);
        }
    }
    order_idle_drops(changes, candidates, req.order);
    for(const auto& change : changes)
    {
        if(budget.is_done(result))
        {
            return;
        }
        add_drop(result, change);
        changed[change.index] = true;
    }
}

void plan_in_use_drops(const std::vector<candidate>& candidates,
                       const request& req,
                       const drop_budget& budget,
                       std::vector<bool>& changed,
                       plan_result& result)
{
    std::vector<planned_change> changes;
    for(std::uint32_t i = 0; i < candidates.size(); ++i)
    {
        const candidate& c = candidates[i];
        if(changed[i] || is_idle(c, req) || !can_drop_in_use(c, req))
        {
            continue;
        }
        const auto next = static_cast<std::uint8_t>(c.level + 1);
        const std::uint64_t now = c.bytes[c.level];
        const std::uint64_t lower = c.bytes[next];
        changes.push_back({i, next, false, now > lower ? now - lower : 0});
    }
    std::stable_sort(changes.begin(),
                     changes.end(),
                     [](const planned_change& a, const planned_change& b)
                     {
                         return a.bytes > b.bytes;
                     });
    std::uint32_t in_use_drops = 0;
    for(const auto& change : changes)
    {
        if(budget.is_done(result) || (req.max_in_use_drops != 0 && in_use_drops >= req.max_in_use_drops))
        {
            return;
        }
        add_drop(result, change);
        changed[change.index] = true;
        ++in_use_drops;
    }
}

/// The highest level @p c can rise to within @p room bytes and @p upload_room of upload; false when
/// none fits.
auto find_rise_level(const candidate& c, std::uint64_t room, std::uint64_t upload_room, std::uint8_t& level) -> bool
{
    for(std::uint8_t l = 0; l < c.level; ++l)
    {
        if(c.bytes[l] <= room && c.bytes[l] <= upload_room)
        {
            level = l;
            return true;
        }
    }
    return false;
}

void plan_rises(const std::vector<candidate>& candidates, const request& req, plan_result& result)
{
    std::vector<std::uint32_t> wanted;
    for(std::uint32_t i = 0; i < candidates.size(); ++i)
    {
        if(is_wanted(candidates[i], req))
        {
            wanted.push_back(i);
        }
    }
    std::stable_sort(wanted.begin(),
                     wanted.end(),
                     [&](std::uint32_t a, std::uint32_t b)
                     {
                         const candidate& ca = candidates[a];
                         const candidate& cb = candidates[b];
                         if(ca.last_use != cb.last_use)
                         {
                             return ca.last_use > cb.last_use;
                         }
                         return ca.use_count > cb.use_count;
                     });

    const bool may_raise = req.drop_bytes == 0 && req.rise_bytes > 0;
    std::uint64_t room = req.rise_bytes;
    std::uint64_t uploaded = 0;
    for(const std::uint32_t index : wanted)
    {
        const candidate& c = candidates[index];
        std::uint8_t level = 0;
        const bool is_first = result.rises == 0;
        const std::uint64_t upload_room = (req.max_upload_bytes == 0 || is_first)
                                              ? std::numeric_limits<std::uint64_t>::max()
                                              : (req.max_upload_bytes > uploaded ? req.max_upload_bytes - uploaded : 0);
        const bool has_rises_left = req.max_rises == 0 || result.rises < req.max_rises;
        if(!may_raise || !has_rises_left || !may_rise(c, req) || !find_rise_level(c, room, upload_room, level))
        {
            ++result.backlog;
            continue;
        }
        result.actions.push_back({index, level, false});
        room -= c.bytes[level];
        uploaded += c.bytes[level];
        result.raised_bytes += c.bytes[level];
        ++result.rises;
    }
}

} // namespace

auto calc_tail_level(std::uint32_t width,
                     std::uint32_t height,
                     std::uint8_t num_mips,
                     std::uint32_t tail_size,
                     std::uint32_t block_width,
                     std::uint32_t block_height) -> std::uint8_t
{
    if(num_mips < 2 || tail_size == 0)
    {
        return 0;
    }
    const auto last = static_cast<std::uint8_t>(num_mips - 1);
    std::uint8_t level = 0;
    while(level < last && std::max(width >> level, height >> level) > tail_size)
    {
        ++level;
    }
    // Level 0 already within the tail size has nothing to drop.
    if(level == 0 || std::max(width >> level, height >> level) > tail_size)
    {
        return 0;
    }
    const auto keeps_blocks = [&](std::uint8_t l)
    {
        const bool width_ok = block_width <= 1 || width % block_width != 0 || (width >> l) % block_width == 0;
        const bool height_ok = block_height <= 1 || height % block_height != 0 || (height >> l) % block_height == 0;
        return width_ok && height_ok;
    };
    while(level > 0 && !keeps_blocks(level))
    {
        --level;
    }
    return level;
}

auto plan(const std::vector<candidate>& candidates, const request& req) -> plan_result
{
    plan_result result;
    std::vector<bool> changed(candidates.size(), false);
    const drop_budget budget(req);
    if(req.drop_bytes != 0)
    {
        plan_idle_drops(candidates, req, budget, changed, result);
        if(req.drop_in_use)
        {
            plan_in_use_drops(candidates, req, budget, changed, result);
        }
    }
    plan_rises(candidates, req, result);
    return result;
}

} // namespace gfx::residency
