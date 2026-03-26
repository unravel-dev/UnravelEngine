#pragma once
#include <graphics/graphics.h>

namespace unravel
{

/// Checkbox to toggle @c BGFX_DEBUG_PROFILER, then encoder (CPU submit) and view (CPU submit / GPU execute) bars.
void draw_gpu_bgfx_submit_profiler_ui(const gfx::stats* stats, bool* enable_profiler);

} // namespace unravel
