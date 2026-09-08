/*
 * Validation suite for the cold-boot settings peek.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite boot
 *
 * Boot configuration is peeked from a project's settings.cfg before any engine system is
 * initialized - editor::init and game::prepare_boot_config both call it ahead of
 * engine::init_core. A full settings load walks asset_handle members (standalone.startup_scene,
 * splash.logos), and an asset handle resolves through the asset manager's per-type storages,
 * which only exist after asset_manager::init. Reading them that early crashed inside the
 * storage mutex.
 *
 * These pin the contract that keeps that from coming back:
 *   - the peek reports the persisted renderer and physics backend;
 *   - the peek leaves the asset-bearing sections untouched, so it cannot reach a storage.
 */

#include "../tests.h"

#include <engine/meta/settings/settings.hpp>
#include <engine/settings/boot_config.h>
#include <engine/settings/settings.h>

#include <filesystem/filesystem.h>

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

/// A uid that is not nil, so the document carries the asset handle that crashed the peek.
constexpr const char* NON_NIL_SCENE_UID = "3f2504e0-4f89-11d3-9a0c-0305e82c3301";

/// Read by the full loader and by nothing in the boot slice - the marker for "went too far".
constexpr const char* WARM_SECTION_MARKER = "boot_peek_marker";

/**
 * @brief Builds the settings document the peek is pointed at.
 *
 * Every platform gets a distinct non-default renderer so the expectation holds wherever the
 * suite runs; the startup scene carries a real uid so the document holds the asset handle
 * whose resolution is what must not happen during a peek.
 */
auto make_authored_settings() -> settings
{
    settings authored{};
    authored.app.product = WARM_SECTION_MARKER;
    authored.graphics.renderer.windows = preferred_renderer::direct3d12;
    authored.graphics.renderer.linux = preferred_renderer::vulkan;
    authored.graphics.renderer.macos = preferred_renderer::metal;
    authored.physics.backend = physics_backend_type::bullet;
    authored.standalone.startup_scene.set_internal_ids(hpp::uuid::from_string(NON_NIL_SCENE_UID).value(),
                                                       "app:/data/startup.spfb");
    return authored;
}

auto write_project_settings(const fs::path& root, const settings& authored) -> bool
{
    fs::error_code err;
    fs::create_directories(root / "settings", err);

    const fs::path settings_path = root / "settings" / "settings.cfg";
    save_to_file(settings_path.string(), authored);
    return fs::exists(settings_path, err);
}

void test_peek_reports_cold_fields(const fs::path& root, const settings& authored)
{
    const boot_config peeked = peek_project_boot_config(root);
    check(peeked.renderer == authored.graphics.renderer.get_for_current_platform(),
          "peek reports the persisted renderer for the current platform");
    check(peeked.physics == physics_backend_type::bullet, "peek reports the persisted physics backend");
}

void test_peek_leaves_asset_sections_untouched(const fs::path& root, const settings& authored)
{
    const fs::path settings_path = root / "settings" / "settings.cfg";

    settings peeked{};
    check(load_boot_sections_from_file(settings_path.string(), peeked), "boot sections load from a settings document");
    check(peeked.physics.backend == authored.physics.backend, "boot sections carry the physics backend");
    check(peeked.graphics.renderer == authored.graphics.renderer, "boot sections carry the per-platform renderer");
    // The guard: nothing outside "graphics" and "physics" may be visited, because the sections
    // in between hold asset handles. If the peek ever goes back to a full load, the marker in
    // "app" comes along with them and this check fails.
    check(peeked.app.product.empty(), "boot sections read nothing outside graphics and physics");

    // A full load still reads everything, so the fixture itself stays honest.
    settings complete{};
    check(load_from_file(settings_path.string(), complete), "full settings load succeeds on the same document");
    check(complete.app.product == WARM_SECTION_MARKER, "full settings load does read the warm sections");
}

void test_missing_settings_peeks_defaults(const fs::path& root)
{
    const boot_config peeked = peek_project_boot_config(root / "absent_project");
    check(peeked.renderer == preferred_renderer::auto_detect, "a missing settings file peeks the default renderer");
    check(peeked.physics == physics_backend_type::auto_detect, "a missing settings file peeks the default physics");
}

} // namespace

auto run_boot_config_peek_suite(rtti::context& ctx) -> int
{
    (void)ctx;
    g_checks = 0;
    g_failures = 0;

    const fs::path root = fs::temp_directory_path() / "unravel_boot_config_peek";
    fs::error_code err;
    fs::remove_all(root, err);

    const settings authored = make_authored_settings();
    if(!write_project_settings(root, authored))
    {
        std::printf("  FAIL: could not write the settings fixture at %s\n", root.string().c_str());
        return 1;
    }

    test_peek_reports_cold_fields(root, authored);
    test_peek_leaves_asset_sections_untouched(root, authored);
    test_missing_settings_peeks_defaults(root);

    fs::remove_all(root, err);

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

REGISTER_TEST_SUITE("boot config peek", run_boot_config_peek_suite)
