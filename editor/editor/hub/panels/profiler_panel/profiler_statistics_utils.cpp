#include "profiler_statistics_utils.h"

#include <algorithm>

namespace unravel::profiler_statistics_utils
{

namespace
{
constexpr float hover_color_multiplier = 0.1f;
}

auto draw_progress_bar(float width, float max_width, float height, const ImVec4& color) -> bool
{
    const ImGuiStyle& style = ImGui::GetStyle();

    ImVec4 hovered_color(color.x + color.x * hover_color_multiplier,
                         color.y + color.y * hover_color_multiplier,
                         color.z + color.z * hover_color_multiplier,
                         color.w + color.w * hover_color_multiplier);

    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered_color);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, style.ItemSpacing.y));

    bool item_hovered = false;

    ImGui::Button("##bar_button", ImVec2(width, height));
    item_hovered |= ImGui::IsItemHovered();

    ImGui::SameLine();
    ImGui::InvisibleButton("##bar_invisible", ImVec2(max_width - width + 1, height));
    item_hovered |= ImGui::IsItemHovered();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    return item_hovered;
}

auto draw_resource_bar(const char* name,
                       const char* tooltip,
                       uint32_t current_value,
                       uint32_t max_value,
                       float max_width,
                       float height) -> void
{
    bool item_hovered = false;
    ImGui::PushID(name);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s: %6d / %6d", name, current_value, max_value);
    item_hovered |= ImGui::IsItemHovered();
    ImGui::SameLine();

    const float percentage = static_cast<float>(current_value) / static_cast<float>(max_value);
    static const ImVec4 color(0.5f, 0.5f, 0.5f, 1.0f);
    item_hovered |= draw_progress_bar(std::max(1.0f, percentage * max_width), max_width, height, color);
    ImGui::SameLine();

    ImGui::Text("%5.2f%%", static_cast<double>(percentage * 100.0f));

    if(item_hovered)
    {
        ImGui::SetNextWindowViewportToCurrent();
        ImGui::BeginTooltip();
        ImGui::Text("%s %5.2f%%", tooltip, static_cast<double>(percentage * 100.0f));
        ImGui::EndTooltip();
    }

    ImGui::PopID();
}

} // namespace unravel::profiler_statistics_utils
