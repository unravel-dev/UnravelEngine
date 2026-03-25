#pragma once
#include "../panel_base.h"
#include <editor/imgui/integration/imgui.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>

// Forward declarations
namespace bgfx { struct Stats; }
struct ImGuiIO;

namespace unravel
{

//-----------------------------------------------------------------------------
/// <summary>
/// Panel that displays real-time performance statistics, profiler data,
/// memory usage, and GPU resource utilization for the engine.
/// </summary>
//-----------------------------------------------------------------------------
class statistics_panel : public panel_base
{
public:
    explicit statistics_panel(const char* name);

    //-----------------------------------------------------------------------------
    /// <summary>
    /// Initialize the statistics panel.
    /// </summary>
    /// <param name="ctx">The application context</param>
    //-----------------------------------------------------------------------------
    auto init(rtti::context& ctx) -> void;

    //-----------------------------------------------------------------------------
    /// <summary>
    /// Deinitialize the statistics panel and clean up resources.
    /// </summary>
    /// <param name="ctx">The application context</param>
    //-----------------------------------------------------------------------------
    auto deinit(rtti::context& ctx) -> void;

    //-----------------------------------------------------------------------------
    /// <summary>
    /// Update the statistics panel logic each frame.
    /// </summary>
    /// <param name="ctx">The application context</param>
    /// <param name="dt">Delta time since last frame</param>
    //-----------------------------------------------------------------------------
    auto on_frame_update(rtti::context& ctx, delta_t dt) -> void;

    //-----------------------------------------------------------------------------
    /// <summary>
    /// Render the statistics panel each frame.
    /// </summary>
    /// <param name="ctx">The application context</param>
    /// <param name="dt">Delta time since last frame</param>
    //-----------------------------------------------------------------------------
    auto on_frame_render(rtti::context& ctx, delta_t dt) -> void;

    //-----------------------------------------------------------------------------
    /// <summary>
    /// Render the statistics panel UI.
    /// </summary>
    /// <param name="ctx">The application context</param>
    /// <param name="name">The panel window name</param>
    //-----------------------------------------------------------------------------
    void draw_ui(rtti::context& ctx) override;

private:
    //-----------------------------------------------------------------------------
    /// <summary>
    /// Draw the menu bar for the statistics panel.
    /// </summary>
    /// <param name="ctx">The application context</param>
    //-----------------------------------------------------------------------------
    auto draw_menubar(rtti::context& ctx) -> void;

    //-----------------------------------------------------------------------------
    /// <summary>
    /// Draw the main statistics display.
    /// </summary>
    /// <param name="enable_profiler">Reference to profiler enable flag</param>
    //-----------------------------------------------------------------------------
    auto draw_statistics_content() -> void;

    //-----------------------------------------------------------------------------
    /// <summary>
    /// Draw frame time and rendering statistics.
    /// </summary>
    /// <param name="overlay_width">Width of the overlay area</param>
    //-----------------------------------------------------------------------------
    auto draw_frame_statistics(float overlay_width) -> void;

    //-----------------------------------------------------------------------------
    /// <summary>
    /// Draw profiler information section.
    /// </summary>
    /// <param name="enable_profiler">Reference to profiler enable flag</param>
    //-----------------------------------------------------------------------------
    auto draw_profiler_section() -> void;

    //-----------------------------------------------------------------------------
    /// <summary>
    /// Draw memory usage information section.
    /// </summary>
    /// <param name="overlay_width">Width of the overlay area</param>
    //-----------------------------------------------------------------------------
    auto draw_memory_info_section(float overlay_width) -> void;

    //-----------------------------------------------------------------------------
    /// <summary>
    /// Draw GPU resources utilization section.
    /// </summary>
    //-----------------------------------------------------------------------------
    auto draw_resources_section() -> void;

    //-----------------------------------------------------------------------------
    /// <summary>
    /// Draw pipeline statistics section.
    /// </summary>
    //-----------------------------------------------------------------------------
    auto draw_pipeline_stats() -> void;

    // Helper methods for updating and drawing specific components
    auto update_sample_data() -> void;
    auto get_draw_call_breakdown(const gfx::stats* stats, std::uint32_t& scene_calls, std::uint32_t& editor_calls, std::uint32_t& total_calls) -> void;
    auto draw_primitive_counts(const gfx::stats* stats, const ImGuiIO& io) -> void;
    auto draw_call_counts(const gfx::stats* stats, const ImGuiIO& io) -> void;
    auto draw_profiler_bars(const gfx::stats* stats) -> void;
    auto draw_encoder_stats(const gfx::stats* stats, float item_height, float item_height_with_spacing, double to_cpu_ms) -> void;
    auto draw_view_stats(const gfx::stats* stats, float item_height, float item_height_with_spacing, double to_cpu_ms, double to_gpu_ms) -> void;
    auto draw_gpu_memory_section(const gfx::stats* stats, int64_t& gpu_memory_max, float overlay_width) -> void;
    auto draw_render_target_memory_section(const gfx::stats* stats, int64_t& gpu_memory_max, float overlay_width) -> void;
    auto draw_texture_memory_section(const gfx::stats* stats, int64_t& gpu_memory_max, float overlay_width) -> void;

    bool enable_gpu_profiler_{false};
    bool show_editor_stats_{false}; // Disabled by default to focus on scene stats
};

} // namespace unravel
