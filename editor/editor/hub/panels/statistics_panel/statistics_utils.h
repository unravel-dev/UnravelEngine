#pragma once
#include <editor/imgui/integration/imgui.h>
#include <base/basetypes.hpp>
#include <array>

namespace unravel::statistics_utils
{

//-----------------------------------------------------------------------------
/// <summary>
/// Class for collecting and managing time-series sample data.
/// Maintains a rolling buffer of samples with automatic statistics calculation.
/// </summary>
//-----------------------------------------------------------------------------
class sample_data
{
public:
    static constexpr uint32_t NUM_SAMPLES = 500;

    //-----------------------------------------------------------------------------
    /// <summary>
    /// Constructor that initializes all samples to zero.
    /// </summary>
    //-----------------------------------------------------------------------------
    sample_data();

    //-----------------------------------------------------------------------------
    /// <summary>
    /// Reset all samples to a specific value.
    /// </summary>
    /// <param name="value">The value to reset all samples to</param>
    //-----------------------------------------------------------------------------
    auto reset(float value) -> void;

    //-----------------------------------------------------------------------------
    /// <summary>
    /// Add a new sample to the rolling buffer.
    /// Automatically updates min, max, and average statistics.
    /// </summary>
    /// <param name="value">The new sample value</param>
    //-----------------------------------------------------------------------------
    auto push_sample(float value) -> void;

    //-----------------------------------------------------------------------------
    /// <summary>
    /// Get the raw sample values array.
    /// </summary>
    /// <returns>Pointer to the internal sample array</returns>
    //-----------------------------------------------------------------------------
    auto get_values() const -> const float*
    {
        return values_.data();
    }

    //-----------------------------------------------------------------------------
    /// <summary>
    /// Get the current offset in the rolling buffer.
    /// </summary>
    /// <returns>The current offset position</returns>
    //-----------------------------------------------------------------------------
    auto get_offset() const -> int32_t
    {
        return offset_;
    }

    //-----------------------------------------------------------------------------
    /// <summary>
    /// Get the minimum value in the current sample set.
    /// </summary>
    /// <returns>The minimum sample value</returns>
    //-----------------------------------------------------------------------------
    auto get_min() const -> float
    {
        return min_;
    }

    //-----------------------------------------------------------------------------
    /// <summary>
    /// Get the maximum value in the current sample set.
    /// </summary>
    /// <returns>The maximum sample value</returns>
    //-----------------------------------------------------------------------------
    auto get_max() const -> float
    {
        return max_;
    }

    //-----------------------------------------------------------------------------
    /// <summary>
    /// Get the average value of the current sample set.
    /// </summary>
    /// <returns>The average sample value</returns>
    //-----------------------------------------------------------------------------
    auto get_average() const -> float
    {
        return average_;
    }

private:
    int32_t offset_{0};
    std::array<float, NUM_SAMPLES> values_{};
    
    float min_{0.0f};
    float max_{0.0f};
    float average_{0.0f};
    
    int32_t smart_init_samples_{-1};
};

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