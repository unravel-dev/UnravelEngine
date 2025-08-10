#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>


namespace unravel
{
class imgui_panels;

class style_panel
{
public:
    style_panel(imgui_panels* parent);
    void show(bool show = true);

    void init(rtti::context& ctx);
    void on_frame_ui_render();

    // Theme setters
    void set_unity_theme();
    void set_unity_inspired_theme();
    void set_modern_purple_theme();
    void set_warm_amber_theme();
    void set_cool_blue_theme();
    void set_minimalist_green_theme();
    void set_professional_dark_theme();
    void set_dark_theme();
    void set_dark_theme_red();
    void set_photoshop_theme();

private:
    imgui_panels* parent_;
    bool visible_ = false;
};
} // namespace unravel