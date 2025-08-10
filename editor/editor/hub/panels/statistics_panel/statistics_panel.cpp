#include "statistics_panel.h"
#include "statistics_utils.h"
#include "../panels_defs.h"

#include <engine/profiler/profiler.h>
#include <graphics/graphics.h>
#include <math/math.h>

#include <algorithm>
#include <numeric>

namespace unravel
{

// Constants
namespace
{
    constexpr float PLOT_HEIGHT = 50.0f;
    constexpr float MAX_FRAME_TIME_MS = 200.0f;
    constexpr float MAX_PASSES = 200.0f;
    constexpr float PROFILER_SCALE = 3.0f;
    constexpr float PROFILER_MAX_WIDTH = 30.0f;
    constexpr float RESOURCE_BAR_WIDTH = 90.0f;
    constexpr float MEGABYTE_DIVISOR = 1024.0f * 1024.0f;
    
    // Colors for profiler bars
    constexpr ImVec4 CPU_COLOR{0.5f, 1.0f, 0.5f, 1.0f};
    constexpr ImVec4 GPU_COLOR{0.5f, 0.5f, 1.0f, 1.0f};
    
    // Static sample data instances
    statistics_utils::sample_data frame_time_samples;
    statistics_utils::sample_data graphics_passes_samples;
    statistics_utils::sample_data gpu_memory_samples;
    statistics_utils::sample_data render_target_memory_samples;
    statistics_utils::sample_data texture_memory_samples;
}

auto statistics_panel::init(rtti::context& ctx) -> void
{
    // No specific initialization needed currently
}

auto statistics_panel::deinit(rtti::context& ctx) -> void
{
    // No specific cleanup needed currently
}

auto statistics_panel::on_frame_update(rtti::context& ctx, delta_t dt) -> void
{
    // No per-frame update logic needed currently
}

auto statistics_panel::on_frame_render(rtti::context& ctx, delta_t dt) -> void
{
    // No per-frame render logic needed currently
}

auto statistics_panel::on_frame_ui_render(rtti::context& ctx, const char* name) -> void
{
    if(ImGui::Begin(name, nullptr, ImGuiWindowFlags_MenuBar))
    {
        draw_menubar(ctx);
        draw_statistics_content(enable_profiler_);
    }
    ImGui::End();
}

auto statistics_panel::draw_menubar(rtti::context& ctx) -> void
{
    if(ImGui::BeginMenuBar())
    {
        // Currently no menu items, but structure is ready for future additions
        ImGui::EndMenuBar();
    }
}

auto statistics_panel::draw_statistics_content(bool& enable_profiler) -> void
{
    const auto& io = ImGui::GetIO();
    const auto area = ImGui::GetContentRegionAvail();
    const float overlay_width = area.x;
    
    // Update sample data with current frame statistics
    update_sample_data();
    
    // Draw main statistics sections
    draw_frame_statistics(overlay_width);
    draw_profiler_section(enable_profiler);
    draw_memory_info_section(overlay_width);
    draw_resources_section();
}

auto statistics_panel::draw_frame_statistics(float overlay_width) -> void
{
    auto stats = gfx::get_stats();
    const auto& io = ImGui::GetIO();
    
    const double to_cpu_ms = 1000.0 / static_cast<double>(stats->cpuTimerFreq);
    const double to_gpu_ms = 1000.0 / static_cast<double>(stats->gpuTimerFreq);
    
    // Create overlay text for frame time plot
    char frame_text_overlay[256];
    bx::snprintf(frame_text_overlay,
                 BX_COUNTOF(frame_text_overlay),
                 "Min: %.3fms, Max: %.3fms\nAvg: %.3fms, %.1f FPS",
                 frame_time_samples.get_min(),
                 frame_time_samples.get_max(),
                 frame_time_samples.get_average(),
                 1000.0f / frame_time_samples.get_average());
    
    ImGui::PushFont(ImGui::Font::Mono);
    
    // Frame time plot
    ImGui::PlotLines("##Frame",
                     frame_time_samples.get_values(),
                     statistics_utils::sample_data::NUM_SAMPLES,
                     frame_time_samples.get_offset(),
                     frame_text_overlay,
                     0.0f,
                     MAX_FRAME_TIME_MS,
                     ImVec2(overlay_width, PLOT_HEIGHT));
    
    // CPU/GPU timing information
    auto submit_cpu_ms = static_cast<double>(stats->cpuTimeEnd - stats->cpuTimeBegin) * to_cpu_ms;
    auto submit_gpu_ms = static_cast<double>(stats->gpuTimeEnd - stats->gpuTimeBegin) * to_gpu_ms;
    ImGui::Text("Submit CPU %0.3f, GPU %0.3f (L: %d)",
                submit_cpu_ms,
                submit_gpu_ms,
                stats->maxGpuLatency);
    ImGui::Text("Render Passes: %u", gfx::render_pass::get_last_frame_max_pass_id());
    
    // Primitive counts
    draw_primitive_counts(stats, io);
    
    // Draw call counts
    draw_call_counts(stats, io);
    
    ImGui::PopFont();
}

auto statistics_panel::draw_profiler_section(bool& enable_profiler) -> void
{
    if(!ImGui::CollapsingHeader(ICON_MDI_CLOCK_OUTLINE "\tProfiler"))
    {
        return;
    }
    
    if(ImGui::Checkbox("Enable GPU profiler", &enable_profiler))
    {
        if(enable_profiler)
        {
            gfx::set_debug(BGFX_DEBUG_PROFILER);
        }
        else
        {
            gfx::set_debug(BGFX_DEBUG_NONE);
        }
    }
    
    ImGui::PushFont(ImGui::Font::Mono);
    
    auto stats = gfx::get_stats();
    
    if(stats->numViews == 0)
    {
        ImGui::Text("Profiler is not enabled.");
    }
    else
    {
        draw_profiler_bars(stats);
    }
    draw_app_profiler_data();
    
    ImGui::PopFont();
}

auto statistics_panel::draw_memory_info_section(float overlay_width) -> void
{
    if(!ImGui::CollapsingHeader(ICON_MDI_INFORMATION "\tMemory Info"))
    {
        return;
    }
    
    ImGui::PushFont(ImGui::Font::Mono);
    
    auto stats = gfx::get_stats();
    auto gpu_memory_max = stats->gpuMemoryMax;
    
    // GPU memory section
    if(stats->gpuMemoryUsed > 0)
    {
        draw_gpu_memory_section(stats, gpu_memory_max, overlay_width);
    }
    
    // Render target memory section
    draw_render_target_memory_section(stats, gpu_memory_max, overlay_width);
    
    // Texture memory section  
    draw_texture_memory_section(stats, gpu_memory_max, overlay_width);
    
    ImGui::PopFont();
}

auto statistics_panel::draw_resources_section() -> void
{
    if(!ImGui::CollapsingHeader(ICON_MDI_PUZZLE "\tResources"))
    {
        return;
    }
    
    const auto caps = gfx::get_caps();
    const auto stats = gfx::get_stats();
    const float item_height = ImGui::GetTextLineHeightWithSpacing();
    
    ImGui::PushFont(ImGui::Font::Mono);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Res: Num  / Max");
    
    // Draw resource usage bars
    using namespace statistics_utils;
    
    draw_resource_bar("DIB", "Dynamic index buffers",
                     stats->numDynamicIndexBuffers, caps->limits.maxDynamicIndexBuffers,
                     RESOURCE_BAR_WIDTH, item_height);
    
    draw_resource_bar("DVB", "Dynamic vertex buffers",
                     stats->numDynamicVertexBuffers, caps->limits.maxDynamicVertexBuffers,
                     RESOURCE_BAR_WIDTH, item_height);
    
    draw_resource_bar(" FB", "Frame buffers",
                     stats->numFrameBuffers, caps->limits.maxFrameBuffers,
                     RESOURCE_BAR_WIDTH, item_height);
    
    draw_resource_bar(" IB", "Index buffers",
                     stats->numIndexBuffers, caps->limits.maxIndexBuffers,
                     RESOURCE_BAR_WIDTH, item_height);
    
    draw_resource_bar(" OQ", "Occlusion queries",
                     stats->numOcclusionQueries, caps->limits.maxOcclusionQueries,
                     RESOURCE_BAR_WIDTH, item_height);
    
    draw_resource_bar("  P", "Programs",
                     stats->numPrograms, caps->limits.maxPrograms,
                     RESOURCE_BAR_WIDTH, item_height);
    
    draw_resource_bar("  S", "Shaders",
                     stats->numShaders, caps->limits.maxShaders,
                     RESOURCE_BAR_WIDTH, item_height);
    
    draw_resource_bar("  T", "Textures",
                     stats->numTextures, caps->limits.maxTextures,
                     RESOURCE_BAR_WIDTH, item_height);
    
    draw_resource_bar("  U", "Uniforms",
                     stats->numUniforms, caps->limits.maxUniforms,
                     RESOURCE_BAR_WIDTH, item_height);
    
    draw_resource_bar(" VB", "Vertex buffers",
                     stats->numVertexBuffers, caps->limits.maxVertexBuffers,
                     RESOURCE_BAR_WIDTH, item_height);
    
    draw_resource_bar(" VD", "Vertex layouts",
                     stats->numVertexLayouts, caps->limits.maxVertexLayouts,
                     RESOURCE_BAR_WIDTH, item_height);
    
    ImGui::PopFont();
}

// Private helper methods

auto statistics_panel::update_sample_data() -> void
{
    auto stats = gfx::get_stats();
    const double to_cpu_ms = 1000.0 / static_cast<double>(stats->cpuTimerFreq);
    const double frame_ms = static_cast<double>(stats->cpuTimeFrame) * to_cpu_ms;
    
    frame_time_samples.push_sample(static_cast<float>(frame_ms));
    graphics_passes_samples.push_sample(static_cast<float>(gfx::render_pass::get_last_frame_max_pass_id()));
    gpu_memory_samples.push_sample(static_cast<float>(stats->gpuMemoryUsed) / MEGABYTE_DIVISOR);
    render_target_memory_samples.push_sample(static_cast<float>(stats->rtMemoryUsed) / MEGABYTE_DIVISOR);
    texture_memory_samples.push_sample(static_cast<float>(stats->textureMemoryUsed) / MEGABYTE_DIVISOR);
}

auto statistics_panel::draw_primitive_counts(const bgfx::Stats* stats, const ImGuiIO& io) -> void
{
    const std::uint32_t total_primitives = std::accumulate(std::begin(stats->numPrims), std::end(stats->numPrims), 0u);
    std::uint32_t ui_primitives = io.MetricsRenderIndices / 3;
    ui_primitives = std::min(ui_primitives, total_primitives);
    const auto scene_primitives = total_primitives - ui_primitives;
    
    ImGui::Text("Scene Primitives: %u", scene_primitives);
    ImGui::Text("UI    Primitives: %u", ui_primitives);
    ImGui::Text("Total Primitives: %u", total_primitives);
}

auto statistics_panel::draw_call_counts(const bgfx::Stats* stats, const ImGuiIO& io) -> void
{
    std::uint32_t ui_draw_calls = ImGui::GetDrawCalls();
    ui_draw_calls = std::min(ui_draw_calls, stats->numDraw);
    const auto scene_draw_calls = stats->numDraw - ui_draw_calls;
    
    ImGui::Text("Scene Draw Calls: %u", scene_draw_calls);
    ImGui::Text("UI    Draw Calls: %u", ui_draw_calls);
    ImGui::Text("Total Draw Calls: %u", stats->numDraw);
    ImGui::Text("Total Comp Calls: %u", stats->numCompute);
    ImGui::Text("Total Blit Calls: %u", stats->numBlit);
}

auto statistics_panel::draw_profiler_bars(const bgfx::Stats* stats) -> void
{
    const float item_height = ImGui::GetTextLineHeightWithSpacing();
    const float item_height_with_spacing = ImGui::GetFrameHeightWithSpacing();
    const double to_cpu_ms = 1000.0 / static_cast<double>(stats->cpuTimerFreq);
    const double to_gpu_ms = 1000.0 / static_cast<double>(stats->gpuTimerFreq);
    
    // Draw encoder stats
    draw_encoder_stats(stats, item_height, item_height_with_spacing, to_cpu_ms);
    
    ImGui::Separator();
    
    // Draw view stats
    draw_view_stats(stats, item_height, item_height_with_spacing, to_cpu_ms, to_gpu_ms);
}

auto statistics_panel::draw_encoder_stats(const bgfx::Stats* stats, float item_height, float item_height_with_spacing, double to_cpu_ms) -> void
{
    if(ImGui::BeginListBox("Encoders", ImVec2(ImGui::GetWindowWidth(), stats->numEncoders * item_height_with_spacing)))
    {
        ImGuiListClipper clipper;
        clipper.Begin(stats->numEncoders, item_height);
        
        while(clipper.Step())
        {
            for(int32_t pos = clipper.DisplayStart; pos < clipper.DisplayEnd; ++pos)
            {
                const bgfx::EncoderStats& encoder_stats = stats->encoderStats[pos];
                ImGui::PushID(pos);
                ImGui::Text("%3d", pos);
                ImGui::SameLine(64.0f);
                
                const float max_width = PROFILER_MAX_WIDTH * PROFILER_SCALE;
                const float cpu_ms = static_cast<float>((encoder_stats.cpuTimeEnd - encoder_stats.cpuTimeBegin) * to_cpu_ms);
                const float cpu_width = bx::clamp(cpu_ms * PROFILER_SCALE, 1.0f, max_width);
                
                if(statistics_utils::draw_progress_bar(cpu_width, max_width, item_height, CPU_COLOR))
                {
                    ImGui::SetItemTooltipEx("Encoder %d, CPU: %f [ms]", pos, cpu_ms);
                }
                
                ImGui::PopID();
            }
        }
        ImGui::EndListBox();
    }
}

auto statistics_panel::draw_view_stats(const bgfx::Stats* stats, float item_height, float item_height_with_spacing, double to_cpu_ms, double to_gpu_ms) -> void
{
    if(ImGui::BeginListBox("Views", ImVec2(ImGui::GetWindowWidth(), stats->numViews * item_height_with_spacing)))
    {
        ImGuiListClipper clipper;
        clipper.Begin(stats->numViews, item_height);
        
        while(clipper.Step())
        {
            for(int32_t pos = clipper.DisplayStart; pos < clipper.DisplayEnd; ++pos)
            {
                const bgfx::ViewStats& view_stats = stats->viewStats[pos];
                ImGui::PushID(view_stats.view);
                ImGui::Text("%3d %3d %s", pos, view_stats.view, view_stats.name);
                
                const float max_width = PROFILER_MAX_WIDTH * PROFILER_SCALE;
                const float cpu_time_elapsed = static_cast<float>((view_stats.cpuTimeEnd - view_stats.cpuTimeBegin) * to_cpu_ms);
                const float gpu_time_elapsed = static_cast<float>((view_stats.gpuTimeEnd - view_stats.gpuTimeBegin) * to_gpu_ms);
                const float cpu_width = bx::clamp(cpu_time_elapsed * PROFILER_SCALE, 1.0f, max_width);
                const float gpu_width = bx::clamp(gpu_time_elapsed * PROFILER_SCALE, 1.0f, max_width);
                
                ImGui::SameLine(64.0f);
                
                ImGui::PushID("cpu");
                if(statistics_utils::draw_progress_bar(cpu_width, max_width, item_height, CPU_COLOR))
                {
                    ImGui::SetItemTooltipEx("View %d \"%s\", CPU: %f [ms]", pos, view_stats.name, cpu_time_elapsed);
                }
                ImGui::PopID();
                
                ImGui::SameLine();
                
                ImGui::PushID("gpu");
                if(statistics_utils::draw_progress_bar(gpu_width, max_width, item_height, GPU_COLOR))
                {
                    ImGui::SetItemTooltipEx("View: %d \"%s\", GPU: %f [ms]", pos, view_stats.name, gpu_time_elapsed);
                }
                ImGui::PopID();
                
                ImGui::PopID();
            }
        }
        ImGui::EndListBox();
    }
}

auto statistics_panel::draw_app_profiler_data() -> void
{
    auto profiler = get_app_profiler();
    const auto& data = profiler->get_per_frame_data_read();
    
    for(const auto& [name, per_frame_data] : data)
    {
        ImGui::TextUnformatted(
            fmt::format("{:>7.3f}ms [{:^5}] - {}", 
                       per_frame_data.time, 
                       per_frame_data.samples, 
                       fmt::string_view(name.data(), name.size())).c_str());
    }
}

auto statistics_panel::draw_gpu_memory_section(const bgfx::Stats* stats, int64_t& gpu_memory_max, float overlay_width) -> void
{
    gpu_memory_max = std::max(stats->gpuMemoryUsed, stats->gpuMemoryMax);
    
    char str_max[64];
    bx::prettify(str_max, 64, static_cast<uint64_t>(gpu_memory_max));
    
    char str_used[64];
    bx::prettify(str_used, BX_COUNTOF(str_used), stats->gpuMemoryUsed);
    
    ImGui::Separator();
    ImGui::Text("GPU mem: %s / %s", str_used, str_max);
    ImGui::PlotLines("##GPU mem",
                     gpu_memory_samples.get_values(),
                     statistics_utils::sample_data::NUM_SAMPLES,
                     gpu_memory_samples.get_offset(),
                     nullptr,
                     0.0f,
                     static_cast<float>(gpu_memory_max),
                     ImVec2(overlay_width, PLOT_HEIGHT));
}

auto statistics_panel::draw_render_target_memory_section(const bgfx::Stats* stats, int64_t& gpu_memory_max, float overlay_width) -> void
{
    gpu_memory_max = std::max(stats->rtMemoryUsed, gpu_memory_max);
    
    char str_max[64];
    bx::prettify(str_max, 64, static_cast<uint64_t>(gpu_memory_max));
    
    char str_used[64];
    bx::prettify(str_used, BX_COUNTOF(str_used), stats->rtMemoryUsed);
    
    ImGui::Separator();
    ImGui::Text("Render Target mem: %s / %s", str_used, str_max);
    ImGui::PlotLines("##Render Target mem",
                     render_target_memory_samples.get_values(),
                     statistics_utils::sample_data::NUM_SAMPLES,
                     render_target_memory_samples.get_offset(),
                     nullptr,
                     0.0f,
                     static_cast<float>(gpu_memory_max),
                     ImVec2(overlay_width, PLOT_HEIGHT));
}

auto statistics_panel::draw_texture_memory_section(const bgfx::Stats* stats, int64_t& gpu_memory_max, float overlay_width) -> void
{
    gpu_memory_max = std::max(stats->textureMemoryUsed, gpu_memory_max);
    
    char str_max[64];
    bx::prettify(str_max, 64, static_cast<uint64_t>(gpu_memory_max));
    
    char str_used[64];
    bx::prettify(str_used, BX_COUNTOF(str_used), stats->textureMemoryUsed);
    
    ImGui::Separator();
    ImGui::Text("Texture mem: %s / %s", str_used, str_max);
    ImGui::PlotLines("##Texture Mem",
                     texture_memory_samples.get_values(),
                     statistics_utils::sample_data::NUM_SAMPLES,
                     texture_memory_samples.get_offset(),
                     nullptr,
                     0.0f,
                     static_cast<float>(gpu_memory_max),
                     ImVec2(overlay_width, PLOT_HEIGHT));
}

} // namespace unravel
