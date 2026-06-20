#include "profiler_gpu_resources_section.h"

#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <graphics/graphics.h>
#include <imgui/imgui.h>
#include <imgui_widgets/tooltips.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>

namespace unravel
{

namespace
{
constexpr ImVec4 usage_low_color{0.27f, 0.72f, 0.42f, 1.0f};    // green
constexpr ImVec4 usage_medium_color{0.93f, 0.74f, 0.20f, 1.0f}; // amber
constexpr ImVec4 usage_high_color{0.90f, 0.32f, 0.28f, 1.0f};   // red
constexpr ImVec4 category_text_color{0.62f, 0.80f, 1.0f, 1.0f};
constexpr ImVec4 muted_text_color{0.55f, 0.55f, 0.55f, 1.0f};

constexpr float usage_medium_threshold = 0.60f;
constexpr float usage_high_threshold = 0.85f;
constexpr float bar_height_scale = 0.72f;

enum class resource_unit : uint8_t
{
    count,
    bytes
};

auto pick_usage_color(float fraction) -> ImVec4
{
    if(fraction >= usage_high_threshold)
    {
        return usage_high_color;
    }
    if(fraction >= usage_medium_threshold)
    {
        return usage_medium_color;
    }
    return usage_low_color;
}

void format_bytes(uint64_t value, char* buffer, size_t buffer_size)
{
    constexpr std::array<const char*, 5> units{"B", "KB", "MB", "GB", "TB"};
    if(value < 1024)
    {
        std::snprintf(buffer, buffer_size, "%llu B", static_cast<unsigned long long>(value));
        return;
    }
    double scaled = static_cast<double>(value);
    size_t unit_index = 0;
    while(scaled >= 1024.0 && unit_index + 1 < units.size())
    {
        scaled /= 1024.0;
        ++unit_index;
    }
    std::snprintf(buffer, buffer_size, "%.2f %s", scaled, units[unit_index]);
}

void format_count(uint64_t value, char* buffer, size_t buffer_size)
{
    std::array<char, 32> digits{};
    const int written = std::snprintf(digits.data(), digits.size(), "%llu", static_cast<unsigned long long>(value));
    if(written <= 0)
    {
        std::snprintf(buffer, buffer_size, "0");
        return;
    }
    size_t out = 0;
    for(int read = 0; read < written && out + 1 < buffer_size; ++read)
    {
        if(read > 0 && ((written - read) % 3) == 0)
        {
            buffer[out++] = ',';
        }
        if(out + 1 < buffer_size)
        {
            buffer[out++] = digits[static_cast<size_t>(read)];
        }
    }
    buffer[out] = '\0';
}

void format_value(uint64_t value, resource_unit unit, char* buffer, size_t buffer_size)
{
    if(unit == resource_unit::bytes)
    {
        format_bytes(value, buffer, buffer_size);
        return;
    }
    format_count(value, buffer, buffer_size);
}

void draw_right_aligned_text(const char* text, const ImVec4* color = nullptr)
{
    const float available_width = ImGui::GetContentRegionAvail().x;
    const float text_width = ImGui::CalcTextSize(text).x;
    const float offset = available_width - text_width;
    if(offset > 0.0f)
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
    }
    if(color != nullptr)
    {
        ImGui::TextColored(*color, "%s", text);
        return;
    }
    ImGui::TextUnformatted(text);
}

void draw_category_row(const char* icon, const char* label)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    const ImU32 background = ImGui::GetColorU32(ImVec4(0.16f, 0.20f, 0.27f, 1.0f));
    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, background);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(category_text_color, "%s  %s", icon, label);
}

void draw_resource_row(const char* name,
                       const char* description,
                       uint64_t current_value,
                       uint64_t max_value,
                       resource_unit unit)
{
    ImGui::PushID(name);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(name);
    ImGui::SetItemTooltipEx("%s", description);

    std::array<char, 32> used_buffer{};
    format_value(current_value, unit, used_buffer.data(), used_buffer.size());
    ImGui::TableSetColumnIndex(1);
    ImGui::AlignTextToFramePadding();
    draw_right_aligned_text(used_buffer.data());

    ImGui::TableSetColumnIndex(2);
    ImGui::AlignTextToFramePadding();
    if(max_value == 0)
    {
        draw_right_aligned_text("--", &muted_text_color);
    }
    else
    {
        std::array<char, 32> max_buffer{};
        format_value(max_value, unit, max_buffer.data(), max_buffer.size());
        draw_right_aligned_text(max_buffer.data());
    }

    ImGui::TableSetColumnIndex(3);
    if(max_value == 0)
    {
        draw_right_aligned_text("n/a", &muted_text_color);
        ImGui::PopID();
        return;
    }
    const float fraction =
        std::clamp(static_cast<float>(current_value) / static_cast<float>(max_value), 0.0f, 1.0f);
    std::array<char, 16> overlay{};
    std::snprintf(overlay.data(), overlay.size(), "%.1f%%", static_cast<double>(fraction * 100.0f));
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, pick_usage_color(fraction));
    ImGui::ProgressBar(fraction, ImVec2(-1.0f, ImGui::GetFrameHeight() * bar_height_scale), overlay.data());
    ImGui::PopStyleColor();
    ImGui::SetItemTooltipEx("%s: %.2f%% used", description, static_cast<double>(fraction * 100.0f));

    ImGui::PopID();
}
} // namespace

