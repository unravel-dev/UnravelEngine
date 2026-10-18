#include "inspector_rig_components.h"
#include "inspectors.h"

#include <editor/editing/actions/entity_actions.h>
#include <editor/editing/actions/lambda_actions.h>
#include <editor/editing/actions/composite_action.h>
#include <editor/editing/editing_manager.h>
#include <engine/animation/animation_humanoid.h>
#include <engine/animation/animation_player.h>
#include <engine/animation/ecs/components/animation_component.h>
#include <engine/animation/ecs/systems/animation_system.h>
#include <engine/animation/ecs/systems/rig_system.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/scene.h>
#include <engine/play_mode.h>
#include <engine/rendering/ecs/components/model_component.h>

#include <imgui/imgui.h>

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace unravel
{
namespace
{
constexpr ImU32 COL_REACHED = IM_COL32(96, 200, 110, 255);
constexpr ImU32 COL_UNREACHED = IM_COL32(240, 150, 60, 255);
constexpr ImU32 COL_IDLE = IM_COL32(150, 150, 150, 255);
/// Weights below this count as off.
constexpr float MIN_ACTIVE_WEIGHT = 1e-3f;
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
/// A spring chain swings only with a child below its root.
constexpr size_t MIN_SPRING_CHAIN_JOINTS = 2;

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

/// A dimmed hint that wraps at the inspector's edge.
void draw_rig_hint(const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

/// Rigs built from separate models are drawn from their transforms: the solve shows in play mode.
void draw_plain_rig_hint(rtti::context& ctx, entt::handle owner)
{
    if(!owner || owner.all_of<bone_component>() || ctx.get_cached<play_mode>().is_active())
    {
        return;
    }
    draw_rig_hint(ICON_MDI_INFORMATION_OUTLINE " Plain hierarchy: the solved pose shows in play mode. The gizmo previews it.");
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

/// One line saying what the modifier did on the last frame.
void draw_solve_status(const ik_debug_state& debug, bool has_target, const char* missing_hint)
{
    ImGui::Spacing();
    if(!has_target)
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COL_IDLE), ICON_MDI_INFORMATION_OUTLINE " %s", missing_hint);
        return;
    }
    if(!debug.solved || debug.effective_weight < MIN_ACTIVE_WEIGHT)
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COL_IDLE),
                           ICON_MDI_PAUSE_CIRCLE_OUTLINE " Idle (weight %.2f)",
                           debug.effective_weight);
        return;
    }
    const ImU32 color = debug.reached ? COL_REACHED : COL_UNREACHED;
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color),
                       "%s  weight %.2f",
                       debug.reached ? ICON_MDI_CHECK_CIRCLE " Reached" : ICON_MDI_ALERT_CIRCLE_OUTLINE " Out of reach",
                       debug.effective_weight);
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

/**
 * @brief Creates an entity at a world `position` and `rotation` under find_created_target_parent()
 * and assigns it to the component's `field`, as one undo step.
 */
