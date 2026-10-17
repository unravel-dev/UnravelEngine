#pragma once

#include "../coord.hpp"

#include <hpp/optional.hpp>

#include <cstdint>

namespace input
{
/// How the cursor is held while the game has input (os_cursor::set_lock_mode).
enum class cursor_lock_mode : uint8_t
{
    /// The cursor moves freely.
    none,
    /// The cursor stays at the center of the work zone, hidden; the mouse axes still report motion.
    locked,
    /// The cursor cannot leave the work zone.
    confined,
};

/**
 * @brief The OS cursor as the game asks for it: held at the center of the work zone (locked) or inside
 * it (confined), shown or hidden.
 *
 * A request applies only while the game has input: input is allowed, one of the application's windows
 * has focus, the host has not suspended it and no virtual pointer stands in for the mouse. So a held
 * cursor never keeps the user from the editor or another application. The input manager reports those
 * conditions as they change and calls apply() once per frame, after it read the frame's motion.
 */
class os_cursor
{
public:
    /// How scripts want the cursor held.
    void set_lock_mode(cursor_lock_mode mode);
    auto get_lock_mode() const -> cursor_lock_mode;
    /// Whether scripts want the cursor shown; a locked cursor is hidden either way.
    void set_visible(bool visible);
    auto is_visible() const -> bool;
    /**
     * @brief Frees the cursor without changing what scripts asked for: the editor suspends it between
     * plays, while paused, while the game view is not focused and after Escape.
     */
    void set_suspended(bool suspended);
    /// Whether the game reads input at all (the input manager's input allowed).
    void set_input_allowed(bool allowed);
    /// Whether one of the application's windows has focus.
    void set_window_focus(bool has_focus);
    /// Whether a virtual pointer stands in for the mouse (synthesized input): it drives the pointer instead.
    void set_pointer_overridden(bool overridden);

    /// The lock or the confinement holds the cursor now.
    auto is_lock_active() const -> bool;
    /// The cursor is hidden now: hidden by scripts or locked, while the game has input.
    auto is_hidden() const -> bool;
    /// The visibility the window should take, when it changed since the last call.
    auto take_visibility_change() -> hpp::optional<bool>;

    /**
     * @brief Where the hold puts a cursor found at @p global_pos (global pixels) inside @p work_zone, or
     * nothing when it may stay where it is.
     */
    auto get_held_position(coord global_pos, const zone& work_zone) const -> hpp::optional<coord>;
    /**
     * @brief Moves the OS cursor where the hold wants it. Call after this frame's motion was read from
     * @p global_pos: the next frame measures from where the cursor is put.
     * @return Where the cursor was put, or nothing when it stayed.
     */
    auto apply(coord global_pos, const zone& work_zone) -> hpp::optional<coord>;

private:
    auto has_game_input() const -> bool;

    cursor_lock_mode lock_mode_{cursor_lock_mode::none};
    bool is_visible_{true};
    bool is_suspended_{};
    bool is_input_allowed_{true};
    bool has_window_focus_{true};
    bool is_pointer_overridden_{};
    /// The visibility last handed out by take_visibility_change.
    bool is_shown_{true};
};

} // namespace input
