#include "os_cursor.hpp"

#include <ospp/mouse.h>

#include <algorithm>

namespace input
{

void os_cursor::set_lock_mode(cursor_lock_mode mode)
{
    lock_mode_ = mode;
}

auto os_cursor::get_lock_mode() const -> cursor_lock_mode
{
    return lock_mode_;
}

void os_cursor::set_visible(bool visible)
{
    is_visible_ = visible;
}

auto os_cursor::is_visible() const -> bool
{
    return is_visible_;
}

void os_cursor::set_suspended(bool suspended)
{
    is_suspended_ = suspended;
}

void os_cursor::set_input_allowed(bool allowed)
{
    is_input_allowed_ = allowed;
}

void os_cursor::set_window_focus(bool has_focus)
{
    has_window_focus_ = has_focus;
}

void os_cursor::set_pointer_overridden(bool overridden)
{
    is_pointer_overridden_ = overridden;
}

auto os_cursor::has_game_input() const -> bool
{
    return is_input_allowed_ && has_window_focus_ && !is_suspended_;
}

auto os_cursor::is_lock_active() const -> bool
{
    return lock_mode_ != cursor_lock_mode::none && has_game_input() && !is_pointer_overridden_;
}

auto os_cursor::is_hidden() const -> bool
{
    return has_game_input() && (!is_visible_ || lock_mode_ == cursor_lock_mode::locked);
}

auto os_cursor::take_visibility_change() -> hpp::optional<bool>
{
    const bool is_shown = !is_hidden();
    if(is_shown == is_shown_)
    {
        return {};
    }
    is_shown_ = is_shown;
    return is_shown;
}

auto os_cursor::get_held_position(coord global_pos, const zone& work_zone) const -> hpp::optional<coord>
{
    if(!is_lock_active())
    {
        return {};
    }
    coord held = global_pos;
    if(lock_mode_ == cursor_lock_mode::locked)
    {
        held = {work_zone.x + work_zone.w / 2, work_zone.y + work_zone.h / 2};
    }
    else
    {
        held.x = std::clamp(global_pos.x, work_zone.x, work_zone.x + std::max(work_zone.w - 1, 0));
        held.y = std::clamp(global_pos.y, work_zone.y, work_zone.y + std::max(work_zone.h - 1, 0));
    }
    if(held == global_pos)
    {
        return {};
    }
    return held;
}

auto os_cursor::apply(coord global_pos, const zone& work_zone) -> hpp::optional<coord>
{
    const auto held = get_held_position(global_pos, work_zone);
    if(held)
    {
        os::mouse::set_position({held->x, held->y});
    }
    return held;
}

} // namespace input
