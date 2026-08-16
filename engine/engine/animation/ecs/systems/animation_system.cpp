#include "animation_system.h"
#include <engine/animation/animation.h>
#include <engine/animation/ecs/components/animation_component.h>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/events.h>
#include <engine/physics/ecs/components/physics_component.h>
#include <engine/rendering/ecs/components/model_component.h>

#include <engine/ecs/ecs.h>
#include <glm/gtc/epsilon.hpp>
#include <engine/engine.h>
#include <engine/profiler/profiler.h>
#include <engine/threading/threader.h>
#include <logging/logging.h>

#include <concurrency/parallel.h>

#include <algorithm>
#include <utility>

namespace unravel
{

namespace
{
auto remap_root_motion_translation(const math::vec3& delta, const math::quat& correction) noexcept -> math::vec3
{
    if(math::all(math::epsilonEqual(correction,
                                     math::identity<math::quat>(),
                                     math::epsilon<float>())))
    {
        return delta;
    }

    return correction * delta;
}

auto remap_root_motion_rotation(const math::quat& delta, const math::quat& correction) noexcept -> math::quat
{
    if(math::all(math::epsilonEqual(correction,
                                     math::identity<math::quat>(),
                                     math::epsilon<float>())))
    {
        return delta;
    }

    return correction * delta * glm::conjugate(correction);
}

void on_play_begin_impl(animation_component& comp)
{
    if(comp.get_autoplay())
    {
        auto& player = comp.get_player();
        player.blend_to(0, comp.get_animation());
        player.play();
    }
}
} // namespace

auto animation_system::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);
    auto& ev = ctx.get_cached<events>();

    ev.on_play_begin.connect(sentinel_, 10, this, &animation_system::on_play_begin);
    ev.on_play_end.connect(sentinel_, -10, this, &animation_system::on_play_end);
    ev.on_pause.connect(sentinel_, 10, this, &animation_system::on_pause);
    ev.on_resume.connect(sentinel_, -10, this, &animation_system::on_resume);
    ev.on_skip_next_frame.connect(sentinel_, 10, this, &animation_system::on_skip_next_frame);

    return true;
}

auto animation_system::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
}

void animation_system::on_create_component(entt::registry& r, entt::entity e)
{
}

void animation_system::on_destroy_component(entt::registry& r, entt::entity e)
{
}

void animation_system::on_play_begin(rtti::context& ctx)
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();

    scn.registry->view<animation_component>().each(
        [&](auto e, auto&& animation_comp)
        {
            on_play_begin_impl(animation_comp);
        });
}

void animation_system::on_play_begin(hpp::span<const entt::handle> entities, delta_t dt)
{
    for(auto entity : entities)
    {
        if(auto animation_comp = entity.try_get<animation_component>())
        {
            on_play_begin_impl(*animation_comp);
        }
    }
}

void animation_system::on_play_end(rtti::context& ctx)
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();

    scn.registry->view<animation_component>().each(
        [&](auto e, auto&& animation_comp)
        {
            auto& player = animation_comp.get_player();
            player.stop();
        });
}

void animation_system::on_pause(rtti::context& ctx)
{
}

void animation_system::on_resume(rtti::context& ctx)
{
}

void animation_system::on_skip_next_frame(rtti::context& ctx)
{
    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();

    delta_t step(1.0f / 60.0f);
    on_update(scn, step, true);
}

