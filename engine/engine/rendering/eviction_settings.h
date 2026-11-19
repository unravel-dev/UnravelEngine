#pragma once
#include <engine/engine_export.h>

#include <graphics/residency_planner.h>

namespace unravel
{

/// Engine-side policy for the GPU eviction/paging system. Owned by @ref renderer and driven once
/// per frame from @ref renderer::frame_begin, so paging works in standalone builds without any editor
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
    /// The order in which idle resources give up memory. Saved as its number, as the int it replaced was.
    gfx::residency::strategy strategy = gfx::residency::strategy::lru;
    /// Resources used within this many frames are protected from eviction.
    int min_age_frames = 60;
    /// Maximum resources evicted per frame (0 = unlimited).
    int max_evictions = 64;
    /// Evicted textures keep their smallest mips on the GPU, so materials draw a blurry copy until the
    /// full texture is back instead of waiting for it.
    bool keep_mip_tails = true;
    /// Longest side (pixels) of the mips an evicted texture keeps on the GPU.
    int mip_tail_size = 64;
    /// Most texture data (MiB) brought back to the GPU in one frame.
    int max_upload_mb = 32;
    /// When the textures on screen exceed the budget, materials draw them one mip lower instead.
    bool reduce_textures_in_use = true;

    friend auto operator==(const eviction_settings& lhs, const eviction_settings& rhs) -> bool = default;
};

/// Per-frame eviction driver. Must run on the graphics API (render) thread. Builds a
/// gfx::eviction::budget_state from the GPU memory stats (or the manual budget when no GPU budget
/// is reported), publishes it via gfx::eviction::set_budget so reclaim_for and tooling see the
/// same watermarks, and, when enabled, runs the frame's residency pass: over the budget resources
/// give up memory down to the target, under the target textures read lately sharpen again within
/// the upload limit. A no-op when the backend does not support eviction.
ENGINE_EXPORT void update_eviction(const eviction_settings& settings);

} // namespace unravel
