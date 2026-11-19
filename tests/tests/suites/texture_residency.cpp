/*
 * Validation suite for texture residency: the planner (engine/core/graphics/residency_planner.h)
 * and the eviction registry it drives (engine/core/graphics/eviction_registry.h).
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "texture residency"
 *
 * The test backend reports no GPU memory budget, so the engine's own registry stays off; the suite
 * activates a registry of its own and fills it with fake resources whose sizes per level are known.
 *
 * What it pins:
 *   - idle textures go to their tails and resources without one are evicted whole, in the strategy's
 *     order, until the bytes asked for are freed or the drop limit is reached;
 *   - a texture read at full size within the minimum age is never reduced, one read only by sampled
 *     draws loses one level at a time under pressure, never below its tail;
 *   - a resource that a read needed soon after a drop stays as long again past the minimum age, so one
 *     read on a cycle stops dropping; a read after a long gap forgets it;
 *   - rises go to the highest level that fits the room, the upload limit and the rise limit, most recently read
 *     first; the first rise of a pass may exceed the upload limit; nothing rises within the minimum
 *     age of its drop, or while there are bytes to drop; the backlog counts who waits;
 *   - plans are the same for the same input;
 *   - the registry's totals match its resources over drops, rises, on-demand creates and unregisters;
 *   - a sampled read of an evicted texture creates only its tail, a full read the whole texture;
 *   - a level change that fails keeps the resource where it was;
 *   - the frame pass drops above the soft budget, rises below the target, and does neither between;
 *   - admissions count at once and never pass the limit together, from any number of threads, and
 *     registering hands them over to the resource's own bytes;
 *   - the recent creates the load path reads match the traffic window frame by frame;
 *   - restoring everything is no read: it counts no thrash and teaches no reuse gap;
 *   - a resource registered this frame counts as read in full, so no pass takes a level from it first;
 *   - a budget read while another thread publishes one comes back whole, never mixing two.
 */

#include "../tests.h"

#include <graphics/eviction_registry.h>
#include <graphics/residency_planner.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace unravel::test;

