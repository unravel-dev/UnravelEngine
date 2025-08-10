#include "tooltips.h"
#include <imgui/imgui_internal.h>

namespace ImGui
{
void HelpMarker(const char* desc)
{
    ImGui::TextDisabled("%s", "(?)");
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    {
        ImGui::SetNextWindowViewportToCurrent();
        if(ImGui::BeginTooltip())
        {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted(desc);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
}

void AddItemTooltipEx(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if (IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        SetNextWindowViewportToCurrent();
        if (BeginTooltipEx(ImGuiTooltipFlags_None, ImGuiWindowFlags_None))
        {
            TextV(fmt, args);
            EndTooltip();
        }
    }
    va_end(args);
}


// Shortcut to use 'style.HoverFlagsForTooltipMouse' or 'style.HoverFlagsForTooltipNav'.
// Defaults to == ImGuiHoveredFlags_Stationary | ImGuiHoveredFlags_DelayShort when using the mouse.
void SetItemTooltipEx(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if (IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        SetNextWindowViewportToCurrent();
        SetTooltipV(fmt, args);
    }
    va_end(args);
}

} // namespace ImGui
