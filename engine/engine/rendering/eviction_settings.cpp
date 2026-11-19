#include "eviction_settings.h"

#include <graphics/eviction.h>
#include <graphics/graphics.h>

#include <algorithm>
#include <limits>

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
    constexpr std::uint64_t MIN_SAFETY_MARGIN_BYTES = std::uint64_t(64) * 1024 * 1024;
    constexpr std::uint64_t HARD_LIMIT_PER_MARGIN = 50;
    return std::max(MIN_SAFETY_MARGIN_BYTES, hard_limit_bytes / HARD_LIMIT_PER_MARGIN);
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
        const auto soft_budget = static_cast<std::uint64_t>(static_cast<double>(gpu_max) * settings.budget_fraction);
        if(soft_budget == 0)
        {
            return b; // No budget to hold to, as with a manual budget of 0.
        }
        b.hard_limit_bytes = gpu_max;
        b.soft_budget_bytes = soft_budget;
        // The eviction policy assumes target <= soft (see manual_target_ratio).
        b.target_bytes = std::min(soft_budget, static_cast<std::uint64_t>(static_cast<double>(gpu_max) * settings.target_fraction));
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
        b.counts_engine_bytes = true;
        b.target_bytes = static_cast<std::uint64_t>(static_cast<double>(mb) * manual_target_ratio(settings));
    }
    b.safety_margin_bytes = safety_margin_for(b.hard_limit_bytes);
    return b;
}

/// The strategy the settings name; least recently used for a number no strategy has, from a settings
/// file written elsewhere.
auto get_strategy(const eviction_settings& settings) -> gfx::eviction::strategy
{
    return settings.strategy <= gfx::eviction::strategy::age_ttl ? settings.strategy : gfx::eviction::strategy::lru;
}

/// How textures keep and regain their mips under these settings.
auto make_residency_options(const eviction_settings& settings) -> gfx::eviction::residency_options
{
    gfx::eviction::residency_options options;
    options.tail_size = settings.keep_mip_tails ? static_cast<std::uint32_t>(std::max(1, settings.mip_tail_size)) : 0;
    options.max_upload_bytes = clamp_u64(settings.max_upload_mb) * 1024ull * 1024ull;
    options.reduce_in_use = settings.reduce_textures_in_use;
    options.paging_enabled = settings.enabled;
    return options;
}

/// Usage as the drops and the rises judge it. bgfx shows a create or a destroy in gpuMemoryUsed only
/// a few frames later. Drops credit the recent releases, so they never take more than the pressure
/// asks; rises add the recent creates and credit no release, so they never overshoot the target.
/// A manual budget counts the engine's own bytes, which are exact at once.
struct judged_usage
{
    std::uint64_t for_drops = 0;
    std::uint64_t for_rises = 0;
};

auto judge_usage(const eviction_settings& settings, std::uint64_t gpu_max, std::uint64_t gpu_used) -> judged_usage
{
    namespace ev = gfx::eviction;
    if(!settings.auto_budget || gpu_max == 0)
    {
        const auto stats = ev::get_stats();
        const std::uint64_t own = stats.resident_bytes + stats.reduced_bytes + ev::peek_external_queued_bytes();
        return {own, own};
    }
    const ev::recent_traffic traffic = ev::get_recent_traffic();
    const std::uint64_t with_queued = gpu_used + ev::peek_queued_bytes();
    judged_usage usage;
    usage.for_drops = with_queued > traffic.released ? with_queued - traffic.released : 0;
    usage.for_rises = gpu_used + traffic.created;
    return usage;
}
} // namespace

void update_eviction(const eviction_settings& settings)
{
    namespace ev = gfx::eviction;

    if(!ev::is_supported())
    {
        return;
    }
    ev::set_residency_options(make_residency_options(settings));

    const auto* bx = bgfx::getStats();
    const std::uint64_t gpu_max = (bx != nullptr) ? clamp_u64(bx->gpuMemoryMax) : 0;
    const std::uint64_t gpu_used = (bx != nullptr) ? clamp_u64(bx->gpuMemoryUsed) : 0;

    const ev::budget_state budget = build_budget(settings, gpu_max);
    const judged_usage usage = judge_usage(settings, gpu_max, gpu_used);

    // Publish the budget so reclaim_for and tooling see the same numbers.
    ev::set_budget(budget, usage.for_drops);

    // A zero hard limit is no budget at all, not a budget of nothing: evicting to it would drop
    // everything every frame. Without a budget, or with paging off, nothing drops, and reduced
    // textures still sharpen again within the upload limit.
    const bool has_budget = budget.hard_limit_bytes != 0;
    const bool may_drop = settings.enabled && has_budget;
    constexpr std::uint64_t no_limit = std::numeric_limits<std::uint64_t>::max();

    ev::frame_request request;
    request.used_for_drops = usage.for_drops;
    request.used_for_rises = usage.for_rises;
    request.soft_budget_bytes = may_drop ? budget.soft_budget_bytes : no_limit;
    // With paging off nothing drops again, so textures reduced earlier sharpen whatever the target.
    request.target_bytes = may_drop ? budget.target_bytes : no_limit;
    request.strat = get_strategy(settings);
    request.min_age_frames = static_cast<std::uint32_t>(std::max(0, settings.min_age_frames));
    // Past the hard limit every drop counts, however many it takes.
    request.max_drops = (usage.for_drops > budget.hard_limit_bytes)
                            ? static_cast<std::uint32_t>(0)
                            : static_cast<std::uint32_t>(std::max(0, settings.max_evictions));
    // Each rise and each drop of a texture in use creates a texture: the cap bounds the driver calls of a
    // pass even past the hard limit, where only whole evictions (destroys alone) are unbounded.
    request.max_rises = static_cast<std::uint32_t>(std::max(0, settings.max_evictions));
    request.max_in_use_drops = request.max_rises;
    ev::update_residency(request);
}

} // namespace unravel
