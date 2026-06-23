#include "viewport_stats_overlay.h"
#include "editor/format/format_bytes.h"
#include "editor/imgui/integration/imgui.h"
#include "imgui_widgets/utils.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <engine/ecs/scene.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/pipeline/pipeline.h>
#include <graphics/eviction.h>
#include <graphics/graphics.h>

#include <bx/string.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <numeric>

namespace unravel
{
namespace
{
constexpr float overlay_width = 460.0f;
constexpr float overlay_padding = 8.0f;
constexpr float overlay_rounding = 4.0f;
constexpr float overlay_bg_alpha = 0.75f;
constexpr ImVec4 overlay_bg_color{0.08f, 0.08f, 0.08f, overlay_bg_alpha};

constexpr ImVec4 color_good{0.2f, 0.8f, 0.2f, 1.0f};
constexpr ImVec4 color_warning{1.0f, 0.7f, 0.0f, 1.0f};
constexpr ImVec4 color_bad{1.0f, 0.3f, 0.3f, 1.0f};
constexpr ImVec4 color_label{0.6f, 0.6f, 0.6f, 1.0f};

// Muted fills for progress bars (the saturated status colors above are too harsh as a solid block).
constexpr ImVec4 bar_good{0.30f, 0.49f, 0.32f, 1.0f};
constexpr ImVec4 bar_warning{0.58f, 0.45f, 0.20f, 1.0f};
constexpr ImVec4 bar_bad{0.55f, 0.28f, 0.28f, 1.0f};

auto get_fps_color(float fps) -> ImVec4
{
    if(fps < 30.0f)
    {
        return color_bad;
    }
    if(fps < 55.0f)
    {
        return color_warning;
    }
    return color_good;
}

auto get_memory_color(float percentage) -> ImVec4
{
    if(percentage > 80.0f)
    {
        return color_bad;
    }
    if(percentage > 60.0f)
    {
        return color_warning;
    }
    return color_good;
}

void draw_label(const char* label)
{
    ImGui::TextColored(color_label, "%s", label);
    ImGui::SameLine(200.0f);
}

void draw_eviction_overlay_stats()
{
    const auto evict_stats = gfx::eviction::get_stats();
    if(evict_stats.registered_count == 0)
    {
        return;
    }

    // Group the paging stats under their own labeled sub-header so they read distinctly from the
    // raw memory counters above.
    ImGui::Separator();
    ImGui::TextColored(color_label, "  " ICON_MDI_SWAP_HORIZONTAL " Paging");
    ImGui::SetItemTooltipEx("GPU resource eviction / paging. Idle CPU-backed resources are released\n"
                            "from GPU memory under pressure and restored automatically on next use.");

    // Budget that drives eviction (GPU memory used vs. the configured budget).
    if(evict_stats.budget_bytes > 0)
    {
        const bool over_budget = evict_stats.budget_used_bytes > evict_stats.budget_bytes;
        const auto used_str = format_bytes(static_cast<int64_t>(evict_stats.budget_used_bytes));
        const auto budget_str = format_bytes(static_cast<int64_t>(evict_stats.budget_bytes));

        ImGui::BeginGroup();
        draw_label("    Budget");
        if(over_budget)
        {
            ImGui::TextColored(color_bad, "%s / %s " ICON_MDI_ALERT " OVER", used_str.c_str(), budget_str.c_str());
        }
        else
        {
            ImGui::TextColored(color_good, "%s / %s", used_str.c_str(), budget_str.c_str());
        }
        ImGui::EndGroup();
        ImGui::SetItemTooltipEx("GPU memory used vs. the eviction budget. When usage exceeds the\n"
                                "budget, resources are evicted down to the target watermark.\n"
                                "Marked red when over budget.");

        ImGui::BeginGroup();
        draw_label("    Target");
        ImGui::TextUnformatted(format_bytes(static_cast<int64_t>(evict_stats.target_bytes)).c_str());
        ImGui::EndGroup();
        ImGui::SetItemTooltipEx("Watermark the pager evicts down to once over budget\n"
                                "(hysteresis below the budget to avoid thrashing).");
    }

    // Current residency: how much is resident (still available to evict) vs. already evicted.
    const auto resident_str = fmt::format("{} ({})",
                                          evict_stats.resident_count,
                                          format_bytes(static_cast<int64_t>(evict_stats.resident_bytes)));
    ImGui::BeginGroup();
    draw_label("    Resident");
    ImGui::TextUnformatted(resident_str.c_str());
    ImGui::EndGroup();
    ImGui::SetItemTooltipEx("Resources currently resident on the GPU that can still be paged out,\n"
                            "and their total GPU memory.");

    const auto evicted_str = fmt::format("{} ({})",
                                         evict_stats.evicted_count,
                                         format_bytes(static_cast<int64_t>(evict_stats.evicted_bytes)));
    ImGui::BeginGroup();
    draw_label("    Evicted");
    if(evict_stats.evicted_count > 0)
    {
        ImGui::TextColored(color_warning, "%s", evicted_str.c_str());
    }
    else
    {
        ImGui::TextUnformatted(evicted_str.c_str());
    }
    ImGui::EndGroup();
    ImGui::SetItemTooltipEx("Resources currently evicted (their GPU memory reclaimed).\n"
                            "They restore automatically the next time they are used.");

    // Headroom bar: share of the managed pool still resident, i.e. how much can still be freed. The
    // overlay text is kept short so it always fits the bar.
    const double pool_bytes = static_cast<double>(evict_stats.resident_bytes + evict_stats.evicted_bytes);
    const float resident_fraction =
        pool_bytes > 0.0 ? static_cast<float>(static_cast<double>(evict_stats.resident_bytes) / pool_bytes) : 0.0f;
    ImVec4 headroom_color = bar_good;
    if(evict_stats.resident_bytes == 0)
    {
        headroom_color = bar_bad;
    }
    else if(resident_fraction < 0.2f)
    {
        headroom_color = bar_warning;
    }
    const auto headroom_overlay = fmt::format("{} free", format_bytes(static_cast<int64_t>(evict_stats.resident_bytes)));
    ImGui::BeginGroup();
    draw_label("    Headroom");
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, headroom_color);
    // Fill the remaining content width so the bar (and its overlay text) never runs under the scrollbar.
    ImGui::ProgressBar(resident_fraction, ImVec2(ImGui::GetContentRegionAvail().x, 0.0f), headroom_overlay.c_str());
    ImGui::PopStyleColor();
    ImGui::EndGroup();
    ImGui::SetItemTooltipEx("Share of the managed pool still resident on the GPU - how much can\n"
                            "still be paged out under further memory pressure. Red when nothing\n"
                            "is left to evict.");

