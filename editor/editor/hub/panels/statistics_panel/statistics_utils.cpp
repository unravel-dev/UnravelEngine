#include "statistics_utils.h"

#include <algorithm>
#include <numeric>

namespace unravel::statistics_utils
{

// Constants
namespace
{
    constexpr float HOVER_COLOR_MULTIPLIER = 0.1f;
    constexpr int32_t SMART_INIT_SAMPLES = 20;
}

sample_data::sample_data()
{
    reset(0.0f);
}

auto sample_data::reset(float value) -> void
{
    offset_ = 0;
    std::fill(values_.begin(), values_.end(), value);
    
    min_ = value;
    max_ = value;
    average_ = value;
    
    smart_init_samples_ = SMART_INIT_SAMPLES;
}

auto sample_data::push_sample(float value) -> void
{
    if(smart_init_samples_ > 0 && offset_ > smart_init_samples_)
    {
        reset(value);
        smart_init_samples_ = -1;
    }
    
    values_[offset_] = value;
    offset_ = (offset_ + 1) % NUM_SAMPLES;
    
    float min = std::numeric_limits<float>::max();
    float max = std::numeric_limits<float>::lowest();
    float sum = 0.0f;
    
    for(const auto& val : values_)
    {
        min = std::min(min, val);
        max = std::max(max, val);
        sum += val;
    }
    
    min_ = min;
    max_ = max;
    average_ = sum / NUM_SAMPLES;
}

auto draw_progress_bar(float width, float max_width, float height, const ImVec4& color) -> bool
{
    const ImGuiStyle& style = ImGui::GetStyle();
    
    ImVec4 hovered_color(
        color.x + color.x * HOVER_COLOR_MULTIPLIER,
        color.y + color.y * HOVER_COLOR_MULTIPLIER,
        color.z + color.z * HOVER_COLOR_MULTIPLIER,
        color.w + color.w * HOVER_COLOR_MULTIPLIER
    );
    
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
        ImGui::BeginTooltip();
        ImGui::Text("%s %5.2f%%", tooltip, static_cast<double>(percentage * 100.0f));
        ImGui::EndTooltip();
    }
    
    ImGui::PopID();
}

} // namespace unravel::statistics_utils 