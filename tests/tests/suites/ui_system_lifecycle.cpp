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
 * The runner is headless and never initialises RmlUi.
 */

#include "../tests.h"

#include <engine/ui/ecs/systems/ui_system.h>
#include <engine/ui/rmlui/RmlUi_Backend_Engine.h>

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

auto run_ui_system_lifecycle_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_release_without_rmlui();
    std::printf("ui system lifecycle: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("ui system lifecycle", run_ui_system_lifecycle_suite)
