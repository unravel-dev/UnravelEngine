/*
 * Validation suite for key and button states between frames, and for positions in a window whose pixels are not
 * its points.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "input states"
 *
 * A frame reads each key and button as up, pressed, down or released. This pins:
 *   - a key or button held over frames reads pressed, then down, then released, then up;
 *   - one let go in the frame it went down reads pressed that frame and released the next, so a tap shorter
 *     than a frame counts;
 *   - pressed again in that frame, it stays pressed;
 *   - the mouse reads where its button was pressed in the frame of the press, though the pointer moved on before
 *     the frame read it, so a swipe that starts and ends between two frames keeps its length; the frame's motion
 *     still runs from where the last frame left the mouse;
 *   - without a work zone, positions in the window zone (points) are scaled to the window's pixels; with one,
 *     the work zone's reference size applies, as in the editor's Game panel;
 *   - a press anywhere in a window whose pixels outnumber its points counts as inside it;
 *   - a press in another window (an editor panel floating in its own) does not move the pointer.
 */

#include "../tests.h"

#include <engine/input/action_map/button_state_map.hpp>
#include <engine/input/action_map/impl/os_input_manager.hpp>
#include <engine/input/action_map/key_state_map.hpp>
#include <engine/input/action_map/mouse_action_map.hpp>
#include <engine/input/action_map/mouse_button.hpp>

#include <cstdio>
#include <string>

using namespace unravel;

