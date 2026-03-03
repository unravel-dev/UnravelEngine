#include "statistics_panel.h"
#include "imgui/imgui.h"
#include "imgui_widgets/utils.h"
#include "monopp/mono_gc_handle.h"
#include "statistics_utils.h"


#include <engine/engine.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/batch_collector.h>
#include <engine/ecs/scene.h>
#include <engine/profiler/profiler.h>
#include <graphics/graphics.h>
#include <math/math.h>

#include <algorithm>
#include <array>
#include <numeric>

namespace unravel
{

// Constants
namespace
{
    constexpr float plot_height = 50.0f;
    constexpr float max_frame_time_ms = 200.0f;
    constexpr float max_passes = 200.0f;
    constexpr float profiler_scale = 3.0f;
    constexpr float profiler_max_width = 30.0f;
    constexpr float resource_bar_width = 90.0f;
    constexpr float megabyte_divisor = 1024.0f * 1024.0f;
    
    // Colors for profiler bars
    constexpr ImVec4 cpu_color{0.2f, 0.8f, 0.2f, 1.0f};  // More professional green
    constexpr ImVec4 gpu_color{0.2f, 0.6f, 1.0f, 1.0f};  // More professional blue
    constexpr ImVec4 warning_color{1.0f, 0.7f, 0.0f, 1.0f};  // Warning orange
    constexpr ImVec4 error_color{1.0f, 0.3f, 0.3f, 1.0f};    // Error red
    
    // Static sample data instances
    statistics_utils::sample_data frame_time_samples;
    statistics_utils::sample_data graphics_passes_samples;
    statistics_utils::sample_data gpu_memory_samples;
    statistics_utils::sample_data render_target_memory_samples;
    statistics_utils::sample_data texture_memory_samples;
}

statistics_panel::statistics_panel(const char* name) : panel_base(name)
{
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

void statistics_panel::draw_ui(rtti::context& ctx)
{
    draw_menubar(ctx);
    draw_statistics_content();
}

auto statistics_panel::draw_menubar(rtti::context& ctx) -> void
{
    if(ImGui::BeginMenuBar())
    {
        ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_TabSelected));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_TabSelected));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyleColorVec4(ImGuiCol_TabSelectedOverline));
        
        if(ImGui::BeginMenu("View " ICON_MDI_ARROW_DOWN_BOLD))
        {
            ImGui::Checkbox("Show Editor Stats", &show_editor_stats_);
            ImGui::SetItemTooltip("Show editor/UI related draw calls and triangles\n(Focus on scene stats when disabled)");
            ImGui::EndMenu();
        }
        
        if(ImGui::BeginMenu("Rendering " ICON_MDI_ARROW_DOWN_BOLD))
        {
            bool batching_enabled = batch_collector::is_static_mesh_batching_enabled();
            if(ImGui::Checkbox("Static Mesh Batching", &batching_enabled))
            {
                batch_collector::set_static_mesh_batching_enabled(batching_enabled);
            }
            ImGui::SetItemTooltip("Enable/disable static mesh batching for performance comparison");
            ImGui::EndMenu();
        }
        ImGui::PopStyleColor(3);
        ImGui::EndMenuBar();
    }
}

auto statistics_panel::draw_statistics_content() -> void
{
    const auto& io = ImGui::GetIO();
    const auto area = ImGui::GetContentRegionAvail();
    const float overlay_width = area.x;
    
    // Update sample data with current frame statistics
    update_sample_data();
    
    // Draw main statistics sections
    draw_frame_statistics(overlay_width);
    draw_profiler_section();
    draw_memory_info_section(overlay_width);
    draw_resources_section();
}

