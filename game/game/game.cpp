#include "game.h"

#include <engine/engine.h>
#include <engine/events.h>
#include <engine/rendering/renderer.h>
#include <engine/meta/settings/settings.hpp>
#include <engine/assets/asset_manager.h>
#include <engine/meta/assets/asset_database.hpp>
#include <engine/scripting/ecs/systems/script_system.h>
#include "runner/runner.h"

#include <filesystem/filesystem.h>
#include <logging/logging.h>
#include <rttr/registration>

namespace unravel
{
RTTR_REGISTRATION
{
    rttr::registration::class_<game>("game")
        .constructor<>()
        .method("create", &game::create)
        .method("init", &game::init)
        .method("deinit", &game::deinit)
        .method("destroy", &game::destroy)
        .method("process", &game::process)
        .method("interrupt", &game::interrupt);

}

auto game::create(rtti::context& ctx, cmd_line::parser& parser) -> bool
{
    ctx.add<deploy>();

    if(!engine::create(ctx, parser))
    {
        return false;
    }

    ctx.add<runner>(ctx);

    fs::path binary_path = fs::resolve_protocol("binary:/");
    fs::path app_data = binary_path / "data" / "app";

    parser.set_optional<std::string>("a", "appdata", app_data.string(), "Application data directory. Defaults to binary directory.");


    return true;
}

auto game::init(const cmd_line::parser& parser) -> bool
{
    if(!engine::init_core(parser))
    {
        return false;
    }

    auto& ctx = engine::context();

    if(!init_assets(ctx, parser))
    {
        return false;
    }

    if(!init_settings(ctx))
    {
        return false;
    }

    if(!init_window(ctx))
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

    auto& scr = ctx.get_cached<script_system>();
    if(!scr.load_app_domain(ctx, true))
    {
        return false;
    }

    auto& ev = ctx.get_cached<events>();
    ev.set_play_mode(ctx, true);

    return true;
}

auto game::init_settings(rtti::context& ctx) -> bool
{
    auto& s = ctx.add<settings>();
    auto settings_path = fs::resolve_protocol("app:/settings/settings.cfg");
    if(!load_from_file(settings_path.string(), s))
    {
        APPLOG_CRITICAL("Failed to load project settings {}", settings_path.string());
        return false;
    }

    return true;
}

auto game::init_assets(rtti::context& ctx, const cmd_line::parser& parser) -> bool
{
    std::string appdata;
    parser.try_get("appdata", appdata);

    if(!appdata.empty())
    {
        fs::path app_data = appdata;
        fs::add_path_protocol("app", app_data);
    }
    else
    {
        APPLOG_CRITICAL("Failed to get appdata path.");
        return false;
    }

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

auto game::init_window(rtti::context& ctx) -> bool
{
    auto& s = ctx.get<settings>();

    auto title = fmt::format("Ace Game <{}>", gfx::get_renderer_name(gfx::get_renderer_type()));

    if(!s.app.product.empty())
    {
        title = fmt::format("{}", s.app.product);
    }

    if(!s.app.version.empty())
    {
        title += fmt::format("v{}", s.app.version);
    }
    uint32_t flags = os::window::resizable | os::window::maximized;
    auto primary_display = os::display::get_primary_display_index();

    auto& rend = ctx.get_cached<renderer>();
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

auto game::process() -> bool
{
    return engine::process();
}

auto game::interrupt() -> bool
{
    return engine::interrupt();
}
} // namespace unravel
