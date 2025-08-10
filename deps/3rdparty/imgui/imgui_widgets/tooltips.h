#pragma once
#include "utils.h"
#include <imgui/imgui.h>
namespace ImGui
{
IMGUI_API void HelpMarker(const char* desc);

template<typename F>
void HelpMarker(const char* help, bool disabled, F&& f)
{
    if(disabled)
    {
        ImGui::TextDisabled("%s", help);
    }
    else
    {
        ImGui::Text("%s", help);
    }

    if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    {
        ImGui::SetNextWindowViewportToCurrent();
        if(ImGui::BeginTooltip())
        {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            f();
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
}

IMGUI_API void AddItemTooltipEx(const char* fmt, ...) IM_FMTARGS(1);
IMGUI_API void SetItemTooltipEx(const char* fmt, ...) IM_FMTARGS(1);                 // set a text-only tooltip if preceding item was hovered. override any previous call to SetTooltip().

} // namespace ImGui
