#pragma once
#include <engine/engine_export.h>

#include <engine/physics/ecs/components/ragdoll_component.h>
#include <math/math.h>

#include <entt/entity/registry.hpp>

#include <string>
#include <vector>

namespace unravel
{
class model_component;

/**
 * @brief Finds the bones of a ragdoll's bodies on its model, each body's parent body, the order poses are written in
 *        and the bone that carries the root body (the armature root).
 *
 * Runs again when the definition or the armature changes. Bodies whose bone the model lacks are left out.
 * @return False while the model has no armature (a mesh still loading).
 */
auto prepare_ragdoll_runtime(ragdoll_component& ragdoll) -> bool;

/**
 * @brief The rest pose of every armature node in the model entity's space, indexed like the armature. Each node is
 *        worked out once; a missing node gets the identity.
 */
auto get_bind_poses_in_model_space(const model_component& model, entt::handle model_entity)
    -> std::vector<math::transform>;

/**
 * @brief The armature index of the bone a ragdoll body names: by full name, else by the name without its rig prefix.
 * @return -1 when the model has no such bone.
 */
auto find_ragdoll_bone_index(const model_component& model, const std::string& name) -> int;

/// The bone entity a ragdoll body names, found as find_ragdoll_bone_index finds it; none when the model lacks it.
auto find_ragdoll_bone(const model_component& model, const std::string& name) -> entt::handle;

/**
 * @brief Reads every ragdoll's animated pose before the fixed steps.
 *
 * A bone still holding what the ragdoll wrote last frame was not posed by the animation since (a paused or culled
 * animation, or none at all); its last animated pose stays the target.
 */
void capture_ragdoll_animations(entt::registry& registry, float frame_seconds);

/**
 * @brief Writes every ragdoll's shown pose after the fixed steps, parents first.
 *
 * Each body bone takes, in its parent's space, the rotation between its animated one and its physics one by the body's
 * weight; the root body also takes the physics position. Physics poses are placed between the last two fixed steps
 * by @p step_fraction. Bodies that stopped simulating blend from where they were back to the animation.
 */
void show_ragdolls(entt::registry& registry, float step_fraction, float frame_seconds);

/**
 * @brief Shows again the ragdolls whose model entity moved or turned since show_ragdolls, where their bodies are.
 *
 * The bones are written in their parents' space, so a script that moves the character after the fixed steps (to
 * stand it where its ragdoll lies before a get-up, say) would carry the shown physics pose along until the next
 * frame. Weights do not advance.
 */
void show_moved_ragdolls(entt::registry& registry);

} // namespace unravel