auto statistics_panel::draw_frame_statistics(float overlay_width) -> void
{
    auto stats = gfx::get_stats();
    const auto& io = ImGui::GetIO();
    
    const double to_cpu_ms = 1000.0 / static_cast<double>(stats->cpuTimerFreq);
    const double to_gpu_ms = 1000.0 / static_cast<double>(stats->gpuTimerFreq);
    
    // Performance Overview Section
    if(ImGui::CollapsingHeader(ICON_MDI_CHART_LINE "\tPerformance Overview", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PushFont(ImGui::Font::Mono);
        
        // Frame time statistics with color coding
        const float avg_frame_time = frame_time_samples.get_average();
        const float fps = 1000.0f / avg_frame_time;
        
        // Color code based on performance
        ImVec4 fps_color = cpu_color; // Green for good performance
        if(fps < 30.0f) 
        {
            fps_color = error_color;     // Red for poor
        }
        else if(fps < 55.0f) 
        {
            fps_color = warning_color; // Orange for moderate
        }
        
        // Performance summary with better layout
        ImGui::BeginColumns("PerformanceColumns", 2, ImGuiOldColumnFlags_NoResize);
        ImGui::SetColumnWidth(0, overlay_width * 0.5f);
        
        ImGui::Text("Frame Time:");
        ImGui::Text("  Average: %.3f ms", avg_frame_time);
        ImGui::Text("  Min/Max: %.3f / %.3f ms", frame_time_samples.get_min(), frame_time_samples.get_max());
        
        ImGui::NextColumn();
        
        ImGui::TextColored(fps_color, "FPS: %.1f", fps);
        if(fps < 55.0f)
        {
            ImGui::SameLine();
            ImGui::TextColored(warning_color, fps < 30.0f ? " (Poor)" : " (Low)");
        }
        
        // GPU Memory usage with color coding
        if(stats->gpuMemoryUsed > 0)
        {
            std::array<char, 64> gpu_used_str;
            bx::prettify(gpu_used_str.data(), gpu_used_str.size(), stats->gpuMemoryUsed);
            
            ImGui::Text("GPU Memory:");
            
            if(stats->gpuMemoryMax > 0)
            {
                // Full memory info with percentage when max is available
                const float gpu_usage_percentage = (static_cast<float>(stats->gpuMemoryUsed) / static_cast<float>(stats->gpuMemoryMax)) * 100.0f;
                
                // Color code based on GPU memory usage (only for percentage)
                ImVec4 gpu_memory_color = cpu_color; // Green for low usage
                if(gpu_usage_percentage > 80.0f) 
                {
                    gpu_memory_color = error_color;      // Red for high
                }
                else if(gpu_usage_percentage > 60.0f) 
                {
                    gpu_memory_color = warning_color;    // Orange for medium
                }
                
                std::array<char, 64> gpu_max_str;
                bx::prettify(gpu_max_str.data(), gpu_max_str.size(), stats->gpuMemoryMax);
                
                ImGui::Text("%s / %s", gpu_used_str.data(), gpu_max_str.data());
                ImGui::SameLine();
                ImGui::TextColored(gpu_memory_color, "(%.1f%%)", gpu_usage_percentage);
            }
            else
            {
                // Only current usage when max is not available
                ImGui::Text("%s used", gpu_used_str.data());
                ImGui::SameLine();
                ImGui::TextColored(warning_color, "(max unknown)");
            }
        }
        else
        {
            ImGui::Text("GPU Memory:");
            ImGui::TextColored(warning_color, "No data available");
        }
        
        ImGui::EndColumns();
        
        // Frame time plot with improved overlay
        std::array<char, 256> frame_text_overlay;
        bx::snprintf(frame_text_overlay.data(),
                     frame_text_overlay.size(),
                     "Performance: %.1f FPS (%.3f ms avg)\nRange: %.3f - %.3f ms",
                     fps, avg_frame_time,
                     frame_time_samples.get_min(),
                     frame_time_samples.get_max());
                     
        ImGui::SetNextWindowViewportToCurrent();
        ImGui::PlotLines("##FrameTime",
                         frame_time_samples.get_values(),
                         statistics_utils::sample_data::num_samples,
                         frame_time_samples.get_offset(),
                         frame_text_overlay.data(),
                         0.0f,
                         max_frame_time_ms,
                         ImVec2(overlay_width, plot_height));
        
        ImGui::Separator();
        
        // CPU/GPU timing with better formatting
        const auto submit_cpu_ms = static_cast<double>(stats->cpuTimeEnd - stats->cpuTimeBegin) * to_cpu_ms;
        const auto submit_gpu_ms = static_cast<double>(stats->gpuTimeEnd - stats->gpuTimeBegin) * to_gpu_ms;
        
        ImGui::BeginColumns("TimingColumns", 4, ImGuiOldColumnFlags_NoResize);
        ImGui::SetColumnWidth(0, overlay_width * 0.25f);
        ImGui::SetColumnWidth(1, overlay_width * 0.25f);
        ImGui::SetColumnWidth(2, overlay_width * 0.25f);
        
        ImGui::TextColored(cpu_color, "CPU Submit");
        ImGui::Text("%.3f ms", submit_cpu_ms);
        
        ImGui::NextColumn();
        ImGui::TextColored(gpu_color, "GPU Submit");
        ImGui::Text("%.3f ms", submit_gpu_ms);
        
        ImGui::NextColumn();
        ImGui::Text("GPU Latency");
        ImGui::Text("%d frames", stats->maxGpuLatency);
        
        ImGui::NextColumn();
        ImGui::Text("Draw Calls");
        
        std::uint32_t scene_calls = 0, editor_calls = 0, total_calls = 0;
        get_draw_call_breakdown(stats, scene_calls, editor_calls, total_calls);
        
        if(show_editor_stats_)
        {
            ImGui::Text("%u total", total_calls);
        }
        else
        {
            ImGui::Text("%u scene", scene_calls);
        }

        ImGui::EndColumns();
        
        ImGui::PopFont();
    }
    
    // Rendering Statistics Section
    if(ImGui::CollapsingHeader(ICON_MDI_CUBE_OUTLINE "\tRendering Statistics", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PushFont(ImGui::Font::Mono);
        
        // Render passes at the top
        ImGui::Text("Render Passes: %u", gfx::render_pass::get_last_frame_max_pass_id());
        ImGui::Separator();
        
        // Draw call counts
        draw_call_counts(stats, io);
     
        ImGui::Separator();
           
        // Primitive counts
        draw_primitive_counts(stats, io);

        // Pipeline stats
        draw_pipeline_stats();
        
      
        ImGui::PopFont();
    }
}

auto statistics_panel::draw_pipeline_stats() -> void
{
    if(ImGui::CollapsingHeader(ICON_MDI_CUBE_OUTLINE "\tPipeline Statistics", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for(auto scn : scene::get_all_scenes())
        {
            if(!scn)
            {
                continue;
            }

            scn->registry->view<camera_component>().each([&](auto e, auto&& camera_comp)
            {
                auto& pipeline = camera_comp.get_pipeline_data().get_pipeline();
                const auto& stats = pipeline->get_stats();
                if(!stats.anything_drawn())
                {
                    return;
                }

                ImGui::Text("Pipeline Stats for %s:", scn->tag.c_str());
                ImGui::Indent();
                
                ImGui::Text("Drawn Particles: %u", stats.drawn_particles);
                ImGui::Text("Drawn Particles Batches: %u", stats.drawn_particles_batches);
                ImGui::Text("Drawn Models: %u", stats.drawn_models);
                ImGui::Text("Drawn Skinned Models: %u", stats.drawn_skinned_models);
                ImGui::Text("Drawn Models for Shadows: %u", stats.drawn_models_for_shadows);
                ImGui::Text("Drawn Skinned Models for Shadows: %u", stats.drawn_skinned_models_for_shadows);
                ImGui::Text("Drawn Lights: %u", stats.drawn_lights);
                ImGui::Text("Drawn Lights Casting Shadows: %u", stats.drawn_lights_casting_shadows);
                
                // Static Mesh Batching Statistics
                {
                    ImGui::Separator();
                    ImGui::Text("Static Mesh Batching:");
                    ImGui::Indent();
                    
                    const auto& batch_stats = stats.batching_stats;
                    ImGui::Text("Total Batches: %u", batch_stats.total_batches);
                    ImGui::Text("Total Instances: %u", batch_stats.total_instances);
                    ImGui::Text("Avg Batch Size: %.1f", batch_stats.average_batch_size);
                    ImGui::Text("Batching Efficiency: %.1f%%", batch_stats.batching_efficiency * 100.0f);
                    
                    {
                        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "Split Batches: %u", batch_stats.split_batches);
                    }
                    
                    // Performance timings
                    {
                        ImGui::Text("Collection Time: %.3f ms", batch_stats.collection_time_ms);
                        ImGui::Text("Preparation Time: %.3f ms", batch_stats.preparation_time_ms);
                        ImGui::Text("Submission Time: %.3f ms", batch_stats.submission_time_ms);
                    }
                    
                    // Memory usage
                    {
                        float memory_mb = static_cast<float>(batch_stats.instance_buffer_memory_used) / (1024.0f * 1024.0f);
                        ImGui::Text("Instance Buffer Memory: %.2f MB", memory_mb);
                    }
                    
                    ImGui::Unindent();
                }
                
                ImGui::Unindent();
            });
        }
    }
}

auto statistics_panel::draw_profiler_section() -> void
{
    if(!ImGui::CollapsingHeader(ICON_MDI_CLOCK_OUTLINE "\tProfiler"))
    {
        return;
    }
    
    ImGui::PushFont(ImGui::Font::Mono);
    
    // CPU Profiler - always shown
    ImGui::Text("CPU Profiler:");
    draw_app_profiler_data();
    
    ImGui::Separator();
    
    // GPU Profiler controls
    ImGui::AlignTextToFramePadding();
    ImGui::Text("GPU Profiler:");
    ImGui::SameLine();
    
    if(ImGui::Checkbox("Enable##GPUProfiler", &enable_gpu_profiler_))
    {
        if(enable_gpu_profiler_)
        {
            gfx::set_debug(BGFX_DEBUG_PROFILER);
        }
        else
        {
            gfx::set_debug(BGFX_DEBUG_NONE);
        }
    }
    
    // GPU Profiler data - conditionally shown
    if(enable_gpu_profiler_)
    {
        auto stats = gfx::get_stats();
        
        if(stats->numViews == 0)
        {
            ImGui::TextColored(warning_color, "No GPU profiling data available.");
            ImGui::Text("Profiler may be initializing...");
        }
        else
        {
            ImGui::Text("GPU Timing (per view/encoder):");
            draw_profiler_bars(stats);
        }
    }
    else
    {
        ImGui::TextColored(warning_color, "GPU profiler is disabled.");
        ImGui::Text("Enable to see detailed GPU timing information.");
    }
    
    ImGui::PopFont();
}

auto statistics_panel::draw_memory_info_section(float overlay_width) -> void
{
    if(!ImGui::CollapsingHeader(ICON_MDI_INFORMATION "\tMemory Usage"))
    {
        return;
    }
    
    ImGui::PushFont(ImGui::Font::Mono);


    ImGui::BeginGroup();
    std::array<char, 64> str_max;
    bx::prettify(str_max.data(), str_max.size(), static_cast<uint64_t>(mono::gc_get_heap_size()));
    
    std::array<char, 64> str_used;
    bx::prettify(str_used.data(), str_used.size(), mono::gc_get_used_size());
    
    ImGui::TextUnformatted(fmt::format("GC Heap Size: {}", str_max.data()).c_str());
    ImGui::TextUnformatted(fmt::format("GC Used Size: {}", str_used.data()).c_str());
    ImGui::EndGroup();

    ImGui::SameLine();
    if(ImGui::Button("Collect GC"))
    {
        mono::gc_collect();
    }
    
    
    auto stats = gfx::get_stats();
    auto gpu_memory_max = stats->gpuMemoryMax;
    
    // GPU memory section
    if(stats->gpuMemoryUsed > 0)
    {
        draw_gpu_memory_section(stats, gpu_memory_max, overlay_width);
    }
    else
    {
        ImGui::TextColored(warning_color, "No GPU memory usage data available");
    }
    
    // Render target memory section
    draw_render_target_memory_section(stats, gpu_memory_max, overlay_width);
    
    // Texture memory section  
    draw_texture_memory_section(stats, gpu_memory_max, overlay_width);
    
    ImGui::Unindent();
    ImGui::PopFont();
}

auto statistics_panel::draw_resources_section() -> void
{
    if(!ImGui::CollapsingHeader(ICON_MDI_PUZZLE "\tGPU Resources"))
    {
        return;
    }
    
    const auto caps = gfx::get_caps();
    const auto stats = gfx::get_stats();
    const float item_height = ImGui::GetTextLineHeightWithSpacing();
    
    ImGui::PushFont(ImGui::Font::Mono);
    
    ImGui::Text("Resource Usage (Current / Maximum):");
    ImGui::Separator();
    
    // Group resources by category for better organization
    ImGui::Text("Buffers:");
    ImGui::Indent();
    
    using namespace statistics_utils;
    draw_resource_bar("TIB", "Transient Index Buffer Used",
                     stats->transientIbUsed, caps->limits.maxTransientIbSize,
                     resource_bar_width, item_height);
    
    draw_resource_bar("TVB", "Transient Vertex Buffer Used",
                     stats->transientVbUsed, caps->limits.maxTransientVbSize,
                     resource_bar_width, item_height);
    
    draw_resource_bar("DIB", "Dynamic Index Buffers",
                     stats->numDynamicIndexBuffers, caps->limits.maxDynamicIndexBuffers,
                     resource_bar_width, item_height);
    
    draw_resource_bar("DVB", "Dynamic Vertex Buffers",
                     stats->numDynamicVertexBuffers, caps->limits.maxDynamicVertexBuffers,
                     resource_bar_width, item_height);
    
    draw_resource_bar(" IB", "Index Buffers",
                     stats->numIndexBuffers, caps->limits.maxIndexBuffers,
                     resource_bar_width, item_height);
    
    draw_resource_bar(" VB", "Vertex Buffers",
                     stats->numVertexBuffers, caps->limits.maxVertexBuffers,
                     resource_bar_width, item_height);
    
    ImGui::Unindent();
    ImGui::Separator();
    
    ImGui::Text("Shading:");
    ImGui::Indent();
    
    draw_resource_bar("  P", "Shader Programs",
                     stats->numPrograms, caps->limits.maxPrograms,
                     resource_bar_width, item_height);
    
    draw_resource_bar("  S", "Shaders",
                     stats->numShaders, caps->limits.maxShaders,
                     resource_bar_width, item_height);
    
    draw_resource_bar("  U", "Uniforms",
                     stats->numUniforms, caps->limits.maxUniforms,
                     resource_bar_width, item_height);
    
    ImGui::Unindent();
    ImGui::Separator();
    
    ImGui::Text("Rendering:");
    ImGui::Indent();
    
    draw_resource_bar("  T", "Textures",
                     stats->numTextures, caps->limits.maxTextures,
                     resource_bar_width, item_height);
    
    draw_resource_bar(" FB", "Frame Buffers",
                     stats->numFrameBuffers, caps->limits.maxFrameBuffers,
                     resource_bar_width, item_height);
    
    draw_resource_bar(" VD", "Vertex Layouts",
                     stats->numVertexLayouts, caps->limits.maxVertexLayouts,
                     resource_bar_width, item_height);
    
    draw_resource_bar(" OQ", "Occlusion Queries",
                     stats->numOcclusionQueries, caps->limits.maxOcclusionQueries,
                     resource_bar_width, item_height);
    
    ImGui::Unindent();
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
    gpu_memory_samples.push_sample(static_cast<float>(stats->gpuMemoryUsed) / megabyte_divisor);
    render_target_memory_samples.push_sample(static_cast<float>(stats->rtMemoryUsed) / megabyte_divisor);
    texture_memory_samples.push_sample(static_cast<float>(stats->textureMemoryUsed) / megabyte_divisor);
}

auto statistics_panel::get_draw_call_breakdown(const gfx::stats* stats, std::uint32_t& scene_calls, std::uint32_t& editor_calls, std::uint32_t& total_calls) -> void
{
    const ImGuiIO& io = ImGui::GetIO();
    total_calls = stats->numDraw;
    editor_calls = ImGui::GetDrawCalls();
    editor_calls = std::min(editor_calls, total_calls);
    scene_calls = total_calls - editor_calls;
}

auto statistics_panel::draw_primitive_counts(const gfx::stats* stats, const ImGuiIO& io) -> void
{
    const std::uint32_t total_primitives = std::accumulate(std::begin(stats->numPrims), std::end(stats->numPrims), 0u);
    std::uint32_t ui_primitives = io.MetricsRenderIndices / 3;
    ui_primitives = std::min(ui_primitives, total_primitives);
    const auto scene_primitives = total_primitives - ui_primitives;
    
    ImGui::Text("Triangle Counts:");
    ImGui::Indent();
    
    if(show_editor_stats_)
    {
        // Show detailed breakdown with editor stats
        ImGui::BeginColumns("PrimitiveColumns", 2, ImGuiOldColumnFlags_NoResize);
        ImGui::SetColumnWidth(0, 120.0f);
        
        ImGui::Text("Scene:");
        ImGui::NextColumn();
        ImGui::TextColored(cpu_color, "%u triangles", scene_primitives);
        ImGui::NextColumn();
        
        ImGui::Text("Editor:");
        ImGui::NextColumn();
        ImGui::TextColored(gpu_color, "%u triangles", ui_primitives);
        ImGui::NextColumn();
        
        ImGui::Text("Total:");
        ImGui::NextColumn();
        ImGui::Text("%u triangles", total_primitives);
        
        ImGui::EndColumns();
    }
    else
    {
        // Show only scene stats (main focus)
        ImGui::TextColored(cpu_color, "Scene: %u triangles", scene_primitives);
    }
    
    ImGui::Unindent();
}

auto statistics_panel::draw_call_counts(const gfx::stats* stats, const ImGuiIO& io) -> void
{
    std::uint32_t scene_calls = 0, editor_calls = 0, total_calls = 0;
    get_draw_call_breakdown(stats, scene_calls, editor_calls, total_calls);
    
    ImGui::Text("GPU Commands:");
    ImGui::Indent();
    
    if(show_editor_stats_)
    {
        // Show detailed breakdown with editor stats
        ImGui::BeginColumns("CallCountColumns", 2, ImGuiOldColumnFlags_NoResize);
        ImGui::SetColumnWidth(0, 120.0f);
        
        // Draw calls section
        ImGui::Text("Draw Calls:");
        ImGui::NextColumn();
        ImGui::Text("%u total", total_calls);
        ImGui::NextColumn();
        
        ImGui::Text("  Scene:");
        ImGui::NextColumn();
        ImGui::TextColored(cpu_color, "%u calls", scene_calls);
        ImGui::NextColumn();
        
        ImGui::Text("  Editor:");
        ImGui::NextColumn();
        ImGui::TextColored(gpu_color, "%u calls", editor_calls);
        ImGui::NextColumn();
        
        // Other command types
        ImGui::Text("Compute:");
        ImGui::NextColumn();
        ImGui::Text("%u calls", stats->numCompute);
        ImGui::NextColumn();
        
        ImGui::Text("Blit:");
        ImGui::NextColumn();
        ImGui::Text("%u calls", stats->numBlit);
        
        ImGui::EndColumns();
    }
    else
    {
        // Show only scene-focused stats
        ImGui::TextColored(cpu_color, "Scene Draw Calls: %u", scene_calls);
        
        // Still show compute and blit as they're typically scene-related
        if(stats->numCompute > 0 || stats->numBlit > 0)
        {
            ImGui::BeginColumns("SceneCallColumns", 2, ImGuiOldColumnFlags_NoResize);
            ImGui::SetColumnWidth(0, 120.0f);
            
            if(stats->numCompute > 0)
            {
                ImGui::Text("Compute:");
                ImGui::NextColumn();
                ImGui::Text("%u calls", stats->numCompute);
                ImGui::NextColumn();
            }
            
            if(stats->numBlit > 0)
            {
                ImGui::Text("Blit:");
                ImGui::NextColumn();
                ImGui::Text("%u calls", stats->numBlit);
                ImGui::NextColumn();
            }
            
            ImGui::EndColumns();
        }
    }
    
    ImGui::Unindent();
}

auto statistics_panel::draw_profiler_bars(const gfx::stats* stats) -> void
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

auto statistics_panel::draw_encoder_stats(const gfx::stats* stats, float item_height, float item_height_with_spacing, double to_cpu_ms) -> void
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
                
                const float max_width = profiler_max_width * profiler_scale;
                const float cpu_ms = static_cast<float>((encoder_stats.cpuTimeEnd - encoder_stats.cpuTimeBegin) * to_cpu_ms);
                const float cpu_width = bx::clamp(cpu_ms * profiler_scale, 1.0f, max_width);
                
                if(statistics_utils::draw_progress_bar(cpu_width, max_width, item_height, cpu_color))
                {
                    ImGui::SetItemTooltipEx("Encoder %d, CPU: %f [ms]", pos, cpu_ms);
                }
                
                ImGui::PopID();
            }
        }
        ImGui::EndListBox();
    }
}

