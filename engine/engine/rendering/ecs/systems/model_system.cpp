#include "model_system.h"
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/model_component.h>

#include <engine/ecs/ecs.h>
#include <engine/events.h>
#include <engine/profiler/profiler.h>

#include <logging/logging.h>

#include <concurrency/parallel.h>

namespace unravel
{

template<typename ...Ts>
void process_armatures(scene& scn, bool recreate_armature)
{
    // this pass can create new entities so we cannot parallelize it
    constexpr bool parallel = false;
    auto view = scn.registry->view<Ts...>();
    poolstl::for_each_par_if(parallel,
                 view.begin(),
                 view.end(),
                 [&](entt::entity entity)
                 {
                     auto& model_comp = view.template get<model_component>(entity);
                     model_comp.init_armature(recreate_armature);
                 });
}


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

    constexpr bool recreate_armature = true;
    process_armatures<model_component>(scn, recreate_armature);
}

void model_system::on_frame_update(scene& scn, delta_t dt)
{
    APP_SCOPE_PERF("Model/System Update");

    constexpr bool recreate_armature = false;
    process_armatures<model_component, active_component>(scn, recreate_armature);
}

void model_system::on_frame_before_render(scene& scn, delta_t dt)
{
    APP_SCOPE_PERF("Model/System Before Render");
    static const std::string thread_name = "Model/Pool Thread";
    auto view = scn.registry->view<transform_component, model_component, active_component>();

    auto frame = gfx::get_render_frame();

    // Retires the cached shadow caster lists when a STATIC caster changed. Only written on a
    // change, so the common frame never touches it; relaxed because the single bump below is
    // what publishes the result, after the pool has joined.
    std::atomic<bool> static_casters_changed{false};
    // this code should be thread safe as each task works with a whole hierarchy and
    // there is no interleaving between tasks.
    // Over the view's leading pool, not its iterator: an entt view iterator is forward-only and
    // poolstl re-walks it once per chunk (see for_each_entity_par).
    poolstl::for_each_entity_par(true,
                  view,
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

                      auto& transform_comp = view.get<transform_component>(entity);

                      // Velocity (motion vector) state: consume the transform dirty bit and
                      // promote the previous-frame snapshot BEFORE update_armature overwrites
                      // the pose caches (they still hold last frame's values here). The call
                      // is frame-stamped internally and no-ops entirely while no pipeline
                      // requests velocity recording.
                      const bool transform_moved =
                          transform_comp.is_dirty(dirty_ids::velocity);
                      transform_comp.set_dirty(dirty_ids::velocity, false);
                      model_comp.record_velocity_state(frame,
                                                       transform_comp.get_transform_global().get_matrix(),
                                                       transform_moved);

                      // Refresh pose-derived render data (submesh/bone poses, cached proxy
                      // bounds, skinning palettes) before world bounds are computed. This is
                      // change-driven, not visibility-driven: update_armature early-outs when
                      // no armature transform changed, so idle models cost a bit-scan, while
                      // culled-but-still-animating models keep their bounds fresh so they can
                      // re-enter the frustum correctly. (Skipping animation work for culled
                      // models is the animation system's job via its renderer-based culling
                      // mode, which freezes the transforms and thus also skips this refresh.)
                      const bool pose_refreshed = model_comp.update_armature();
                      // Node/bone animation moves submeshes without touching the owner
                      // transform; a pose refresh that actually ran is motion evidence.
                      model_comp.mark_motion(pose_refreshed);

                      model_comp.update_world_bounds(transform_comp.get_transform_global());

                      // AFTER update_world_bounds: a bounds change it just published has to be
                      // seen this frame, not next. The transform slot covers where the model
                      // is (including a moved parent), the model slot what it renders as.
                      const bool caster_dirty =
                          transform_comp.is_dirty(dirty_ids::shadow_caster) ||
                          model_comp.is_dirty(dirty_ids::shadow_caster);
                      transform_comp.set_dirty(dirty_ids::shadow_caster, false);
                      model_comp.set_dirty(dirty_ids::shadow_caster, false);

                      // Dynamic casters are re-read every frame by the shadow pass, so only a
                      // static one can invalidate the cache. A model that STOPPED being a
                      // static caster bumped the revision through model_component::touch().
                      if(caster_dirty && model_comp.is_static() && model_comp.casts_shadow())
                      {
                          static_casters_changed.store(true, std::memory_order_relaxed);
                      }
                  });

    if(static_casters_changed.load(std::memory_order_relaxed))
    {
        bump_shadow_caster_revision(*scn.registry, entt::null);
    }
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
