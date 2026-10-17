#include "animation_system.h"
#include <engine/animation/animation.h>
#include <engine/animation/ecs/components/animation_component.h>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/events.h>
#include <engine/physics/ecs/components/character_controller_component.h>
#include <engine/rendering/ecs/components/model_component.h>

#include <engine/ecs/ecs.h>
#include <engine/engine.h>
#include <engine/play_mode.h>
#include <engine/profiler/profiler.h>
#include <engine/threading/threader.h>
#include <logging/logging.h>

#include <concurrency/parallel.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace unravel
{

namespace
{
/**
 * @brief The transform from the parent space of model node `node_index` into the model entity's
 * space, composed from local transforms: the root delta lives in that parent's frame, which may be
 * rotated or scaled (a Z-up, centimeter Rig node) or animated (a pelvis under a turning root). No
 * world transform is resolved: other characters may share the entity's ancestors.
 */
auto get_node_parent_frame(const model_component& model, entt::handle model_entity, int node_index) -> math::mat4
{
    math::mat4 frame(1.0f);
    const auto node = node_index >= 0 ? model.get_armature_by_index(size_t(node_index)) : entt::handle{};
    if(!node)
    {
        return frame;
    }
    for(entt::handle parent = node.get<transform_component>().get_parent(); parent && parent != model_entity;)
    {
        const auto& parent_transform = parent.get<transform_component>();
        frame = parent_transform.get_transform_local().get_matrix() * frame;
        parent = parent_transform.get_parent();
    }
    return frame;
}

/// The rotation of a linear map with scale (its axes normalized).
auto get_frame_rotation(const math::mat4& frame) -> math::quat
{
    math::mat3 axes(frame);
    for(int i = 0; i < 3; ++i)
    {
        const float length = math::length(axes[i]);
        axes[i] = length > 0.0f ? axes[i] / length : axes[i];
    }
    return math::normalize(glm::quat_cast(axes));
}

auto is_finite(const math::vec3& v) -> bool
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

auto is_finite(const math::quat& q) -> bool
{
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}

/**
 * @brief Moves the model entity by a frame's root motion: the delta is carried from the root
 * node's parent frame into the entity's own space, so it follows every rotation and scale between
 * them, then the entity steps along its own axes (its scale included) and turns about its own up.
 * A character controller takes the step as a world move instead, so it collides.
 */
void move_by_root_motion(const animation_pose::root_motion_result& motion,
                       const model_component& model,
                       entt::handle model_entity,
                       transform_component& entity_transform,
                       character_controller_component* character)
{
    math::vec3 move(0.0f);
    if(motion.root_position_node_index >= 0)
    {
        const math::vec3 delta =
            math::mix(math::vec3(0.0f), motion.root_transform_delta.get_translation(), motion.root_position_weights);
        move = math::mat3(get_node_parent_frame(model, model_entity, motion.root_position_node_index)) * delta;
    }
    math::quat turn = math::identity<math::quat>();
    if(motion.root_rotation_node_index >= 0)
    {
        const math::quat frame = get_frame_rotation(get_node_parent_frame(model, model_entity, motion.root_rotation_node_index));
        const math::quat delta =
            math::slerp(math::identity<math::quat>(), motion.root_transform_delta.get_rotation(), motion.root_rotation_weight);
        turn = math::normalize(frame * delta * glm::inverse(frame));
    }
    if(!is_finite(move) || !is_finite(turn))
    {
        return;
    }
    // The step is in the entity's frame before this frame's turn, so it moves first.
    if(character != nullptr)
    {
        character->move(math::mat3(entity_transform.get_transform_global().get_matrix()) * move);
    }
    else
    {
        entity_transform.set_position_local(entity_transform.get_position_local() +
                                            entity_transform.get_rotation_local() * (entity_transform.get_scale_local() * move));
    }
    entity_transform.set_rotation_local(math::normalize(entity_transform.get_rotation_local() * turn));
}

void on_play_begin_impl(animation_component& comp)
{
    if(comp.get_autoplay())
    {
        auto& player = comp.get_player();
        if(auto* controller = comp.get_controller_instance())
        {
            // The controller enters its default states on its first update.
            controller->reset();
        }
        else
        {
            player.blend_to(0, comp.get_animation());
        }
        player.play();
    }
}

/**
 * @brief Bone names and parent indices of a model's armature, for layer masks.
 */
void describe_skeleton(const model_component& model, animation_skeleton_desc& desc)
{
    const auto& armature = model.get_armature_entities();
    desc.names.resize(armature.size());
    desc.parents.assign(armature.size(), -1);
    std::unordered_map<entt::entity, int> index_of;
    index_of.reserve(armature.size());
    for(size_t i = 0; i < armature.size(); ++i)
    {
        if(armature[i])
        {
            index_of[armature[i].entity()] = int(i);
            if(auto* tag = armature[i].try_get<tag_component>())
            {
                desc.names[i] = tag->name;
            }
        }
    }
    for(size_t i = 0; i < armature.size(); ++i)
    {
        if(!armature[i])
        {
            continue;
        }
        auto parent = armature[i].get<transform_component>().get_parent();
        if(parent)
        {
            auto it = index_of.find(parent.entity());
            desc.parents[i] = it != index_of.end() ? it->second : -1;
        }
    }
}
/**
 * @brief The model's armature as rest-aligned retargeting needs it: parents and bind locals by
 * armature index.
 */
void describe_target_skeleton(const model_component& model, animation_target_skeleton& skeleton)
{
    animation_skeleton_desc desc;
    describe_skeleton(model, desc);
    skeleton.set_bones(std::move(desc.names), std::move(desc.parents), model.get_bind_pose());
}
} // namespace

auto animation_system::make_skeleton_binding(animation_component& animation, const model_component& model)
    -> animation_skeleton_binding
{
    animation_skeleton_binding binding;
    if(animation.get_retargeting_mode() != animation_retargeting_mode::name_based)
    {
        return binding;
    }
    binding.version = model.get_armature_version() * 2;
    binding.resolve = [&model](const animation_channel& channel)
    {
        const int index = model.get_armature_index_by_name_cached(channel.node_name);
        // Rigs of one family prefix their bones differently ("mixamorig:" and "mixamorig1:").
        return index >= 0 ? index : model.get_armature_index_by_base_name_cached(channel.node_name);
    };
    if(animation.get_pose_retargeting() == animation_pose_retargeting::rest_aligned)
    {
        binding.version += 1;
        binding.rest_aligned_target = get_target_skeleton(animation, model);
    }
    return binding;
}

auto animation_system::get_target_skeleton(animation_component& animation, const model_component& model)
    -> const std::shared_ptr<const animation_target_skeleton>&
{
    return animation.get_target_skeleton(model.get_armature_version(),
                                         [&model](animation_target_skeleton& skeleton)
                                         {
                                             describe_target_skeleton(model, skeleton);
                                         });
}

auto animation_system::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);
    auto& ev = ctx.get_cached<events>();

    ev.on_play_begin.connect(sentinel_, 10, this, &animation_system::on_play_begin);
    ev.on_play_end.connect(sentinel_, -10, this, &animation_system::on_play_end);
    ev.on_pause.connect(sentinel_, 10, this, &animation_system::on_pause);
    ev.on_resume.connect(sentinel_, -10, this, &animation_system::on_resume);

    return true;
}

