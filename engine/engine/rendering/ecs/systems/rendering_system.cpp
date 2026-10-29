#include "rendering_system.h"
#include "particle_system.h"

#include <engine/ui/ecs/systems/ui_system.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/model.h>
#include <engine/rendering/pipeline/pipeline.h>
#include <engine/rendering/render_feature_support.h>
#include <engine/profiler/profiler.h>

#include <engine/animation/ecs/systems/animation_system.h>
#include <engine/animation/ecs/systems/rig_system.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/systems/transform_system.h>
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/play_mode.h>

#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/model_component.h>

#include <engine/rendering/ecs/systems/camera_system.h>
#include <engine/rendering/ecs/systems/model_system.h>
#include <engine/rendering/ecs/systems/reflection_probe_system.h>
#include <engine/rendering/ecs/systems/skylight_system.h>
#include <engine/settings/settings.h>

namespace unravel
{
namespace
{
/**
 * @brief How a camera run renders the view its host shows: with the project's graphics settings (the feature
 * limits bound the effects the camera and its volumes ask for, the scene renders at the render scale) unless
 * the view asks for the engine's defaults, and with the view's own choices over them.
 */
void apply_view_params(rtti::context& ctx, rendering::pipeline::run_params& params, const view_render_params& view)
{
    params.is_being_edited = view.is_being_edited;
    params.render_resolution = view.render_resolution;
    params.needs_output_depth = view.needs_output_depth;
    // What the device cannot run stays off in every view, the engine's defaults included.
    const auto& support = get_render_feature_support();
    const auto device_limits = support.get_device_limits();
    if(view.settings_use == graphics_settings_use::project && ctx.has<settings>())
    {
        const auto& graphics = ctx.get<settings>().graphics;
        rendering::pipeline::apply_feature_limits(params, intersect_feature_limits(graphics.limits, device_limits));
        params.render_scale = graphics.render_scale;
    }
    else
    {
        rendering::pipeline::apply_feature_limits(params, device_limits);
    }
    rendering::pipeline::apply_feature_support(params, support);
    if(view.render_scale)
    {
        params.render_scale = *view.render_scale;
    }
}
} // namespace

auto rendering_system::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);
    auto& ev = ctx.get_cached<events>();

    ev.on_frame_update.connect(sentinel_,
                               frame_update_priority::script_draws_reset,
                               this,
                               &rendering_system::reset_script_draws);
    ev.on_frame_end.connect(sentinel_, 1000, this, &rendering_system::on_frame_end);
    ev.on_play_end.connect(sentinel_, -10, this, &rendering_system::on_play_end);

    debug_draw_callbacks_.reserve(128);
    return true;
}

auto rendering_system::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);
    baked_animations_.clear();
    baked_animations_.destroy();
    material_instances_.clear();
    return true;
}

void rendering_system::on_play_end(rtti::context& ctx)
{
    // Scripts make draws, bakes and material instances in play mode; their scripts are gone now.
    instanced_draws_.clear();
    baked_animations_.clear();
    material_instances_.clear();
}

void rendering_system::reset_script_draws(rtti::context& ctx, const frame_times& times)
{
    // Scripts draw their instances again in every frame they update. While the game is paused they draw nothing
    // new, so the last draws stay on screen.
    const auto& play = ctx.get_cached<play_mode>();
    if(!play.is_simulation_running() || !play.is_simulation_paused())
    {
        instanced_draws_.clear();
    }
    baked_animations_.reclaim_released();
}

void rendering_system::on_frame_end(rtti::context& ctx, const frame_times& times)
{
    APP_SCOPE_PERF("Rendering/On Frame End");
    debug_draw_callbacks_.clear();
    instanced_draws_.end_frame();
}

auto rendering_system::get_instanced_draws() -> instanced_draw_list&
{
    return instanced_draws_;
}

auto rendering_system::get_baked_animations() -> baked_animation_store&
{
    return baked_animations_;
}

auto rendering_system::create_material_instance(const asset_handle<material>& shared) -> uint32_t
{
    if(!shared.is_valid())
    {
        return 0;
    }
    auto instance = std::make_shared<material_instance>();
    instance->set_shared_material(shared);
    const uint32_t id = next_material_instance_id_++;
    material_instances_.emplace(id, std::move(instance));
    return id;
}

