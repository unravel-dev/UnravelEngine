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

    // The backend's gpuMemoryUsed lags by a frame or more, so a burst of resource creations is not
    // reflected until later. The registry tracks evictable resident bytes synchronously (updated in
    // register_resource, even from worker threads), so use it as a safety net: react to whichever
    // metric is higher this frame.
    const std::uint64_t resident = ev::get_stats().resident_bytes;

    // Resolve the budget metrics once: GPU memory when an auto budget is available, otherwise the
    // evictable resident bytes against the manual budget.
    std::uint64_t used = 0;
    std::uint64_t budget = 0;
    std::uint64_t target = 0;
    if(settings.auto_budget && gpu_max > 0)
    {
        used = std::max(gpu_used, resident);
        budget = static_cast<std::uint64_t>(static_cast<double>(gpu_max) * settings.budget_fraction);
        target = static_cast<std::uint64_t>(static_cast<double>(gpu_max) * settings.target_fraction);
    }
    else
    {
        used = resident;
        budget = static_cast<std::uint64_t>(std::max(0, settings.manual_budget_mb)) * 1024ull * 1024ull;
        target = budget;
    }

    // Always report the budget so tooling (overlay/profiler) can display it even while paging is off.
    ev::report_budget(used, budget, target);

    if(!settings.enabled || used <= budget || used <= target)
    {
        return;
    }

    const auto strat = static_cast<ev::strategy>(settings.strategy);
    const auto min_age = static_cast<std::uint32_t>(std::max(0, settings.min_age_frames));
    const auto max_evictions = static_cast<std::uint32_t>(std::max(0, settings.max_evictions));
    ev::evict_bytes(used - target, strat, min_age, max_evictions);
}

} // namespace unravel
