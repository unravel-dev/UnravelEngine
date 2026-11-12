#pragma once

#include <context/context.hpp>
#include <engine/ecs/scene.h>

#include <cstdint>

/// The setup helpers of the rig components the Inspector offers: Foot IK Auto Setup and the targets and poles the
/// reaching and look-at modifiers aim with. Each is one undo step; on a prefab instance the slots it assigns become
/// overrides.
namespace unravel::rig_setup
{

/// The IK modifier a target or pole is made for.
enum class ik_modifier : uint8_t
{
    two_bone,
    chain,
    look_at,
};

/// The feet and the hips of a character's skeleton, by humanoid role; null where none was found.
struct foot_ik_bones
{
    entt::handle left_foot;
    entt::handle right_foot;
    entt::handle pelvis;
};

/// Finds the feet and the hips of @p character's skeleton by humanoid role, from the skeleton's shape.
auto find_foot_ik_bones(entt::handle character) -> foot_ik_bones;

/**
 * @brief Assigns the feet and hips find_foot_ik_bones finds to the Foot IK on @p owner, leaving the slots it did not
 *        find as they are; no undo step when nothing changes.
 * @return What it found.
 */
auto auto_setup_foot_ik(rtti::context& ctx, entt::handle owner) -> foot_ik_bones;

/// Whether @p owner has that modifier.
auto has_ik_modifier(entt::handle owner, ik_modifier modifier) -> bool;

/**
 * @brief Creates a target and assigns it to the modifier on @p owner: at the bone's animated pose for the reaching
 *        modifiers, in front of the eyes for look-at. It goes under the character, or at the scene root when the
 *        modifier poses the character itself, so it never moves with what it drives.
 * @return The new entity; null when @p owner has no such modifier.
 */
auto create_ik_target(rtti::context& ctx, entt::handle owner, ik_modifier modifier) -> entt::handle;

/// Whether create_ik_pole can place a pole: a reaching modifier whose chain has a middle joint.
auto can_create_ik_pole(entt::handle owner, ik_modifier modifier) -> bool;

/**
 * @brief Creates a pole in front of the middle joint of the reaching modifier's chain, along its current bend (elbows
 *        bend back, knees forward when straight), and assigns it.
 * @return The new entity; null when can_create_ik_pole is false.
 */
auto create_ik_pole(rtti::context& ctx, entt::handle owner, ik_modifier modifier) -> entt::handle;

} // namespace unravel::rig_setup