auto statistics_panel::draw_view_stats(const gfx::stats* stats, float item_height, float item_height_with_spacing, double to_cpu_ms, double to_gpu_ms) -> void
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
                
                const float max_width = profiler_max_width * profiler_scale;
                const float cpu_time_elapsed = static_cast<float>((view_stats.cpuTimeEnd - view_stats.cpuTimeBegin) * to_cpu_ms);
                const float gpu_time_elapsed = static_cast<float>((view_stats.gpuTimeEnd - view_stats.gpuTimeBegin) * to_gpu_ms);
                const float cpu_width = bx::clamp(cpu_time_elapsed * profiler_scale, 1.0f, max_width);
                const float gpu_width = bx::clamp(gpu_time_elapsed * profiler_scale, 1.0f, max_width);
                
                ImGui::SameLine(64.0f);
                
                ImGui::PushID("cpu");
                if(statistics_utils::draw_progress_bar(cpu_width, max_width, item_height, cpu_color))
                {
                    ImGui::SetItemTooltipEx("View %d \"%s\", CPU: %f [ms]", pos, view_stats.name, cpu_time_elapsed);
                }
                ImGui::PopID();
                
                ImGui::SameLine();
                
                ImGui::PushID("gpu");
                if(statistics_utils::draw_progress_bar(gpu_width, max_width, item_height, gpu_color))
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
        if(ImGui::TreeNode(
            fmt::format("{:>7.3f}ms [{:^5}] - {}###{}", 
                       per_frame_data.get_time_since_swap(), 
                       per_frame_data.get_samples_since_swap(), 
                       name,
                       name).c_str()))
        {
                       ImGui::TextUnformatted(
            fmt::format("- {:>7.3f}ms [{:^5}] - Avg", 
                       per_frame_data.get_avg(),
                       sample_data::num_samples).c_str());
                       ImGui::TextUnformatted(
            fmt::format("- {:>7.3f}ms [{:^5}] - Max", 
                       per_frame_data.get_max(),
                       sample_data::num_samples).c_str());
                       ImGui::TextUnformatted(
            fmt::format("- {:>7.3f}ms [{:^5}] - Min", 
                       per_frame_data.get_min(),
                       sample_data::num_samples).c_str());
            ImGui::TreePop();
        }
    }
}

