#include "model_system.h"
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/model_component.h>

#include <engine/ecs/ecs.h>
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/play_mode.h>
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
                     model_comp.resolve_pending_default_materials();
                     model_comp.init_armature(recreate_armature);
                 });
}

namespace
{
/// Destroys the node entities only a previous mesh of each model used.
void destroy_stale_armature_entities(scene& scn)
{
    // Collected first: destroying a node subtree can remove model components the view walks.
    std::vector<entt::handle> stale;
    scn.registry->view<model_component>().each(
        [&](auto entity, auto&& model_comp)
        {
            auto taken = model_comp.take_stale_armature_entities();
            stale.insert(stale.end(), taken.begin(), taken.end());
        });
    for(auto& entity : stale)
    {
        if(entity.valid())
        {
            scene::destroy_entity(entity);
        }
    }
}
} // namespace


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

void model_system::on_frame_update(scene& scn, const frame_times& times)
{
    APP_SCOPE_PERF("Model/System Update");

    constexpr bool recreate_armature = false;
    process_armatures<model_component, active_component>(scn, recreate_armature);

    // A running game records nothing for undo, so a script's mesh change drops the old mesh's
    // nodes at once. While editing, the editor removes them inside the change's undo step.
    if(engine::context().get_cached<play_mode>().is_active())
    {
        destroy_stale_armature_entities(scn);
    }
}

void model_system::on_frame_before_render(scene& scn, const frame_times& times)
{
    APP_SCOPE_PERF("Model/System Before Render");
    static const std::string thread_name = "Model/Pool Thread";
    auto view = scn.registry->view<transform_component, model_component, active_component>();

    auto frame = gfx::get_render_frame();

    // Retires the cached shadow caster lists when a STATIC caster changed. Only written on a
    // change, so the common frame never touches it; relaxed because the single bump below is
    // what publishes the result, after the pool has joined.
    std::atomic<bool> static_casters_changed{false};
    // Models that share an ancestor (a sword under a character's hand bone, props under one
    // group) would resolve it lazily from several tasks at once; resolve those chains first.
    for(const auto entity : view)
    {
        view.get<transform_component>(entity).resolve_shared_ancestors();
    }
    // Each task works with one model's hierarchy and only reads the ancestors resolved above.
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

                      // The overrides set since the last frame reach the materials this model draws with. Here,
                      // in the model's own task, so the passes below only read them.
                      model_comp.update_material_instances();

                      if(model_comp.is_newly_created())
                      {
                          model_comp.set_last_render_frame(frame);
                      }

                      auto& transform_comp = view.get<transform_component>(entity);

                      // The transform's render change slot: where the model is, its own move or
                      // a parent's. Velocity (motion vector) state promotes the previous-frame
                      // snapshot BEFORE update_armature overwrites the pose caches (they still
                      // hold last frame's values here). The call is frame-stamped internally and
                      // no-ops entirely while no pipeline requests velocity recording.
                      const bool is_moved = transform_comp.is_dirty(dirty_ids::render_change);
                      transform_comp.set_dirty(dirty_ids::render_change, false);
                      model_comp.record_velocity_state(frame,
                                                       transform_comp.get_transform_global().get_matrix(),
                                                       is_moved);

                      // Refresh pose-derived render data (submesh/bone poses, cached proxy
                      // bounds, skinning palettes) before world bounds are computed. This is
                      // change-driven, not visibility-driven: update_armature early-outs when
                      // no armature transform changed, so idle models cost a bit-scan, while
                      // culled-but-still-animating models keep their bounds fresh so they can
                      // re-enter the frustum correctly. (Skipping animation work for culled
                      // models is the animation system's job via its renderer-based culling
                      // mode, which freezes the transforms and thus also skips this refresh.)
                      const bool is_pose_changed = model_comp.update_armature();
                      // Node/bone animation moves submeshes without touching the owner
                      // transform; a refresh that changed the pose is motion evidence.
                      model_comp.mark_motion(is_pose_changed);

                      model_comp.update_world_bounds(transform_comp.get_transform_global());

                      // When each aspect of the model last changed, for every system that keeps
                      // what it rendered (render_interest).
                      const render_change::mask changes = (is_moved ? render_change::placement : render_change::none) |
                                                          (is_pose_changed ? render_change::pose : render_change::none);
                      model_comp.update_change_stamps(frame, changes);

                      // AFTER update_world_bounds: a bounds change it just published has to be
                      // seen this frame, not next. The model slot says its bounds moved or a
                      // setter touched it.
                      const bool are_bounds_changed = model_comp.is_dirty(dirty_ids::render_change);
                      model_comp.set_dirty(dirty_ids::render_change, false);

                      // Dynamic casters are re-read every frame by the shadow pass, so only a
                      // static one can invalidate the cache. A model that STOPPED being a
                      // static caster bumped the revision through model_component::touch().
                      if((is_moved || are_bounds_changed) && model_comp.is_static() && model_comp.casts_shadow())
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
