#pragma once
#include <engine/engine_export.h>

namespace unravel
{

/// Engine-side policy for the GPU eviction/paging system. Owned by @ref renderer and driven once
/// per frame from @ref renderer::frame_end, so paging works in standalone builds without any editor
/// involvement. The editor surfaces the same struct for inspection/tuning.
struct eviction_settings
{
    /// Master toggle: when true the per-frame driver evicts to keep usage under the budget.
    bool enabled = true;
    /// Derive the budget from the bgfx GPU memory stats. Falls back to @ref manual_budget_mb when
    /// the backend does not report a GPU memory budget (gpuMemoryMax == 0).
    bool auto_budget = true;
    /// Start evicting once GPU usage exceeds this fraction of the reported maximum.
    float budget_fraction = 0.85f;
    /// Evict down to this fraction of the reported maximum (hysteresis; should be < budget).
    float target_fraction = 0.75f;
    /// Manual budget (MiB) compared against the evictable resident bytes when not using auto budget.
    int manual_budget_mb = 1024;
    /// Selected gfx::eviction::strategy (index).
    int strategy = 0;
    /// Resources used within this many frames are protected from eviction.
    int min_age_frames = 60;
    /// Maximum resources evicted per frame (0 = unlimited).
    int max_evictions = 64;

    friend auto operator==(const eviction_settings& lhs, const eviction_settings& rhs) -> bool = default;
};

/// Per-frame eviction driver. Must run on the graphics API (render) thread. Builds a
/// gfx::eviction::budget_state from the GPU memory stats (or the manual budget when no GPU budget
/// is reported), publishes it via gfx::eviction::set_budget so reclaim_for and tooling see the
/// same watermarks, and—when enabled and over budget—evicts down to the target. A no-op when the
/// backend does not support eviction.
ENGINE_EXPORT void update_eviction(const eviction_settings& settings);

} // namespace unravel
