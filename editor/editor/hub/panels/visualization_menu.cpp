#include "visualization_menu.h"

#include <editor/imgui/integration/imgui.h>

#include <imgui/imgui_internal.h>

#include <algorithm>
#include <array>
#include <cstdio>

namespace unravel
{
namespace
{

constexpr float legend_overlay_width = 400.0f;
constexpr float legend_overlay_padding = 8.0f;
constexpr float legend_overlay_rounding = 4.0f;
constexpr ImVec4 legend_overlay_bg_color{0.08f, 0.08f, 0.08f, 0.85f};
constexpr ImVec4 legend_label_color{0.6f, 0.6f, 0.6f, 1.0f};
/// Menu-bar tint while a debug view is active, so a left-on pass never reads as a bug.
constexpr ImVec4 active_mode_color{1.0f, 0.75f, 0.2f, 1.0f};
/// Outline drawn around every swatch: without it a black or near-black entry - which several
/// views use as a meaningful category - is invisible against the panel background.
constexpr ImU32 swatch_border_color = IM_COL32(255, 255, 255, 90);

/// Draws one square color chip and leaves the cursor on the same line for its text.
void draw_swatch(const visualization_swatch& swatch)
{
    const float size = ImGui::GetTextLineHeight();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + size, min.y + size);
    const ImU32 fill =
        ImGui::ColorConvertFloat4ToU32(ImVec4(swatch.color[0], swatch.color[1], swatch.color[2], 1.0f));

    auto* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(min, max, fill, 2.0f);
    draw_list->AddRect(min, max, swatch_border_color, 2.0f);

    ImGui::Dummy(ImVec2(size, size));
    ImGui::SameLine();
}

void draw_legend_rows(const visualization_mode_entry& entry)
{
    for(const auto& swatch : entry.legend)
    {
        draw_swatch(swatch);
        ImGui::TextUnformatted(swatch.meaning);
    }
}

/// The hover card behind a menu entry: what the view shows, then its color legend.
void draw_mode_tooltip(const visualization_mode_entry& entry)
{
    ImGui::ItemTooltipEx(
        [&]()
        {
            ImGui::TextUnformatted(entry.label);
            ImGui::Separator();
            ImGui::TextUnformatted(entry.description);
            if(!entry.legend.empty())
            {
                ImGui::Spacing();
                ImGui::TextColored(legend_label_color, "Legend");
                draw_legend_rows(entry);
            }
        });
}

void draw_group_menu(const visualization_group_entry& group, int& mode)
{
    std::array<char, 128> label{};
    std::snprintf(label.data(), label.size(), "%s %s", group.icon, group.label);

    ImGui::SetNextWindowViewportToCurrent();
    // The tooltip goes after EndMenu: while a menu is open the "last item" belongs to the
    // popup window, not to the entry that opened it.
    if(ImGui::BeginMenu(label.data()))
    {
        for(const auto& entry : get_visualization_modes(group.group))
        {
            ImGui::RadioButton(entry.label, &mode, static_cast<int>(entry.mode));
            draw_mode_tooltip(entry);
        }
        ImGui::EndMenu();
    }
    ImGui::ItemTooltipEx(
        [&]()
        {
            ImGui::TextUnformatted(group.label);
            ImGui::Separator();
            ImGui::TextUnformatted(group.description);
        });
}

} // namespace

