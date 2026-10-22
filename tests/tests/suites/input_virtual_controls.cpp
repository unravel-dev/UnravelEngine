/*
 * Validation suite for on-screen control input: the virtual gamepad scripts drive, and the pointer zones
 * that take the fingers and mouse presses landing on on-screen controls.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "input virtual controls"
 *
 * Pins:
 *   - what scripts set on the virtual gamepad reads from the next frame on; a press let go within one
 *     frame still reads pressed, then released; releasing all input lets go at once;
 *   - the action map reads the virtual gamepad as a gamepad, and the input manager lists it;
 *   - a finger or a left mouse press that lands in a free zone is the zone's until it lifts: the game's
 *     touches and mouse button never see it, nor the platform's mouse press made from that finger;
 *   - where zones overlap the newest free one takes a press; a held zone takes no second pointer;
 *   - a zone's mouse drag and a finger's motion read as no mouse motion;
 *   - fingers press the mouse only while it is simulated with touches.
 * Frames advance with a virtual mouse position, so the real cursor is never read or moved.
 */

#include "../tests.h"

#include <engine/input/action_map/action_map.hpp>
#include <engine/input/action_map/impl/os_input_manager.hpp>
#include <engine/input/action_map/impl/pointer_zones.hpp>
#include <engine/input/action_map/virtual_gamepad.hpp>

#include <algorithm>
#include <cstdio>
#include <string>

using namespace unravel;

