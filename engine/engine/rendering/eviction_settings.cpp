#include "eviction_settings.h"

#include <graphics/eviction.h>
#include <graphics/graphics.h>

#include <algorithm>

namespace unravel
{
namespace
{
auto clamp_u64(std::int64_t value) -> std::uint64_t
{
    return static_cast<std::uint64_t>(std::max<std::int64_t>(0, value));
}

/// Same hysteresis depth for manual mode that the auto budget uses, derived from the two user
/// settings. Pegging this ratio keeps the manual path predictable: drop @ref manual_budget_mb and
/// the target falls in proportion without exposing a third knob.
auto manual_target_ratio(const eviction_settings& s) -> double
{
    if(s.budget_fraction <= 0.0f)
    {
        return 1.0;
    }
    const double ratio = static_cast<double>(s.target_fraction) / static_cast<double>(s.budget_fraction);
    // Clamp into a sensible band; a configuration with target_fraction > budget_fraction is a
    // user mistake but should not push target_bytes above soft_budget_bytes (the entire eviction
    // policy assumes target <= soft).
    return std::clamp(ratio, 0.1, 1.0);
}

/// Safety margin used by reclaim_for when projecting future occupancy. 2% of the hard limit, with
/// a floor of 64 MiB so very small budgets (or simulated debug budgets) keep a usable cushion.
auto safety_margin_for(std::uint64_t hard_limit_bytes) -> std::uint64_t
{
    constexpr std::uint64_t k_floor = std::uint64_t(64) * 1024 * 1024;
    return std::max(k_floor, hard_limit_bytes / 50);
}

/// Build the @ref gfx::eviction::budget_state for this frame.
///   - Auto mode: hard = gpu_max, soft = gpu_max * budget_fraction, target = gpu_max * target_fraction.
///     This is the device-wide budget reclaim_for projects against.
///   - Manual mode (or auto with no reported budget): hard = soft = manual_budget_mb, target =
///     manual_budget_mb * (target_fraction / budget_fraction). The user explicitly capped the
///     engine's footprint so soft has no headroom over hard; target stays in proportion so the
///     two modes share the same hysteresis depth.
auto build_budget(const eviction_settings& settings, std::uint64_t gpu_max)
    -> gfx::eviction::budget_state
{
    gfx::eviction::budget_state b;
    if(settings.auto_budget && gpu_max > 0)
    {
        b.hard_limit_bytes = gpu_max;
        b.soft_budget_bytes = static_cast<std::uint64_t>(static_cast<double>(gpu_max) * settings.budget_fraction);
        b.target_bytes = static_cast<std::uint64_t>(static_cast<double>(gpu_max) * settings.target_fraction);
    }
    else
    {
        const std::uint64_t mb = clamp_u64(settings.manual_budget_mb) * 1024ull * 1024ull;
        if(mb == 0)
        {
            return b; // hard_limit_bytes == 0 disables reclaim_for entirely.
        }
        b.hard_limit_bytes = mb;
        b.soft_budget_bytes = mb;
        b.target_bytes = static_cast<std::uint64_t>(static_cast<double>(mb) * manual_target_ratio(settings));
    }
    b.safety_margin_bytes = safety_margin_for(b.hard_limit_bytes);
    return b;
}
} // namespace

void update_eviction(const eviction_settings& settings)
{
    namespace ev = gfx::eviction;

    if(!ev::is_supported())
    {
        return;
    }

    const auto* bx = gfx::get_stats();
    const std::uint64_t gpu_max = (bx != nullptr) ? clamp_u64(bx->gpuMemoryMax) : 0;
    const std::uint64_t gpu_used = (bx != nullptr) ? clamp_u64(bx->gpuMemoryUsed) : 0;

    // Peek (do not consume) the bytes queued since the last bgfx::frame/flush. The backend's
    // gpuMemoryUsed lags by a frame or more, so a burst of creates is invisible until later; the
    // queued counter adds the in-flight bytes back into the prediction. It is cleared centrally by
    // gfx::frame()/gfx::flush(), so this read must NOT drain it (reclaim_for and any other
    // observers in the same frame all need to see the same number).
    const std::uint64_t queued = ev::peek_queued_bytes();

    const ev::budget_state budget = build_budget(settings, gpu_max);

    // The metric we compare against the budget for the proactive sweep. In auto mode we treat
    // gpu memory as ground truth. In manual mode the user is asking us to cap our OWN footprint,
    // so we use evictable resident bytes — touching the device-wide gpu_used would have us
    // evicting engine resources to make room for external programs.
    const std::uint64_t used = (settings.auto_budget && gpu_max > 0)
                                   ? gpu_used + queued
                                   : ev::get_stats().resident_bytes;

    // Publish the budget so reclaim_for and tooling see the same numbers.
    ev::set_budget(budget, used);

    if(!settings.enabled || used <= budget.soft_budget_bytes)
    {
        return;
    }

    const auto strat = static_cast<ev::strategy>(settings.strategy);
    const auto min_age = static_cast<std::uint32_t>(std::max(0, settings.min_age_frames));
    const auto max_evictions = static_cast<std::uint32_t>(std::max(0, settings.max_evictions));
    ev::evict_bytes(used - budget.target_bytes, strat, min_age, max_evictions);
}

} // namespace unravel
