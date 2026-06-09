#include "imgui_context_menu_style.h"

#include <imgui/imgui_internal.h>

namespace ImGui
{
namespace
{

constexpr int k_context_menu_style_var_count = 5;
constexpr int k_context_menu_style_color_count = 4;

} // namespace

void PushContextMenuStyle()
{
    PushStyleVar(ImGuiStyleVar_PopupRounding, 6.0f);
    PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
    PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 6.0f));
    PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(8.0f, 6.0f));

    ImVec4 popup_bg = GetStyleColorVec4(ImGuiCol_PopupBg);
    popup_bg.x = std::min(popup_bg.x + 0.02f, 1.0f);
    popup_bg.y = std::min(popup_bg.y + 0.02f, 1.0f);
    popup_bg.z = std::min(popup_bg.z + 0.02f, 1.0f);
    PushStyleColor(ImGuiCol_PopupBg, popup_bg);

    ImVec4 header_hovered = GetStyleColorVec4(ImGuiCol_HeaderHovered);
    header_hovered.w = std::min(header_hovered.w + 0.08f, 1.0f);
    PushStyleColor(ImGuiCol_HeaderHovered, header_hovered);

    ImVec4 header_active = GetStyleColorVec4(ImGuiCol_HeaderActive);
    header_active.w = std::min(header_active.w + 0.12f, 1.0f);
    PushStyleColor(ImGuiCol_HeaderActive, header_active);

    ImVec4 separator = GetStyleColorVec4(ImGuiCol_Separator);
    separator.w = std::min(separator.w + 0.35f, 1.0f);
    PushStyleColor(ImGuiCol_Separator, separator);
}

void PopContextMenuStyle()
{
    PopStyleColor(k_context_menu_style_color_count);
    PopStyleVar(k_context_menu_style_var_count);
}

auto MenuItemIcon(const char* icon, const char* label, const char* shortcut, bool enabled) -> bool
{
    if(icon == nullptr || icon[0] == '\0')
    {
        return MenuItem(label, shortcut, false, enabled);
    }

    char buffer[256];
    ImFormatString(buffer, IM_ARRAYSIZE(buffer), "%s  %s", icon, label);
    return MenuItem(buffer, shortcut, false, enabled);
}

auto BeginMenuIcon(const char* icon, const char* label, bool enabled) -> bool
{
    if(icon == nullptr || icon[0] == '\0')
    {
        return BeginMenu(label, enabled);
    }

    char buffer[256];
    ImFormatString(buffer, IM_ARRAYSIZE(buffer), "%s  %s", icon, label);
    return BeginMenu(buffer, enabled);
}

} // namespace ImGui
