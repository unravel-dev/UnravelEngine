#include "rendering_system.h"

#include <engine/rendering/mesh.h>
#include <engine/rendering/model.h>
#include <engine/rendering/pipeline/pipeline.h>

#include <engine/animation/ecs/systems/animation_system.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/systems/transform_system.h>
#include <engine/engine.h>
#include <engine/events.h>

#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/model_component.h>

#include <engine/rendering/ecs/systems/camera_system.h>
#include <engine/rendering/ecs/systems/model_system.h>
#include <engine/rendering/ecs/systems/reflection_probe_system.h>

namespace unravel
{

auto rendering_system::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);
    auto& ev = ctx.get_cached<events>();

    ev.on_frame_end.connect(sentinel_, 1000, this, &rendering_system::on_frame_end);

    debug_draw_callbacks_.reserve(128);
    return true;
}

auto rendering_system::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
}

void rendering_system::on_frame_end(rtti::context& ctx, delta_t)
{
    debug_draw_callbacks_.clear();
}

void rendering_system::on_frame_update(scene& scn, delta_t dt)
{
    auto& ctx = engine::context();
    ctx.get_cached<transform_system>().on_frame_update(scn, dt);
    ctx.get_cached<camera_system>().on_frame_update(scn, dt);
    ctx.get_cached<model_system>().on_frame_update(scn, dt);
    ctx.get_cached<animation_system>().on_frame_update(scn, dt);
    ctx.get_cached<reflection_probe_system>().on_frame_update(scn, dt);
}

void rendering_system::on_frame_before_render(scene& scn, delta_t dt)
{
    auto& ctx = engine::context();
    ctx.get_cached<model_system>().on_frame_before_render(scn, dt);
    ctx.get_cached<camera_system>().on_frame_before_render(scn, dt);

}

void rendering_system::on_play_begin(hpp::span<const entt::handle> entities, delta_t dt)
{
    auto& ctx = engine::context();
    ctx.get_cached<transform_system>().on_play_begin(entities, dt);
    ctx.get_cached<camera_system>().on_play_begin(entities, dt);
    ctx.get_cached<model_system>().on_play_begin(entities, dt);
    ctx.get_cached<animation_system>().on_play_begin(entities, dt);
    ctx.get_cached<reflection_probe_system>().on_play_begin(entities, dt);
}

auto rendering_system::render_scene(entt::handle camera_ent, camera_component& camera_comp, scene& scn, delta_t dt)
    -> gfx::frame_buffer::ptr
{
    auto& pipeline_data = camera_comp.get_pipeline_data();
    auto& camera = pipeline_data.get_camera();
    auto& pipeline = pipeline_data.get_pipeline();
    auto& rview = camera_comp.get_render_view();

    auto params = pipeline->create_run_params(camera_ent);

    auto result = pipeline->run_pipeline(scn, camera, rview, dt, params);

    render_debug(camera_ent);

    return result;
}

auto rendering_system::render_scene(scene& scn, delta_t dt) -> gfx::frame_buffer::ptr
{
    gfx::frame_buffer::ptr output{};
    scn.registry->view<camera_component>().each(
        [&](auto e, auto&& camera_comp)
        {
            auto handle = scn.create_handle(e);
            output = render_scene(handle, camera_comp, scn, dt);
            return;
        });

    return output;
}

void rendering_system::render_scene(const gfx::frame_buffer::ptr& output,
                                    entt::handle camera_ent,
                                    camera_component& camera_comp,
                                    scene& scn,
                                    delta_t dt)
{
    auto& pipeline_data = camera_comp.get_pipeline_data();
    auto& camera = pipeline_data.get_camera();
    auto& pipeline = pipeline_data.get_pipeline();
    auto& rview = camera_comp.get_render_view();

    auto params = pipeline->create_run_params(camera_ent);

    pipeline->run_pipeline(output, scn, camera, rview, dt, params);
    render_debug(camera_ent);
}

void rendering_system::render_scene(const gfx::frame_buffer::ptr& output, scene& scn, delta_t dt)
{

    scn.registry->view<camera_component>().each(
        [&](auto e, auto&& camera_comp)
        {
            auto handle = scn.create_handle(e);
            render_scene(output, handle, camera_comp, scn, dt);
        });
}

void rendering_system::render_debug(entt::handle camera_entity)
{
    if(debug_draw_callbacks_.empty())
    {
        return;
    }

    auto& camera_comp = camera_entity.get<camera_component>();
    const auto& rview = camera_comp.get_render_view();
    const auto& camera = camera_comp.get_camera();
    const auto& view = camera.get_view();
    const auto& proj = camera.get_projection();
    const auto& obuffer = rview.fbo_get("OBUFFER");

    gfx::render_pass pass("debug_draw_pass");
    pass.bind(obuffer.get());
    pass.set_view_proj(view, proj);

    gfx::dd_raii dd(pass.id);

    for(const auto& callback : debug_draw_callbacks_)
    {
        callback(dd);
    }
}

void rendering_system::add_debugdraw_call(const std::function<void(gfx::dd_raii& dd)>& callback)
{
    debug_draw_callbacks_.emplace_back(callback);
}

} // namespace unravel