void profiler_draw_gpu_resources_section()
{
    if(!ImGui::CollapsingHeader(ICON_MDI_PUZZLE "\tGPU Resources"))
    {
        return;
    }
    const auto* caps = gfx::get_caps();
    const auto* stats = gfx::get_stats();
    if(caps == nullptr || stats == nullptr)
    {
        return;
    }

    constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                            ImGuiTableFlags_BordersOuter | ImGuiTableFlags_PadOuterX |
                                            ImGuiTableFlags_NoBordersInBodyUntilResize;
    if(!ImGui::BeginTable("##gpu_resources", 4, table_flags))
    {
        return;
    }

    ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_WidthStretch, 0.9f);
    ImGui::TableSetupColumn("Used", ImGuiTableColumnFlags_WidthFixed, 96.0f);
    ImGui::TableSetupColumn("Limit", ImGuiTableColumnFlags_WidthFixed, 96.0f);
    ImGui::TableSetupColumn("Usage", ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableHeadersRow();

    draw_category_row(ICON_MDI_DATABASE, "Buffers");
    draw_resource_row("Transient Index Buffer",
                      "GPU memory used by transient index buffers this frame.",
                      static_cast<uint64_t>(stats->transientIbUsed),
                      caps->limits.maxTransientIbSize,
                      resource_unit::bytes);
    draw_resource_row("Transient Vertex Buffer",
                      "GPU memory used by transient vertex buffers this frame.",
                      static_cast<uint64_t>(stats->transientVbUsed),
                      caps->limits.maxTransientVbSize,
                      resource_unit::bytes);
    draw_resource_row("Dynamic Index Buffers",
                      "Number of dynamic index buffers currently allocated.",
                      stats->numDynamicIndexBuffers,
                      caps->limits.maxDynamicIndexBuffers,
                      resource_unit::count);
    draw_resource_row("Dynamic Vertex Buffers",
                      "Number of dynamic vertex buffers currently allocated.",
                      stats->numDynamicVertexBuffers,
                      caps->limits.maxDynamicVertexBuffers,
                      resource_unit::count);
    draw_resource_row("Index Buffers",
                      "Number of static index buffers currently allocated.",
                      stats->numIndexBuffers,
                      caps->limits.maxIndexBuffers,
                      resource_unit::count);
    draw_resource_row("Vertex Buffers",
                      "Number of static vertex buffers currently allocated.",
                      stats->numVertexBuffers,
                      caps->limits.maxVertexBuffers,
                      resource_unit::count);

    draw_category_row(ICON_MDI_CODE_BRACES, "Shading");
    draw_resource_row("Shader Programs",
                      "Number of linked shader programs currently allocated.",
                      stats->numPrograms,
                      caps->limits.maxPrograms,
                      resource_unit::count);
    draw_resource_row("Shaders",
                      "Number of compiled shaders currently allocated.",
                      stats->numShaders,
                      caps->limits.maxShaders,
                      resource_unit::count);
    draw_resource_row("Uniforms",
                      "Number of uniform handles currently allocated.",
                      stats->numUniforms,
                      caps->limits.maxUniforms,
                      resource_unit::count);

    draw_category_row(ICON_MDI_MONITOR, "Rendering");
    draw_resource_row("Textures",
                      "Number of textures currently allocated.",
                      stats->numTextures,
                      caps->limits.maxTextures,
                      resource_unit::count);
    draw_resource_row("Frame Buffers",
                      "Number of frame buffers currently allocated.",
                      stats->numFrameBuffers,
                      caps->limits.maxFrameBuffers,
                      resource_unit::count);
    draw_resource_row("Vertex Layouts",
                      "Number of vertex layouts currently allocated.",
                      stats->numVertexLayouts,
                      caps->limits.maxVertexLayouts,
                      resource_unit::count);
    draw_resource_row("Occlusion Queries",
                      "Number of occlusion queries currently allocated.",
                      stats->numOcclusionQueries,
                      caps->limits.maxOcclusionQueries,
                      resource_unit::count);

    draw_category_row(ICON_MDI_MEMORY, "Estimated Memory");
    draw_resource_row("Texture Memory",
                      "Estimated GPU memory used by textures.",
                      static_cast<uint64_t>(std::max<int64_t>(0, stats->textureMemoryUsed)),
                      0,
                      resource_unit::bytes);
    draw_resource_row("Render Target Memory",
                      "Estimated GPU memory used by render targets.",
                      static_cast<uint64_t>(std::max<int64_t>(0, stats->rtMemoryUsed)),
                      0,
                      resource_unit::bytes);

    ImGui::EndTable();
}

} // namespace unravel