template<typename Component>
void create_and_assign(rtti::context& ctx,
                       entt::handle owner,
                       int joints,
                       entt::handle Component::*field,
                       const std::string& name,
                       const math::vec3& position,
                       const math::quat& rotation)
{
    auto& em = ctx.get_cached<editing_manager>();
    auto* active_scene = em.get_active_scene(ctx);
    if(active_scene == nullptr || !owner)
    {
        return;
    }
    const auto parent = find_created_target_parent(owner, joints);
    const auto owner_uh = entt::make_uhandle(owner);
    auto created_uh = std::make_shared<entt::uhandle>();
    auto overrides = std::make_shared<prefab_override_record>();
    auto sequence = std::make_shared<sequence_action_t>();
    sequence->add_step(
        [active_scene, parent, name, position, rotation, created_uh]() -> std::shared_ptr<editing_action_t>
        {
            return std::make_shared<create_entities_action_t>(
                [active_scene, parent, name, position, rotation, created_uh]() -> entt::handle
                {
                    auto created = active_scene->create_entity(name, parent);
                    auto& transform = created.get<transform_component>();
                    transform.set_position_global(position);
                    transform.set_rotation_global(rotation);
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
    em.do_action("Create " + name, sequence);
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

/// Create Target / Create Pole for the reaching modifiers (two-bone and chain IK), whose chain has
/// `joints` entities.
template<typename Component>
void draw_reach_tools(rtti::context& ctx, Component& data, int joints, inspect_result& result)
{
    auto owner = data.get_owner();
    auto* owner_transform = find_transform(owner);
    if(owner_transform == nullptr)
    {
        return;
    }
    const std::string bone_name = get_entity_name(owner);
    const bool shows_target_button = !data.target;
    if(shows_target_button && ImGui::Button(ICON_MDI_TARGET " Create Target"))
    {
        create_and_assign<Component>(ctx,
                                     owner,
                                     joints,
                                     &Component::target,
                                     bone_name + " IK Target",
                                     owner_transform->get_position_global(),
                                     owner_transform->get_rotation_global());
        result.changed = true;
        result.edit_finished = true;
        result.change_recorded = true;
    }
    if(shows_target_button && ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Creates a target at the bone's animated position and assigns it.");
    }
    if(data.pole)
    {
        return;
    }
    const auto positions = collect_rig_chain_positions(owner, joints);
    if(positions.size() >= RIG_MIN_POLE_JOINTS)
    {
        if(shows_target_button)
        {
            ImGui::SameLine();
        }
        if(ImGui::Button(ICON_MDI_VECTOR_LINE " Create Pole"))
        {
            create_and_assign<Component>(ctx,
                                         owner,
                                         joints,
                                         &Component::pole,
                                         bone_name + " IK Pole",
                                         compute_pole_position(positions, get_straight_limb_bend(owner)),
                                         math::identity<math::quat>());
            result.changed = true;
            result.edit_finished = true;
            result.change_recorded = true;
        }
        if(ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Creates a pole in front of the middle joint, along its current bend, and assigns it.");
        }
    }
}

struct humanoid_legs
{
    entt::handle left_foot;
    entt::handle right_foot;
    entt::handle pelvis;
};

/// The feet and the hips of the character's skeleton, by humanoid role.
auto find_humanoid_legs(entt::handle character) -> humanoid_legs
{
    humanoid_legs legs;
    const auto model_owner = find_character_model(character);
    auto* model = model_owner ? model_owner.try_get<model_component>() : nullptr;
    if(model == nullptr)
    {
        return legs;
    }
    const auto roles = find_humanoid_roles(model_owner, *model);
    legs.left_foot = get_role_bone(*model, roles, humanoid_bone::left_foot);
    legs.right_foot = get_role_bone(*model, roles, humanoid_bone::right_foot);
    legs.pelvis = get_role_bone(*model, roles, humanoid_bone::hips);
    return legs;
}

auto describe_found(const char* slot, entt::handle bone) -> std::string
{
    return std::string(slot) + ": " + (bone ? get_entity_name(bone) : std::string("not found"));
}

/// Assigns the feet and hips Auto Setup found, leaving slots it did not find as they are; one
/// undo step, none when nothing changes. Returns what it found.
auto auto_setup_foot_ik(rtti::context& ctx, foot_ik_component& data) -> std::string
{
    auto owner = data.get_owner();
    const auto legs = find_humanoid_legs(owner);
    const std::string report = describe_found("Left foot", legs.left_foot) + "\n" +
                               describe_found("Right foot", legs.right_foot) + "\n" + describe_found("Pelvis", legs.pelvis);
    const auto pick = [](entt::handle found, entt::handle current)
    {
        return found ? found : current;
    };
    const humanoid_legs before{data.left_foot, data.right_foot, data.pelvis};
    const humanoid_legs after{pick(legs.left_foot, data.left_foot),
                              pick(legs.right_foot, data.right_foot),
                              pick(legs.pelvis, data.pelvis)};
    if(after.left_foot == before.left_foot && after.right_foot == before.right_foot && after.pelvis == before.pelvis)
    {
        return report;
    }
    const auto owner_uh = entt::make_uhandle(owner);
    const auto make = [](entt::handle entity)
    {
        return entity ? entt::make_uhandle(entity) : entt::uhandle{};
    };
    // On a prefab instance only the slots that change become overrides; undo takes back the ones it added.
    auto overrides = std::make_shared<prefab_override_record>();
    auto& em = ctx.get_cached<editing_manager>();
    em.do_action(
        "Foot IK Auto Setup",
        [owner_uh, overrides, left = make(after.left_foot), right = make(after.right_foot), pelvis = make(after.pelvis)]()
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
        [owner_uh, overrides, left = make(before.left_foot), right = make(before.right_foot), pelvis = make(before.pelvis)]()
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
    return report;
}
} // namespace

auto inspector_two_bone_ik_component::inspect(rtti::context& ctx,
                                              entt::meta_any& var,
                                              const meta_any_proxy& var_proxy,
                                              const var_info& info,
                                              const entt::meta_custom& custom) -> inspect_result
{
    auto result = inspect_var_properties(ctx, var, var_proxy, info, custom);
    auto& data = var.cast<two_bone_ik_component&>();
    ImGui::PushID("two_bone_ik_tools");
    draw_solve_status(data.debug, bool(data.target), "Assign a target, or create one.");
    draw_plain_rig_hint(ctx, data.get_owner());
    draw_reach_tools(ctx, data, RIG_TWO_BONE_JOINTS, result);
    ImGui::PopID();
    return result;
}

auto inspector_chain_ik_component::inspect(rtti::context& ctx,
                                           entt::meta_any& var,
                                           const meta_any_proxy& var_proxy,
                                           const var_info& info,
                                           const entt::meta_custom& custom) -> inspect_result
{
    auto result = inspect_var_properties(ctx, var, var_proxy, info, custom);
    auto& data = var.cast<chain_ik_component&>();
    ImGui::PushID("chain_ik_tools");
    draw_solve_status(data.debug, bool(data.target), "Assign a target, or create one.");
    draw_plain_rig_hint(ctx, data.get_owner());
    draw_reach_tools(ctx, data, data.chain_length + 1, result);
    ImGui::PopID();
    return result;
}

auto inspector_look_at_ik_component::inspect(rtti::context& ctx,
                                             entt::meta_any& var,
                                             const meta_any_proxy& var_proxy,
                                             const var_info& info,
                                             const entt::meta_custom& custom) -> inspect_result
{
    auto result = inspect_var_properties(ctx, var, var_proxy, info, custom);
    auto& data = var.cast<look_at_ik_component&>();
    ImGui::PushID("look_at_ik_tools");
    ImGui::Spacing();
    if(!data.target)
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COL_IDLE), ICON_MDI_INFORMATION_OUTLINE " Assign a target, or create one.");
    }
    else
    {
        const bool looking = data.smoothed_weight >= MIN_ACTIVE_WEIGHT;
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(looking ? COL_REACHED : COL_IDLE),
                           "%s  weight %.2f",
                           looking ? ICON_MDI_EYE " Looking" : ICON_MDI_EYE_OFF " Faded out",
                           data.smoothed_weight);
    }
    auto owner = data.get_owner();
    if(auto* eye = find_transform(owner); eye != nullptr && !data.target)
    {
        if(ImGui::Button(ICON_MDI_TARGET " Create Target"))
        {
            const math::vec3 position = eye->get_position_global() + get_rig_character_forward(owner) * LOOK_TARGET_DISTANCE;
            create_and_assign<look_at_ik_component>(ctx,
                                                    owner,
                                                    data.chain_length,
                                                    &look_at_ik_component::target,
                                                    get_entity_name(owner) + " Look Target",
                                                    position,
                                                    math::identity<math::quat>());
            result.changed = true;
            result.edit_finished = true;
            result.change_recorded = true;
        }
        ImGui::SetItemTooltipEx("Creates a target in front of the eyes and assigns it.");
    }
    ImGui::PopID();
    return result;
}

auto inspector_foot_ik_component::inspect(rtti::context& ctx,
                                          entt::meta_any& var,
                                          const meta_any_proxy& var_proxy,
                                          const var_info& info,
                                          const entt::meta_custom& custom) -> inspect_result
{
    auto result = inspect_var_properties(ctx, var, var_proxy, info, custom);
    auto& data = var.cast<foot_ik_component&>();
    ImGui::PushID("foot_ik_tools");
    ImGui::Spacing();
    const bool assigned = data.left_foot && data.right_foot && data.pelvis;
    if(!assigned)
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COL_IDLE),
                           ICON_MDI_INFORMATION_OUTLINE " Assign both feet and the pelvis, or use Auto Setup.");
    }
    else
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(data.left_debug.solved || data.right_debug.solved ? COL_REACHED : COL_IDLE),
                           ICON_MDI_FOOT_PRINT " Left %.2f  Right %.2f  Pelvis %+.3f m",
                           data.left_weight,
                           data.right_weight,
                           data.pelvis_offset);
        if(!ctx.get_cached<play_mode>().is_active())
        {
            draw_rig_hint("Ground probes use physics: feet plant in play mode.");
        }
    }
    if(ImGui::Button(ICON_MDI_AUTO_FIX " Auto Setup"))
    {
        auto_setup_report_ = auto_setup_foot_ik(ctx, data);
        auto_setup_owner_ = data.get_owner();
        result.changed = true;
        result.edit_finished = true;
        result.change_recorded = true;
    }
    ImGui::SetItemTooltipEx("Finds the feet and the hips of this character's skeleton by humanoid role (from the "
                            "skeleton's shape) and assigns the ones it finds.");
    if(!auto_setup_report_.empty() && auto_setup_owner_ == data.get_owner())
    {
        draw_rig_hint(auto_setup_report_.c_str());
    }
    ImGui::PopID();
    return result;
}

