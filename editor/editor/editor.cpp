#include "editor.h"

#include <engine/engine.h>
#include <engine/events.h>
#include <engine/loading_screen.h>
#include <engine/rendering/renderer.h>
#include <version/version.h>

#include "assets/asset_watcher.h"
#include "editing/editing_manager.h"
#include "editing/picking_manager.h"
#include "editing/thumbnail_manager.h"
#include "events.h"
#include "hub/hub.h"
#include "imgui/imgui_interface.h"
#include "system/project_manager.h"
#include "system/version_manager.h"
#include <filedialog/filedialog.h>


namespace unravel
{

REFLECTION_REGISTRATION
{
    entt::meta_factory<editor>{}
        .type("editor"_hs)
        .func<&editor::create>("create"_hs)
        .func<&editor::init>("init"_hs)
        .func<&editor::deinit>("deinit"_hs)
        .func<&editor::destroy>("destroy"_hs)
        .func<&editor::process>("process"_hs)
        .func<&editor::interrupt>("interrupt"_hs);


}

auto editor::create(rtti::context& ctx, cmd_line::parser& parser) -> bool
{
    if(!engine::create(ctx, parser))
    {
        return false;
    }

    fs::path binary_path = fs::resolve_protocol("binary:/");
    fs::path editor_data = binary_path / "data" / "editor";
    fs::add_path_protocol("editor", editor_data);

    ctx.add<ui_events>();
    ctx.add<project_manager>(ctx, parser);
    ctx.add<imgui_interface>(ctx);
    ctx.add<hub>(ctx);
    ctx.add<editing_manager>();
    ctx.add<picking_manager>();
    ctx.add<thumbnail_manager>();
    ctx.add<asset_watcher>();
    ctx.add<version_manager>();

    return true;
}

auto editor::init(const cmd_line::parser& parser) -> bool
{
    auto& ctx = engine::context();
    auto& ls = ctx.get_cached<loading_screen>();

    ls.set_on_fail([](const std::string& module, const std::string& message) -> void
    {
        native::message_box(message, native::dialog_type::ok, native::icon_type::error, module);
    });
    // --- Phase 1: core systems (no ImGui yet, console-only progress) ---

    if(!engine::init_core(parser))
    {
        return false;
    }

    ls.begin_module("Window");
    if(!ls.check(init_window(ctx)))
    {
        return false;
    }

    ls.begin_module("Editor UI");
    if(!ls.check(ctx.get_cached<imgui_interface>().init_basic(ctx)))
    {
        return false;
    }

    // --- ImGui is now available: register the visual loading screen ---

    auto& imgui = ctx.get_cached<imgui_interface>();
    ls.set_visualizer([&imgui, &ctx](const loading_screen& screen) -> void
    {
        imgui.render_loading_frame(ctx,
                                   screen.current_module(),
                                   screen.completed(),
                                   screen.total(),
                                   screen.current_job());
    });


    // --- Phase 2: engine assets (visual progress bar) ---

    ls.begin_module("Asset Watcher");
    auto& aw = ctx.get_cached<asset_watcher>();
    if(!ls.check(aw.init(ctx)))
    {
        return false;
    }
    ls.begin_module("Engine Assets");
    aw.watch_assets(ctx, "engine:/", true, [&ls](size_t completed, size_t total, const std::string& job) -> void
    {
        ls.progress(completed, total, job);
    });


    // --- Phase 3: engine subsystems (visual module names) ---

    if(!engine::init_systems(parser))
    {
        return false;
    }

    // --- Phase 4: editor assets (visual progress bar) ---

    ls.begin_module("Editor Assets");
    aw.watch_assets(ctx, "editor:/", true, [&ls](size_t completed, size_t total, const std::string& job) -> void
    {
        ls.progress(completed, total, job);
    });

    // --- Phase 5: editor subsystems ---

    ls.begin_module("Editor UI Finalize");
    if(!ls.check(ctx.get_cached<imgui_interface>().init_finalize(ctx)))
    {
        return false;
    }

    ls.begin_module("Hub");
    if(!ls.check(ctx.get_cached<hub>().init(ctx)))
    {
        return false;
    }

    ls.begin_module("Editing");
    if(!ls.check(ctx.get_cached<editing_manager>().init(ctx)))
    {
        return false;
    }

    ls.begin_module("Picking");
    if(!ls.check(ctx.get_cached<picking_manager>().init(ctx)))
    {
        return false;
    }

    ls.begin_module("Thumbnails");
    if(!ls.check(ctx.get_cached<thumbnail_manager>().init(ctx)))
    {
        return false;
    }

    ls.begin_module("Project Manager");
    if(!ls.check(ctx.get_cached<project_manager>().init(ctx, parser)))
    {
        return false;
    }

    ls.begin_module("Version Manager");
    if(!ls.check(ctx.get_cached<version_manager>().init(ctx)))
    {
        return false;
    }

    return true;
}

auto editor::init_window(rtti::context& ctx) -> bool
{
    auto title = fmt::format("Unravel Editor <{}> {}", gfx::get_renderer_name(gfx::get_renderer_type()), version::get_full());
    uint32_t flags = os::window::resizable | os::window::maximized;
    auto primary_display = os::display::get_primary_display_index();

    auto& rend = ctx.get_cached<renderer>();
    rend.create_window_for_display(primary_display, title, flags);
    return true;
}

auto editor::deinit() -> bool
{
    auto& ctx = engine::context();


    if(!ctx.get_cached<thumbnail_manager>().deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<picking_manager>().deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<editing_manager>().deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<hub>().deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<imgui_interface>().deinit(ctx))
    {
        return false;
    }

    
    if(!ctx.get_cached<asset_watcher>().deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<project_manager>().deinit(ctx))
    {
        return false;
    }

    if(!ctx.get_cached<version_manager>().deinit(ctx))
    {
        return false;
    }

    {
        auto& aw = ctx.get_cached<asset_watcher>();
        aw.unwatch_assets(ctx, "editor:/");
        aw.unwatch_assets(ctx, "engine:/");
    }

    return engine::deinit();
}

auto editor::destroy() -> bool
{
    auto& ctx = engine::context();

    ctx.remove<asset_watcher>();
    ctx.remove<thumbnail_manager>();
    ctx.remove<picking_manager>();
    ctx.remove<editing_manager>();

    ctx.remove<hub>();
    ctx.remove<imgui_interface>();

    ctx.remove<project_manager>();
    ctx.remove<version_manager>();

    ctx.remove<ui_events>();

    return engine::destroy();
}

auto editor::process() -> int
{
    return engine::process();
}
auto editor::interrupt() -> bool
{
    return engine::interrupt();
}

} // namespace unravel
