#include "visualization_menu.h"
#include "panel_toolbar.h"
#include "visualization_overlays.h"
#include "visualization_widgets.h"

#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui.h>
#include <editor/imgui/integration/imgui_style.h>
#include <editor/imgui/screen_card.h>

#include <imgui/imgui_internal.h>
#include <imgui_widgets/tooltips.h>
#include <imgui_widgets/utils.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdio>
#include <string>
#include <utility>

namespace unravel
{
namespace
{
using screen_card::to_pixels;

constexpr float legend_overlay_width = 400.0f;
constexpr float legend_overlay_padding = 8.0f;
constexpr float legend_overlay_rounding = 4.0f;
constexpr ImVec4 legend_overlay_bg_color{0.08f, 0.08f, 0.08f, 0.85f};
constexpr ImVec4 legend_label_color{0.6f, 0.6f, 0.6f, 1.0f};
/// Tint of the button while a debug view or an overlay is on, so a left-on pass never reads as a bug.
constexpr ImVec4 active_mode_color{1.0f, 0.75f, 0.2f, 1.0f};
/// Outline drawn around every swatch: without it a black or near-black entry - which several
/// views use as a meaningful category - is invisible against the panel background.
constexpr ImU32 swatch_border_color = IM_COL32(255, 255, 255, 90);
/// The labels a view puts on the image: yellow text over a one-pixel drop shadow.
constexpr ImU32 view_label_color = IM_COL32(255, 255, 0, 255);
constexpr ImU32 view_label_shadow_color = IM_COL32(0, 0, 0, 255);
// The popover's sizes, in units of the font size.
constexpr float popover_width = 32.0f;
constexpr float tab_bar_width = 15.0f;
constexpr float category_column_width = 12.5f;
constexpr float views_list_height = 17.5f;
constexpr float description_height = 8.5f;
constexpr float pane_rounding = 0.5f;
constexpr float pane_padding = 0.4f;
constexpr float pane_gap = 0.35f;
/// The Overlays tab scrolls past the room the viewport leaves under it, less this margin, and never gets shorter
/// than the minimum.
constexpr float overlays_bottom_margin = 1.5f;
constexpr float overlays_min_height = 10.0f;
/// The panes of the Views tab: a shade darker than the popover, as the settings windows' sidebar.
constexpr ImU32 pane_color = IM_COL32(0, 0, 0, 38);
constexpr std::array<const char*, 2> tab_labels = {"Views", "Overlays"};

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

/// The button's text: the icon, then the active view's name and the count of the overlays that are on.
auto make_button_text(const visualization_mode_entry* active_view, int overlay_count) -> std::string
{
    std::string name = active_view != nullptr ? active_view->label : "";
    if(overlay_count > 0)
    {
        std::array<char, 32> count{};
        if(active_view != nullptr)
        {
            std::snprintf(count.data(), count.size(), " +%d", overlay_count);
        }
        else
        {
            std::snprintf(count.data(), count.size(), "%d %s", overlay_count, overlay_count == 1 ? "Overlay" : "Overlays");
        }
        name += count.data();
    }
    return name.empty() ? std::string(ICON_MDI_DRAWING_BOX) : panel_toolbar::make_text(ICON_MDI_DRAWING_BOX, name.c_str(), false);
}

/// Begins a pane of the Views tab, a shade darker than the popover. Always pair with EndChild().
auto begin_pane(const char* id, const ImVec2& size) -> bool
{
    const float padding = to_pixels(pane_padding);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, pane_color);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, to_pixels(pane_rounding));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
    const bool is_visible = ImGui::BeginChild(id, size, ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    return is_visible;
}

/// The tab bar, and the reset button at the other end of its line.
void draw_header(int& mode, visualization_menu::state& menu_state, int overlay_count)
{
    int tab = static_cast<int>(menu_state.tab);
    const std::array<int, 2> badges = {0, overlay_count};
    if(visualization_widgets::draw_segmented("##tabs",
                                             tab_labels.data(),
                                             badges.data(),
                                             int(tab_labels.size()),
                                             to_pixels(tab_bar_width),
                                             tab))
    {
        menu_state.tab = static_cast<visualization_menu::popover_tab>(tab);
    }
    const float bar_top = ImGui::GetItemRectMin().y;
    const float bar_height = ImGui::GetItemRectSize().y;
    const char* reset_text = ICON_MDI_RESTORE " Reset";
    const float button_width = ImGui::CalcTextSize(reset_text).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - button_width);
    ImGui::SetCursorScreenPos(
        ImVec2(ImGui::GetCursorScreenPos().x, ImFloor(bar_top + (bar_height - ImGui::GetFrameHeight()) * 0.5f)));
    const bool is_active = mode != static_cast<int>(visualization_mode::full) || overlay_count > 0;
    ImGui::BeginDisabled(!is_active);
    if(ImGui::Button(reset_text))
    {
        mode = static_cast<int>(visualization_mode::full);
        visualization_overlays::turn_off(menu_state);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltipEx("%s", "Back to the lit image, with every overlay off.");
}

/// The categories of the Views tab: Lit, then every visualization group. Picking Lit turns the debug view off; a
/// group that holds the active view while another is listed carries a marker.
void draw_categories(int& mode, visualization_menu::state& menu_state)
{
    const auto* active = find_visualization_mode(mode);
    const visualization_group active_group = active != nullptr ? active->group : visualization_group::none;
    if(visualization_widgets::draw_list_row("##lit",
                                            ICON_MDI_WHITE_BALANCE_SUNNY,
                                            "Lit",
                                            menu_state.views_group == visualization_group::none,
                                            false))
    {
        menu_state.views_group = visualization_group::none;
        mode = static_cast<int>(visualization_mode::full);
    }
    ImGui::SetItemTooltipEx("%s", "The lit image, without a debug view.");
    for(const auto& group : get_visualization_groups())
    {
        const bool is_listed = menu_state.views_group == group.group;
        if(visualization_widgets::draw_list_row(group.name,
                                                group.icon,
                                                group.label,
                                                is_listed,
                                                !is_listed && active_group == group.group))
        {
            menu_state.views_group = group.group;
        }
        ImGui::SetItemTooltipEx("%s", group.description);
    }
}

/// The views of the listed category. Returns the one under the mouse, if any.
auto draw_modes(int& mode, const visualization_menu::state& menu_state) -> const visualization_mode_entry*
{
    const visualization_mode_entry* hovered = nullptr;
    for(const auto& entry : get_visualization_modes(menu_state.views_group))
    {
        if(visualization_widgets::draw_list_row(entry.name, nullptr, entry.label, static_cast<int>(entry.mode) == mode, false))
        {
            mode = static_cast<int>(entry.mode);
        }
        if(ImGui::IsItemHovered())
        {
            hovered = &entry;
        }
    }
    return hovered;
}

/// What a view shows: its name, its description and its legend.
void draw_description(const visualization_mode_entry& entry)
{
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::TextUnformatted(entry.label);
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color());
    ImGui::TextUnformatted(entry.description);
    ImGui::PopStyleColor();
    if(!entry.legend.empty())
    {
        ImGui::Spacing();
        draw_legend_rows(entry);
    }
    ImGui::PopTextWrapPos();
}

