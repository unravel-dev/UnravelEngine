#pragma once
#include <editor/imgui/integration/imgui.h>
#include <base/basetypes.hpp>
#include <engine/profiler/profiler.h>

namespace unravel::statistics_utils
{

// Use sample_data from profiler.h
using sample_data = unravel::sample_data;

//-----------------------------------------------------------------------------
/// <summary>
/// Draw a colored progress bar with hover effects.
/// </summary>
/// <param name="width">Width of the filled portion</param>
/// <param name="max_width">Maximum width of the bar</param>
/// <param name="height">Height of the bar</param>
/// <param name="color">Color of the bar</param>
/// <returns>True if the bar is being hovered</returns>
//-----------------------------------------------------------------------------
auto draw_progress_bar(float width, float max_width, float height, const ImVec4& color) -> bool;

//-----------------------------------------------------------------------------
/// <summary>
/// Draw a resource usage bar with label and percentage.
/// </summary>
/// <param name="name">Name of the resource</param>
/// <param name="tooltip">Tooltip text to show on hover</param>
/// <param name="current_value">Current usage value</param>
/// <param name="max_value">Maximum possible value</param>
/// <param name="max_width">Maximum width of the progress bar</param>
/// <param name="height">Height of the progress bar</param>
//-----------------------------------------------------------------------------
auto draw_resource_bar(const char* name,
                      const char* tooltip,
                      uint32_t current_value,
                      uint32_t max_value,
                      float max_width,
                      float height) -> void;

} // namespace unravel::statistics_utils 