auto rendering_system::find_material_instance(uint32_t id) const -> std::shared_ptr<material_instance>
{
    const auto found = material_instances_.find(id);
    return found != material_instances_.end() ? found->second : nullptr;
}

void rendering_system::release_material_instance(uint32_t id)
{
    material_instances_.erase(id);
}

void rendering_system::on_frame_update(scene& scn, const frame_times& times)
{
    APP_SCOPE_PERF("Rendering/On Frame Update");
    auto& ctx = engine::context();
    // Rigs drawn from their transforms get their input pose back before anything animates them.
    ctx.get_cached<rig_system>().restore_deferred(*scn.registry);
    ctx.get_cached<transform_system>().on_frame_update(scn, times);
    ctx.get_cached<camera_system>().on_frame_update(scn, times);
    ctx.get_cached<model_system>().on_frame_update(scn, times);
    ctx.get_cached<animation_system>().on_frame_update(scn, times);
    ctx.get_cached<reflection_probe_system>().on_frame_update(scn, times);
    ctx.get_cached<skylight_system>().on_frame_update(scn, times);
}

void rendering_system::on_frame_before_render(scene& scn, const frame_times& times)
{
    APP_SCOPE_PERF("Rendering/On Frame Before Render");
    auto& ctx = engine::context();
    // Rig modifiers (IK, spring bones) pose the bones for skinning, then hand back the input pose.
    auto& rig = ctx.get_cached<rig_system>();
    rig.before_skinning(*scn.registry, times.scaled);
    ctx.get_cached<model_system>().on_frame_before_render(scn, times);
    rig.after_skinning(*scn.registry);
    ctx.get_cached<camera_system>().on_frame_before_render(scn, times);
    ctx.get_cached<particle_system>().on_frame_before_render(scn, times);


}

void rendering_system::on_play_begin(hpp::span<const entt::handle> entities, delta_t dt)
{
    APP_SCOPE_PERF("Rendering/On Play Begin");
    auto& ctx = engine::context();
    ctx.get_cached<transform_system>().on_play_begin(entities, dt);
    ctx.get_cached<camera_system>().on_play_begin(entities, dt);
    ctx.get_cached<model_system>().on_play_begin(entities, dt);
    ctx.get_cached<animation_system>().on_play_begin(entities, dt);
    ctx.get_cached<reflection_probe_system>().on_play_begin(entities, dt);
}

auto rendering_system::release_pipeline_resources(scene& scn) -> bool
{
    APP_SCOPE_PERF("Rendering/Release Pipeline Resources");
    auto& ctx = engine::context();
    scn.registry->view<camera_component>().each(
        [&](auto e, auto&& camera_comp)
        {
            auto& pipeline_data = camera_comp.get_pipeline_data();
            auto& camera = pipeline_data.get_camera();
            auto& pipeline = pipeline_data.get_pipeline();
            auto& rview = camera_comp.get_render_view();
            rview = {};
            return;
        });
    return true;
}

auto rendering_system::render_scene(entt::handle camera_ent,
                                    camera_component& camera_comp,
                                    scene& scn,
                                    const frame_times& times,
                                    const view_render_params& view) -> gfx::frame_buffer::ptr
{
    APP_SCOPE_PERF("Rendering/Render Scene Camera");
    auto& ctx = engine::context();
    auto& pipeline_data = camera_comp.get_pipeline_data();
    auto& camera = pipeline_data.get_camera();
    auto& pipeline = pipeline_data.get_pipeline();
    auto& rview = camera_comp.get_render_view();

    auto& ui = ctx.get_cached<ui_system>();
    bool process_input = view.render_screen_space;
    ui.update_screen_space(ctx, camera_ent, scn, process_input);
    ui.update_world_space(ctx, camera_ent, scn, process_input);

    ui.render_world_space(ctx, camera_ent, scn, times.scaled);

    auto params = pipeline->create_run_params(camera_ent, &scn, &camera);
    apply_view_params(ctx, params, view);
    auto result = pipeline->run_pipeline(scn, camera, rview, times.scaled, params, camera_comp.get_render_mask());
    render_debug(camera_ent);
    if(view.render_screen_space)
    {
        ui.render_screen_space(ctx, result, camera_ent, scn, times.scaled);
    }
    return result;
}