    // Lifetime activity: an eviction count that keeps climbing every frame means paging is constant.
    ImGui::BeginGroup();
    draw_label("    Lifetime");
    ImGui::TextUnformatted(fmt::format("{} evicted", evict_stats.total_evictions).c_str());
    if(evict_stats.total_restores > 0)
    {
        ImGui::SameLine();
        ImGui::TextColored(color_label, "%s", fmt::format("/ {} restored", evict_stats.total_restores).c_str());
    }
    ImGui::EndGroup();
    ImGui::SetItemTooltipEx("Lifetime evictions (and restores). If the eviction count keeps\n"
                            "climbing every frame, paging is happening constantly - consider\n"
                            "raising the budget or the min-age window.");

    if(evict_stats.thrash_events > 0)
    {
        ImGui::BeginGroup();
        draw_label("    Thrash");
        ImGui::TextColored(color_warning, "%s", fmt::format("{}", evict_stats.thrash_events).c_str());
        ImGui::EndGroup();
        ImGui::SetItemTooltipEx("Resources restored shortly after being evicted. High values\n"
                                "indicate the budget is too tight; raise it or the min-age window.");
    }
}

void draw_performance_section()
{
    if(!ImGui::CollapsingSection(ICON_MDI_SPEEDOMETER "\tPerformance", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }

    auto& io = ImGui::GetIO();
    auto* stats = gfx::get_stats();
    const double to_cpu_ms = 1000.0 / static_cast<double>(stats->cpuTimerFreq);
    const double to_gpu_ms = 1000.0 / static_cast<double>(stats->gpuTimerFreq);

    const float fps = io.Framerate;
    const float frame_ms = 1000.0f / fps;

    ImGui::BeginGroup();
    ImGui::TextColored(get_fps_color(fps), "  FPS: %.1f", static_cast<double>(fps));
    ImGui::SameLine();
    ImGui::TextColored(color_label, "(%.2f ms)", static_cast<double>(frame_ms));
    ImGui::EndGroup();
    ImGui::SetItemTooltipEx("Frames per second (higher is better)\n"
                            "Green: >55 FPS (smooth)\n"
                            "Yellow: 30-55 FPS (acceptable)\n"
                            "Red: <30 FPS (poor performance)");

    const double cpu_submit_ms = static_cast<double>(stats->cpuTimeEnd - stats->cpuTimeBegin) * to_cpu_ms;
    const double gpu_submit_ms = static_cast<double>(stats->gpuTimeEnd - stats->gpuTimeBegin) * to_gpu_ms;

    ImGui::BeginGroup();
    draw_label("  CPU Submit");
    ImGui::Text("%.3f ms", cpu_submit_ms);
    ImGui::EndGroup();
    ImGui::SetItemTooltipEx("Time the CPU spent submitting render commands\n"
                            "to the graphics driver. High values indicate\n"
                            "a CPU-bound rendering bottleneck.");

    ImGui::BeginGroup();
    draw_label("  GPU Submit");
    ImGui::Text("%.3f ms", gpu_submit_ms);
    ImGui::EndGroup();
    ImGui::SetItemTooltipEx("Time the GPU spent executing render commands.\n"
                            "High values indicate a GPU-bound bottleneck\n"
                            "such as complex shaders or high fill rate.");

    ImGui::BeginGroup();
    draw_label("  GPU Latency");
    ImGui::Text("%d frames", stats->maxGpuLatency);
    ImGui::EndGroup();
    ImGui::SetItemTooltipEx("Number of frames the GPU is behind the CPU.\n"
                            "Higher latency means more input lag but can\n"
                            "improve throughput. Typically 1-3 frames.");
}

void draw_scene_section()
{
    if(!ImGui::CollapsingSection(ICON_MDI_CUBE_OUTLINE "\tScene", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }

    auto* stats = gfx::get_stats();
    const auto& io = ImGui::GetIO();

    const std::uint32_t total_primitives = std::accumulate(
        std::begin(stats->numPrims), std::end(stats->numPrims), 0u);
    std::uint32_t ui_primitives = io.MetricsRenderIndices / 3;
    ui_primitives = std::min(ui_primitives, total_primitives);
    const auto scene_primitives = total_primitives - ui_primitives;

    std::uint32_t total_calls = stats->numDraw;
    std::uint32_t editor_calls = ImGui::GetDrawCalls();
    editor_calls = std::min(editor_calls, total_calls);
    std::uint32_t scene_calls = total_calls - editor_calls;

    auto format_count = [](std::uint32_t count) -> std::string
    {
        if(count >= 1000000)
        {
            return fmt::format("{:.1f}M", static_cast<double>(count) / 1000000.0);
        }
        if(count >= 1000)
        {
            return fmt::format("{:.1f}k", static_cast<double>(count) / 1000.0);
        }
        return fmt::format("{}", count);
    };

    ImGui::BeginGroup();
    draw_label("  Triangles");
    ImGui::TextUnformatted(format_count(scene_primitives).c_str());
    ImGui::EndGroup();
    ImGui::SetItemTooltipEx("Total triangle count rendered for the scene\n"
                            "(excludes editor UI). Reducing triangle count\n"
                            "via LODs or culling improves GPU performance.");

    ImGui::BeginGroup();
    draw_label("  Draw Calls");
    ImGui::Text("%u", scene_calls);
    ImGui::EndGroup();
    ImGui::SetItemTooltipEx("Number of draw commands sent to the GPU for\n"
                            "scene rendering (excludes editor UI draws).\n"
                            "Fewer draw calls generally means better performance.");

    ImGui::BeginGroup();
    draw_label("  Render Passes");
    ImGui::Text("%u", gfx::render_pass::get_last_frame_max_pass_id());
    ImGui::EndGroup();
    ImGui::SetItemTooltipEx("Number of rendering passes executed this frame.\n"
                            "Includes geometry, lighting, shadow, and\n"
                            "post-processing passes.");

    if(stats->numCompute > 0)
    {
        ImGui::BeginGroup();
        draw_label("  Compute Calls");
        ImGui::Text("%u", stats->numCompute);
        ImGui::EndGroup();
        ImGui::SetItemTooltipEx("Number of GPU compute shader dispatches.\n"
                                "Used for GPGPU tasks.");
    }
    if(stats->numBlit > 0)
    {
        ImGui::BeginGroup();
        draw_label("  Blit Calls");
        ImGui::Text("%u", stats->numBlit);
        ImGui::EndGroup();
        ImGui::SetItemTooltipEx("Number of GPU blit (copy/transfer) operations.\n"
                                "Used for copying textures, resolving MSAA,\n"
                                "or transferring between render targets.");
    }
}

void draw_memory_section()
{
    if(!ImGui::CollapsingSection(ICON_MDI_MEMORY "\tMemory"))
    {
        return;
    }

    auto* stats = gfx::get_stats();

    if(stats->gpuMemoryUsed > 0)
    {
        auto used_str = format_bytes(stats->gpuMemoryUsed);
        ImGui::BeginGroup();
        if(stats->gpuMemoryMax > 0)
        {
            auto max_str = format_bytes(stats->gpuMemoryMax);
            float pct = (static_cast<float>(stats->gpuMemoryUsed) /
                         static_cast<float>(stats->gpuMemoryMax)) * 100.0f;
            ImGui::TextColored(color_label, "  GPU Memory");
            ImGui::SameLine(200.0f);
            ImGui::TextColored(get_memory_color(pct), "%s / %s (%.0f%%)",
                               used_str.c_str(), max_str.c_str(), static_cast<double>(pct));
        }
        else
        {
            draw_label("  GPU Memory");
            ImGui::TextUnformatted(used_str.c_str());
        }
        ImGui::EndGroup();
        ImGui::SetItemTooltipEx("Total GPU video memory allocated vs available.\n"
                                "Color-coded by usage percentage:\n"
                                "Green: <60%%, Yellow: 60-80%%, Red: >80%%.\n"
                                "High usage may cause performance degradation\n"
                                "or out-of-memory errors.");
    }

    if(stats->textureMemoryUsed > 0)
    {
        ImGui::BeginGroup();
        draw_label("  Texture Mem");
        ImGui::TextUnformatted(format_bytes(stats->textureMemoryUsed).c_str());
        ImGui::EndGroup();
        ImGui::SetItemTooltipEx("GPU memory consumed by texture resources.\n"
                                "Reduce with smaller textures or compressed formats.");
    }

    if(stats->rtMemoryUsed > 0)
    {
        ImGui::BeginGroup();
        draw_label("  RT Memory");
        ImGui::TextUnformatted(format_bytes(stats->rtMemoryUsed).c_str());
        ImGui::EndGroup();
        ImGui::SetItemTooltipEx("GPU memory consumed by render targets\n"
                                "(framebuffers). Scales with resolution.");
    }

    draw_eviction_overlay_stats();
}

void draw_pipeline_stats(const rendering::pipeline_stats& pstats)
{
    ImGui::BeginGroup();
    draw_label("  Static Models");
    ImGui::Text("%u (%u Meshes)", pstats.drawn_models, pstats.drawn_static_submeshes);
    ImGui::EndGroup();
    ImGui::SetItemTooltipEx("Visible static models and submeshes drawn this frame.\n"
                            "The value in parentheses counts individual submeshes\n"
                            "after per-submesh frustum culling.");

    ImGui::BeginGroup();
    draw_label("  Skinned Models");
    ImGui::Text("%u (%u Meshes)", pstats.drawn_skinned_models, pstats.drawn_skinned_submeshes);
    ImGui::EndGroup();
    ImGui::SetItemTooltipEx("Visible skinned models and submeshes drawn this frame.\n"
                            "The value in parentheses counts individual submeshes\n"
                            "submitted for GPU skinning.");

    ImGui::BeginGroup();
    draw_label("  Lights");
    ImGui::Text("%u", pstats.drawn_lights);
    ImGui::EndGroup();
    ImGui::SetItemTooltipEx("Number of active lights evaluated for the\n"
                            "scene. Each additional light increases shading\n"
                            "cost in deferred and forward rendering.");

    ImGui::BeginGroup();
    draw_label("  Shadow Lights");
    ImGui::Text("%u", pstats.drawn_lights_casting_shadows);
    ImGui::EndGroup();
    ImGui::SetItemTooltipEx("Number of lights with shadow casting enabled.\n"
                            "Each shadow light requires one or more extra\n"
                            "render passes to generate shadow maps.");

    const uint32_t shadow_models = pstats.drawn_models_for_shadows + pstats.drawn_skinned_models_for_shadows;
    const uint32_t shadow_submeshes = pstats.drawn_submeshes_for_shadows + pstats.drawn_skinned_submeshes_for_shadows;
    ImGui::BeginGroup();
    draw_label("  Shadow Models");
    ImGui::Text("%u (%u meshes)", shadow_models, shadow_submeshes);
    ImGui::EndGroup();
    ImGui::SetItemTooltipEx("Models and submeshes rendered into shadow maps.\n"
                            "The mesh count includes cascade and face redraws,\n"
                            "so it can exceed the main-pass submesh count.");

    ImGui::BeginGroup();
    draw_label("  Particles");
    ImGui::Text("%u (%u Batches)", pstats.drawn_particles, pstats.drawn_particles_batches);
    ImGui::EndGroup();
    ImGui::SetItemTooltipEx("Active particle emitters and their GPU draw\n"
                            "batches. Particles with different materials\n"
                            "or blend modes produce separate batches.");

    const auto& batch = pstats.batching_stats;
    ImGui::Separator();
    ImGui::TextColored(color_label, "  Batching");
    ImGui::SetItemTooltipEx("Static mesh batching combines multiple meshes\n"
                            "sharing the same material into fewer draw calls,\n"
                            "reducing CPU overhead.");

    ImGui::BeginGroup();
    draw_label("    Batches");
    ImGui::Text("%u (%u Inst)", batch.total_batches, batch.total_instances);
    ImGui::EndGroup();
    ImGui::SetItemTooltipEx("Total batches created and total mesh instances\n"
                            "across all batches. A lower batch count relative\n"
                            "to instance count indicates effective batching.");

    ImGui::BeginGroup();
    draw_label("    Efficiency");
    ImGui::Text("%.0f%%", static_cast<double>(batch.batching_efficiency * 100.0f));
    ImGui::EndGroup();
    ImGui::SetItemTooltipEx("Percentage of eligible meshes successfully\n"
                            "combined into batches. 100%% means all eligible\n"
                            "meshes are batched. Low values may indicate\n"
                            "too many unique materials or shader variants.");

    ImGui::BeginGroup();
    draw_label("    Saved");
    ImGui::Text("%u calls", batch.draw_calls_saved);
    ImGui::EndGroup();
    ImGui::SetItemTooltipEx("Number of draw calls eliminated by batching.\n"
                            "Higher values mean more CPU time saved from\n"
                            "reduced driver overhead.");
}

void draw_pipeline_section(const rendering::pipeline_stats& pstats)
{
    if(!ImGui::CollapsingSection(ICON_MDI_PIPE "\tPipeline"))
    {
        return;
    }

    draw_pipeline_stats(pstats);
}

} // namespace

void viewport_stats_overlay::draw(const rendering::pipeline_stats& pstats, state& overlay_state, const char* id)
{
    if(!overlay_state.is_visible)
    {
        return;
    }

    auto* window = ImGui::GetCurrentWindow();
    if(!window || window->SkipItems)
    {
        return;
    }

    auto content_rect = window->ContentRegionRect;
    float max_height = content_rect.GetHeight() - 2.0f * overlay_padding;

    float pos_x = content_rect.Max.x - overlay_width - overlay_padding;
    float pos_y = content_rect.Min.y + overlay_padding;

    ImGui::SetCursorScreenPos(ImVec2(pos_x, pos_y));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, overlay_bg_color);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.15f, 0.15f, 0.15f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.22f, 0.22f, 0.22f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.18f, 0.18f, 0.18f, 0.9f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, overlay_rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 3.0f));

    ImGuiChildFlags child_flags = ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize;
    ImGuiWindowFlags window_flags = 0;

    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(overlay_width, max_height));
    auto child_name = fmt::format("##viewport_stats_{}", id);
    if(ImGui::BeginChild(child_name.c_str(), ImVec2(overlay_width, 0), child_flags, window_flags))
    {
        // ImGui::PushFont(ImGui::Font::SemiBold);
        ImGui::PushWindowFontScale(1.25f);
        auto header_text = ICON_MDI_CHART_LINE " Statistics";
        auto header_text_width = ImGui::CalcTextSize(header_text).x;
        ImGui::AlignedItem(0.5f, overlay_width, header_text_width, [&]() -> void {
            ImGui::TextUnformatted(header_text);
        });
        ImGui::PopWindowFontScale();

        ImGui::PushFont(ImGui::Font::Mono);
        draw_performance_section();
        draw_scene_section();
        draw_memory_section();
        draw_pipeline_section(pstats);
        ImGui::PopFont();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float btn_width = ImGui::CalcTextSize(ICON_MDI_CHART_BAR " Open Profiler").x
            + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetCursorPosX((overlay_width - btn_width) * 0.5f);
        if(ImGui::Button(ICON_MDI_CHART_BAR " Open Profiler"))
        {
            overlay_state.open_profiler_requested = true;
        }
        ImGui::Spacing();
    }
    ImGui::EndChild();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
}