auto statistics_panel::draw_gpu_memory_section(const gfx::stats* stats, int64_t& gpu_memory_max, float overlay_width) -> void
{
    gpu_memory_max = std::max(stats->gpuMemoryUsed, stats->gpuMemoryMax);
    
    std::array<char, 64> str_max;
    bx::prettify(str_max.data(), str_max.size(), static_cast<uint64_t>(gpu_memory_max));
    
    std::array<char, 64> str_used;
    bx::prettify(str_used.data(), str_used.size(), stats->gpuMemoryUsed);
    
    const float usage_percentage = gpu_memory_max > 0 ? 
        (static_cast<float>(stats->gpuMemoryUsed) / static_cast<float>(gpu_memory_max)) * 100.0f : 0.0f;
    
    // Color code based on usage
    ImVec4 usage_color = cpu_color; // Green for low usage
    if(usage_percentage > 80.0f) 
    {
        usage_color = error_color;      // Red for high
    }
    else if(usage_percentage > 60.0f) 
    {
        usage_color = warning_color; // Orange for medium
    }
    
    ImGui::Separator();
    ImGui::Text("General GPU Memory:");
    ImGui::Indent();
    ImGui::Text("Usage: %s / %s", str_used.data(), str_max.data());
    ImGui::SameLine();
    ImGui::TextColored(usage_color, "(%.1f%%)", usage_percentage);
    
    ImGui::SetNextWindowViewportToCurrent();
    ImGui::PlotLines("##GPUMemory",
                     gpu_memory_samples.get_values(),
                     statistics_utils::sample_data::num_samples,
                     gpu_memory_samples.get_offset(),
                     "GPU Memory Usage Over Time",
                     0.0f,
                     static_cast<float>(gpu_memory_max),
                     ImVec2(overlay_width, plot_height));
    ImGui::Unindent();
}

