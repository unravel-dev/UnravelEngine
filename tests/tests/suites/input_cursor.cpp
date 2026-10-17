/*
 * Validation suite for the cursor hold (os_cursor; scripts' Cursor.lockState, Cursor.visible).
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "input cursor"
 *
 * The lock holds the OS cursor only while the game has input, so a locked cursor can never keep the
 * user from the editor or another application. This pins when it applies, where it puts the cursor and
 * when it is hidden:
 *   - locked holds and hides the cursor while the game has input;
 *   - a host suspension, input not allowed, a lost window focus or a virtual pointer each free it;
 *   - locked puts the cursor at the work zone's center, confined keeps it inside the zone;
 *   - hidden by scripts is reported once as a visibility change, and shown again when freed;
 *   - the input manager reports its input conditions to its cursor.
 * Nothing here calls os_cursor::apply or runs a frame, so the real cursor is never moved.
 */

#include "../tests.h"

#include <engine/input/action_map/impl/os_input_manager.hpp>

#include <cstdio>
#include <string>

using namespace unravel;

namespace
{

int g_checks = 0;
int g_failures = 0;

/// A work zone away from the screen origin, with an even size so its center is exact.
const input::zone k_cursor_work_zone{100, 50, 400, 300};

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

auto make_window_event(os::window_event_id type) -> os::event
{
    os::event e{};
    e.type = os::events::window;
    e.window.type = type;
    return e;
}

auto is_held_at(const hpp::optional<input::coord>& position, int x, int y) -> bool
{
    return position && position->x == x && position->y == y;
}

void test_lock_holds_while_the_game_has_input()
{
    std::printf("test_lock_holds_while_the_game_has_input\n");
    input::os_cursor cursor;
    check(!cursor.is_lock_active() && !cursor.is_hidden(), "a free cursor is neither held nor hidden");

    cursor.set_lock_mode(input::cursor_lock_mode::locked);
    check(cursor.is_lock_active(), "a locked cursor is held while the game has input");
    check(cursor.is_hidden(), "and hidden");

    cursor.set_suspended(true);
    check(!cursor.is_lock_active() && !cursor.is_hidden(), "a suspended lock frees and shows the cursor");
    check(cursor.get_lock_mode() == input::cursor_lock_mode::locked, "and keeps what the script asked for");
    cursor.set_suspended(false);

    cursor.set_input_allowed(false);
    check(!cursor.is_lock_active() && !cursor.is_hidden(), "nor is it held while the game has no input");
    cursor.set_input_allowed(true);

    cursor.set_window_focus(false);
    check(!cursor.is_lock_active(), "nor while no window of the application has focus");
    cursor.set_window_focus(true);
    check(cursor.is_lock_active(), "and again once one has");

    cursor.set_pointer_overridden(true);
    check(!cursor.is_lock_active(), "a virtual pointer drives the pointer instead of the lock");
    cursor.set_pointer_overridden(false);

    cursor.set_lock_mode(input::cursor_lock_mode::confined);
    check(cursor.is_lock_active() && !cursor.is_hidden(), "a confined cursor is held but shown");
}

void test_the_hold_puts_the_cursor_in_the_work_zone()
{
    std::printf("test_the_hold_puts_the_cursor_in_the_work_zone\n");
    input::os_cursor cursor;
    check(!cursor.get_held_position({10, 10}, k_cursor_work_zone), "a free cursor stays where it is");

    cursor.set_lock_mode(input::cursor_lock_mode::locked);
    check(is_held_at(cursor.get_held_position({10, 10}, k_cursor_work_zone), 300, 200), "a locked cursor goes to the zone's center");
    check(!cursor.get_held_position({300, 200}, k_cursor_work_zone), "and stays there once it is");

    cursor.set_lock_mode(input::cursor_lock_mode::confined);
    check(!cursor.get_held_position({250, 120}, k_cursor_work_zone), "a confined cursor inside the zone stays");
    check(is_held_at(cursor.get_held_position({10, 120}, k_cursor_work_zone), 100, 120), "one left of the zone comes to its edge");
    check(is_held_at(cursor.get_held_position({900, 900}, k_cursor_work_zone), 499, 349),
          "one beyond its far corner comes to its last pixel");

    cursor.set_suspended(true);
    check(!cursor.get_held_position({900, 900}, k_cursor_work_zone), "a suspended hold leaves the cursor alone");
}

void test_visibility_changes_are_reported_once()
{
    std::printf("test_visibility_changes_are_reported_once\n");
    input::os_cursor cursor;
    check(!cursor.take_visibility_change().has_value(), "nothing to report at first");

    cursor.set_visible(false);
    const auto hidden = cursor.take_visibility_change();
    check(hidden.has_value() && !*hidden, "hiding is reported");
    check(!cursor.take_visibility_change().has_value(), "once");

    cursor.set_suspended(true);
    const auto shown = cursor.take_visibility_change();
    check(shown.has_value() && *shown, "a suspension shows the cursor again");
}

void test_the_manager_reports_the_game_input()
{
    std::printf("test_the_manager_reports_the_game_input\n");
    input::os_input_manager manager;
    manager.init();
    auto& cursor = manager.get_cursor();
    cursor.set_lock_mode(input::cursor_lock_mode::locked);
    check(cursor.is_lock_active(), "the manager's cursor holds while its game has input");

    manager.set_is_input_allowed(false);
    check(!cursor.is_lock_active(), "input not allowed reaches the cursor");
    manager.set_is_input_allowed(true);

    manager.on_os_event(make_window_event(os::window_event_id::focus_lost));
    check(!cursor.is_lock_active(), "a lost window focus reaches the cursor");
    manager.on_os_event(make_window_event(os::window_event_id::focus_gained));
    check(cursor.is_lock_active(), "and a regained one");

    manager.set_virtual_mouse_position(input::coord{10, 10});
    check(!cursor.is_lock_active(), "a virtual pointer reaches the cursor");
    manager.set_virtual_mouse_position({});
    check(cursor.is_lock_active(), "and its release");
}

auto run_input_cursor_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_lock_holds_while_the_game_has_input();
    test_the_hold_puts_the_cursor_in_the_work_zone();
    test_visibility_changes_are_reported_once();
    test_the_manager_reports_the_game_input();
    std::printf("input cursor: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("input cursor", run_input_cursor_suite)
