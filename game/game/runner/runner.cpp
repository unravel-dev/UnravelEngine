#include "runner.h"
#include <engine/assets/asset_manager.h>
#include <engine/events.h>
#include <engine/meta/settings/settings.hpp>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/systems/rendering_system.h>
#include <engine/rendering/renderer.h>

#include <logging/logging.h>

namespace unravel
{

runner::runner(rtti::context& ctx)
{
    auto& ev = ctx.get_cached<events>();

    ev.on_frame_update.connect(sentinel_, this, &runner::on_frame_update);
    ev.on_frame_before_render.connect(sentinel_, this, &runner::on_frame_before_render);
    ev.on_frame_render.connect(sentinel_, this, &runner::on_frame_render);
    ev.on_play_begin.connect(sentinel_, -100000, this, &runner::on_play_begin);
    ev.on_play_end.connect(sentinel_, 100000, this, &runner::on_play_end);
}

auto runner::init(rtti::context& ctx) -> bool
{
    APPLOG_INFO("{}::{}", hpp::type_name_str(*this), __func__);

    auto& s = ctx.get<settings>();
    auto scn = s.standalone.startup_scene;
    if(!scn)
    {
        APPLOG_CRITICAL("Failed to load initial scene {}", scn.id());
        return false;
    }
    return true;
}

auto runner::deinit(rtti::context& ctx) -> bool
{
    APPLOG_INFO("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
}

void runner::on_frame_update(rtti::context& ctx, delta_t dt)
{
    auto& rend = ctx.get_cached<renderer>();
    auto& path = ctx.get_cached<rendering_system>();
    auto& ec = ctx.get_cached<ecs>();
    auto& scene = ec.get_scene();
    auto& window = rend.get_main_window();
    auto size = window->get_window().get_size();

    scene.registry->view<camera_component>().each(
        [&](auto e, auto&& camera_comp)
        {
            camera_comp.set_viewport_size({size.w, size.h});
        });
    path.on_frame_update(scene, dt);
}

void runner::on_frame_before_render(rtti::context& ctx, delta_t dt)
{
    auto& path = ctx.get_cached<rendering_system>();
    auto& ec = ctx.get_cached<ecs>();
    auto& scene = ec.get_scene();

    path.on_frame_before_render(scene, dt);
}

void runner::on_frame_render(rtti::context& ctx, delta_t dt)
{
    auto& rend = ctx.get_cached<renderer>();
    auto& path = ctx.get_cached<rendering_system>();
    auto& ec = ctx.get_cached<ecs>();
    auto& scene = ec.get_scene();
    auto& window = rend.get_main_window();

    path.render_scene(window->get_surface(), scene, dt);
}

void runner::on_play_begin(rtti::context& ctx)
{
    APPLOG_INFO("{}::{}", hpp::type_name_str(*this), __func__);

    auto& s = ctx.get<settings>();

    auto scn = s.standalone.startup_scene;
    if(!scn)
    {
        APPLOG_CRITICAL("Failed to load initial scene {}", scn.id());
        return;
    }
    auto& ec = ctx.get_cached<ecs>();
    if(!ec.get_scene().load_from(scn))
    {
        return;
    }
}

void runner::on_play_end(rtti::context& ctx)
{
    APPLOG_INFO("{}::{}", hpp::type_name_str(*this), __func__);
}
} // namespace unravel