void animation_system::on_update(scene& scn, delta_t dt, bool force)
{
    APP_SCOPE_PERF("Animation/System Update");

    static const std::string thread_name = "Animation/System Update Thread";
    auto& ctx = engine::context();
    auto& th = ctx.get_cached<threader>();
    // Create a view for entities with transform_component and submesh_component
    auto view = scn.registry->view<model_component, animation_component, transform_component>();

    // Pose application writes armature transforms, and transform dirty flags
    // propagate down the hierarchy. Two animated entities whose hierarchies
    // overlap (e.g. an animated prop parented under another character's bone)
    // must therefore be updated by the same task. Tag every animated entity
    // with its topmost animated ancestor and turn runs of equal roots into
    // tasks - disjoint hierarchies (the common case) still run fully in
    // parallel. The scratch buffers are members reused across frames, so the
    // steady state performs no heap allocations here.
    grouping_scratch_.clear();
    bool any_nested = false;
    for(auto entity : view)
    {
        entt::entity group_root = entity;
        auto parent = view.get<transform_component>(entity).get_parent();
        while(parent)
        {
            if(parent.all_of<model_component, animation_component, transform_component>())
            {
                group_root = parent.entity();
            }
            parent = parent.get<transform_component>().get_parent();
        }
        any_nested |= group_root != entity;
        grouping_scratch_.emplace_back(group_root, entity);
    }

    if(any_nested)
    {
        // Bring equal roots together. Entities can only share a root when
        // nesting exists, so most frames skip the sort entirely.
        std::sort(grouping_scratch_.begin(), grouping_scratch_.end());
    }

    group_ranges_.clear();
    for(size_t begin = 0; begin < grouping_scratch_.size();)
    {
        size_t end = begin + 1;
        while(end < grouping_scratch_.size() && grouping_scratch_[end].first == grouping_scratch_[begin].first)
        {
            ++end;
        }
        group_ranges_.emplace_back(begin, end);
        begin = end;
    }

    auto update_entity = [&](entt::entity entity)
                  {
                      APP_SCOPE_PERF_THREAD("Animation/Update Poses", "Pool Thread");

                      // This is needed as we call .get on the animations inside the player
                      tpp::this_thread::register_this_thread(thread_name, true);

                      auto& animation_comp = view.get<animation_component>(entity);
                      auto& model_comp = view.get<model_component>(entity);

                      bool should_update_poses = true;
                      if(animation_comp.get_culling_mode() == animation_component::culling_mode::renderer_based)
                      {
                          if(!model_comp.was_used_last_frame())
                          {
                              should_update_poses = false;
                          }
                      }

                      auto& player = animation_comp.get_player();

                      // Apply speed to delta time
                      auto speed = animation_comp.get_speed();
                      auto adjusted_dt = dt * speed;

                      bool updated = player.update_time(adjusted_dt, force);

                      if(updated && should_update_poses)
                      {
                          auto& transform_comp = view.get<transform_component>(entity);
                          // auto physics_comp_ptr = scn.registry->try_get<physics_component>(entity);

                          bool apply_root_motion = animation_comp.get_apply_root_motion();
                          auto retargeting_mode = animation_comp.get_retargeting_mode();
                          const auto facing_correction = model_comp.get_facing_adjustment_rotation();

                          player.update_poses(
                              model_comp.get_bind_pose(),
                              retargeting_mode,
                              [&](const animation_pose::node_desc& desc,
                                  const math::transform& transform,
                                  const animation_pose::root_motion_result& motion_result)
                              {
                                  // Resolve armature entity based on retargeting mode
                                  auto armature_index = desc.index;
                                  if(retargeting_mode == animation_retargeting_mode::name_based)
                                  {
                                      // Name-based: use cached name lookup
                                      int name_based_armature_index =
                                          model_comp.get_armature_index_by_name_cached(desc.name);
                                      if(name_based_armature_index < 0)
                                      {
                                          // The target skeleton has no bone with this name.
                                          // Skip the node rather than falling back to the
                                          // clip's index, which would write an unrelated bone.
                                          return;
                                      }
                                      armature_index = size_t(name_based_armature_index);
                                  }

                                  auto armature = model_comp.get_armature_by_index(armature_index);
                                  if(armature)
                                  {
                                      auto& armature_transform_comp = armature.template get<transform_component>();

                                      bool processed_by_root_motion = false;

                                      bool is_root_position_node = false;
                                      bool is_root_rotation_node = false;
                                      if(retargeting_mode == animation_retargeting_mode::name_based)
                                      {
                                          is_root_position_node = desc.name == motion_result.root_position_node_name;
                                          is_root_rotation_node = desc.name == motion_result.root_rotation_node_name;
                                      }
                                      else
                                      {
                                          is_root_position_node = desc.index == motion_result.root_position_node_index;
                                          is_root_rotation_node = desc.index == motion_result.root_rotation_node_index;
                                      }

                                      if(apply_root_motion && is_root_position_node)
                                      {
                                          armature_transform_comp.set_scale_local(transform.get_scale());

                                          auto position_local = armature_transform_comp.get_position_local();
                                          auto result_positon_local = math::mix(position_local,
                                                                                transform.get_position(),
                                                                                motion_result.bone_position_weights);
                                          armature_transform_comp.set_position_local(result_positon_local);

                                          math::vec3 delta_translation_logical = remap_root_motion_translation(
                                              motion_result.root_transform_delta.get_translation(),
                                              facing_correction);

                                          // // Apply scaling if needed (for example, if BoneRoot’s scale differs
                                          // significantly)
                                          auto scale_global = armature_transform_comp.get_scale_global();
                                          delta_translation_logical *= scale_global;

                                          // Blend translation as needed:
                                          auto result_move_local = math::mix(math::zero<math::vec3>(),
                                                                             delta_translation_logical,
                                                                             motion_result.root_position_weights);
                                          // APPLOG_INFO("position_weights {}", motion_result.position_weights);
                                          // if(physics_comp_ptr)
                                          // {
                                          //     if(math::length2(result_move_local) > 0.0f)
                                          //     {
                                          //         auto global_delta =
                                          //             armature_transform_comp.get_transform_global().transform_normal(
                                          //                 result_move_local);
                                          //         auto rm_velocity = global_delta / dt.count();
                                          //         physics_comp_ptr->set_velocity(rm_velocity);
                                          //     }
                                          // }
                                          // else
                                          {
                                              transform_comp.move_by_local(result_move_local);
                                          }
                                          processed_by_root_motion = true;
                                      }

                                      if(apply_root_motion && is_root_rotation_node)
                                      {
                                          armature_transform_comp.set_scale_local(transform.get_scale());

                                          auto rotation_local = armature_transform_comp.get_rotation_local();
                                          auto result_rotation_local = math::slerp(rotation_local,
                                                                                   transform.get_rotation(),
                                                                                   motion_result.bone_rotation_weight);
                                          armature_transform_comp.set_rotation_local(result_rotation_local);

                                          // --- Rotation ---
                                          math::quat delta_rotation_logical = remap_root_motion_rotation(
                                              motion_result.root_transform_delta.get_rotation(),
                                              facing_correction);

                                          // Optionally blend this delta toward identity:
                                          auto result_rotate_local = math::slerp(math::identity<math::quat>(),
                                                                                 delta_rotation_logical,
                                                                                 motion_result.root_rotation_weight);
                                          transform_comp.rotate_by_local(result_rotate_local);

                                          processed_by_root_motion = true;
                                      }

                                      if(!processed_by_root_motion)
                                      {
                                          armature_transform_comp.set_transform_local(transform);
                                      }

                                      // if(desc.index == root_motion_entity_index)
                                      // {
                                      //     model_comp.update_world_bounds(
                                      //         armature_transform_comp.get_transform_global());
                                      // }
                                  }
                                  else
                                  {
                                      APPLOG_WARNING("Cannot find armature with index {}", desc.index);
                                  }
                              });
                      }
                  };

    poolstl::for_each_par_if(true,
                  group_ranges_.begin(),
                  group_ranges_.end(),
                  [&](const std::pair<size_t, size_t>& range)
                  {
                      for(size_t i = range.first; i < range.second; ++i)
                      {
                          update_entity(grouping_scratch_[i].second);
                      }
                  });
}

void animation_system::on_frame_update(scene& scn, delta_t dt)
{
    on_update(scn, dt, false);
}

} // namespace unravel
