/*
 * Validation suite for touch input (os_touches; scripts' Input.touchCount and Input.GetTouch).
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "input touch"
 *
 * Pins how fingers read frame by frame:
 *   - a finger begins, is moved or stationary while it stays, and ends (or is canceled) for one frame;
 *   - a finger that touches and leaves within one frame still begins first;
 *   - ids stay the same while a finger touches and are the lowest free numbers;
 *   - the input manager maps the platform's normalized positions to work zone pixels, takes only
 *     fingers that land in the work zone, and cancels every finger when it releases all input.
 * Frames advance with a virtual mouse position, so the real cursor is never read or moved.
 */

#include "../tests.h"

#include <engine/input/action_map/impl/os_input_manager.hpp>
#include <engine/input/action_map/impl/os_touches.hpp>

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

void test_a_finger_lives_frame_by_frame()
{
    input::os_touches touches;
    touches.begin(k_device, 11, {10, 20}, 0.5f);
    check(touches.get_count() == 1, "a finger that touches is counted");
    check(touches.get(0).phase == input::touch_phase::began, "it begins");
    check(is_at(touches.get(0).position, 10, 20), "where it touched");
    check(touches.get(0).pressure == 0.5f, "with its pressure");

    touches.update();
    check(touches.get(0).phase == input::touch_phase::stationary, "it is stationary in a frame without motion");

    touches.move(k_device, 11, {15, 18}, 0.6f);
    touches.move(k_device, 11, {17, 18}, 0.6f);
    check(touches.get(0).phase == input::touch_phase::moved, "it moved");
    check(is_at(touches.get(0).delta, 7, -2), "by the sum of the frame's motion");

    touches.update();
    check(is_at(touches.get(0).delta, 0, 0), "the next frame starts with no motion");

    touches.end(k_device, 11, {18, 19}, false);
    check(touches.get(0).phase == input::touch_phase::ended, "it ends");
    check(is_at(touches.get(0).position, 18, 19), "where it left");

    touches.update();
    check(touches.get_count() == 0, "and is gone the frame after");
}

void test_a_tap_within_one_frame_begins_first()
{
    input::os_touches touches;
    touches.begin(k_device, 3, {5, 5}, 1.0f);
    touches.end(k_device, 3, {6, 5}, false);
    check(touches.get(0).phase == input::touch_phase::began, "a finger that leaves in the frame it touched still begins");

    touches.update();
    check(touches.get_count() == 1 && touches.get(0).phase == input::touch_phase::ended, "its end shows next frame");

    touches.update();
    check(touches.get_count() == 0, "then it is gone");
}

void test_cancels_and_unknown_fingers()
{
    input::os_touches touches;
    touches.move(k_device, 99, {1, 1}, 1.0f);
    touches.end(k_device, 99, {1, 1}, false);
    check(touches.get_count() == 0, "motion and ends of unknown fingers are ignored");

    touches.begin(k_device, 1, {0, 0}, 1.0f);
    touches.update();
    touches.end(k_device, 1, {0, 0}, true);
    check(touches.get(0).phase == input::touch_phase::canceled, "a canceled finger reads canceled");

    touches.update();
    touches.begin(k_device, 2, {0, 0}, 1.0f);
    touches.begin(k_device, 3, {0, 0}, 1.0f);
    touches.cancel_all();
    check(touches.get(0).phase == input::touch_phase::canceled && touches.get(1).phase == input::touch_phase::canceled,
          "cancel_all cancels every finger, ones that began this frame included");
    touches.update();
    check(touches.get_count() == 0, "and they are gone the next frame");
}

void test_ids_are_stable_and_lowest_free()
{
    input::os_touches touches;
    touches.begin(k_device, 100, {0, 0}, 1.0f);
    touches.begin(k_device, 200, {0, 0}, 1.0f);
    check(touches.get(0).id == 0 && touches.get(1).id == 1, "fingers get the lowest free ids");

    touches.update();
    touches.end(k_device, 100, {0, 0}, false);
    touches.update();
    check(touches.get_count() == 1 && touches.get(0).id == 1, "a finger keeps its id while another leaves");

    touches.begin(k_device, 300, {0, 0}, 1.0f);
    check(touches.get(1).id == 0, "a new finger takes the freed id");
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

void test_the_manager_maps_fingers_to_the_work_zone()
{
    input::os_input_manager manager;
    manager.init();
    manager.set_window_zone(k_window_zone);
    manager.set_work_zone(k_work_zone);
    manager.set_virtual_mouse_position(input::coord{0, 0});
    manager.before_events_update();

    // The window center is global (300, 200): (100, 100) in the work zone.
    manager.on_os_event(make_finger_event(os::events::finger_down, 1, 0.5f, 0.5f));
    const auto& touches = manager.get_touches();
    check(touches.get_count() == 1, "a finger in the work zone is the game's");
    check(is_at(touches.get(0).position, 100, 100), "at its work zone position");

    // The window's top-left corner area is outside the game view.
    manager.on_os_event(make_finger_event(os::events::finger_down, 2, 0.05f, 0.05f));
    check(touches.get_count() == 1, "a finger outside the work zone is not");

    manager.before_events_update();
    manager.on_os_event(make_finger_event(os::events::finger_motion, 1, 0.75f, 0.5f));
    check(touches.get(0).phase == input::touch_phase::moved && is_at(touches.get(0).delta, 100, 0),
          "finger motion reaches the touch in work zone pixels");

    manager.release_all();
    check(touches.get(0).phase == input::touch_phase::canceled, "releasing all input cancels the fingers");
    manager.before_events_update();
    check(touches.get_count() == 0, "and they are gone the next frame");
    manager.set_virtual_mouse_position({});
}

auto run_input_touch_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_a_finger_lives_frame_by_frame();
    test_a_tap_within_one_frame_begins_first();
    test_cancels_and_unknown_fingers();
    test_ids_are_stable_and_lowest_free();
    test_the_manager_maps_fingers_to_the_work_zone();
    std::printf("input touch: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("input touch", run_input_touch_suite)