auto statistics_panel::draw_render_target_memory_section(const gfx::stats* stats, int64_t& gpu_memory_max, float overlay_width) -> void
{
    gpu_memory_max = std::max(stats->rtMemoryUsed, gpu_memory_max);
    
    std::array<char, 64> str_max;
    bx::prettify(str_max.data(), str_max.size(), static_cast<uint64_t>(gpu_memory_max));
    
    std::array<char, 64> str_used;
    bx::prettify(str_used.data(), str_used.size(), stats->rtMemoryUsed);
    
    const float usage_percentage = gpu_memory_max > 0 ? 
        (static_cast<float>(stats->rtMemoryUsed) / static_cast<float>(gpu_memory_max)) * 100.0f : 0.0f;
    
    // Color code based on usage
    ImVec4 usage_color = cpu_color; // Green for low usage
    if(usage_percentage > 80.0f) 
    {
        usage_color = error_color;      // Red for high
    }
    else if(usage_percentage > 60.0f) 
    {
        usage_color = warning_color; // Orange for medium
    }
    
    ImGui::Separator();
    ImGui::Text("Render Target Memory:");
    ImGui::Indent();
    ImGui::Text("Usage: %s / %s", str_used.data(), str_max.data());
    ImGui::SameLine();
    ImGui::TextColored(usage_color, "(%.1f%%)", usage_percentage);
    
    ImGui::SetNextWindowViewportToCurrent();
    ImGui::PlotLines("##RenderTargetMemory",
                     render_target_memory_samples.get_values(),
                     statistics_utils::sample_data::num_samples,
                     render_target_memory_samples.get_offset(),
                     "Render Target Memory Usage Over Time",
                     0.0f,
                     static_cast<float>(gpu_memory_max),
                     ImVec2(overlay_width, plot_height));
    ImGui::Unindent();
}

