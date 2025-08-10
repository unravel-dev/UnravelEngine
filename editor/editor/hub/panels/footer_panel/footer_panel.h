#pragma once
#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <functional>
#include <chrono>

namespace unravel
{
class footer_panel
{
public:
    void on_frame_ui_render(rtti::context& ctx, float footer_size, const std::function<void()>& on_draw = {});

private:
    void draw_footer_child(rtti::context& ctx, float footer_size, const std::function<void()>& on_draw);

    std::chrono::steady_clock::time_point last_notification_time_;
};
} // namespace unravel
