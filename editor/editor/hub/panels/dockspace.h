#pragma once

namespace unravel
{
class dockspace
{
public:
    void on_frame_ui_render(float headerSize, float footerSize);

    void execute_dock_builder_order_and_focus_workaround();
    void refresh();

    int reset_focus_counter_{-1};
};
} // namespace unravel
