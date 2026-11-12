#include "rig_setup.h"

#include <editor/editing/actions/composite_action.h>
#include <editor/editing/actions/entity_actions.h>
#include <editor/editing/actions/lambda_actions.h>
#include <editor/editing/editing_manager.h>
#include <engine/animation/animation_humanoid.h>
#include <engine/animation/ecs/components/animation_component.h>
#include <engine/animation/ecs/components/ik_components.h>
#include <engine/animation/ecs/systems/animation_system.h>
#include <engine/animation/ecs/systems/rig_system.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/model_component.h>

#include <algorithm>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace unravel::rig_setup
{
namespace
{
/// A created look-at target sits this far in front of the eyes, in meters.
constexpr float LOOK_TARGET_DISTANCE = 2.0f;
/// A created pole sits this fraction of the limb length in front of the middle joint.
constexpr float POLE_DISTANCE_FRACTION = 0.5f;
/// Bend offsets shorter than this cannot give the pole a direction.
constexpr float MIN_BEND = 1e-4f;
/// A limb axis shorter than this (squared) has no direction to measure the bend from.
constexpr float MIN_AXIS_LENGTH_SQUARED = 1e-8f;
/// How deep below the character Auto Setup looks for the model with the skeleton.
constexpr int MAX_MODEL_SEARCH_DEPTH = 4;

auto find_transform(entt::handle entity) -> transform_component*
{
    return entity && entity.valid() ? entity.try_get<transform_component>() : nullptr;
}

auto get_entity_name(entt::handle entity) -> std::string
{
    auto* tag = entity && entity.valid() ? entity.try_get<tag_component>() : nullptr;
    return tag ? tag->name : std::string{};
}

/// The entity whose model owns the bone: the bone itself or its first ancestor with a model.
auto find_bone_model_owner(entt::handle bone) -> entt::handle
{
    for(entt::handle current = bone; current && current.valid();)
    {
        if(current.all_of<model_component>())
        {
            return current;
        }
        auto* transform = current.try_get<transform_component>();
        current = transform ? transform->get_parent() : entt::handle{};
    }
    return {};
}

/// The entity with the character's skinned model: the character itself or the first one under it.
auto find_character_model(entt::handle character, int depth = 0) -> entt::handle
{
    auto* model = character && character.valid() ? character.try_get<model_component>() : nullptr;
    if(model != nullptr && !model->get_armature_entities().empty())
    {
        return character;
    }
    auto* transform = find_transform(character);
    if(transform == nullptr || depth >= MAX_MODEL_SEARCH_DEPTH)
    {
        return {};
    }
    for(const auto& child : transform->get_children())
    {
        if(auto found = find_character_model(child, depth + 1))
        {
            return found;
        }
    }
    return {};
}

/**
 * @brief The humanoid role of every armature bone of the model on `model_owner`: the roles
 * rest-aligned retargeting finds from the skeleton's bind pose, which the Animation inspector's
 * Humanoid Mapping lists, so a posed character gets the same answer.
 */
auto find_humanoid_roles(entt::handle model_owner, const model_component& model) -> humanoid_map
{
    if(auto* animation = model_owner.try_get<animation_component>())
    {
        if(const auto& skeleton = animation_system::get_target_skeleton(*animation, model))
        {
            return skeleton->humanoid;
        }
    }
    // Without an animation component nothing caches the target skeleton: the same one, built here.
    const auto& bones = model.get_armature_entities();
    std::unordered_map<entt::entity, int> index_of;
    for(size_t i = 0; i < bones.size(); ++i)
    {
        index_of[bones[i].entity()] = int(i);
    }
    std::vector<std::string> names(bones.size());
    std::vector<int> parents(bones.size(), -1);
    for(size_t i = 0; i < bones.size(); ++i)
    {
        names[i] = get_entity_name(bones[i]);
        if(auto* transform = find_transform(bones[i]))
        {
            const auto parent = transform->get_parent();
            const auto it = parent ? index_of.find(parent.entity()) : index_of.end();
            parents[i] = it != index_of.end() ? it->second : -1;
        }
    }
    animation_target_skeleton skeleton;
    skeleton.set_bones(std::move(names), std::move(parents), model.get_bind_pose());
    skeleton.compute_rest_globals();
    skeleton.compute_humanoid_bones();
    return skeleton.humanoid;
}

/// The armature bone with a humanoid role, or a null handle.
auto get_role_bone(const model_component& model, const humanoid_map& roles, humanoid_bone role) -> entt::handle
{
    const int index = roles[size_t(role)];
    const auto& bones = model.get_armature_entities();
    return index >= 0 && size_t(index) < bones.size() ? bones[size_t(index)] : entt::handle{};
}

/**
 * @brief Edits a component and records, on a prefab instance, an override for each field the edit
 * changed. Returns what undo hands to prefab_override_context::restore_overrides.
 */
template<typename Component, typename Edit>
auto edit_recording_overrides(entt::handle entity, const Edit& edit) -> prefab_override_record
{
    const auto type = entt::resolve<Component>();
    const auto state_before = prefab_override_context::save_component_state(entity, type);
    edit(entity.get<Component>());
    return prefab_override_context::mark_changed_properties(entity, type, state_before);
}

/**
 * @brief Where a created target or pole goes: under the character the rig belongs to, or at the
 * scene root when the modifier poses that entity itself (a plain hierarchy whose top is a joint),
 * so the target never moves with what it drives.
 */
auto find_created_target_parent(entt::handle owner, int joints) -> entt::handle
{
    const auto character = find_rig_character(owner);
    std::vector<entt::handle> chain;
    collect_rig_chain(owner, joints, chain);
    const bool is_posed = std::find(chain.begin(), chain.end(), character) != chain.end();
    return is_posed ? entt::handle{} : character;
}

/// Where a created target or pole goes and what it is called.
struct created_pose
{
    std::string name;
    math::vec3 position{};
    math::quat rotation{math::identity<math::quat>()};
};

/**
 * @brief Creates an entity at a world pose under find_created_target_parent() and assigns it to the
 * component's `field`, as one undo step. Returns the new entity.
 */
template<typename Component>
auto create_and_assign(rtti::context& ctx,
                       entt::handle owner,
                       int joints,
                       entt::handle Component::*field,
                       const created_pose& pose) -> entt::handle
{
    auto& em = ctx.get_cached<editing_manager>();
    auto* active_scene = em.get_active_scene(ctx);
    if(active_scene == nullptr || !owner)
    {
        return {};
    }
    const auto parent = find_created_target_parent(owner, joints);
    const auto owner_uh = entt::make_uhandle(owner);
    auto created_uh = std::make_shared<entt::uhandle>();
    auto overrides = std::make_shared<prefab_override_record>();
    auto sequence = std::make_shared<sequence_action_t>();
    sequence->add_step(
        [active_scene, parent, pose, created_uh]() -> std::shared_ptr<editing_action_t>
        {
            return std::make_shared<create_entities_action_t>(
                [active_scene, parent, pose, created_uh]() -> entt::handle
                {
                    auto created = active_scene->create_entity(pose.name, parent);
                    auto& transform = created.get<transform_component>();
                    transform.set_position_global(pose.position);
                    transform.set_rotation_global(pose.rotation);
                    *created_uh = entt::make_uhandle(created);
                    return created;
                });
        });
    sequence->add_step(
        [owner_uh, created_uh, field, overrides]() -> std::shared_ptr<editing_action_t>
        {
            return std::make_shared<tracked_lambda_action_t>(
                [owner_uh, created_uh, field, overrides]()
                {
                    if(auto entity = owner_uh.resolve(); entity && entity.all_of<Component>())
                    {
                        // On a prefab instance the assignment is an override, kept when it re-syncs.
                        *overrides = edit_recording_overrides<Component>(entity,
                                                                         [&](Component& component)
                                                                         {
                                                                             component.*field = created_uh->resolve();
                                                                         });
                    }
                },
                [owner_uh, field, overrides]()
                {
                    if(auto entity = owner_uh.resolve(); entity && entity.all_of<Component>())
                    {
                        entity.get<Component>().*field = {};
                        prefab_override_context::restore_overrides(*overrides);
                    }
                });
        });
    em.do_action("Create " + pose.name, sequence);
    return created_uh->resolve();
}

/// Pole position in front of the middle joint of a chain, along its current bend; a straight limb
/// bends toward `fallback_direction`.
auto compute_pole_position(const std::vector<math::vec3>& joints, const math::vec3& fallback_direction) -> math::vec3
{
    const math::vec3& root = joints.front();
    const math::vec3& tip = joints.back();
    const math::vec3& middle = joints[joints.size() / 2];
    float limb_length = 0.0f;
    for(size_t i = 0; i + 1 < joints.size(); ++i)
    {
        limb_length += math::length(joints[i + 1] - joints[i]);
    }
    const math::vec3 axis = tip - root;
    const float axis_length_squared = math::dot(axis, axis);
    const math::vec3 on_axis = axis_length_squared > MIN_AXIS_LENGTH_SQUARED
                                   ? root + axis * (math::dot(middle - root, axis) / axis_length_squared)
                                   : root;
    math::vec3 bend = middle - on_axis;
    if(math::length(bend) < MIN_BEND)
    {
        bend = fallback_direction;
    }
    return middle + math::normalize(bend) * (limb_length * POLE_DISTANCE_FRACTION);
}

/// The way a straight limb ending at `effector` bends: elbows back, knees and anything else forward.
auto get_straight_limb_bend(entt::handle effector) -> math::vec3
{
    const math::vec3 forward = get_rig_character_forward(effector);
    const auto model_owner = find_bone_model_owner(effector);
    auto* model = model_owner ? model_owner.try_get<model_component>() : nullptr;
    if(model == nullptr || !effector.all_of<bone_component>())
    {
        return forward;
    }
    const auto roles = find_humanoid_roles(model_owner, *model);
    const bool is_hand = get_role_bone(*model, roles, humanoid_bone::left_hand) == effector ||
                         get_role_bone(*model, roles, humanoid_bone::right_hand) == effector;
    return is_hand ? -forward : forward;
}

/// Calls @p visit with the modifier's component on @p owner and the number of joints its chain has; false when
/// @p owner has no such modifier.
template<typename Visit>
auto visit_modifier(entt::handle owner, ik_modifier modifier, const Visit& visit) -> bool
{
    if(!owner || !owner.valid())
    {
        return false;
    }
    switch(modifier)
    {
        case ik_modifier::two_bone:
            if(auto* component = owner.try_get<two_bone_ik_component>())
            {
                visit(*component, RIG_TWO_BONE_JOINTS);
                return true;
            }
            return false;
        case ik_modifier::chain:
            if(auto* component = owner.try_get<chain_ik_component>())
            {
                visit(*component, component->chain_length + 1);
                return true;
            }
            return false;
        case ik_modifier::look_at:
            if(auto* component = owner.try_get<look_at_ik_component>())
            {
                visit(*component, component->chain_length);
                return true;
            }
            return false;
    }
    return false;
}

/// A slot's entity as undo resolves it later; empty for an empty slot.
auto make_slot(entt::handle entity) -> entt::uhandle
{
    return entity ? entt::make_uhandle(entity) : entt::uhandle{};
}

} // namespace

auto find_foot_ik_bones(entt::handle character) -> foot_ik_bones
{
    foot_ik_bones bones;
    const auto model_owner = find_character_model(character);
    auto* model = model_owner ? model_owner.try_get<model_component>() : nullptr;
    if(model == nullptr)
    {
        return bones;
    }
    const auto roles = find_humanoid_roles(model_owner, *model);
    bones.left_foot = get_role_bone(*model, roles, humanoid_bone::left_foot);
    bones.right_foot = get_role_bone(*model, roles, humanoid_bone::right_foot);
    bones.pelvis = get_role_bone(*model, roles, humanoid_bone::hips);
    return bones;
}

auto auto_setup_foot_ik(rtti::context& ctx, entt::handle owner) -> foot_ik_bones
{
    const auto found = find_foot_ik_bones(owner);
    auto* data = owner && owner.valid() ? owner.try_get<foot_ik_component>() : nullptr;
    if(data == nullptr)
    {
        return found;
    }
    const auto pick = [](entt::handle chosen, entt::handle current)
    {
        return chosen ? chosen : current;
    };
    const foot_ik_bones before{data->left_foot, data->right_foot, data->pelvis};
    const foot_ik_bones after{pick(found.left_foot, data->left_foot),
                              pick(found.right_foot, data->right_foot),
                              pick(found.pelvis, data->pelvis)};
    if(after.left_foot == before.left_foot && after.right_foot == before.right_foot && after.pelvis == before.pelvis)
    {
        return found;
    }
    const auto owner_uh = entt::make_uhandle(owner);
    // On a prefab instance only the slots that change become overrides; undo takes back the ones it added.
    auto overrides = std::make_shared<prefab_override_record>();
    auto& em = ctx.get_cached<editing_manager>();
    em.do_action(
        "Foot IK Auto Setup",
        [owner_uh, overrides, left = make_slot(after.left_foot), right = make_slot(after.right_foot), pelvis = make_slot(after.pelvis)]()
        {
            if(auto entity = owner_uh.resolve(); entity && entity.all_of<foot_ik_component>())
            {
                *overrides = edit_recording_overrides<foot_ik_component>(entity,
                                                                         [&](foot_ik_component& component)
                                                                         {
                                                                             component.left_foot = left.resolve();
                                                                             component.right_foot = right.resolve();
                                                                             component.pelvis = pelvis.resolve();
                                                                         });
            }
        },
        [owner_uh, overrides, left = make_slot(before.left_foot), right = make_slot(before.right_foot), pelvis = make_slot(before.pelvis)]()
        {
            if(auto entity = owner_uh.resolve(); entity && entity.all_of<foot_ik_component>())
            {
                auto& component = entity.get<foot_ik_component>();
                component.left_foot = left.resolve();
                component.right_foot = right.resolve();
                component.pelvis = pelvis.resolve();
                prefab_override_context::restore_overrides(*overrides);
            }
        });
    return found;
}

auto has_ik_modifier(entt::handle owner, ik_modifier modifier) -> bool
{
    return visit_modifier(owner,
                          modifier,
                          [](const auto&, int)
                          {
                          });
}

auto create_ik_target(rtti::context& ctx, entt::handle owner, ik_modifier modifier) -> entt::handle
{
    auto* owner_transform = find_transform(owner);
    if(owner_transform == nullptr)
    {
        return {};
    }
    entt::handle created;
    visit_modifier(owner,
                   modifier,
                   [&](auto& component, int joints)
                   {
                       using component_t = std::decay_t<decltype(component)>;
                       created_pose pose;
                       if constexpr(std::is_same_v<component_t, look_at_ik_component>)
                       {
                           pose.name = get_entity_name(owner) + " Look Target";
                           pose.position = owner_transform->get_position_global() +
                                           get_rig_character_forward(owner) * LOOK_TARGET_DISTANCE;
                       }
                       else
                       {
                           pose.name = get_entity_name(owner) + " IK Target";
                           pose.position = owner_transform->get_position_global();
                           pose.rotation = owner_transform->get_rotation_global();
                       }
                       created = create_and_assign<component_t>(ctx, owner, joints, &component_t::target, pose);
                   });
    return created;
}

auto can_create_ik_pole(entt::handle owner, ik_modifier modifier) -> bool
{
    if(modifier == ik_modifier::look_at)
    {
        return false;
    }
    int joints = 0;
    const bool has_modifier = visit_modifier(owner,
                                             modifier,
                                             [&joints](const auto&, int count)
                                             {
                                                 joints = count;
                                             });
    return has_modifier && collect_rig_chain_positions(owner, joints).size() >= RIG_MIN_POLE_JOINTS;
}

auto create_ik_pole(rtti::context& ctx, entt::handle owner, ik_modifier modifier) -> entt::handle
{
    entt::handle created;
    visit_modifier(owner,
                   modifier,
                   [&](auto& component, int joints)
                   {
                       using component_t = std::decay_t<decltype(component)>;
                       if constexpr(!std::is_same_v<component_t, look_at_ik_component>)
                       {
                           const auto positions = collect_rig_chain_positions(owner, joints);
                           if(positions.size() < RIG_MIN_POLE_JOINTS)
                           {
                               return;
                           }
                           created_pose pose;
                           pose.name = get_entity_name(owner) + " IK Pole";
                           pose.position = compute_pole_position(positions, get_straight_limb_bend(owner));
                           created = create_and_assign<component_t>(ctx, owner, joints, &component_t::pole, pose);
                       }
                   });
    return created;
}

} // namespace unravel::rig_setup
