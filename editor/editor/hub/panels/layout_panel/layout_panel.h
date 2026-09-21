#pragma once

#include "../panel_base.h"

#include <imgui/imgui_internal.h>

#include <array>
#include <string>

namespace unravel
{
class imgui_panels;
class layout_manager;

/// The saved arrangements of the editor panels: a toolbar to save, reset and find them on disk,
/// and a list where a click applies one.
class layout_panel : public panel_base
{
public:
    layout_panel(imgui_panels* parent, const char* name);

    void draw_ui(rtti::context& ctx) override;

private:
    auto get_window_flags() const -> ImGuiWindowFlags override;

    void draw_toolbar(layout_manager& manager);
    void draw_save_dropdown(layout_manager& manager, bool is_compact);
    void draw_save_form(layout_manager& manager);
    void draw_layout_list(layout_manager& manager);
    void draw_layout_row(layout_manager& manager, const std::string& name, int row_index);
    void draw_layout_row_actions(layout_manager& manager, const std::string& name, const ImRect& row_rect);
    void draw_layout_context_menu(layout_manager& manager, const std::string& name);
    void confirm_delete(const std::string& name);

    imgui_panels* parent_{};
    std::array<char, 128> new_layout_name_{};
};
} // namespace unravel
