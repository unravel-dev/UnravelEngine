#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/ecs/scene.h>

#include <math/math.h>

#include <vector>

namespace unravel
{

/**
 * @brief Runs the rig modifiers (IK and spring bone components) on top of the animated pose.
 *
 * Order per frame and scene: the animation system writes the pose, scripts run (they may move IK
 * targets), then before_skinning() saves the local transform of every entity a modifier may write
 * and runs the modifiers parents first: a modifier runs after every modifier whose joints are
 * higher in the hierarchy, so a look-at that turns the chest runs before the hand IK on the arm
 * it moves. Skinning reads the result, and after_skinning() hands the input pose back, so the
 * modifiers never accumulate and never dirty a scene.
 *
 * In the scene being played the solved pose is held through the render: what hangs under a solved
 * bone (a weapon in the hand, a head camera) and rigs drawn from their transforms show it. The
 * input pose comes back at the start of the next frame (restore_deferred()), unless something
 * moved the entity in between. Everywhere else (edit mode, prefab and thumbnail scenes) it comes
 * back right after skinning, so editing and saving never see a solved pose: skinned meshes show
 * the solve, attachments and plain entity rigs show it in their gizmos.
 *
 * The state of a scene lives in its registry's context and goes away with it.
 */
class rig_system
{
public:
    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    void before_skinning(entt::registry& registry, delta_t dt);
    void after_skinning(entt::registry& registry);

    /**
     * @brief Gives held entities the input pose back after a play-mode render; call at the start
     * of the frame, before animation. Entities moved since the solve keep their value.
     */
    void restore_deferred(entt::registry& registry);
};

/// Entities a two-bone solve writes: the effector, its parent and its grandparent.
constexpr int RIG_TWO_BONE_JOINTS = 3;
/// A pole needs a joint between the root and the tip of a chain to bend.
constexpr size_t RIG_MIN_POLE_JOINTS = 3;

/**
 * @brief The entities a chain modifier poses: `joints` entities up from `tip` (itself included),
 * root first, appended to `out`. The same walk as the IK solvers: a chain that starts on a skinned
 * bone ends below the armature container, plain entity hierarchies walk any parent.
 */
void collect_rig_chain(entt::handle tip, int joints, std::vector<entt::handle>& out);

/**
 * @brief A spring bone chain: `root` and its first child at every level (the first bone child on
 * skinned rigs), at most `length` + 1 entities, appended to `out`.
 */
void collect_rig_spring_chain(entt::handle root, int length, std::vector<entt::handle>& out);

/// World positions of collect_rig_chain(), root first: what gizmos and tools draw before a solve.
auto collect_rig_chain_positions(entt::handle tip, int joints) -> std::vector<math::vec3>;

/// World positions of collect_rig_spring_chain(), root first.
auto collect_rig_spring_chain_positions(entt::handle root, int length) -> std::vector<math::vec3>;

/**
 * @brief The entity that animates the rig a bone belongs to: the first ancestor (or the bone) with
 * an animation component; without one the skinned model that owns a bone, or the top of a plain
 * entity hierarchy. Weight curves, forward axes and created targets are relative to it.
 */
auto find_rig_character(entt::handle bone) -> entt::handle;

/// The forward (+Z) axis of find_rig_character() in world space; world +Z without a transform.
auto get_rig_character_forward(entt::handle bone) -> math::vec3;

} // namespace unravel
