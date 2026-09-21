#include "layout_panel.h"
#include "../panel.h"
#include "../panel_toolbar.h"

#include "editor/imgui/integration/fonts/icons/icons_material_design_icons.h"

#include <editor/imgui/integration/imgui_context_menu_style.h>
#include <editor/imgui/integration/imgui_messagebox.h>
#include <editor/imgui/integration/imgui_style.h>
#include <imgui/imgui.h>
#include <imgui_widgets/tooltips.h>

#include <algorithm>
#include <cctype>

namespace unravel
{
namespace
{
// Sizes are in units of the font size, so the panel follows the UI scale of the editor.
// Narrower than this, the toolbar buttons drop their labels.
constexpr float LAYOUT_TOOLBAR_COMPACT_WIDTH = 17.0f;
constexpr float LAYOUT_SAVE_POPUP_WIDTH = 20.0f;
constexpr float LAYOUT_SAVE_BUTTON_WIDTH = 4.5f;

constexpr float LAYOUT_ROW_HEIGHT = 1.9f;
constexpr float LAYOUT_ROW_PADDING_X = 0.45f;
constexpr float LAYOUT_ROW_ICON_COLUMN = 1.75f;
// The actions sit inside the row, this far from its edges.
constexpr float LAYOUT_ROW_ACTION_INSET = 0.2f;
constexpr float LAYOUT_ROW_ACTION_GAP = 0.1f;
constexpr float LAYOUT_ROW_ACTION_ROUNDING = 0.3f;
constexpr int LAYOUT_ROW_ACTION_COUNT = 2;

// The rows look like the console's: faint stripes, the hover a wash of the header color.
constexpr ImU32 LAYOUT_ROW_STRIPE_COLOR = IM_COL32(255, 255, 255, 7);
constexpr float LAYOUT_ROW_HOVERED_ALPHA = 0.45f;
constexpr float LAYOUT_MUTED_TEXT_ALPHA = 0.55f;
constexpr ImU32 LAYOUT_ACTION_HOVERED_COLOR = IM_COL32(255, 255, 255, 26);
constexpr ImU32 LAYOUT_ACTION_HELD_COLOR = IM_COL32(255, 255, 255, 46);
constexpr float LAYOUT_ACTION_TEXT_ALPHA = 0.86f;
// Delete stays as quiet as the other actions until it is pointed at.
constexpr ImU32 LAYOUT_DANGER_COLOR = IM_COL32(255, 95, 95, 255);
constexpr ImU32 LAYOUT_DANGER_FILL_COLOR = IM_COL32(255, 95, 95, 40);
constexpr ImU32 LAYOUT_WARNING_COLOR = IM_COL32(255, 190, 60, 255);
// The theme's field color is the popup's own: the name field gets a light wash to stand out.
constexpr ImU32 LAYOUT_FIELD_COLOR = IM_COL32(255, 255, 255, 14);
constexpr ImU32 LAYOUT_FIELD_HOVERED_COLOR = IM_COL32(255, 255, 255, 24);
constexpr ImU32 LAYOUT_FIELD_ACTIVE_COLOR = IM_COL32(255, 255, 255, 30);
constexpr float LAYOUT_ACCENT_ACTIVE_DARKEN = 0.85f;

// Share of the empty list above its hint, and the size of the hint's icon.
constexpr float LAYOUT_HINT_TOP_SHARE = 0.3f;
constexpr float LAYOUT_HINT_ICON_SCALE = 2.0f;

constexpr const char* LAYOUT_CONTEXT_MENU_ID = "##layout_context_menu";

auto calc_layout_pixels(float font_units) -> float
{
    return ImFloor(ImGui::GetFontSize() * font_units);
}

auto filter_layout_name_char(ImGuiInputTextCallbackData* data) -> int
{
    // The name becomes a file name.
    const ImWchar c = data->EventChar;
    const bool is_forbidden = c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' ||
                              c == '?' || c == '*';
    return is_forbidden ? 1 : 0;
}

/// Alphabetical the way a person reads it; the saved order is by case and puts "a" after "Z".
auto is_layout_name_before(const std::string& lhs, const std::string& rhs) -> bool
{
    const auto is_char_before = [](char lhs_char, char rhs_char) -> bool
    {
        return std::tolower(static_cast<unsigned char>(lhs_char)) < std::tolower(static_cast<unsigned char>(rhs_char));
    };
    return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), is_char_before);
}

auto get_layout_muted_text_color() -> ImU32
{
    return ImGui::GetColorU32(ImGuiCol_Text, LAYOUT_MUTED_TEXT_ALPHA);
}

