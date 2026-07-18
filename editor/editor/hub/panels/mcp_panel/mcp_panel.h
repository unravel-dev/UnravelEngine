#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>

namespace unravel
{
class imgui_panels;

class mcp_panel
{
public:
    explicit mcp_panel(imgui_panels* parent);

    void init(rtti::context& ctx);
    void deinit(rtti::context& ctx);

    void on_frame_ui_render(rtti::context& ctx, const char* name);

    void show(bool s);

private:
    void draw_ui(rtti::context& ctx);

    imgui_panels* parent_{};
    bool show_request_{};
    bool show_{false};
    bool auto_scroll_{true};
};
} // namespace unravel
