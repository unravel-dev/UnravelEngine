/*
 * Validation suite for the UI system outside its initialised lifetime.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "ui system lifecycle"
 *
 * Closing a project releases the UI resources. That also runs when the editor's start-up failed before the
 * UI system was initialised (a failed scripting load stops start-up just before it), and RmlUi's release
 * functions dereference its core data unchecked, so the editor crashed on the way out. This pins:
 *   - releasing the UI resources while RmlUi is not initialised does nothing.
 *   - the dp ratio is the display's scale: a device preset's, or 1 without a window.
 * The runner is headless and never initialises RmlUi.
 */

#include "../tests.h"

#include <engine/rendering/ecs/systems/rendering_system.h>
#include <engine/ui/ecs/systems/ui_system.h>
#include <engine/ui/rmlui/RmlUi_Backend_Engine.h>

#include <cmath>
#include <cstdio>
#include <string>

using namespace unravel;

namespace
{

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void test_release_without_rmlui()
{
    std::printf("test_release_without_rmlui\n");
    check(!RmlUi_Backend_Engine::is_initialized(), "the headless runner has not initialised RmlUi");
    ui_system ui;
    ui.release_resources();
    check(true, "releasing UI resources without RmlUi returns");
}

void test_dp_ratio_is_the_display_scale(rtti::context& ctx)
{
    std::printf("test_dp_ratio_is_the_display_scale\n");
    const float tolerance = 1e-4f;
    if(!ctx.has<rendering_system>())
    {
        check(false, "the runner has a rendering system");
        return;
    }
    auto& rendering = ctx.get_cached<rendering_system>();
    const auto kept_device = rendering.get_game_view_device();

    rendering_system::game_view_device device;
    device.dpi = 450.0f;
    rendering.set_game_view_device(device);
    check(std::abs(ui_system::get_screen_dp_ratio(ctx) - 2.8125f) < tolerance, "a 450 dpi phone preset gets its scale");

    // The same phone shown at half its pixels: the layout must stay the same share of the view.
    device.dpi = 225.0f;
    rendering.set_game_view_device(device);
    check(std::abs(ui_system::get_screen_dp_ratio(ctx) - 1.40625f) < tolerance, "a smaller view of it scales along");

    rendering.set_game_view_device(hpp::nullopt);
    check(std::abs(ui_system::get_screen_dp_ratio(ctx) - 1.0f) < tolerance, "no device and no window: 1");
    rendering.set_game_view_device(kept_device);
}

auto run_ui_system_lifecycle_suite(rtti::context& ctx) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_release_without_rmlui();
    test_dp_ratio_is_the_display_scale(ctx);
    std::printf("ui system lifecycle: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("ui system lifecycle", run_ui_system_lifecycle_suite)