auto statistics_panel::draw_texture_memory_section(const gfx::stats* stats, int64_t& gpu_memory_max, float overlay_width) -> void
{
    gpu_memory_max = std::max(stats->textureMemoryUsed, gpu_memory_max);
    
    std::array<char, 64> str_max;
    bx::prettify(str_max.data(), str_max.size(), static_cast<uint64_t>(gpu_memory_max));
    
    std::array<char, 64> str_used;
    bx::prettify(str_used.data(), str_used.size(), stats->textureMemoryUsed);
    
    const float usage_percentage = gpu_memory_max > 0 ? 
        (static_cast<float>(stats->textureMemoryUsed) / static_cast<float>(gpu_memory_max)) * 100.0f : 0.0f;
    
    // Color code based on usage
    ImVec4 usage_color = cpu_color; // Green for low usage
    if(usage_percentage > 80.0f) 
    {
        usage_color = error_color;      // Red for high
    }
    else if(usage_percentage > 60.0f) 
    {
        usage_color = warning_color; // Orange for medium
    }
    
    ImGui::Separator();
    ImGui::Text("Texture Memory:");
    ImGui::Indent();
    ImGui::Text("Usage: %s / %s", str_used.data(), str_max.data());
    ImGui::SameLine();
    ImGui::TextColored(usage_color, "(%.1f%%)", usage_percentage);
    
    ImGui::SetNextWindowViewportToCurrent();
    ImGui::PlotLines("##TextureMemory",
                     texture_memory_samples.get_values(),
                     statistics_utils::sample_data::num_samples,
                     texture_memory_samples.get_offset(),
                     "Texture Memory Usage Over Time",
                     0.0f,
                     static_cast<float>(gpu_memory_max),
                     ImVec2(overlay_width, plot_height));
    ImGui::Unindent();
}

} // namespace unravel