namespace
{

int g_checks = 0;
int g_failures = 0;

constexpr uint32_t k_left_button = 0;
/// A window of 640 x 360 points drawn in 1280 x 720 pixels, as at a device pixel ratio of 2.
const input::zone k_window_points{0, 0, 640, 360};
const input::input_reference_size k_window_pixels{1280.0f, 720.0f};
/// The OS ids of the game's window and of another one (an editor panel floating in a window of its own).
constexpr uint32_t k_game_window_id = 1;
constexpr uint32_t k_other_window_id = 2;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

auto read_key(const input::key_state_map& keys) -> input::key_state
{
    return keys.get_state(input::key_code::a, input::key_state::up);
}

/// A left button event made from a finger, at @p window_x, @p window_y in the window's points.
auto make_touch_button_event(bool is_down, int window_x, int window_y) -> os::event
{
    os::event e{};
    e.type = os::events::mouse_button;
    e.button.button = os::mouse::button::left;
    e.button.state_id = is_down ? os::state::pressed : os::state::released;
    e.button.is_from_touch = true;
    e.button.x = window_x;
    e.button.y = window_y;
    return e;
}

auto read_mouse_motion(const input::os_input_manager& manager) -> input::coord
{
    const auto& mouse = manager.get_mouse();
    return {static_cast<int>(mouse.get_axis_value(static_cast<uint32_t>(input::mouse_axis::x))),
            static_cast<int>(mouse.get_axis_value(static_cast<uint32_t>(input::mouse_axis::y)))};
}

void test_held_key()
{
    std::printf("test_held_key\n");
    input::key_state_map keys;
    keys.update();
    keys.set_state(input::key_code::a, input::key_state::pressed);
    check(read_key(keys) == input::key_state::pressed, "a key reads pressed in the frame it went down");
    keys.update();
    check(read_key(keys) == input::key_state::down, "and down in the next");
    keys.update();
    keys.set_state(input::key_code::a, input::key_state::released);
    check(read_key(keys) == input::key_state::released, "released in the frame it was let go");
    keys.update();
    check(read_key(keys) == input::key_state::up, "and up after");
}

void test_key_tap_within_a_frame()
{
    std::printf("test_key_tap_within_a_frame\n");
    input::key_state_map keys;
    keys.update();
    keys.set_state(input::key_code::a, input::key_state::pressed);
    keys.set_state(input::key_code::a, input::key_state::released);
    check(read_key(keys) == input::key_state::pressed, "a key let go in the frame it went down reads pressed");
    keys.update();
    check(read_key(keys) == input::key_state::released, "and released in the next frame");
    keys.update();
    check(read_key(keys) == input::key_state::up, "then up");
    keys.set_state(input::key_code::a, input::key_state::pressed);
    keys.set_state(input::key_code::a, input::key_state::released);
    keys.set_state(input::key_code::a, input::key_state::pressed);
    check(read_key(keys) == input::key_state::pressed, "pressed again in that frame, it still reads pressed");
    keys.update();
    check(read_key(keys) == input::key_state::down, "and down in the next frame, held");
}

void test_button_tap_within_a_frame()
{
    std::printf("test_button_tap_within_a_frame\n");
    input::button_state_map buttons;
    buttons.update();
    buttons.set_state(k_left_button, input::button_state::pressed);
    buttons.set_state(k_left_button, input::button_state::released);
    check(buttons.get_state(k_left_button, input::button_state::up) == input::button_state::pressed,
          "a click within a frame reads pressed");
    buttons.update();
    check(buttons.get_state(k_left_button, input::button_state::up) == input::button_state::released,
          "and released in the next frame");
}

void test_positions_in_window_pixels()
{
    std::printf("test_positions_in_window_pixels\n");
    input::os_input_manager manager;
    manager.set_window_zone(k_window_points);
    manager.set_window_pixel_size(k_window_pixels);
    const auto remapped = manager.remap_to_work_zone({100, 50});
    check(remapped.x == 200 && remapped.y == 100, "a position in points is scaled to the window's pixels");
    const auto back = manager.remap_from_work_zone({200, 100});
    check(back.x == 100 && back.y == 50, "and back");
    manager.set_work_zone({0, 0, 320, 180});
    manager.set_reference_size({960.0f, 540.0f});
    const auto in_view = manager.remap_to_work_zone({100, 50});
    check(in_view.x == 300 && in_view.y == 150, "a work zone scales to its own reference size");
}

void test_press_reads_where_it_happened()
{
    std::printf("test_press_reads_where_it_happened\n");
    input::os_input_manager manager;
    manager.init();
    manager.set_window_zone(k_window_points);
    manager.set_window_pixel_size(k_window_pixels);
    manager.set_mouse_simulated_with_touches(true);
    manager.before_events_update();
    const input::coord polled = manager.get_mouse().get_position();
    manager.on_os_event(make_touch_button_event(true, 100, 50));
    manager.on_os_event(make_touch_button_event(false, 300, 50));
    const input::coord pressed = manager.get_mouse().get_position();
    check(pressed.x == 200 && pressed.y == 100, "a press reads where it happened, in the window's pixels");
    check(manager.get_mouse().is_pressed(static_cast<uint32_t>(input::mouse_button::left_button)),
          "and the button reads pressed that frame");
    const input::coord motion = read_mouse_motion(manager);
    check(motion.x == pressed.x - polled.x && motion.y == polled.y - pressed.y,
          "the frame's motion runs from where the frame read the pointer to the press");
    manager.after_events_update();
    manager.before_events_update();
    check(manager.get_mouse().is_released(static_cast<uint32_t>(input::mouse_button::left_button)),
          "a release in the frame of its press reads in the next frame");
}

void test_press_anywhere_in_a_window_of_more_pixels_than_points()
{
    std::printf("test_press_anywhere_in_a_window_of_more_pixels_than_points\n");
    input::os_input_manager manager;
    manager.init();
    manager.set_window_zone(k_window_points);
    manager.set_window_pixel_size(k_window_pixels);
    manager.set_mouse_simulated_with_touches(true);
    manager.before_events_update();
    manager.on_os_event(make_touch_button_event(true, 600, 340));
    const input::coord pressed = manager.get_mouse().get_position();
    check(pressed.x == 1200 && pressed.y == 680, "a press near the window's far corner reads in its pixels");
    check(manager.get_mouse().is_pressed(static_cast<uint32_t>(input::mouse_button::left_button)),
          "and inside the window, whose pixels outnumber its points");
}

void test_press_in_another_window_is_not_placed()
{
    std::printf("test_press_in_another_window_is_not_placed\n");
    input::os_input_manager manager;
    manager.init();
    manager.set_window_zone(k_window_points);
    manager.set_window_pixel_size(k_window_pixels);
    manager.set_input_window(k_game_window_id);
    manager.set_mouse_simulated_with_touches(true);
    manager.before_events_update();
    const input::coord polled = manager.get_mouse().get_position();
    auto elsewhere = make_touch_button_event(true, 100, 50);
    elsewhere.button.window_id = k_other_window_id;
    manager.on_os_event(elsewhere);
    const input::coord after_elsewhere = manager.get_mouse().get_position();
    check(after_elsewhere.x == polled.x && after_elsewhere.y == polled.y,
          "a press in another window leaves the pointer where the frame read it: its coordinates are that window's");
    auto own = make_touch_button_event(true, 100, 50);
    own.button.window_id = k_game_window_id;
    manager.on_os_event(own);
    const input::coord after_own = manager.get_mouse().get_position();
    check(after_own.x == 200 && after_own.y == 100, "one in the game's window is placed where it happened");
}

auto run_input_states_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_held_key();
    test_key_tap_within_a_frame();
    test_button_tap_within_a_frame();
    test_positions_in_window_pixels();
    test_press_reads_where_it_happened();
    test_press_anywhere_in_a_window_of_more_pixels_than_points();
    test_press_in_another_window_is_not_placed();
    std::printf("input states: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("input states", run_input_states_suite)