auto inspector_spring_bone_component::inspect(rtti::context& ctx,
                                              entt::meta_any& var,
                                              const meta_any_proxy& var_proxy,
                                              const var_info& info,
                                              const entt::meta_custom& custom) -> inspect_result
{
    auto result = inspect_var_properties(ctx, var, var_proxy, info, custom);
    auto& data = var.cast<spring_bone_component&>();
    ImGui::PushID("spring_bone_tools");
    ImGui::Spacing();
    const int joints = int(data.debug.joints.size());
    std::vector<entt::handle> chain;
    collect_rig_spring_chain(data.get_owner(), data.chain_length, chain);
    if(data.debug.solved)
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COL_REACHED),
                           ICON_MDI_WAVES " Simulating %d bones",
                           std::max(joints - 1, 0));
    }
    else if(chain.size() < MIN_SPRING_CHAIN_JOINTS)
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COL_IDLE),
                           ICON_MDI_INFORMATION_OUTLINE " Needs at least one child bone below this one.");
    }
    else
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(COL_IDLE),
                           ICON_MDI_PAUSE_CIRCLE_OUTLINE " Idle (weight %.2f)",
                           data.weight);
    }
    draw_plain_rig_hint(ctx, data.get_owner());
    if(ImGui::Button(ICON_MDI_RESTART " Reset Simulation"))
    {
        data.reset_simulation();
    }
    ImGui::PopID();
    return result;
}

} // namespace unravel
