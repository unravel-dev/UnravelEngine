#pragma once
#include <engine/engine_export.h>

#include <engine/physics/ecs/components/ragdoll_component.h>
#include <math/math.h>

#include <entt/entity/registry.hpp>

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
 * @brief The rest pose of armature node @p index in the model entity's space.
 */
auto get_bind_pose_in_model_space(const model_component& model, entt::handle model_entity, int index) -> math::transform;

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

} // namespace unravel