void viewport_stats_overlay::draw_stats_toggle(state& overlay_state)
{
    const float fps = ImGui::GetIO().Framerate;
    std::array<char, 96> fps_label_buf{};
    const char* label = ICON_MDI_CHART_LINE " Stats";
    if(!overlay_state.is_visible)
    {
        
        std::snprintf(fps_label_buf.data(),
                      fps_label_buf.size(),
                      ICON_MDI_CHART_LINE_VARIANT " Stats (%.1f FPS)",
                      static_cast<double>(fps));
        label = fps_label_buf.data();
    }

    auto label_size = ImGui::CalcTextSize(label).x;

    ImGui::SameLine();

    ImGui::AlignedItem(1.0f,
                       ImGui::GetContentRegionAvail().x,
                       label_size,
                       [&]() -> void
                       {
                           bool is_visible = overlay_state.is_visible;
                           if(is_visible)
                           {
                               ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
                           }
                           if(ImGui::MenuItem(label, "", is_visible))
                           {
                               overlay_state.is_visible = !overlay_state.is_visible;
                           }
                           if(is_visible)
                           {
                               ImGui::PopStyleColor();
                           }
                       });
    ImGui::SetItemTooltipEx("%s", overlay_state.is_visible ? "Hide Statistics" : "Show Statistics");
}
} // namespace unravel