void push_layout_field_style()
{
    ImGui::PushStyleColor(ImGuiCol_FrameBg, LAYOUT_FIELD_COLOR);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, LAYOUT_FIELD_HOVERED_COLOR);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, LAYOUT_FIELD_ACTIVE_COLOR);
}

void pop_layout_field_style()
{
    ImGui::PopStyleColor(3);
}

void push_layout_accent_button_style()
{
    const ImVec4 accent = imgui_style::get_accent_color();
    const ImVec4 hovered(accent.x, accent.y, accent.z, 1.0f);
    const ImVec4 active(accent.x * LAYOUT_ACCENT_ACTIVE_DARKEN,
                        accent.y * LAYOUT_ACCENT_ACTIVE_DARKEN,
                        accent.z * LAYOUT_ACCENT_ACTIVE_DARKEN,
                        1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
}

void pop_layout_accent_button_style()
{
    ImGui::PopStyleColor(3);
}

void draw_layout_empty_hint()
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float start_x = ImGui::GetCursorPosX();
    const auto draw_centered_line = [&](const char* text, ImU32 color)
    {
        const float line_width = ImGui::CalcTextSize(text).x;
        ImGui::SetCursorPosX(start_x + ImMax(0.0f, (avail.x - line_width) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
    };
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + avail.y * LAYOUT_HINT_TOP_SHARE);
    ImGui::PushWindowFontScale(LAYOUT_HINT_ICON_SCALE);
    draw_centered_line(ICON_MDI_VIEW_DASHBOARD_OUTLINE, get_layout_muted_text_color());
    ImGui::PopWindowFontScale();
    draw_centered_line("No saved layouts", ImGui::GetColorU32(ImGuiCol_Text));
    draw_centered_line("Arrange the panels, then keep them with New Layout.", get_layout_muted_text_color());
}

void draw_layout_row_background(const ImRect& rect, int row_index, bool is_highlighted)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if(is_highlighted)
    {
        draw_list->AddRectFilled(rect.Min,
                                 rect.Max,
                                 ImGui::GetColorU32(ImGuiCol_HeaderHovered, LAYOUT_ROW_HOVERED_ALPHA));
        return;
    }
    const bool is_striped = row_index % 2 == 1;
    if(is_striped)
    {
        draw_list->AddRectFilled(rect.Min, rect.Max, LAYOUT_ROW_STRIPE_COLOR);
    }
}

/// Slot 0 is the action at the right end of the row, slot 1 the one left of it, and so on.
auto calc_layout_row_action_rect(const ImRect& row_rect, int slot) -> ImRect
{
    const float inset = calc_layout_pixels(LAYOUT_ROW_ACTION_INSET);
    const float size = row_rect.GetHeight() - 2.0f * inset;
    const float stride = size + calc_layout_pixels(LAYOUT_ROW_ACTION_GAP);
    const float max_x = row_rect.Max.x - inset - static_cast<float>(slot) * stride;
    return ImRect(ImVec2(max_x - size, row_rect.Min.y + inset), ImVec2(max_x, row_rect.Max.y - inset));
}

/// Layout icon and name. The name gives way with an ellipsis to the actions, when they show.
void draw_layout_row_label(const ImRect& rect, const std::string& name, bool has_actions)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float font_size = ImGui::GetFontSize();
    const float padding_x = calc_layout_pixels(LAYOUT_ROW_PADDING_X);
    const float text_y = ImFloor(rect.Min.y + (rect.GetHeight() - font_size) * 0.5f);
    draw_list->AddText(ImVec2(rect.Min.x + padding_x, text_y),
                       get_layout_muted_text_color(),
                       ICON_MDI_VIEW_DASHBOARD_OUTLINE);
    const float text_max_x =
        has_actions ? calc_layout_row_action_rect(rect, LAYOUT_ROW_ACTION_COUNT - 1).Min.x - padding_x
                    : rect.Max.x - padding_x;
    const ImVec2 text_min(rect.Min.x + calc_layout_pixels(LAYOUT_ROW_ICON_COLUMN), text_y);
    ImGui::RenderTextEllipsis(draw_list,
                              text_min,
                              ImVec2(text_max_x, rect.Max.y),
                              text_max_x,
                              name.c_str(),
                              nullptr,
                              nullptr);
}