namespace
{

int g_checks = 0;
int g_failures = 0;

constexpr uint64_t k_device = 7;
/// The window in global pixels, and the work zone (a game view) inside it.
const input::zone k_window_zone{100, 50, 400, 300};
const input::zone k_work_zone{200, 100, 200, 150};
/// Around the window center, (100, 100) in the work zone.
const input::zone k_stick_area{80, 80, 40, 40};
/// The window center in global pixels; (100, 100) in the work zone.
const input::coord k_center_global{300, 200};

constexpr auto k_left_x = input::gamepad_axis::left_x;
constexpr auto k_south = input::gamepad_button::south;
constexpr auto k_east = input::gamepad_button::east;
constexpr auto k_left_button = static_cast<uint32_t>(input::mouse_button::left_button);

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

auto is_at(const input::coord& position, int x, int y) -> bool
{
    return position.x == x && position.y == y;
}

auto make_finger_event(os::events type, uint64_t finger_id, float x, float y) -> os::event
{
    os::event e{};
    e.type = type;
    e.finger.touch_id = k_device;
    e.finger.finger_id = finger_id;
    e.finger.x = x;
    e.finger.y = y;
    e.finger.pressure = 1.0f;
    return e;
}

/// A left button event; one from touch carries the window position the platform puts it at.
auto make_left_button_event(bool is_down, bool is_from_touch, int window_x = 0, int window_y = 0) -> os::event
{
    os::event e{};
    e.type = os::events::mouse_button;
    e.button.button = os::mouse::button::left;
    e.button.state_id = is_down ? os::state::pressed : os::state::released;
    e.button.is_from_touch = is_from_touch;
    e.button.x = window_x;
    e.button.y = window_y;
    return e;
}

auto make_touch_motion_event() -> os::event
{
    os::event e{};
    e.type = os::events::mouse_motion;
    e.motion.is_from_touch = true;
    return e;
}

/// A manager with the window and work zones, a virtual pointer at @p pointer, in its first frame.
void start_manager(input::os_input_manager& manager, input::coord pointer)
{
    manager.init();
    manager.set_window_zone(k_window_zone);
    manager.set_work_zone(k_work_zone);
    manager.set_virtual_mouse_position(pointer);
    manager.before_events_update();
}

/// The frame boundary with the virtual pointer at @p pointer.
void next_frame(input::os_input_manager& manager, input::coord pointer)
{
    manager.after_events_update();
    manager.set_virtual_mouse_position(pointer);
    manager.before_events_update();
}

auto get_mouse_motion(const input::os_input_manager& manager) -> input::coord
{
    const auto& mouse = manager.get_mouse();
    return {static_cast<int>(mouse.get_axis_value(static_cast<uint32_t>(input::mouse_axis::x))),
            static_cast<int>(mouse.get_axis_value(static_cast<uint32_t>(input::mouse_axis::y)))};
}

void test_the_virtual_gamepad_applies_at_the_frame_boundary()
{
    input::virtual_gamepad pad;
    pad.set_axis(k_left_x, 0.5f);
    pad.set_button(k_south, true);
    check(pad.get_axis_value(static_cast<uint32_t>(k_left_x)) == 0.0f && !pad.is_down(static_cast<uint32_t>(k_south)),
          "nothing set reads before the frame boundary");

    pad.update();
    check(pad.get_axis_value(static_cast<uint32_t>(k_left_x)) == 0.5f, "the axis reads from the next frame");
    check(pad.is_pressed(static_cast<uint32_t>(k_south)), "the button reads pressed");

    pad.update();
    check(!pad.is_pressed(static_cast<uint32_t>(k_south)) && pad.is_down(static_cast<uint32_t>(k_south)),
          "then held while it stays down");

    pad.set_button(k_south, false);
    pad.update();
    check(pad.is_released(static_cast<uint32_t>(k_south)), "letting go reads released");

    pad.update();
    check(pad.get_button_state(static_cast<uint32_t>(k_south)) == input::button_state::up, "then up");

    pad.set_axis(k_left_x, 3.0f);
    pad.update();
    check(pad.get_axis_value(static_cast<uint32_t>(k_left_x)) == 1.0f, "axes clamp to 1");
}

void test_a_press_let_go_within_a_frame_still_reads()
{
    input::virtual_gamepad pad;
    pad.set_button(k_east, true);
    pad.set_button(k_east, false);
    pad.update();
    check(pad.is_pressed(static_cast<uint32_t>(k_east)), "a press let go within the frame still reads pressed");

    pad.update();
    check(pad.is_released(static_cast<uint32_t>(k_east)), "and released the frame after");
}

void test_release_all_lets_go_at_once()
{
    input::virtual_gamepad pad;
    pad.set_axis(k_left_x, -1.0f);
    pad.set_button(k_south, true);
    pad.update();
    pad.release_all();
    check(pad.get_axis_value(static_cast<uint32_t>(k_left_x)) == 0.0f && !pad.is_down(static_cast<uint32_t>(k_south)),
          "release_all centers the axes and lets go of the buttons at once");

    pad.update();
    check(pad.get_axis_value(static_cast<uint32_t>(k_left_x)) == 0.0f && !pad.is_down(static_cast<uint32_t>(k_south)),
          "and what was set before it stays let go");
}

void test_the_action_map_reads_the_virtual_gamepad()
{
    input::action_map map;
    map.map("Horizontal", input::gamepad_axis::left_x);
    map.map("Vertical", input::gamepad_axis::left_y, input::axis_range::full, 1.0f, -1.0f);
    map.map("Jump", input::gamepad_button::south);

    input::virtual_gamepad pad;
    pad.set_axis(input::gamepad_axis::left_x, 0.25f);
    pad.set_axis(input::gamepad_axis::left_y, -1.0f);
    pad.set_button(input::gamepad_button::south, true);
    pad.update();
    check(map.get_analog_value("Horizontal", pad) == 0.25f, "an action mapped to a stick axis reads it");
    check(map.get_analog_value("Vertical", pad) == 1.0f, "a stick pushed up reads +1 through an inverted mapping");
    check(map.is_pressed("Jump", pad), "an action mapped to a button reads its press");

    input::os_input_manager manager;
    start_manager(manager, k_center_global);
    const auto& devices = manager.get_all_devices();
    const bool is_listed = std::any_of(devices.begin(),
                                       devices.end(),
                                       [&](const auto& device)
                                       {
                                           return device.get() == &manager.get_virtual_gamepad();
                                       });
    check(is_listed, "the input manager lists the virtual gamepad with its devices");
    manager.get_virtual_gamepad().set_button(input::gamepad_button::south, true);
    next_frame(manager, k_center_global);
    check(map.is_pressed("Jump", manager.get_virtual_gamepad()), "and applies it at its frame boundary");
    manager.set_virtual_mouse_position({});
}

void test_overlapping_zones_take_the_newest_free_one()
{
    input::pointer_zones zones;
    const auto older = zones.create();
    const auto newer = zones.create();
    zones.set_area(older, k_stick_area);
    zones.set_area(newer, k_stick_area);
    check(zones.take_finger(k_device, 1, {100, 100}, 1.0f) && zones.get_pointer(newer) != nullptr,
          "the newest zone at a point takes a finger there");
    check(zones.take_finger(k_device, 2, {100, 100}, 1.0f) && zones.get_pointer(older) != nullptr,
          "a held zone leaves the next finger to the free one");
    check(!zones.take_finger(k_device, 3, {100, 100}, 1.0f), "with every zone held, nothing takes it");
    check(!zones.take_finger(k_device, 4, {10, 10}, 1.0f), "nor does any zone take a finger outside them");

    zones.destroy(newer);
    check(!zones.move_finger(k_device, 1, {101, 100}, 1.0f), "a removed zone's pointer is nobody's");
    check(zones.move_finger(k_device, 2, {102, 100}, 1.0f) && zones.get_pointer(older)->delta.x == 2,
          "the other zone's pointer still moves it");

    const auto empty = zones.create();
    check(zones.get_pointer(empty) == nullptr && !zones.take_finger(k_device, 5, {0, 0}, 1.0f),
          "a zone without an area takes nothing");
}

void test_a_zone_takes_a_finger_from_the_game()
{
    input::os_input_manager manager;
    start_manager(manager, k_center_global);
    auto& zones = manager.get_pointer_zones();
    const auto stick = zones.create();
    zones.set_area(stick, k_stick_area);

    // The platform's press made from the finger comes first, at the window position the finger lands on.
    manager.on_os_event(make_left_button_event(true, true, 200, 150));
    manager.on_os_event(make_finger_event(os::events::finger_down, 1, 0.5f, 0.5f));
    check(manager.get_touches().get_count() == 0, "a finger landing in a zone is not among the game's touches");
    check(manager.get_mouse().get_button_state(k_left_button) == input::button_state::up,
          "nor does the mouse press made from it reach the game");
    const input::touch_point* pointer = zones.get_pointer(stick);
    check(pointer != nullptr && pointer->phase == input::touch_phase::began && is_at(pointer->position, 100, 100),
          "the zone holds it where it landed");
    check(!zones.is_mouse_pointer(stick), "as a finger");

    manager.on_os_event(make_finger_event(os::events::finger_down, 2, 0.5f, 0.5f));
    check(manager.get_touches().get_count() == 1, "a second finger on the held zone is the game's");

    next_frame(manager, k_center_global);
    manager.on_os_event(make_finger_event(os::events::finger_motion, 1, 0.75f, 0.5f));
    pointer = zones.get_pointer(stick);
    check(pointer->phase == input::touch_phase::moved && is_at(pointer->delta, 100, 0),
          "its motion is the zone's, in work zone pixels");
    check(manager.get_touches().get(0).phase == input::touch_phase::stationary, "and not the game finger's");

    manager.on_os_event(make_finger_event(os::events::finger_up, 1, 0.75f, 0.5f));
    manager.on_os_event(make_left_button_event(false, true, 300, 150));
    check(zones.get_pointer(stick)->phase == input::touch_phase::ended, "it ends in the zone");
    check(manager.get_mouse().get_button_state(k_left_button) == input::button_state::up,
          "the release of the taken press does not reach the game either");

    next_frame(manager, k_center_global);
    check(zones.get_pointer(stick) == nullptr, "the zone is free the frame after");
    manager.set_virtual_mouse_position({});
}

void test_a_zone_takes_a_left_mouse_press()
{
    input::os_input_manager manager;
    start_manager(manager, k_center_global);
    auto& zones = manager.get_pointer_zones();
    const auto stick = zones.create();
    zones.set_area(stick, k_stick_area);

    manager.on_os_event(make_left_button_event(true, false));
    check(manager.get_mouse().get_button_state(k_left_button) == input::button_state::up,
          "a left press in a zone does not reach the game");
    check(zones.get_pointer(stick) != nullptr && zones.is_mouse_pointer(stick), "the zone holds the mouse");

    next_frame(manager, {k_center_global.x + 10, k_center_global.y + 5});
    const input::touch_point* pointer = zones.get_pointer(stick);
    check(pointer->phase == input::touch_phase::moved && is_at(pointer->delta, 10, 5), "the drag moves the zone's pointer");
    manager.after_events_update();
    check(is_at(get_mouse_motion(manager), 0, 0), "and is no mouse motion");

    manager.set_virtual_mouse_position(input::coord{k_center_global.x + 10, k_center_global.y + 5});
    manager.before_events_update();
    check(zones.get_pointer(stick)->phase == input::touch_phase::stationary, "a mouse that stays is stationary");
    manager.on_os_event(make_left_button_event(false, false));
    check(zones.get_pointer(stick)->phase == input::touch_phase::ended, "the release ends it");
    check(manager.get_mouse().get_button_state(k_left_button) == input::button_state::up,
          "and does not reach the game");

    next_frame(manager, {k_center_global.x + 20, k_center_global.y + 5});
    manager.after_events_update();
    check(is_at(get_mouse_motion(manager), 10, 0), "once let go, the mouse moves for the game again");

    manager.set_virtual_mouse_position(input::coord{k_center_global.x + 100, k_center_global.y});
    manager.before_events_update();
    manager.on_os_event(make_left_button_event(true, false));
    check(manager.get_mouse().get_button_state(k_left_button) == input::button_state::pressed,
          "a press outside every zone is the game's");
    manager.set_virtual_mouse_position({});
}

void test_fingers_never_move_the_mouse()
{
    input::os_input_manager manager;
    start_manager(manager, k_center_global);
    next_frame(manager, {k_center_global.x + 40, k_center_global.y});
    manager.on_os_event(make_touch_motion_event());
    manager.after_events_update();
    check(is_at(get_mouse_motion(manager), 0, 0),
          "the pointer jumping to a finger reads as no motion, though its event came after the jump");
    check(manager.get_mouse().get_position().x == 140, "the pointer is still where the finger is");

    manager.set_virtual_mouse_position(input::coord{k_center_global.x + 50, k_center_global.y});
    manager.before_events_update();
    os::event wheel{};
    wheel.type = os::events::mouse_wheel;
    manager.on_os_event(wheel);
    manager.after_events_update();
    check(is_at(get_mouse_motion(manager), 10, 0), "a real mouse event gives the motion back to the mouse");
    manager.set_virtual_mouse_position({});
}

void test_fingers_press_the_mouse_only_while_simulated()
{
    input::os_input_manager manager;
    start_manager(manager, k_center_global);
    manager.on_os_event(make_left_button_event(true, true, 200, 150));
    check(manager.get_mouse().get_button_state(k_left_button) == input::button_state::pressed,
          "by default a finger presses the mouse");
    manager.on_os_event(make_left_button_event(false, true, 200, 150));

    next_frame(manager, k_center_global);
    manager.set_mouse_simulated_with_touches(false);
    manager.on_os_event(make_left_button_event(true, true, 200, 150));
    check(manager.get_mouse().get_button_state(k_left_button) == input::button_state::up,
          "not while touches do not simulate the mouse");
    manager.on_os_event(make_left_button_event(false, true, 200, 150));
    check(manager.get_mouse().get_button_state(k_left_button) == input::button_state::up,
          "nor does its release read");
    manager.on_os_event(make_left_button_event(true, false));
    check(manager.get_mouse().get_button_state(k_left_button) == input::button_state::pressed,
          "a real mouse still presses");
    manager.set_virtual_mouse_position({});
}

void test_releasing_all_input_frees_the_controls()
{
    input::os_input_manager manager;
    start_manager(manager, k_center_global);
    auto& zones = manager.get_pointer_zones();
    const auto stick = zones.create();
    zones.set_area(stick, k_stick_area);
    manager.on_os_event(make_finger_event(os::events::finger_down, 1, 0.5f, 0.5f));
    manager.get_virtual_gamepad().set_button(k_south, true);
    next_frame(manager, k_center_global);

    manager.release_all();
    check(zones.get_pointer(stick)->phase == input::touch_phase::canceled, "releasing all input cancels a zone's pointer");
    check(!manager.get_virtual_gamepad().is_down(static_cast<uint32_t>(k_south)), "and lets go of the virtual gamepad");
    next_frame(manager, k_center_global);
    check(zones.get_pointer(stick) == nullptr, "the zone is free the frame after");
    manager.set_virtual_mouse_position({});
}

auto run_input_virtual_controls_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_the_virtual_gamepad_applies_at_the_frame_boundary();
    test_a_press_let_go_within_a_frame_still_reads();
    test_release_all_lets_go_at_once();
    test_the_action_map_reads_the_virtual_gamepad();
    test_overlapping_zones_take_the_newest_free_one();
    test_a_zone_takes_a_finger_from_the_game();
    test_a_zone_takes_a_left_mouse_press();
    test_fingers_never_move_the_mouse();
    test_fingers_press_the_mouse_only_while_simulated();
    test_releasing_all_input_frees_the_controls();
    std::printf("input virtual controls: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("input virtual controls", run_input_virtual_controls_suite)