auto animation_system::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
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

void animation_system::on_update(scene& scn, delta_t dt)
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
        // World transforms resolve lazily into caches: an ancestor several characters share is
        // resolved here, on one thread, before the pool reads it (character controllers move in
        // world space).
        if(parent)
        {
            parent.get<transform_component>().get_transform_global();
        }
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

    // Character controllers only move while the simulation runs.
    const bool is_simulating = ctx.has<play_mode>() && ctx.get_cached<play_mode>().is_active();

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

                      // A controller decides what the player plays before time advances, so a
                      // transition it fires is visible in this frame's pose.
                      if(player.is_playing())
                      {
                          if(auto* controller = animation_comp.try_get_controller_instance())
                          {
                              controller->update(player,
                                                 model_comp.get_armature_version(),
                                                 [&model_comp](animation_skeleton_desc& desc)
                                                 {
                                                     describe_skeleton(model_comp, desc);
                                                 });
                          }
                      }

                      bool updated = player.update_time(adjusted_dt);

                      if(updated && should_update_poses)
                      {
                          auto& transform_comp = view.get<transform_component>(entity);

                          const bool apply_root_motion = animation_comp.get_apply_root_motion();
                          auto* character = is_simulating
                                                ? scn.registry->try_get<character_controller_component>(entity)
                                                : nullptr;

                          // Clips are sampled straight onto armature indices: name-based retargeting
                          // resolves each channel once per (clip, armature) instead of per bone per
                          // frame, and clips exported from different files blend bone by bone.
                          const animation_skeleton_binding binding = make_skeleton_binding(animation_comp, model_comp);

                          animation_pose::root_motion_result root_motion;
                          bool has_root_motion = false;
                          player.update_poses(
                              model_comp.get_bind_pose(),
                              binding,
                              apply_root_motion,
                              [&](const animation_pose::node_desc& desc,
                                  const math::transform& transform,
                                  const animation_pose::root_motion_result& motion_result)
                              {
                                  auto armature = model_comp.get_armature_by_index(desc.index);
                                  if(!armature)
                                  {
                                      APPLOG_WARNING("Cannot find armature with index {}", desc.index);
                                      return;
                                  }
                                  auto& armature_transform_comp = armature.template get<transform_component>();

                                  const bool is_root_position_node = int(desc.index) == motion_result.root_position_node_index;
                                  const bool is_root_rotation_node = int(desc.index) == motion_result.root_rotation_node_index;
                                  // A root node takes what the root motion does not own from the pose:
                                  // a pelvis that only translates still turns with the animation.
                                  math::transform local = transform;
                                  if(apply_root_motion && is_root_position_node)
                                  {
                                      local.set_position(math::mix(armature_transform_comp.get_position_local(),
                                                                   transform.get_position(),
                                                                   motion_result.bone_position_weights));
                                  }
                                  if(apply_root_motion && is_root_rotation_node)
                                  {
                                      local.set_rotation(math::slerp(armature_transform_comp.get_rotation_local(),
                                                                     transform.get_rotation(),
                                                                     motion_result.bone_rotation_weight));
                                  }
                                  armature_transform_comp.set_transform_local(local);
                                  if(is_root_position_node || is_root_rotation_node)
                                  {
                                      root_motion = motion_result;
                                      has_root_motion = true;
                                  }
                              });

                          // Once per frame, after the pose is written: the root node's parent frame
                          // is the posed one.
                          if(apply_root_motion && has_root_motion)
                          {
                              move_by_root_motion(root_motion, model_comp, entt::handle(*scn.registry, entity), transform_comp, character);
                          }
                      }
                      else if(updated)
                      {
                          // Poses were skipped: the root motion they missed is not made up in one jump
                          // when the character is seen again.
                          player.reset_root_motion();
                      }
                  };

    // A vector of ranges, not a view: already random access, so the plain range form is the
    // right one here (for_each_entity_par is for entt views, whose iterators are forward-only).
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
    on_update(scn, dt);
}

} // namespace unravel
