#pragma once
#include <editor/imgui/integration/imgui.h>
#include <base/basetypes.hpp>
#include <engine/profiler/profiler.h>

namespace unravel::profiler_statistics_utils
{

using sample_data = unravel::sample_data;

auto draw_progress_bar(float width, float max_width, float height, const ImVec4& color) -> bool;

auto draw_resource_bar(const char* name,
                       const char* tooltip,
                       uint32_t current_value,
                       uint32_t max_value,
                       float max_width,
                       float height) -> void;

} // namespace unravel::profiler_statistics_utils