/// The Views tab: the categories beside their views, the description of the view under the mouse (else of the active
/// one) under both, and the legend switch.
void draw_views_tab(int& mode, visualization_menu::state& menu_state)
{
    if(ImGui::IsWindowAppearing())
    {
        const auto* active = find_visualization_mode(mode);
        menu_state.views_group = active != nullptr ? active->group : visualization_group::none;
    }
    const float list_height = to_pixels(views_list_height);
    if(begin_pane("##categories", ImVec2(to_pixels(category_column_width), list_height)))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        draw_categories(mode, menu_state);
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
    ImGui::SameLine(0.0f, to_pixels(pane_gap));
    const visualization_mode_entry* hovered = nullptr;
    if(begin_pane("##modes", ImVec2(0.0f, list_height)))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        hovered = draw_modes(mode, menu_state);
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
    const auto* shown = hovered != nullptr ? hovered : find_visualization_mode(mode);
    if(begin_pane("##description", ImVec2(0.0f, to_pixels(description_height))) && shown != nullptr)
    {
        draw_description(*shown);
    }
    ImGui::EndChild();
    visualization_widgets::draw_switch_row("Legend in Viewport",
                                           "Show the active view's description and color legend in the bottom-left "
                                           "corner of the viewport.",
                                           menu_state.show_legend);
}

