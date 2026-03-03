#pragma once

#include "../panel_base.h"

#include <array>

namespace unravel
{
class imgui_panels;

class layout_panel : public panel_base
{
public:
    layout_panel(imgui_panels* parent, const char* name);

    void draw_ui(rtti::context& ctx) override;

private:
    auto get_window_flags() const -> ImGuiWindowFlags override;

    void draw_create_popup();
    void draw_presets_list();

    imgui_panels* parent_{};
    bool open_create_popup_{};
    std::array<char, 128> create_name_buf_{};
};
} // namespace unravel