auto rendering_system::render_scene(scene& scn, const frame_times& times, const view_render_params& view) -> gfx::frame_buffer::ptr
{
    APP_SCOPE_PERF("Rendering/Render Scene");
    auto& ctx = engine::context();
    gfx::frame_buffer::ptr output{};
    // Active cameras only: an inactive camera entity renders nothing (each camera runs a whole pipeline).
    scn.registry->view<camera_component, active_component>().each(
        [&](auto e, auto&& camera_comp, auto&& /*active*/)
        {
            auto handle = scn.create_handle(e);
            output = render_scene(handle, camera_comp, scn, times, view);
            return;
        });
    return output;
}

void rendering_system::render_scene(const gfx::frame_buffer::ptr& output,
                                    entt::handle camera_ent,
                                    camera_component& camera_comp,
                                    scene& scn,
                                    const frame_times& times,
                                    const view_render_params& view)
{
    APP_SCOPE_PERF("Rendering/Render Scene");
    auto& ctx = engine::context();
    auto& pipeline_data = camera_comp.get_pipeline_data();
    auto& camera = pipeline_data.get_camera();
    auto& pipeline = pipeline_data.get_pipeline();
    auto& rview = camera_comp.get_render_view();
    auto& ui = ctx.get_cached<ui_system>();

    bool process_input = view.render_screen_space;
    ui.update_screen_space(ctx, camera_ent, scn, process_input);
    ui.update_world_space(ctx, camera_ent, scn, process_input);

    ui.render_world_space(ctx, camera_ent, scn, times.scaled);

    auto params = pipeline->create_run_params(camera_ent, &scn, &camera);
    apply_view_params(ctx, params, view);
    pipeline->run_pipeline(output, scn, camera, rview, times.scaled, params, camera_comp.get_render_mask());
    render_debug(camera_ent);
    if(view.render_screen_space)
    {
        ui.render_screen_space(ctx, output, camera_ent, scn, times.scaled);
    }
}

auto rendering_system::render_scene(const gfx::frame_buffer::ptr& output, scene& scn, const frame_times& times) -> bool
{
    APP_SCOPE_PERF("Rendering/Render Scene");
    bool has_rendered = false;
    scn.registry->view<camera_component, active_component>().each(
        [&](auto e, auto&& camera_comp, auto&& /*active*/)
        {
            auto handle = scn.create_handle(e);
            render_scene(output, handle, camera_comp, scn, times);
            has_rendered = true;
        });
    return has_rendered;
}

void rendering_system::render_debug(entt::handle camera_entity)
{
    if(debug_draw_callbacks_.empty())
    {
        return;
    }

    APP_SCOPE_PERF("Rendering/Render Debug");

    auto& camera_comp = camera_entity.get<camera_component>();
    const auto& rview = camera_comp.get_render_view();
    const auto& camera = camera_comp.get_camera();
    const auto& view = camera.get_view();
    // Drawn over the finished image, after temporal AA: a jittered projection would shake every line.
    const auto proj = camera.get_projection_unjittered();
    // A scaled player view upscales straight into the window and has no OBUFFER.
    const auto& obuffer = rview.fbo_find("OBUFFER");
    if(!obuffer)
    {
        return;
    }

    gfx::render_pass pass("Debug Draw Pass");
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

void rendering_system::set_game_view_size(const usize32_t& size)
{
    game_view_size_ = size;
}

void rendering_system::apply_game_view_size(scene& scn, const usize32_t& size)
{
    set_game_view_size(size);
    scn.registry->view<camera_component>().each(
        [&](auto e, auto&& camera_comp)
        {
            camera_comp.set_viewport_size(size);
        });
}

auto rendering_system::get_game_view_size() const -> const usize32_t&
{
    return game_view_size_;
}

void rendering_system::set_game_view_device(const hpp::optional<game_view_device>& device)
{
    game_view_device_ = device;
}

auto rendering_system::get_game_view_device() const -> const hpp::optional<game_view_device>&
{
    return game_view_device_;
}


} // namespace unravel
