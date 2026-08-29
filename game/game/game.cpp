#include "game.h"

#include <engine/engine.h>
#include <engine/play_mode.h>
#include <engine/rendering/renderer.h>
#include <engine/meta/settings/settings.hpp>
#include <engine/assets/asset_manager.h>
#include <engine/meta/assets/asset_database.hpp>
#include <engine/scripting/ecs/systems/script_system.h>
#include <engine/settings/boot_config.h>
#include "runner/runner.h"

#include <filesystem/filesystem.h>
#include <logging/logging.h>
#include <reflection/registration.h>

namespace unravel
{

REFLECTION_REGISTRATION
{
    entt::meta_factory<game>{}
        .type("game"_hs)
        .func<&game::create>("create"_hs)
        .func<&game::init>("init"_hs)
        .func<&game::deinit>("deinit"_hs)
        .func<&game::destroy>("destroy"_hs)
        .func<&game::process>("process"_hs)
        .func<&game::interrupt>("interrupt"_hs);
}

auto game::create(rtti::context& ctx, cmd_line::parser& parser) -> bool
{
    ctx.add<deploy>();

    if(!engine::create(ctx, parser))
    {
        return false;
    }

    ctx.add<runner>(ctx);

    parser.set_optional<std::string>("a", "appdata", "", "Application data directory. Defaults to binary directory.");


    return true;
}

auto game::init(const cmd_line::parser& parser) -> bool
{
    if(!init_protocols(parser))
    {
        return false;
    }

    auto& ctx = engine::context();

    // Peek settings for cold boot fields only. Asset handles inside settings.cfg
    // cannot resolve yet (databases not loaded); that is expected here.
    if(!prepare_boot_config(ctx, parser))
    {
        return false;
    }

    if(!engine::init_core(parser))
    {
        return false;
    }

    if(!init_assets(ctx))
    {
        return false;
    }

    // Full settings load now that asset databases can resolve handles.
    if(!init_settings(ctx))
    {
        return false;
    }

    if(!init_window(ctx, parser))
    {
        return false;
    }

    if(!engine::init_systems(parser))
    {
        return false;
    }

    if(!ctx.get_cached<runner>().init(ctx))
    {
        return false;
    }

    {
        auto& am = ctx.get_cached<asset_manager>();
        am.preload_all_assets();
    }

    auto& scr = ctx.get_cached<script_system>();
    if(!scr.load_app_domain(ctx, false))
    {
        return false;
    }

    auto& play = ctx.get_cached<play_mode>();
    play.set_active(ctx, true);

    return true;
}

auto game::init_protocols(const cmd_line::parser& parser) -> bool
{
    std::string appdata;
    parser.try_get("appdata", appdata);
    if(!appdata.empty())
    {
        fs::path app_data = appdata;
        fs::add_path_protocol("engine", app_data / "engine");
        fs::add_path_protocol("app", app_data / "app");
    }
    else
    {
        fs::path binary_path = fs::resolve_protocol("binary:/");
        fs::path app_data = binary_path / "data" / "app";
        fs::add_path_protocol("app", app_data);
    }
    return true;
}

auto game::prepare_boot_config(rtti::context& ctx, const cmd_line::parser& parser) -> bool
{
    const fs::path app_root = fs::resolve_protocol("app:/");
    const boot_config project_hint = peek_project_boot_config(app_root);
    const boot_config resolved = resolve_boot_config(parser, project_hint);
    if(ctx.has<boot_config>())
    {
        ctx.get<boot_config>() = resolved;
    }
    else
    {
        ctx.add<boot_config>(resolved);
    }
    APPLOG_INFO("Resolved boot config: renderer={} (cli={}) physics={} (cli={})",
                preferred_renderer_to_string(resolved.renderer),
                resolved.cli.renderer,
                physics_backend_to_string(resolved.physics),
                resolved.cli.physics);
    return true;
}

auto game::init_settings(rtti::context& ctx) -> bool
{
    auto& s = ctx.add<settings>();
    const auto settings_path = fs::resolve_protocol("app:/settings/settings.cfg");
    if(!load_from_file(settings_path.string(), s))
    {
        APPLOG_CRITICAL("Failed to load project settings {}", settings_path.string());
        return false;
    }
    return true;
}

auto game::init_assets(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    if(!am.load_database("engine:/"))
    {
        APPLOG_CRITICAL("Failed to load engine asset pack.");
        return false;
    }
    if(!am.load_database("app:/"))
    {
        APPLOG_CRITICAL("Failed to load app asset pack.");
        return false;
    }
    return true;
}

auto game::init_window(rtti::context& ctx, const cmd_line::parser& parser) -> bool
{
    auto& s = ctx.get<settings>();

    auto title = fmt::format("Unravel Game <{}>", gfx::get_renderer_name(gfx::get_renderer_type()));

    if(!s.app.product.empty())
    {
        title = fmt::format("{}", s.app.product);
    }

    if(!s.app.version.empty())
    {
        title += fmt::format("v{}", s.app.version);
    }
    auto& rend = ctx.get_cached<renderer>();
    std::string window_geometry;
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool maximized = false;
    if(parser.try_get("window", window_geometry) &&
       renderer::parse_window_geometry(window_geometry, x, y, width, height, maximized))
    {
        uint32_t flags = os::window::resizable;
        if(maximized)
        {
            flags |= os::window::maximized;
        }
        rend.create_window(title, x, y, width, height, flags);
        return true;
    }
    // Deploy default "Fullscreen Window"
    const uint32_t flags = os::window::fullscreen_desktop;
    const auto primary_display = os::display::get_primary_display_index();
    rend.create_window_for_display(primary_display, title, flags);
    return true;
}


auto game::deinit() -> bool
{
    auto& ctx = engine::context();

    if(!ctx.get_cached<runner>().deinit(ctx))
    {
        return false;
    }

    return engine::deinit();
}

auto game::destroy() -> bool
{
    auto& ctx = engine::context();

    ctx.remove<settings>();
    ctx.remove<runner>();
    ctx.remove<deploy>();

    return engine::destroy();
}

auto game::process() -> int
{
    return engine::process();
}

auto game::interrupt() -> bool
{
    return engine::interrupt();
}
} // namespace unravel
