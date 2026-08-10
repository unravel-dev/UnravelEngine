#include "model_system.h"
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/model_component.h>

#include <engine/ecs/ecs.h>
#include <engine/events.h>
#include <engine/profiler/profiler.h>

#include <logging/logging.h>

#define POOLSTL_STD_SUPPLEMENT 1
#include <poolstl/poolstl.hpp>

namespace unravel
{

auto model_system::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);
    auto& ev = ctx.get_cached<events>();

    ev.on_play_begin.connect(sentinel_, 1000, this, &model_system::on_play_begin);

    return true;
}

auto model_system::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
}

void model_system::on_play_begin(rtti::context& ctx)
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();

    auto view = scn.registry->view<model_component>();

    // this pass can create new entities so we cannot parallelize it
    std::for_each(view.begin(),
                  view.end(),
                  [&](entt::entity entity)
                  {
                      auto& model_comp = view.get<model_component>(entity);
                      model_comp.init_armature(true);
                  });
}

void model_system::on_frame_update(scene& scn, delta_t dt)
{
    APP_SCOPE_PERF("Model/System Update");

    auto view = scn.registry->view<transform_component, model_component, active_component>();

    // this pass can create new entities so we cannot parallelize it
    std::for_each(view.begin(),
                  view.end(),
                  [&](entt::entity entity)
                  {
                      auto& model_comp = view.get<model_component>(entity);
                      model_comp.init_armature(false);
                  });
}

void model_system::on_frame_before_render(scene& scn, delta_t dt)
{
    APP_SCOPE_PERF("Model/Skinning");
    static const std::string thread_name = "Model/Skinning Pool Thread";
    auto view = scn.registry->view<transform_component, model_component, active_component>();

    auto frame = gfx::get_render_frame();
    // this code should be thread safe as each task works with a whole hierarchy and
    // there is no interleaving between tasks.
    std::for_each(poolstl::par,//std::execution::par,
                  view.begin(),
                  view.end(),
                  [&](entt::entity entity)
                  {
                      APP_SCOPE_PERF_THREAD("Model/Skinning/Update Armature & World Bounds","Pool Thread");
                      // This is needed as we call .get on the model inside the update_armature
                      tpp::this_thread::register_this_thread(thread_name, true);

                      auto& model_comp = view.get<model_component>(entity);

                      // Cleanup stale per-view LOD data (views not accessed for 2 seconds at 60fps)
                      model_comp.cleanup_stale_lod_data(frame, 120);

                      if(model_comp.is_newly_created())
                      {
                          model_comp.set_last_render_frame(frame);
                      }

                      // Refresh pose-derived render data (submesh/bone poses, cached proxy
                      // bounds, skinning palettes) before world bounds are computed. This is
                      // change-driven, not visibility-driven: update_armature early-outs when
                      // no armature transform changed, so idle models cost a bit-scan, while
                      // culled-but-still-animating models keep their bounds fresh so they can
                      // re-enter the frustum correctly. (Skipping animation work for culled
                      // models is the animation system's job via its renderer-based culling
                      // mode, which freezes the transforms and thus also skips this refresh.)
                      model_comp.update_armature();

                      auto& transform_comp = view.get<transform_component>(entity);
                      model_comp.update_world_bounds(transform_comp.get_transform_global());
                  });

}

void model_system::on_play_begin(hpp::span<const entt::handle> entities, delta_t dt)
{
    for(auto entity : entities)
    {
        if(auto model_comp = entity.try_get<model_component>())
        {
            model_comp->init_armature(false);

            auto& transform_comp = entity.get<transform_component>();

            model_comp->update_world_bounds(transform_comp.get_transform_global());
        }
    }
}

} // namespace unravel
