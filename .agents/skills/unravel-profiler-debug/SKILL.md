---
name: unravel-profiler-debug
description: >-
  Debugs UnravelEngine performance using the profiler panel, GPU timeline,
  viewport stats overlay, bgfx stats, and GPU eviction/paging metrics. Use for
  frame drops, GPU bottlenecks, memory pressure, or profiling UI work.
---

# Unravel Profiler Debug

## Start here

| Purpose | Path |
|---------|------|
| Profiler panel | `editor/editor/hub/panels/profiler_panel/` |
| GPU frame widgets | `profiler_panel/gpu_frame_stats_widgets.cpp` |
| Viewport stats overlay | `editor/editor/hub/panels/viewport_stats_overlay.cpp` |
| Pipeline stats | `engine/engine/rendering/pipeline/pipeline.h` |
| bgfx stats | `gfx::get_stats()` via `engine/core/graphics/` |
| GPU eviction | `engine/core/graphics/eviction.h` (`gfx::eviction::get_stats()`) |
| Renderer | `engine/engine/rendering/renderer.cpp` |

## Viewport stats overlay

Toggle via menu bar in scene/game panels (`draw_stats_toggle`).

Sections:

- **Performance** - FPS, CPU/GPU submit ms, GPU latency
- **Scene** - triangles, draw calls, render passes, compute/blit
- **Memory** - GPU memory, texture/RT memory, eviction/paging
- **Pipeline** - models, lights, shadows, particles, batching

Color coding: green/yellow/red thresholds for FPS and memory usage.

## GPU eviction / paging

`draw_eviction_overlay_stats()` shows:

- Budget used vs target
- Resident vs evicted resource counts
- Headroom bar (how much can still be paged out)
- Lifetime evictions/restores, thrash events

High thrash -> budget too tight or min-age too low.

## Profiler panel

GPU timeline with encoder/view stats. Open from stats overlay "Open Profiler" button or header menu.

## Interpreting stats

| Symptom | Likely cause |
|---------|--------------|
| High CPU submit | Too many draw calls, batching failure |
| High GPU submit | Heavy shaders, fill rate, post-process chain |
| High GPU latency | Pipeline depth; normal 1-3 frames |
| Rising eviction count | GPU memory pressure |
| Thrash events | Evict/restore cycling - raise budget |
| UI primitives in scene count | Stats separate UI prims - verify separation logic |

## Verification workflow

1. Reproduce with stats overlay visible
2. Note FPS, CPU/GPU ms, draw calls, memory
3. Open profiler for GPU timeline if GPU-bound
4. Check eviction stats if memory-related
5. Change one variable at a time
6. Compare pipeline_stats before/after

## Common mistakes

- Optimizing CPU when GPU-bound (or vice versa) without reading stats
- Ignoring editor UI draw cost in total draw call count
- Confusing submit time with total frame time