/// Icon button laid over a row. It takes no room of its own: the row keeps the line.
auto draw_layout_row_action(const ImRect& bb, const char* id, const char* icon, const char* tooltip, bool is_destructive)
    -> bool
{
    const ImGuiID item_id = ImGui::GetID(id);
    if(!ImGui::ItemAdd(bb, item_id))
    {
        return false;
    }
    bool is_hovered = false;
    bool is_held = false;
    const bool is_pressed = ImGui::ButtonBehavior(bb, item_id, &is_hovered, &is_held);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if(is_hovered || is_held)
    {
        const ImU32 plain_fill_color = is_held ? LAYOUT_ACTION_HELD_COLOR : LAYOUT_ACTION_HOVERED_COLOR;
        const ImU32 fill_color = is_destructive ? LAYOUT_DANGER_FILL_COLOR : plain_fill_color;
        draw_list->AddRectFilled(bb.Min, bb.Max, fill_color, calc_layout_pixels(LAYOUT_ROW_ACTION_ROUNDING));
    }
    const bool is_pointed_at = is_hovered || is_held;
    const ImU32 plain_icon_color = ImGui::GetColorU32(ImGuiCol_Text, is_pointed_at ? 1.0f : LAYOUT_ACTION_TEXT_ALPHA);
    const ImU32 icon_color = is_destructive && is_pointed_at ? LAYOUT_DANGER_COLOR : plain_icon_color;
    const ImVec2 icon_size = ImGui::CalcTextSize(icon);
    draw_list->AddText(ImFloor(bb.GetCenter() - icon_size * 0.5f), icon_color, icon);
    ImGui::SetItemTooltipEx("%s", tooltip);
    return is_pressed;
}
} // namespace

layout_panel::layout_panel(imgui_panels* parent, const char* name) : panel_base(name), parent_(parent)
{
}

auto layout_panel::get_window_flags() const -> ImGuiWindowFlags
{
    // The list scrolls on its own, under a toolbar that stays.
    return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
}

void layout_panel::draw_ui(rtti::context& ctx)
{
    (void)ctx;
    layout_manager& manager = parent_->get_layout_manager();
    draw_toolbar(manager);
    draw_layout_list(manager);
}

void layout_panel::draw_toolbar(layout_manager& manager)
{
    const bool is_compact = ImGui::GetContentRegionAvail().x < calc_layout_pixels(LAYOUT_TOOLBAR_COMPACT_WIDTH);
    if(panel_toolbar::begin_strip("##layout_toolbar"))
    {
        draw_save_dropdown(manager, is_compact);
        panel_toolbar::separator();
        const std::string reset_text = panel_toolbar::make_text(ICON_MDI_BACKUP_RESTORE, "Reset", is_compact);
        if(panel_toolbar::button("##reset", reset_text.c_str(), "Put the panels back in the default layout"))
        {
            manager.reset_to_default();
        }
        panel_toolbar::align_right();
        if(panel_toolbar::button("##open_folder", ICON_MDI_FOLDER_OPEN_OUTLINE, "Open the folder of the saved layouts"))
        {
            fs::show_in_graphical_env(manager.get_layouts_directory());
        }
    }
    panel_toolbar::end_strip();
}

void layout_panel::draw_save_dropdown(layout_manager& manager, bool is_compact)
{
    const std::string text = panel_toolbar::make_text(ICON_MDI_PLUS, "New Layout", is_compact);
    ImGui::SetNextWindowSize(ImVec2(calc_layout_pixels(LAYOUT_SAVE_POPUP_WIDTH), 0.0f));
    if(!panel_toolbar::begin_dropdown("##new_layout", text.c_str(), "Save the current layout of the panels"))
    {
        return;
    }
    draw_save_form(manager);
    panel_toolbar::end_dropdown();
}

void layout_panel::draw_save_form(layout_manager& manager)
{
    ImGui::SeparatorText("Save the Current Layout As");
    const float save_button_width = calc_layout_pixels(LAYOUT_SAVE_BUTTON_WIDTH);
    if(ImGui::IsWindowAppearing())
    {
        new_layout_name_.fill('\0');
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - save_button_width - ImGui::GetStyle().ItemSpacing.x);
    push_layout_field_style();
    const bool is_enter_pressed =
        ImGui::InputTextWithHint("##layout_name",
                                 "Layout name",
                                 new_layout_name_.data(),
                                 new_layout_name_.size(),
                                 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCharFilter,
                                 filter_layout_name_char);
    pop_layout_field_style();
    const std::string name(new_layout_name_.data());
    ImGui::SameLine();
    ImGui::BeginDisabled(name.empty());
    push_layout_accent_button_style();
    const bool is_save_pressed = ImGui::Button("Save", ImVec2(save_button_width, 0.0f));
    pop_layout_accent_button_style();
    ImGui::EndDisabled();
    if(manager.has_preset(name))
    {
        ImGui::PushStyleColor(ImGuiCol_Text, LAYOUT_WARNING_COLOR);
        ImGui::TextWrapped("%s", ICON_MDI_ALERT_OUTLINE " Replaces the saved layout of this name.");
        ImGui::PopStyleColor();
    }
    if((is_save_pressed || is_enter_pressed) && !name.empty())
    {
        manager.save_preset(name);
        ImGui::CloseCurrentPopup();
    }
}