void visualization_menu::draw_menu(int& mode, state& menu_state)
{
    const auto* active = find_visualization_mode(mode);
    const bool is_debugging = active != nullptr && active->mode != visualization_mode::full;

    // "###" keeps the menu id stable while the visible label tracks the active view, so
    // picking a mode does not close the popup out from under the cursor.
    std::array<char, 192> menu_label{};
    if(is_debugging)
    {
        std::snprintf(menu_label.data(),
                      menu_label.size(),
                      "%s %s %s###debug_view_menu",
                      ICON_MDI_DRAWING_BOX,
                      active->label,
                      ICON_MDI_ARROW_DOWN_BOLD);
    }
    else
    {
        std::snprintf(menu_label.data(),
                      menu_label.size(),
                      "%s%s###debug_view_menu",
                      ICON_MDI_DRAWING_BOX,
                      ICON_MDI_ARROW_DOWN_BOLD);
    }

    // Captured before the push: PushStyleColor writes through to style.Colors, so the base
    // color has to be read first to restore it inside the popup.
    const ImVec4 base_text_color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    if(is_debugging)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, active_mode_color);
    }

    ImGui::SetNextWindowViewportToCurrent();
    if(ImGui::BeginMenu(menu_label.data()))
    {
        // The popup body must not inherit the menu-bar tint. Restoring it with a push/pop
        // pair INSIDE the popup keeps every pair on the window that opened it, which is what
        // ImGui's stack check requires.
        if(is_debugging)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, base_text_color);
        }

        const auto* full = find_visualization_mode(static_cast<int>(visualization_mode::full));
        if(full != nullptr)
        {
            ImGui::RadioButton(full->label, &mode, static_cast<int>(full->mode));
            draw_mode_tooltip(*full);
        }

        ImGui::Separator();

        for(const auto& group : get_visualization_groups())
        {
            draw_group_menu(group, mode);
        }

        ImGui::Separator();
        ImGui::Checkbox("Show Legend In Viewport", &menu_state.show_legend);
        ImGui::SetItemTooltipEx("%s",
                                "Overlay the active view's color legend in the bottom-left "
                                "corner of the viewport.");

        if(is_debugging)
        {
            ImGui::PopStyleColor();
        }
        ImGui::EndMenu();
    }

    if(is_debugging)
    {
        ImGui::PopStyleColor();
    }

    ImGui::SetItemTooltipEx("%s",
                            is_debugging ? "Debug View - a visualization is active"
                                         : "Debug View");
}

void visualization_menu::draw_legend_overlay(int mode, state& menu_state, const char* id)
{
    if(!menu_state.show_legend)
    {
        return;
    }

    const auto* entry = find_visualization_mode(mode);
    if(entry == nullptr || entry->mode == visualization_mode::full)
    {
        return;
    }

    auto* window = ImGui::GetCurrentWindow();
    if(window == nullptr || window->SkipItems)
    {
        return;
    }

    const auto content_rect = window->ContentRegionRect;
    const float max_height = content_rect.GetHeight() - 2.0f * legend_overlay_padding;
    const float width =
        std::min(legend_overlay_width, content_rect.GetWidth() - 2.0f * legend_overlay_padding);
    if(width <= 0.0f || max_height <= 0.0f)
    {
        return;
    }

    std::array<char, 64> child_name{};
    std::snprintf(child_name.data(), child_name.size(), "##debug_view_legend_%s", id);

    // The card auto-sizes to its legend, but a bottom-left anchor needs the height before the
    // cursor is placed - so last frame's measurement drives this frame's position. It settles
    // in one frame and only ever moves when the active view changes.
    const ImGuiID height_key = ImGui::GetID(child_name.data());
    const float measured_height = std::min(ImGui::GetStateStorage()->GetFloat(height_key, 0.0f), max_height);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, legend_overlay_bg_color);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, legend_overlay_rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 3.0f));

    ImGui::SetCursorScreenPos(ImVec2(content_rect.Min.x + legend_overlay_padding,
                                     content_rect.Max.y - legend_overlay_padding - measured_height));

    const ImGuiChildFlags child_flags = ImGuiChildFlags_AlwaysUseWindowPadding |
                                        ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize;

    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(width, max_height));
    if(ImGui::BeginChild(child_name.data(), ImVec2(width, 0.0f), child_flags))
    {
        const auto* group = find_visualization_group(entry->group);

        ImGui::TextColored(legend_label_color,
                           "%s %s",
                           ICON_MDI_DRAWING_BOX,
                           group != nullptr ? group->label : "Debug View");
        ImGui::SameLine();

        const float close_width =
            ImGui::CalcTextSize(ICON_MDI_CLOSE).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::AlignedItem(1.0f,
                           ImGui::GetContentRegionAvail().x,
                           close_width,
                           [&]()
                           {
                               if(ImGui::SmallButton(ICON_MDI_CLOSE))
                               {
                                   menu_state.show_legend = false;
                               }
                               ImGui::SetItemTooltipEx("%s",
                                                       "Hide the legend (the Debug View menu "
                                                       "brings it back)");
                           });

        ImGui::TextUnformatted(entry->label);

        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(legend_label_color, "%s", entry->description);
        if(!entry->legend.empty())
        {
            ImGui::Separator();
            draw_legend_rows(*entry);
        }
        ImGui::PopTextWrapPos();
    }
    const float height = ImGui::GetCurrentWindow()->Size.y;
    ImGui::EndChild();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();

    ImGui::GetStateStorage()->SetFloat(height_key, height);
}

} // namespace unravel
