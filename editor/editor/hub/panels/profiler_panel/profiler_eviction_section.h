#pragma once

#include <engine/rendering/eviction_settings.h>

namespace unravel
{

/// Full profiler UI for the GPU eviction/paging system: enable toggle, strategy/budget controls,
/// test buttons and detailed stats. Edits the engine-owned @ref eviction_settings in place; the
/// renderer drives the actual eviction each frame. Must be drawn on the render thread (the test
/// buttons run eviction immediately).
void profiler_draw_eviction_section(eviction_settings& state);

} // namespace unravel