namespace
{

using gfx::residency::candidate;
using gfx::residency::plan;
using gfx::residency::request;
using gfx::residency::strategy;

constexpr std::uint64_t k_frame = 1000;

/// GPU bytes of a square RGBA8 texture of @p size with mips, at each level.
auto rgba8_level_bytes(std::uint32_t size) -> std::array<std::uint64_t, gfx::residency::k_max_levels>
{
    std::array<std::uint64_t, gfx::residency::k_max_levels> bytes{};
    std::size_t level = 0;
    for(std::uint32_t top = size; level < bytes.size(); ++level)
    {
        std::uint64_t total = 0;
        for(std::uint32_t s = top; s > 0; s >>= 1)
        {
            total += std::uint64_t(s) * s * 4;
        }
        bytes[level] = total;
        if(top == 1)
        {
            break;
        }
        top >>= 1;
    }
    return bytes;
}

auto make_texture(std::uint32_t size, std::uint8_t tail_level, std::uint64_t last_use) -> candidate
{
    candidate c;
    c.bytes = rgba8_level_bytes(size);
    c.tail_level = tail_level;
    c.last_use = last_use;
    c.last_full_use = 0;
    return c;
}

auto make_buffer(std::uint64_t bytes, std::uint64_t last_use) -> candidate
{
    candidate c;
    c.bytes[0] = bytes;
    c.last_use = last_use;
    return c;
}

auto find_action(const gfx::residency::plan_result& result, std::uint32_t index) -> const gfx::residency::action*
{
    for(const auto& action : result.actions)
    {
        if(action.index == index)
        {
            return &action;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------
// Planner
// ---------------------------------------------------------------------------------

void test_idle_textures_go_to_their_tails()
{
    std::vector<candidate> candidates{
        make_texture(1024, 4, k_frame - 500), // idle, oldest
        make_texture(1024, 4, k_frame - 100), // idle
        make_texture(1024, 4, k_frame - 10),  // in use
        make_buffer(1 << 20, k_frame - 400),  // idle, no tail
    };
    request req;
    req.frame = k_frame;
    req.min_age_frames = 60;
    req.drop_bytes = std::numeric_limits<std::uint64_t>::max();
    const auto result = plan(candidates, req);

    const auto* oldest = find_action(result, 0);
    require(oldest != nullptr, "the oldest idle texture drops");
    check(!oldest->evict && oldest->level == 4, "it goes to its tail level, keeping a GPU copy");
    const auto* second = find_action(result, 1);
    check(second != nullptr && second->level == 4, "the other idle texture drops to its tail too");
    check(find_action(result, 2) == nullptr, "a texture read 10 frames ago is not idle");
    const auto* buffer = find_action(result, 3);
    check(buffer != nullptr && buffer->evict, "a resource without a tail is evicted whole");
    check_eq(result.actions.front().index, 0, "LRU drops the oldest first");
    const std::uint64_t expected =
        2 * (candidates[0].bytes[0] - candidates[0].bytes[4]) + candidates[3].bytes[0];
    check_eq(result.dropped_bytes, expected, "dropped bytes add up");
    check_eq(result.drops, 3, "three drops");
}

void test_drops_stop_at_the_goal_and_the_limit()
{
    std::vector<candidate> candidates;
    for(int i = 0; i < 8; ++i)
    {
        candidates.push_back(make_texture(512, 3, k_frame - 200 - i));
    }
    const std::uint64_t each = candidates[0].bytes[0] - candidates[0].bytes[3];
    request req;
    req.frame = k_frame;
    req.min_age_frames = 60;
    req.drop_bytes = each * 2 + 1;
    const auto by_goal = plan(candidates, req);
    check_eq(by_goal.drops, 3, "drops stop once the bytes asked for are freed");

    req.drop_bytes = std::numeric_limits<std::uint64_t>::max();
    req.max_drops = 5;
    const auto by_limit = plan(candidates, req);
    check_eq(by_limit.drops, 5, "drops stop at the drop limit");
}

void test_strategies_order_idle_drops()
{
    std::vector<candidate> candidates{
        make_texture(256, 2, k_frame - 300),
        make_texture(1024, 4, k_frame - 200),
        make_texture(512, 3, k_frame - 400),
    };
    candidates[0].use_count = 5;
    candidates[1].use_count = 50;
    candidates[2].use_count = 500;
    request req;
    req.frame = k_frame;
    req.min_age_frames = 60;
    req.drop_bytes = 1;

    req.order = strategy::lru;
    check_eq(plan(candidates, req).actions.front().index, 2, "LRU takes the longest idle");
    req.order = strategy::lfu;
    check_eq(plan(candidates, req).actions.front().index, 0, "LFU takes the least used");
    req.order = strategy::largest_first;
    check_eq(plan(candidates, req).actions.front().index, 1, "largest first takes the biggest saving");

    req.order = strategy::age_ttl;
    req.max_idle_frames = 250;
    const auto ttl = plan(candidates, req);
    check_eq(ttl.drops, 2, "Age TTL drops every resource idle past its limit, whatever the goal");
    check(find_action(ttl, 1) == nullptr, "and none idle for less");
}

void test_full_reads_protect_and_sampled_reads_degrade()
{
    std::vector<candidate> candidates{
        make_texture(2048, 5, k_frame - 1), // sampled lately, full read long ago
        make_texture(1024, 4, k_frame - 1), // read at full size lately
        make_texture(512, 3, k_frame - 1),  // sampled lately, already at its tail
    };
    candidates[0].last_full_use = k_frame - 500;
    candidates[1].last_full_use = k_frame - 5;
    candidates[2].level = 3;
    request req;
    req.frame = k_frame;
    req.min_age_frames = 60;
    req.drop_bytes = std::numeric_limits<std::uint64_t>::max();

    check(plan(candidates, req).actions.empty(), "without drop_in_use nothing in use drops");

    req.drop_in_use = true;
    const auto result = plan(candidates, req);
    const auto* sampled = find_action(result, 0);
    require(sampled != nullptr, "a texture only sampled lately drops");
    std::vector<candidate> many(5, candidates[0]);
    req.max_in_use_drops = 2;
    check_eq(plan(many, req).drops, 2, "the in-use drop limit bounds the textures created in a pass");
    req.max_in_use_drops = 0;
    check_eq(sampled->level, 1, "it loses one level, not all down to its tail");
    check(find_action(result, 1) == nullptr, "a texture read at full size within the minimum age never drops");
    check(find_action(result, 2) == nullptr, "a texture at its tail drops no further");
}

void test_reuse_gaps_extend_the_minimum_age()
{
    std::vector<candidate> candidates{
        make_buffer(1024, k_frame - 120),   // unread for 120 frames; came back 100 frames after a read once
        make_texture(1024, 4, k_frame - 1), // sampled lately; full reads came 100 frames apart once
    };
    candidates[0].reuse_frames = 100;
    candidates[1].last_full_use = k_frame - 120;
    candidates[1].reuse_frames = 100;
    request req;
    req.frame = k_frame;
    req.min_age_frames = 60;
    req.drop_bytes = std::numeric_limits<std::uint64_t>::max();
    req.drop_in_use = true;

    check(plan(candidates, req).actions.empty(), "within the minimum age plus the reuse gap nothing drops");

    candidates[0].last_use = k_frame - 160;
    candidates[1].last_full_use = k_frame - 160;
    const auto result = plan(candidates, req);
    const auto* buffer = find_action(result, 0);
    check(buffer != nullptr && buffer->evict, "past both an unread resource drops");
    check(find_action(result, 1) != nullptr, "past both a texture in use loses a level");
}

void test_in_use_drops_take_the_largest_first()
{
    std::vector<candidate> candidates{
        make_texture(512, 3, k_frame - 1),
        make_texture(2048, 5, k_frame - 1),
        make_texture(1024, 4, k_frame - 1),
    };
    request req;
    req.frame = k_frame;
    req.min_age_frames = 60;
    req.drop_in_use = true;
    req.drop_bytes = candidates[1].bytes[0] - candidates[1].bytes[1];
    const auto result = plan(candidates, req);
    check_eq(result.drops, 1, "one level of the largest covers the goal");
    check_eq(result.actions.front().index, 1, "the largest saving goes first");
}

void test_rises_fit_room_and_upload()
{
    std::vector<candidate> candidates{
        make_texture(1024, 4, k_frame - 1),
        make_texture(1024, 4, k_frame - 2),
        make_texture(1024, 4, k_frame - 50), // not read lately
    };
    for(auto& c : candidates)
    {
        c.level = 4;
        c.last_drop = k_frame - 500;
    }
    const auto& bytes = candidates[0].bytes;
    request req;
    req.frame = k_frame;
    req.min_age_frames = 60;
    req.rise_bytes = bytes[0] + bytes[1];
    const auto roomy = plan(candidates, req);
    require(roomy.rises == 2, "two wanted textures rise");
    check_eq(find_action(roomy, 0)->level, 0, "the most recently read rises to full size");
    check_eq(find_action(roomy, 1)->level, 1, "the next takes the highest level that still fits");
    check(find_action(roomy, 2) == nullptr, "a texture not read lately stays reduced");
    check_eq(roomy.raised_bytes, bytes[0] + bytes[1], "raised bytes add up");

    req.rise_bytes = std::numeric_limits<std::uint64_t>::max();
    req.max_upload_bytes = bytes[2];
    const auto capped = plan(candidates, req);
    check_eq(find_action(capped, 0)->level, 0, "the first rise may exceed the upload limit");
    check(find_action(capped, 1) == nullptr, "after it the limit holds");
    check_eq(capped.backlog, 1, "the backlog counts the wanted texture that waits");

    req.max_upload_bytes = 0;
    req.max_rises = 1;
    const auto counted = plan(candidates, req);
    check_eq(counted.rises, 1, "the rise limit bounds the textures created in a pass");
    check_eq(counted.backlog, 1, "the texture past the limit waits");
}

void test_rises_wait_after_a_drop_and_under_pressure()
{
    std::vector<candidate> candidates{make_texture(1024, 4, k_frame - 1)};
    candidates[0].level = 4;
    candidates[0].last_drop = k_frame - 10;
    request req;
    req.frame = k_frame;
    req.min_age_frames = 60;
    req.rise_bytes = std::numeric_limits<std::uint64_t>::max();
    check_eq(plan(candidates, req).rises, 0, "nothing rises within the minimum age of its drop");

    candidates[0].last_drop = k_frame - 100;
    check_eq(plan(candidates, req).rises, 1, "after the minimum age it rises");

    req.drop_bytes = 1;
    const auto pressed = plan(candidates, req);
    check_eq(pressed.rises, 0, "nothing rises while there are bytes to drop");
    check_eq(pressed.backlog, 1, "the wanted texture waits in the backlog");

    candidates[0].last_drop = 0;
    req.drop_bytes = 0;
    check_eq(plan(candidates, req).rises, 1, "a texture loaded at its tail never dropped and rises at once");
}

void test_plans_are_deterministic()
{
    std::vector<candidate> candidates;
    for(int i = 0; i < 32; ++i)
    {
        auto c = make_texture(256u << (i % 3), static_cast<std::uint8_t>(2 + i % 3), k_frame - 100 - (i % 4));
        c.use_count = static_cast<std::uint64_t>(i % 5);
        candidates.push_back(c);
    }
    request req;
    req.frame = k_frame;
    req.min_age_frames = 60;
    req.drop_bytes = candidates[0].bytes[0] * 3;
    for(const auto order : {strategy::lru, strategy::lfu, strategy::largest_first})
    {
        req.order = order;
        const auto a = plan(candidates, req);
        const auto b = plan(candidates, req);
        bool same = a.actions.size() == b.actions.size();
        for(std::size_t i = 0; same && i < a.actions.size(); ++i)
        {
            same = a.actions[i].index == b.actions[i].index && a.actions[i].level == b.actions[i].level;
        }
        check(same, "equal inputs give equal plans");
    }
}

// ---------------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------------

/// A resource with known sizes per level; a tail size of 64 keeps levels from tail_level on.
class fake_resource final : public gfx::ievictable
{
public:
    fake_resource(gfx::eviction_registry& registry, std::uint32_t size, std::uint8_t tail_level)
        : registry_(registry)
        , bytes_(rgba8_level_bytes(size))
        , tail_level_(tail_level)
    {
        evict_class_ = gfx::evict_class::evictable;
    }

    ~fake_resource() override
    {
        registry_.unregister_resource(this);
    }

    fake_resource(const fake_resource&) = delete;
    auto operator=(const fake_resource&) -> fake_resource& = delete;
    fake_resource(fake_resource&&) = delete;
    auto operator=(fake_resource&&) -> fake_resource& = delete;

    auto on_evict() -> std::uint64_t override
    {
        has_copy_ = false;
        return bytes_[0];
    }

    auto on_set_level(std::uint8_t level) -> bool override
    {
        if(fail_next_)
        {
            fail_next_ = false;
            return false;
        }
        if(tail_level_ == 0 && level != 0)
        {
            return false;
        }
        has_copy_ = true;
        copy_level_ = level;
        ++level_changes_;
        return true;
    }

    [[nodiscard]] auto gpu_size() const -> std::uint64_t override
    {
        return bytes_[0];
    }

    [[nodiscard]] auto gpu_size_at(std::uint8_t level) const -> std::uint64_t override
    {
        return level < bytes_.size() ? bytes_[level] : 0;
    }

    [[nodiscard]] auto get_tail_level(std::uint32_t tail_size) const -> std::uint8_t override
    {
        return tail_size == 64 ? tail_level_ : 0;
    }

    auto bytes(std::uint8_t level) const -> std::uint64_t
    {
        return bytes_[level];
    }

    bool has_copy_{true};
    std::uint8_t copy_level_{0};
    int level_changes_{0};
    bool fail_next_{false};

private:
    gfx::eviction_registry& registry_;
    std::array<std::uint64_t, gfx::residency::k_max_levels> bytes_;
    std::uint8_t tail_level_{0};
};

void set_frame(std::uint64_t frame)
{
    gfx::detail::g_eviction_frame.store(frame);
}

/// A registry with a budget, tails of 64 and no upload limit.
void activate(gfx::eviction_registry& registry)
{
    gfx::eviction::budget_state budget;
    budget.hard_limit_bytes = std::uint64_t(1) << 32;
    budget.soft_budget_bytes = budget.hard_limit_bytes;
    budget.target_bytes = budget.hard_limit_bytes;
    registry.activate({}, budget, 0);
    gfx::eviction::residency_options options;
    options.tail_size = 64;
    options.max_upload_bytes = 0;
    registry.set_residency_options(options);
}

/// Whether the registry's totals equal what its resources hold, recounted from @p resources.
auto totals_match(const gfx::eviction_registry& registry, const std::vector<std::unique_ptr<fake_resource>>& resources)
    -> bool
{
    std::uint64_t resident = 0;
    std::uint64_t reduced = 0;
    std::uint64_t saved = 0;
    std::uint64_t evicted = 0;
    std::uint64_t resident_count = 0;
    std::uint64_t reduced_count = 0;
    std::uint64_t evicted_count = 0;
    for(const auto& r : resources)
    {
        if(!r)
        {
            continue;
        }
        switch(r->get_evict_state())
        {
            case gfx::evict_state::resident:
                resident += r->bytes(0);
                ++resident_count;
                break;
            case gfx::evict_state::reduced:
                reduced += r->bytes(r->get_level());
                saved += r->bytes(0) - r->bytes(r->get_level());
                ++reduced_count;
                break;
            case gfx::evict_state::evicted:
                evicted += r->bytes(0);
                ++evicted_count;
                break;
        }
    }
    const auto s = registry.get_stats();
    return s.resident_bytes == resident && s.reduced_bytes == reduced && s.saved_bytes == saved &&
           s.evicted_bytes == evicted && s.resident_count == resident_count && s.reduced_count == reduced_count &&
           s.evicted_count == evicted_count;
}

/// Whether every fake's own copy agrees with the state the registry gave it.
auto copies_match_states(const std::vector<std::unique_ptr<fake_resource>>& resources) -> bool
{
    for(const auto& r : resources)
    {
        if(!r)
        {
            continue;
        }
        const bool expects_copy = r->get_evict_state() != gfx::evict_state::evicted;
        if(r->has_copy_ != expects_copy || (expects_copy && r->copy_level_ != r->get_level()))
        {
            return false;
        }
    }
    return true;
}

void test_registry_accounting_over_a_run()
{
    gfx::eviction_registry registry;
    activate(registry);
    set_frame(10);
    std::vector<std::unique_ptr<fake_resource>> resources;
    for(int i = 0; i < 6; ++i)
    {
        resources.push_back(std::make_unique<fake_resource>(registry, 256u << (i % 3), static_cast<std::uint8_t>(i % 2 == 0 ? 2 + i % 3 : 0)));
        registry.register_resource(resources.back().get(), 0);
    }
    check(totals_match(registry, resources), "totals match after registering");

    set_frame(200);
    registry.evict_all();
    check(totals_match(registry, resources), "totals match after evicting all");
    check(copies_match_states(resources), "copies match states after evicting all");
    const auto after_evict = registry.get_stats();
    check_eq(after_evict.resident_count, 0, "nothing stays full");
    check_eq(after_evict.reduced_count, 3, "the textures with tails keep them");
    check_eq(after_evict.evicted_count, 3, "the rest are evicted");

    resources[1]->touch(gfx::texture_read::full);
    registry.request_residency(resources[1].get(), gfx::texture_read::full);
    resources[2]->touch(gfx::texture_read::any_level);
    registry.request_residency(resources[2].get(), gfx::texture_read::any_level);
    check(totals_match(registry, resources), "totals match after on-demand creates");

    resources[3].reset();
    resources[4].reset();
    check(totals_match(registry, resources), "totals match after unregistering a reduced and an evicted resource");

    registry.restore_all();
    check(totals_match(registry, resources), "totals match after restoring all");
    check(copies_match_states(resources), "copies match states after restoring all");
    check_eq(registry.get_stats().resident_count, 4, "every remaining resource is full again");
    resources.clear();
    check_eq(registry.get_stats().registered_count, 0, "unregistering every resource leaves none");
    const auto empty = registry.get_stats();
    check(empty.resident_bytes == 0 && empty.reduced_bytes == 0 && empty.evicted_bytes == 0,
          "and no bytes counted");
}

void test_reads_create_what_they_need()
{
    gfx::eviction_registry registry;
    activate(registry);
    set_frame(10);
    auto texture = std::make_unique<fake_resource>(registry, 1024, 4);
    registry.register_evicted_resource(texture.get());
    texture->has_copy_ = false;

    check(texture->needs_residency_for(gfx::texture_read::any_level), "an evicted texture needs a copy for any read");
    registry.request_residency(texture.get(), gfx::texture_read::any_level);
    check(texture->get_evict_state() == gfx::evict_state::reduced && texture->get_level() == 4,
          "a sampled read of an evicted texture creates its tail only");
    check(!texture->needs_residency_for(gfx::texture_read::any_level), "a reduced texture serves sampled reads as it is");
    check(texture->needs_residency_for(gfx::texture_read::full), "but not full reads");

    registry.request_residency(texture.get(), gfx::texture_read::full);
    check(texture->get_evict_state() == gfx::evict_state::resident && texture->copy_level_ == 0,
          "a full read raises it to full size at once");
    const auto s = registry.get_stats();
    check_eq(s.total_restores, 1, "the tail counts as a restore");
    check_eq(s.total_raises, 1, "the full read counts as a rise");

    registry.set_residency_options({0, 0, true});
    registry.evict_all();
    check(texture->get_evict_state() == gfx::evict_state::evicted, "with tails off an idle texture is evicted whole");
    registry.request_residency(texture.get(), gfx::texture_read::any_level);
    check(texture->get_evict_state() == gfx::evict_state::resident, "and a sampled read then brings the whole texture");
}

void test_failed_level_changes_keep_the_resource()
{
    gfx::eviction_registry registry;
    activate(registry);
    set_frame(10);
    auto texture = std::make_unique<fake_resource>(registry, 1024, 4);
    registry.register_resource(texture.get(), 0);
    set_frame(200);
    texture->fail_next_ = true;
    registry.evict_all();
    check(texture->get_evict_state() == gfx::evict_state::resident, "a drop whose copy fails keeps the full texture");
    check_eq(registry.get_stats().failed_restores, 1, "and counts as failed");

    registry.evict_all();
    require(texture->get_evict_state() == gfx::evict_state::reduced, "the next drop goes through");
    texture->fail_next_ = true;
    registry.request_residency(texture.get(), gfx::texture_read::full);
    check(texture->get_evict_state() == gfx::evict_state::reduced && texture->get_level() == 4,
          "a rise whose copy fails keeps the tail");
    std::vector<std::unique_ptr<fake_resource>> resources;
    resources.push_back(std::move(texture));
    check(totals_match(registry, resources), "totals match after failed changes");
}

void test_frame_pass_follows_the_budget()
{
    gfx::eviction_registry registry;
    activate(registry);
    set_frame(10);
    std::vector<std::unique_ptr<fake_resource>> resources;
    for(int i = 0; i < 4; ++i)
    {
        resources.push_back(std::make_unique<fake_resource>(registry, 1024, 4));
        registry.register_resource(resources.back().get(), 0);
    }
    const std::uint64_t full = resources[0]->bytes(0);
    const std::uint64_t tail = resources[0]->bytes(4);

    gfx::eviction::frame_request req;
    req.soft_budget_bytes = 3 * full;
    // Two drops to the tail meet the target exactly.
    req.target_bytes = 2 * full + 2 * tail;
    req.min_age_frames = 60;

    set_frame(100);
    req.used_for_drops = 4 * full;
    req.used_for_rises = 4 * full;
    registry.update_residency(req);
    check_eq(registry.get_stats().reduced_count, 2, "over the soft budget idle textures drop until the target is met");
    check(totals_match(registry, resources), "totals match after the drops");

    req.used_for_drops = 2 * full + 2 * tail + full / 2;
    req.used_for_rises = req.used_for_drops;
    for(auto& r : resources)
    {
        r->touch(gfx::texture_read::any_level);
    }
    set_frame(300);
    registry.update_residency(req);
    check_eq(registry.get_stats().reduced_count, 2, "between the target and the soft budget nothing changes");
    check_eq(registry.get_stats().raise_backlog, 0, "textures not read lately are not waiting");

    for(auto& r : resources)
    {
        r->touch(gfx::texture_read::any_level);
    }
    req.used_for_drops = 2 * full + 2 * tail;
    req.used_for_rises = full + 2 * tail;
    registry.update_residency(req);
    const auto s = registry.get_stats();
    check_eq(s.reduced_count, 1, "under the target a wanted texture rises within the room");
    check_eq(s.raise_backlog, 1, "the one without room waits");
    check_eq(s.last_pass_raised, 1, "one rise this frame");
    check(totals_match(registry, resources), "totals match after the rise");
    check(copies_match_states(resources), "copies match states after the rise");
}

void test_reads_on_a_cycle_stop_dropping()
{
    gfx::eviction_registry registry;
    activate(registry);
    set_frame(10);
    auto buffer = std::make_unique<fake_resource>(registry, 256, 0);
    registry.register_resource(buffer.get(), 0);
    const std::uint64_t full = buffer->bytes(0);
    const auto read = [&](std::uint64_t frame)
    {
        set_frame(frame);
        registry.request_residency(buffer.get(), gfx::texture_read::any_level);
        buffer->touch(gfx::texture_read::any_level);
    };
    const auto pass = [&](std::uint64_t frame)
    {
        set_frame(frame);
        gfx::eviction::frame_request req;
        req.used_for_drops = full;
        req.used_for_rises = full;
        req.soft_budget_bytes = 0;
        req.target_bytes = 0;
        req.min_age_frames = 60;
        registry.update_residency(req);
        return buffer->get_evict_state();
    };

    // Read at frames 10, 110, 210: once every 100 frames.
    check(pass(71) == gfx::evict_state::evicted, "unread past the minimum age it drops");
    read(110);
    check(buffer->get_evict_state() == gfx::evict_state::resident && buffer->has_copy_, "the read brings it back");
    check_eq(registry.get_stats().thrash_events, 1, "a read within the minimum age of the drop is a thrash");
    check(pass(171) == gfx::evict_state::resident, "past the minimum age it stays: it came back after 100 frames");
    check(pass(209) == gfx::evict_state::resident, "it stays until the next read of its cycle");
    read(210);
    check(pass(271) == gfx::evict_state::resident, "the cycle goes on without a drop");

    check(pass(369) == gfx::evict_state::resident, "unread for less than the minimum age plus its gap it stays");
    check(pass(370) == gfx::evict_state::evicted, "once the cycle stops it drops after that long");
    read(2370);
    check(pass(2431) == gfx::evict_state::evicted, "a read after a long gap keeps nothing: it drops after the minimum age");
    check_eq(registry.get_stats().thrash_events, 1, "a read long after the drop is no thrash");
    const auto s = registry.get_stats();
    check(s.evicted_count == 1 && s.evicted_bytes == full && s.resident_bytes == 0, "the registry counts it evicted");
}

void test_recent_traffic_spans_the_frames_in_flight()
{
    gfx::eviction_registry registry;
    activate(registry);
    set_frame(10);
    auto texture = std::make_unique<fake_resource>(registry, 1024, 4);
    registry.register_resource(texture.get(), 0);
    const std::uint64_t full = texture->bytes(0);
    check_eq(registry.get_recent_traffic().created, full, "a registration counts as created this frame");
    check_eq(registry.peek_recent_created_bytes(), full, "the load path reads the same");
    registry.on_frame_submitted();
    check_eq(registry.get_recent_traffic().created, full, "it stays counted after the frame is submitted");
    registry.on_frame_submitted();
    registry.on_frame_submitted();
    check_eq(registry.get_recent_traffic().created, full, "and while the frame may still be in flight");
    check_eq(registry.peek_recent_created_bytes(), full, "the load path still counts it");
    registry.on_frame_submitted();
    check_eq(registry.get_recent_traffic().created, 0, "three frames after its own, bgfx's stats show it");
    check_eq(registry.peek_recent_created_bytes(), 0, "and the load path stops counting it");
    texture.reset();
    check_eq(registry.get_recent_traffic().released, full, "an unregister counts as released");
}

void test_restore_all_is_no_read()
{
    gfx::eviction_registry registry;
    activate(registry);
    set_frame(10);
    auto buffer = std::make_unique<fake_resource>(registry, 256, 0);
    registry.register_resource(buffer.get(), 0);
    const std::uint64_t full = buffer->bytes(0);
    gfx::eviction::frame_request req;
    req.used_for_drops = full;
    req.used_for_rises = full;
    req.soft_budget_bytes = 0;
    req.target_bytes = 0;
    req.min_age_frames = 60;

    set_frame(71);
    registry.update_residency(req);
    require(buffer->get_evict_state() == gfx::evict_state::evicted, "unread past the minimum age it drops");
    set_frame(72);
    registry.restore_all();
    check(buffer->get_evict_state() == gfx::evict_state::resident && buffer->has_copy_, "restore all brings it back");
    check_eq(registry.get_stats().thrash_events, 0, "a restore all is no thrash");
    set_frame(133);
    registry.update_residency(req);
    check(buffer->get_evict_state() == gfx::evict_state::evicted,
          "it drops again after the minimum age: restore all taught it no reuse gap");
}

void test_new_resources_count_as_read_in_full()
{
    gfx::eviction_registry registry;
    activate(registry);
    set_frame(500);
    auto texture = std::make_unique<fake_resource>(registry, 1024, 4);
    registry.register_resource(texture.get(), 0);
    texture->touch(gfx::texture_read::any_level);
    const std::uint64_t full = texture->bytes(0);
    gfx::eviction::frame_request req;
    req.used_for_drops = full;
    req.used_for_rises = full;
    req.soft_budget_bytes = 0;
    req.target_bytes = 0;
    req.min_age_frames = 60;

    set_frame(501);
    registry.update_residency(req);
    check(texture->get_evict_state() == gfx::evict_state::resident,
          "a texture registered a frame ago keeps its levels until it goes the minimum age without a full read");
    set_frame(560);
    texture->touch(gfx::texture_read::any_level);
    registry.update_residency(req);
    check(texture->get_evict_state() == gfx::evict_state::reduced && texture->get_level() == 1,
          "after that, sampled reads alone let a pass take a level");
}

void test_budgets_read_whole_while_published()
{
    gfx::eviction_registry registry;
    activate(registry);
    // Starts from a budget that follows the pattern every published one does: all fields 0, engine bytes.
    gfx::eviction::budget_state start;
    start.counts_engine_bytes = true;
    registry.set_budget(start, 0);
    std::atomic<bool> stop{false};
    std::atomic<bool> reading{false};
    std::thread writer(
        [&]
        {
            // Publishes only once the reader runs, so the two overlap however they are scheduled.
            while(!reading.load())
            {
                std::this_thread::yield();
            }
            for(std::uint64_t value = 1; value <= 20000; ++value)
            {
                gfx::eviction::budget_state budget;
                budget.hard_limit_bytes = value;
                budget.soft_budget_bytes = value;
                budget.target_bytes = value;
                budget.safety_margin_bytes = value;
                budget.counts_engine_bytes = (value % 2) == 0;
                registry.set_budget(budget, value);
            }
            stop.store(true);
        });
    std::uint64_t reads = 0;
    std::uint64_t torn = 0;
    reading.store(true);
    while(!stop.load())
    {
        const auto budget = registry.current_budget();
        const std::uint64_t value = budget.hard_limit_bytes;
        const bool whole = budget.soft_budget_bytes == value && budget.target_bytes == value &&
                           budget.safety_margin_bytes == value && budget.counts_engine_bytes == ((value % 2) == 0);
        torn += whole ? 0 : 1;
        ++reads;
    }
    writer.join();
    check(reads > 0, "the reader ran while the writer published");
    check_eq(torn, 0, "every budget read while another thread publishes is one whole budget");
}

void test_admissions_hold_to_the_limit()
{
    constexpr std::uint64_t k_mib = 1024 * 1024;
    gfx::eviction_registry registry;
    activate(registry);
    set_frame(10);

    check(registry.try_admit(60 * k_mib, 30 * k_mib, 100 * k_mib), "bytes that fit under the limit are admitted");
    check_eq(registry.peek_admitted_bytes(), 60 * k_mib, "admitted bytes count at once");
    check(!registry.try_admit(20 * k_mib, 30 * k_mib, 100 * k_mib), "bytes past the limit with what is admitted are refused");
    check_eq(registry.peek_admitted_bytes(), 60 * k_mib, "a refusal counts nothing");

    auto texture = std::make_unique<fake_resource>(registry, 1024, 4);
    registry.register_resource(texture.get(), 0, 60 * k_mib);
    check_eq(registry.peek_admitted_bytes(), 0, "registering hands the admitted bytes over");
    check_eq(registry.peek_held_bytes(), texture->bytes(0), "the registry holds the resource's own bytes");
    auto deferred = std::make_unique<fake_resource>(registry, 1024, 0);
    check(registry.try_admit(k_mib, 0, 100 * k_mib), "a second admission");
    registry.register_evicted_resource(deferred.get(), k_mib);
    check_eq(registry.peek_admitted_bytes(), 0, "a copy that was not created gives its bytes back");
    registry.register_resource(texture.get(), 0, k_mib);
    check_eq(registry.peek_admitted_bytes(), 0, "a second registration gives back nothing it was not given");

    constexpr int k_threads = 8;
    constexpr int k_tries = 100;
    std::atomic<int> admitted{0};
    std::vector<std::thread> threads;
    for(int t = 0; t < k_threads; ++t)
    {
        threads.emplace_back(
            [&]
            {
                for(int i = 0; i < k_tries; ++i)
                {
                    if(registry.try_admit(k_mib, 0, 100 * k_mib))
                    {
                        admitted.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });
    }
    for(auto& thread : threads)
    {
        thread.join();
    }
    check_eq(admitted.load(), 100, "threads racing for room admit exactly what the limit holds");
    check_eq(registry.peek_admitted_bytes(), 100 * k_mib, "and count exactly that");
    registry.shutdown();
    check_eq(registry.peek_admitted_bytes(), 0, "shutting down drops every admission");
    registry.register_evicted_resource(nullptr, k_mib);
    check_eq(registry.peek_admitted_bytes(), 0, "a late hand-back after that never wraps around");
}

} // namespace

void run_texture_residency_suite(rtti::context& /*ctx*/)
{
    RUN_CASE(test_idle_textures_go_to_their_tails());
    RUN_CASE(test_drops_stop_at_the_goal_and_the_limit());
    RUN_CASE(test_strategies_order_idle_drops());
    RUN_CASE(test_full_reads_protect_and_sampled_reads_degrade());
    RUN_CASE(test_reuse_gaps_extend_the_minimum_age());
    RUN_CASE(test_in_use_drops_take_the_largest_first());
    RUN_CASE(test_rises_fit_room_and_upload());
    RUN_CASE(test_rises_wait_after_a_drop_and_under_pressure());
    RUN_CASE(test_plans_are_deterministic());
    const std::uint64_t frame = gfx::detail::eviction_frame();
    RUN_CASE(test_registry_accounting_over_a_run());
    RUN_CASE(test_reads_create_what_they_need());
    RUN_CASE(test_failed_level_changes_keep_the_resource());
    RUN_CASE(test_frame_pass_follows_the_budget());
    RUN_CASE(test_reads_on_a_cycle_stop_dropping());
    RUN_CASE(test_recent_traffic_spans_the_frames_in_flight());
    RUN_CASE(test_admissions_hold_to_the_limit());
    RUN_CASE(test_restore_all_is_no_read());
    RUN_CASE(test_new_resources_count_as_read_in_full());
    RUN_CASE(test_budgets_read_whole_while_published());
    gfx::detail::g_eviction_frame.store(frame);
}

REGISTER_TEST_SUITE("texture residency", run_texture_residency_suite)
