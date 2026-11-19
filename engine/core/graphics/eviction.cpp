#include "eviction.h"

#include "eviction_registry.h"
#include "graphics.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace gfx
{

namespace detail
{
std::atomic<std::uint64_t> g_eviction_frame{0};
} // namespace detail

namespace
{
/// Frames pumped so queued destroys have executed before the caller reads usage back. With the
/// multithreaded renderer one bgfx::frame only hands the command buffer to the render thread; the
/// next frame waits until the render thread has executed it, destroys included.
constexpr int k_frames_to_service_destroys = 2;

auto active_registry() -> eviction_registry&
{
    return eviction_registry::instance();
}


/// The budget published until the first frame's driver runs, matching the default
/// unravel::eviction_settings fractions so reclaim_for works from the start.
auto make_startup_budget(std::uint64_t gpu_max) -> eviction::budget_state
{
    eviction::budget_state b;
    b.hard_limit_bytes = gpu_max;
    b.soft_budget_bytes = static_cast<std::uint64_t>(static_cast<double>(gpu_max) * 0.85);
    b.target_bytes = static_cast<std::uint64_t>(static_cast<double>(gpu_max) * 0.75);
    b.safety_margin_bytes = eviction::get_safety_margin(gpu_max, !eviction::driver_pages_gpu_memory());
    return b;
}

/// D3D11 and OpenGL drivers page resources behind the API, so a second layer of eviction on top
/// would only thrash. Vulkan, Metal and D3D12 report explicit memory budgets through bgfx and need it.
auto is_paged_by_driver(bgfx::RendererType::Enum type) -> bool
{
    return type == bgfx::RendererType::Direct3D11 || type == bgfx::RendererType::OpenGL;
}
} // namespace

namespace eviction
{

auto init(const config& cfg) -> init_status
{
    const auto* gpu_stats = bgfx::getStats();
    if(gpu_stats == nullptr)
    {
        active_registry().set_inactive(init_status::failed);
        return init_status::failed;
    }
    if(gpu_stats->gpuMemoryMax <= 0)
    {
        active_registry().set_inactive(init_status::unsupported);
        return init_status::unsupported;
    }
    if(is_paged_by_driver(bgfx::getRendererType()))
    {
        active_registry().set_inactive(init_status::unnecessary);
        return init_status::unnecessary;
    }
    const auto gpu_max = static_cast<std::uint64_t>(gpu_stats->gpuMemoryMax);
    const auto gpu_used = static_cast<std::uint64_t>(std::max<std::int64_t>(0, gpu_stats->gpuMemoryUsed));
    active_registry().activate(cfg, make_startup_budget(gpu_max), gpu_used);
    return init_status::ok;
}

auto is_supported() -> bool
{
    return active_registry().get_init_status() == init_status::ok;
}

void shutdown()
{
    if(debug_consumed_bytes() > 0)
    {
        debug_release_memory();
    }
    active_registry().shutdown();
}

auto evict(const config& cfg) -> stats
{
    return active_registry().evict(cfg);
}

auto evict() -> stats
{
    return active_registry().evict_default();
}

auto evict_bytes(std::uint64_t free_bytes, strategy strat, std::uint32_t min_age_frames, std::uint32_t max_evictions)
    -> stats
{
    return active_registry().evict_bytes(free_bytes, strat, min_age_frames, max_evictions);
}

auto evict_all() -> stats
{
    return active_registry().evict_all();
}

auto update_residency(const frame_request& request) -> stats
{
    return active_registry().update_residency(request);
}

void set_residency_options(const residency_options& options)
{
    active_registry().set_residency_options(options);
}

auto get_residency_options() -> residency_options
{
    return active_registry().get_residency_options();
}

auto get_recent_traffic() -> recent_traffic
{
    return active_registry().get_recent_traffic();
}

auto get_load_tail_size() -> std::uint32_t
{
    return active_registry().get_load_tail_size();
}

auto driver_pages_gpu_memory() -> bool
{
    return !(BX_PLATFORM_LINUX || BX_PLATFORM_ANDROID);
}

auto get_safety_margin(std::uint64_t hard_limit_bytes, bool without_driver_paging) -> std::uint64_t
{
    constexpr std::uint64_t k_mib = 1024 * 1024;
    if(without_driver_paging)
    {
        return 256 * k_mib + hard_limit_bytes / 25;
    }
    return std::max(64 * k_mib, hard_limit_bytes / 50);
}

namespace
{
auto live_gpu_used() -> std::uint64_t
{
    const auto* gpu_stats = bgfx::getStats();
    if(gpu_stats == nullptr)
    {
        return 0;
    }
    return static_cast<std::uint64_t>(std::max<std::int64_t>(0, gpu_stats->gpuMemoryUsed));
}

auto credit_pending_release(std::uint64_t gross) -> std::uint64_t
{
    const std::uint64_t pending = active_registry().peek_pending_release_bytes();
    return gross > pending ? gross - pending : 0;
}

auto project_occupancy(std::uint64_t used, std::uint64_t queued, std::uint64_t request, std::uint64_t margin)
    -> std::uint64_t
{
    return credit_pending_release(used + queued + request + margin);
}

/// GPU bytes in use as the hard limit sees them, with the safety margin and without the bytes admitted
/// for loads: the device's usage with this frame's creates, releases credited. A manual budget counts
/// the engine's tracked bytes and this frame's untracked creates.
auto hard_limit_usage(const budget_state& budget) -> std::uint64_t
{
    auto& registry = active_registry();
    if(budget.counts_engine_bytes)
    {
        return registry.peek_held_bytes() + registry.peek_external_queued_bytes() + budget.safety_margin_bytes;
    }
    return project_occupancy(live_gpu_used(), registry.peek_queued_bytes(), 0, budget.safety_margin_bytes);
}

/// GPU bytes in use as a load that can start smaller judges them, without the bytes admitted for
/// loads: the device's usage with every create bgfx's stats may not show yet, no release credited,
/// so a burst of loads never looks lighter than it is. A manual budget counts as the hard limit does.
auto soft_budget_usage(const budget_state& budget) -> std::uint64_t
{
    auto& registry = active_registry();
    if(budget.counts_engine_bytes)
    {
        return registry.peek_held_bytes() + registry.peek_external_queued_bytes();
    }
    return live_gpu_used() + registry.peek_recent_created_bytes();
}

/// GPU bytes a create may take now and keep the hard limit, its margin and the admitted loads; the
/// largest number when no budget applies.
auto get_room_under_hard_limit() -> std::uint64_t
{
    const budget_state budget = active_registry().current_budget();
    if(budget.hard_limit_bytes == 0)
    {
        return std::numeric_limits<std::uint64_t>::max();
    }
    const std::uint64_t used = hard_limit_usage(budget) + active_registry().peek_admitted_bytes();
    return budget.hard_limit_bytes > used ? budget.hard_limit_bytes - used : 0;
}

auto projected_allocation_bytes(std::uint64_t bytes) -> std::uint64_t
{
    const budget_state budget = active_registry().current_budget();
    if(budget.hard_limit_bytes == 0)
    {
        return 0;
    }
    return hard_limit_usage(budget) + active_registry().peek_admitted_bytes() + bytes;
}
} // namespace

auto admit_allocation(std::uint64_t bytes, admit_limit limit) -> admission
{
    const admission everything{true, 0};
    if(bytes == 0 || !is_supported())
    {
        return everything;
    }
    const budget_state budget = active_registry().current_budget();
    if(budget.hard_limit_bytes == 0)
    {
        return everything;
    }
    const bool under_soft = limit == admit_limit::soft_budget;
    const std::uint64_t used = under_soft ? soft_budget_usage(budget) : hard_limit_usage(budget);
    const std::uint64_t cap = under_soft ? budget.soft_budget_bytes : budget.hard_limit_bytes;
    if(!active_registry().try_admit(bytes, used, cap))
    {
        return {};
    }
    return {true, bytes};
}

auto would_allocation_fit(std::uint64_t bytes) -> bool
{
    if(bytes == 0 || !is_supported())
    {
        return true;
    }
    const budget_state budget = active_registry().current_budget();
    if(budget.hard_limit_bytes == 0)
    {
        return true;
    }
    return projected_allocation_bytes(bytes) <= budget.hard_limit_bytes;
}

auto reclaim_for(std::uint64_t bytes, reclaim_kind kind) -> reclaim_result
{
    if(bytes == 0 || !is_supported())
    {
        return reclaim_result::headroom;
    }
    const budget_state budget = active_registry().current_budget();
    if(budget.hard_limit_bytes == 0)
    {
        return reclaim_result::headroom;
    }

    // Off the API thread (a render target made by a loader) the call only projects: a sweep there would
    // swap handles under the render thread and pump frames from the wrong thread. With paging off nothing
    // is taken from resources either.
    if(kind == reclaim_kind::evictable || !active_registry().is_api_thread() || !active_registry().is_paging_enabled())
    {
        return would_allocation_fit(bytes) ? reclaim_result::headroom : reclaim_result::insufficient;
    }

    const std::uint64_t projected = projected_allocation_bytes(bytes);
    if(projected <= budget.soft_budget_bytes)
    {
        return reclaim_result::headroom;
    }

    const config cfg = active_registry().snapshot_default_config();
    const std::uint64_t deficit = projected > budget.target_bytes ? projected - budget.target_bytes : 0;
    const stats sweep = evict_bytes(deficit, cfg.strat, cfg.min_age_frames, cfg.max_evictions);
    const std::uint64_t freed = sweep.last_pass_freed_bytes;

    std::uint64_t occupancy = projected_allocation_bytes(bytes);

    // immediate: evicted destroys must land on the GPU before the imminent allocation.
    if(freed > 0)
    {
        gfx::frames(k_frames_to_service_destroys, BGFX_FRAME_FLUSH);
        occupancy = projected_allocation_bytes(bytes);
        return occupancy > budget.hard_limit_bytes ? reclaim_result::insufficient : reclaim_result::reclaimed;
    }

    if(occupancy <= budget.hard_limit_bytes)
    {
        return reclaim_result::reclaimed;
    }

    gfx::frames(k_frames_to_service_destroys, BGFX_FRAME_FLUSH);
    occupancy = projected_allocation_bytes(bytes);
    return occupancy > budget.hard_limit_bytes ? reclaim_result::insufficient : reclaim_result::reclaimed;
}

auto peek_queued_bytes() -> std::uint64_t
{
    return active_registry().peek_queued_bytes();
}

auto peek_pending_release_bytes() -> std::uint64_t
{
    return active_registry().peek_pending_release_bytes();
}

auto peek_external_queued_bytes() -> std::uint64_t
{
    return active_registry().peek_external_queued_bytes();
}

void note_pending_allocation(std::uint64_t bytes)
{
    active_registry().note_pending_allocation(bytes);
}

void clear_queued_allocations()
{
    active_registry().on_frame_submitted();
}

namespace
{
/// Test-only reserved memory: raw, non-evictable GPU textures used to inflate device occupancy. Held
/// outside the registry so the eviction system treats them as immovable "real" usage. API-thread only.
struct debug_reserve
{
    std::vector<bgfx::TextureHandle> chunks;
    std::uint64_t bytes = 0;
};

auto reserved() -> debug_reserve&
{
    static debug_reserve s_reserved;
    return s_reserved;
}

constexpr std::uint16_t k_reserve_dim = 4096;                                               // 4096x4096 RGBA8
constexpr std::uint64_t k_reserve_chunk = std::uint64_t(k_reserve_dim) * k_reserve_dim * 4; // == 64 MiB

/// Allocate one uninitialized square RGBA8 texture of @p dim and track it. Returns its nominal byte
/// size, or 0 if the allocation failed (e.g. genuinely out of memory).
auto alloc_reserve_chunk(std::uint16_t dim) -> std::uint64_t
{
    const bgfx::TextureHandle handle =
        bgfx::createTexture2D(dim, dim, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE, nullptr);
    if(!bgfx::isValid(handle))
    {
        return 0;
    }
    reserved().chunks.push_back(handle);
    const std::uint64_t sz = std::uint64_t(dim) * dim * 4;
    reserved().bytes += sz;
    return sz;
}
} // namespace

auto debug_consume_memory(std::uint64_t bytes) -> std::uint64_t
{
    std::uint64_t remaining = bytes;
    while(remaining >= k_reserve_chunk)
    {
        const std::uint64_t got = alloc_reserve_chunk(k_reserve_dim);
        if(got == 0)
        {
            return reserved().bytes; // allocation failed - stop reserving
        }
        remaining -= got;
    }
    if(remaining > 0)
    {
        // Smallest square RGBA8 texture that covers the remainder.
        const double texels = static_cast<double>(remaining) / 4.0;
        auto side = static_cast<std::uint32_t>(std::ceil(std::sqrt(texels)));
        side = std::clamp<std::uint32_t>(side, 1, k_reserve_dim);
        alloc_reserve_chunk(static_cast<std::uint16_t>(side));
    }
    return reserved().bytes;
}

auto debug_simulate_budget(std::uint64_t target_free_bytes) -> std::uint64_t
{
    const auto* gpu_stats = bgfx::getStats();
    if(gpu_stats == nullptr || gpu_stats->gpuMemoryMax <= 0)
    {
        return debug_consumed_bytes();
    }
    // Absolute target: start from a clean slate, then let the released VRAM settle so the usage we
    // read back reflects only the real (non-reserved) consumers.
    if(debug_consumed_bytes() > 0)
    {
        debug_release_memory();
    }
    gpu_stats = bgfx::getStats();
    const auto budget = static_cast<std::uint64_t>(gpu_stats->gpuMemoryMax);
    const auto used = static_cast<std::uint64_t>(std::max<std::int64_t>(0, gpu_stats->gpuMemoryUsed));
    if(target_free_bytes >= budget)
    {
        return debug_consumed_bytes();
    }
    const std::uint64_t desired_used = budget - target_free_bytes;
    if(desired_used <= used)
    {
        return debug_consumed_bytes(); // less is already free than the requested target
    }
    return debug_consume_memory(desired_used - used);
}

void debug_release_memory()
{
    for(const auto handle : reserved().chunks)
    {
        if(bgfx::isValid(handle))
        {
            bgfx::destroy(handle);
        }
    }
    reserved().chunks.clear();
    reserved().bytes = 0;
    // Pump the command buffer so the destroys are serviced and their VRAM is reclaimed before return.
    gfx::frames(k_frames_to_service_destroys, BGFX_FRAME_FLUSH);
}

auto debug_consumed_bytes() -> std::uint64_t
{
    return reserved().bytes;
}

auto restore_all() -> stats
{
    if(!is_supported())
    {
        return active_registry().get_stats();
    }
    // No sweep first: it would take idle resources down only for this call to bring them back.
    const auto pre = active_registry().get_stats();
    const std::uint64_t needed = pre.evicted_bytes + pre.saved_bytes;
    if(needed != 0 && !would_allocation_fit(needed))
    {
        gfx::log("warning",
                 "Restoring every resource needs " + std::to_string(needed / (1024 * 1024)) +
                     " MiB more than the GPU memory budget holds. Creating them may fail.",
                 __FILE__,
                 __LINE__);
    }
    return active_registry().restore_all();
}

void set_budget(const budget_state& budget, std::uint64_t used_bytes)
{
    active_registry().set_budget(budget, used_bytes);
}

auto current_budget() -> budget_state
{
    return active_registry().current_budget();
}

auto get_stats() -> stats
{
    return active_registry().get_stats();
}

void set_frame(std::uint64_t frame)
{
    detail::g_eviction_frame.store(frame, std::memory_order_relaxed);
    // New frame: reset the per-frame peak diagnostics so the profiler sees this frame's worst
    // case, not a value carried over from previous frames.
    active_registry().on_frame_advanced();
}

void advance_frame()
{
    detail::g_eviction_frame.fetch_add(1, std::memory_order_relaxed);
    active_registry().on_frame_advanced();
}

void register_resource(ievictable* resource, std::uint8_t level, std::uint64_t reserved_bytes)
{
    active_registry().register_resource(resource, level, reserved_bytes);
}

void register_evicted_resource(ievictable* resource, std::uint64_t reserved_bytes)
{
    active_registry().register_evicted_resource(resource, reserved_bytes);
}

void unregister_resource(ievictable* resource)
{
    active_registry().unregister_resource(resource);
}

void request_residency(ievictable* resource, texture_read read)
{
    if(!active_registry().is_api_thread())
    {
        return;
    }
    active_registry().request_residency(resource, read, get_room_under_hard_limit());
}

auto get_tail_size() -> std::uint32_t
{
    return active_registry().get_tail_size();
}

} // namespace eviction
} // namespace gfx