/// The Overlays tab, scrolling once the options of the overlays that are on outgrow the room under it.
void draw_overlays_tab(visualization_menu::state& menu_state)
{
    const ImGuiViewport* viewport = ImGui::GetWindowViewport();
    const float room = viewport->WorkPos.y + viewport->WorkSize.y - ImGui::GetCursorScreenPos().y -
                       to_pixels(overlays_bottom_margin);
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f),
                                        ImVec2(FLT_MAX, std::max(room, to_pixels(overlays_min_height))));
    if(ImGui::BeginChild("##overlays", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY))
    {
        visualization_overlays::draw_tab(menu_state);
    }
    ImGui::EndChild();
}

} // namespace

void visualization_menu::draw_toolbar_dropdown(int& mode, state& menu_state)
{
    const auto* active = find_visualization_mode(mode);
    const bool is_debugging = active != nullptr && active->mode != visualization_mode::full;
    const int overlay_count = visualization_overlays::count_active(menu_state);
    const bool is_active = is_debugging || overlay_count > 0;

    // The active view and the overlays stay spelled out on the bar, whatever the layout. The id is
    // fixed, so the popover stays open while the text follows the picks.
    const std::string text = make_button_text(is_debugging ? active : nullptr, overlay_count);
    const ImU32 text_color = is_active ? ImGui::ColorConvertFloat4ToU32(active_mode_color) : 0;
    const char* tooltip = is_active ? "Debug views and overlays - one is on" : "Debug views and overlays";
    const char* dropdown_id = "##debug_view";
    const popover_request request = std::exchange(menu_state.request, popover_request::none);
    if(request == popover_request::open)
    {
        panel_toolbar::open_dropdown(dropdown_id);
    }
    ImGui::SetNextWindowSize(ImVec2(to_pixels(popover_width), 0.0f));
    if(!panel_toolbar::begin_dropdown(dropdown_id, text.c_str(), tooltip, text_color))
    {
        return;
    }
    if(request == popover_request::close)
    {
        ImGui::CloseCurrentPopup();
    }
    draw_header(mode, menu_state, overlay_count);
    ImGui::Separator();
    if(menu_state.tab == popover_tab::views)
    {
        draw_views_tab(mode, menu_state);
    }
    else
    {
        draw_overlays_tab(menu_state);
    }
    panel_toolbar::end_dropdown();
}

void visualization_menu::draw_view_labels(int mode,
                                          const state& menu_state,
                                          const std::vector<debug_view_label>& labels,
                                          const ImVec2& image_min,
                                          const ImVec2& image_max)
{
    const bool is_overview = mode == static_cast<int>(visualization_mode::lumen_overview) ||
                             mode == static_cast<int>(visualization_mode::lumen_performance_overview);
    if(labels.empty() || (!is_overview && menu_state.show_legend))
    {
        return;
    }
    auto* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 size = image_max - image_min;
    for(const auto& label : labels)
    {
        const ImVec2 position(image_min.x + label.position.x * size.x, image_min.y + label.position.y * size.y);
        draw_list->AddText(position + ImVec2(1.0f, 1.0f), view_label_shadow_color, label.text.c_str());
        draw_list->AddText(position, view_label_color, label.text.c_str());
    }
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
                                                       "Hide the legend (the debug view popover "
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

auto visualization_menu::make_lumen_visualize(const state& menu_state,
                                              const ImVec2& image_min,
                                              const ImVec2& image_max,
                                              const usize32_t& render_size) -> lumen_visualize_pass::world_settings
{
    auto settings = menu_state.lumen;
    settings.cursor = menu_state.lumen_cursor_override.value_or(math::vec2(-1.0f));
    const ImVec2 extent = image_max - image_min;
    if(menu_state.lumen_cursor_override || !ImGui::IsItemHovered() || extent.x <= 0.0f || extent.y <= 0.0f)
    {
        return settings;
    }
    const ImVec2 local = ImGui::GetMousePos() - image_min;
    settings.cursor = math::vec2(local.x * float(render_size.width) / extent.x,
                                 local.y * float(render_size.height) / extent.y);
    return settings;
}

} // namespace unravel