void layout_panel::draw_layout_list(layout_manager& manager)
{
    // The names are a copy: a row may save or delete a layout while the list is drawn.
    std::vector<std::string> names = manager.get_preset_names();
    std::sort(names.begin(), names.end(), is_layout_name_before);
    if(ImGui::BeginChild("##layout_list", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoSavedSettings))
    {
        if(names.empty())
        {
            draw_layout_empty_hint();
        }
        // Rows tile without a gap, so the stripes read as one surface.
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        for(size_t i = 0; i < names.size(); ++i)
        {
            draw_layout_row(manager, names[i], static_cast<int>(i));
        }
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
}

void layout_panel::draw_layout_row(layout_manager& manager, const std::string& name, int row_index)
{
    ImGui::PushID(name.c_str());
    const ImVec2 row_min = ImGui::GetCursorScreenPos();
    const ImVec2 row_size(ImGui::GetContentRegionAvail().x, calc_layout_pixels(LAYOUT_ROW_HEIGHT));
    const ImRect row_rect(row_min, row_min + row_size);
    // The actions are laid over the row, so it lets them take the hover. It still counts as
    // hovered under them, and while one is held: the actions must not vanish mid-click.
    ImGui::SetNextItemAllowOverlap();
    const bool is_clicked = ImGui::InvisibleButton("##layout_row", row_size);
    const bool is_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem |
                                                 ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    ImGui::SetItemTooltipEx("Apply \"%s\" (right-click for more)", name.c_str());
    ImGui::OpenPopupOnItemClick(LAYOUT_CONTEXT_MENU_ID, ImGuiPopupFlags_MouseButtonRight);
    // The row a menu was opened for stays lit while the menu is up.
    const bool is_highlighted = is_hovered || ImGui::IsPopupOpen(LAYOUT_CONTEXT_MENU_ID);
    draw_layout_row_background(row_rect, row_index, is_highlighted);
    draw_layout_row_label(row_rect, name, is_highlighted);
    if(is_highlighted)
    {
        draw_layout_row_actions(manager, name, row_rect);
    }
    if(is_clicked)
    {
        manager.load_preset(name);
    }
    draw_layout_context_menu(manager, name);
    ImGui::PopID();
}

void layout_panel::draw_layout_row_actions(layout_manager& manager, const std::string& name, const ImRect& row_rect)
{
    const std::string overwrite_tooltip = fmt::format("Overwrite \"{}\" with the current layout", name);
    if(draw_layout_row_action(calc_layout_row_action_rect(row_rect, 1),
                              "##overwrite",
                              ICON_MDI_CONTENT_SAVE_OUTLINE,
                              overwrite_tooltip.c_str(),
                              false))
    {
        manager.save_preset(name);
    }
    const std::string delete_tooltip = fmt::format("Delete \"{}\"", name);
    if(draw_layout_row_action(calc_layout_row_action_rect(row_rect, 0),
                              "##delete",
                              ICON_MDI_DELETE_OUTLINE,
                              delete_tooltip.c_str(),
                              true))
    {
        confirm_delete(name);
    }
}

void layout_panel::draw_layout_context_menu(layout_manager& manager, const std::string& name)
{
    if(!ImGui::BeginPopup(LAYOUT_CONTEXT_MENU_ID))
    {
        return;
    }
    {
        ImGui::ContextMenuStyleScope style_scope;
        if(ImGui::MenuItemIcon(ICON_MDI_CHECK, "Apply"))
        {
            manager.load_preset(name);
        }
        if(ImGui::MenuItemIcon(ICON_MDI_CONTENT_SAVE_OUTLINE, "Overwrite with Current Layout"))
        {
            manager.save_preset(name);
        }
        ImGui::Separator();
        if(ImGui::MenuItemIcon(ICON_MDI_DELETE_OUTLINE, "Delete..."))
        {
            confirm_delete(name);
        }
    }
    ImGui::EndPopup();
}

void layout_panel::confirm_delete(const std::string& name)
{
    imgui_panels* panels = parent_;
    ImBox::ShowDeleteConfirmation("Delete Layout?",
                                  fmt::format("Delete layout preset \"{}\"?\n\nThis cannot be undone.", name),
                                  [panels, name](ImBox::ModalResult result) -> void
                                  {
                                      if(result == ImBox::ModalResult::Delete)
                                      {
                                          panels->get_layout_manager().delete_preset(name);
                                      }
                                  });
}

} // namespace unravel
