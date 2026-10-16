#pragma once

#include <engine/ecs/components/basic_component.h>
#include <engine/layers/layer_mask.h>

#include <entt/entt.hpp>
#include <math/math.h>

#include <string>
#include <vector>

namespace unravel
{

/**
 * @brief What a rig modifier did on the last frame, for gizmos and tools.
 */
struct ik_debug_state
{
    /// The modifier ran and wrote a pose.
    bool solved{};
    /// The effector reached its goal within tolerance.
    bool reached{};
    /// Weight the solve was applied with (component weight x weight curve x fades).
    float effective_weight{};
    /// World-space joint positions after the solve, root first.
    std::vector<math::vec3> joints;
    /// World-space goal the modifier aimed at.
    math::vec3 goal{};
};

/**
 * @brief Where a smoothed IK solve aims while it catches up with its target.
 *
 * A solve that starts eases from the animated effector; a target that jumps or is swapped for
 * another entity is approached from the previous goal instead of snapped to. When the target goes
 * away the goal stays where it was while the weight fades out. Reset while the modifier does not
 * run, so the next run starts from the animated effector again.
 */
struct ik_goal_state
{
    math::vec3 position{};
    math::quat rotation{math::identity<math::quat>()};
    /// How far the goal still is from its target, decaying to nothing: a goal that starts at the
    /// effector or leaves an old target eases over, while it follows a moving target at once.
    math::vec3 position_offset{};
    math::quat rotation_offset{math::identity<math::quat>()};
    /// The target the offsets lead to.
    entt::handle target;
    /// Weight the solve runs with, easing toward the component weight.
    float weight{};
    bool valid{};
};

/**
 * @brief Analytical two-bone IK on the bone it sits on (a hand or a foot): the bone reaches its
 * target, bending its parent (elbow, knee) toward the pole.
 *
 * Rig modifiers run every frame after scripts, before skinning, on top of the animated pose, and
 * leave no trace: the input pose comes back before the next frame animates (in edit mode right after
 * skinning), so a modifier never accumulates and can be previewed in edit mode.
 */
class two_bone_ik_component : public component_crtp<two_bone_ik_component, owned_component>
{
public:
    /// Entity the bone reaches for; its rotation is the goal of rotation_weight.
    entt::handle target;
    /// Entity the middle joint bends toward. Without one the animated bend plane is kept.
    entt::handle pole;
    /// Blend against the animated pose, 0..1.
    float weight{1.0f};
    /// How much the bone also takes the target's rotation, 0..1 (hands gripping a handle).
    float rotation_weight{0.0f};
    /// Fraction of the limb length over which reach eases out before the joint locks, 0..1.
    float soften{0.0f};
    /// Time constant the goal and the weight ease with, in seconds. 0 follows the target exactly.
    float smoothing{0.0f};
    /// Optional animation curve the weight is multiplied by (0 while no playing clip has it).
    std::string weight_curve;

    ik_goal_state goal;
    ik_debug_state debug;
};

/**
 * @brief How a chain IK solves.
 */
enum class chain_ik_solver : uint8_t
{
    /// Forward and backward reaching: smooth, converges fast, best for long chains.
    fabrik,
    /// Cyclic coordinate descent: bends the joints nearest the tip first (tails, tentacles).
    ccd,
};

/**
 * @brief Iterative IK over any number of bones ending at the bone it sits on (tails, tentacles,
 * spines, robot arms).
 */
class chain_ik_component : public component_crtp<chain_ik_component, owned_component>
{
public:
    entt::handle target;
    entt::handle pole;
    /// Bones in the chain, counted up from this bone.
    int chain_length{3};
    chain_ik_solver solver{chain_ik_solver::fabrik};
    int iterations{20};
    /// Distance at which the solve stops early, in world units.
    float tolerance{0.001f};
    float weight{1.0f};
    float rotation_weight{0.0f};
    /// Time constant the goal and the weight ease with, in seconds. 0 follows the target exactly.
    float smoothing{0.0f};
    std::string weight_curve;

    ik_goal_state goal;
    ik_debug_state debug;
};

/**
 * @brief Turns a chain of bones (head, neck, chest...) toward a target, the root bones taking a
 * smaller share and the tip finishing the aim, each limited to a cone around its animated facing.
 * Smoothed, and faded out when the target goes behind the character.
 */
class look_at_ik_component : public component_crtp<look_at_ik_component, owned_component>
{
public:
    entt::handle target;
    /// World-space offset added to the target position (look at a face, not the feet).
    math::vec3 target_offset{0.0f, 0.0f, 0.0f};
    /// Bones turned, counted up from this bone (head, neck, upper chest = 3).
    int chain_length{3};
    float weight{1.0f};
    /// Share of the turn the first (lowest) bone takes; the tip always completes the rest.
    float root_share{0.3f};
    /// Largest turn of each bone away from its animated facing, in degrees; 0 holds the bones.
    float max_bone_angle{50.0f};
    /// Targets further than this from the character's forward fade the look out, in degrees.
    float max_turn_angle{110.0f};
    /// Time constant of the look direction and weight smoothing, in seconds. 0 snaps.
    float smoothing{0.15f};
    /// Bone-local axis the bones look along; zero resolves it from the character's forward in the
    /// model's bind pose (the current pose for rigs without one).
    math::vec3 forward_axis{0.0f, 0.0f, 0.0f};
    std::string weight_curve;

    ik_debug_state debug;

    // Runtime state, not saved.
    /// The point looked at: the target along the smoothed direction from the eye.
    math::vec3 smoothed_target{};
    /// Unit direction from the eye, smoothed: turning the look (not moving a point) keeps a target
    /// swapped from one side of the head to the other from passing through the eye.
    math::vec3 smoothed_direction{};
    float smoothed_weight{};
    bool has_smoothed_target{};
    /// Bone-local facing axis of each bone, resolved for resolved_bones and resolved_forward_axis.
    std::vector<math::vec3> resolved_axes;
    std::vector<entt::handle> resolved_bones;
    math::vec3 resolved_forward_axis{};
};

/**
 * @brief Plants a character's feet on uneven ground: probes the ground under each foot, lowers the
 * pelvis to the lowest planted foot, solves each leg with two-bone IK and tilts the feet onto the
 * slope. Feet lifted by the animation (swing) and ground far below a foot fade the IK out.
 *
 * Sits on the character's root (feet level). Probes need the physics world, so it runs in play mode.
 */
class foot_ik_component : public component_crtp<foot_ik_component, owned_component>
{
public:
    entt::handle left_foot;
    entt::handle right_foot;
    entt::handle pelvis;
    /// Physics layers the probes hit.
    layer_mask ground_layers{layer_reserved::everything_layer};
    /// Probe start above the animated foot and reach below it, in meters.
    float probe_up{0.5f};
    float probe_down{1.0f};
    /// A foot this high above the root (animated swing) has no IK; it fades in between.
    float lift_start{0.14f};
    float lift_end{0.25f};
    /// Ground this far below a foot fades its IK out (stepping off a ledge).
    float drop_start{0.15f};
    float drop_end{0.35f};
    /// The pelvis drops at most this far to reach the lower foot.
    float max_pelvis_drop{0.35f};
    /// Time constant of the pelvis and foot weight smoothing, in seconds.
    float smoothing{0.08f};
    /// How much the feet tilt onto the ground slope, 0..1.
    float align_to_ground{1.0f};
    /// Knee pole for legs the animation holds straight (bent legs keep their animated bend): this far
    /// in front of the pelvis and this far out to each side, in meters.
    float knee_forward{1.0f};
    float knee_outward{0.15f};
    float weight{1.0f};
    std::string weight_curve;

    ik_debug_state left_debug;
    ik_debug_state right_debug;

    /// Ground height under a foot, smoothed so a probe crossing a step edge does not pop the foot.
    struct ground_state
    {
        float height{};
        bool valid{};
    };

    // Runtime state, not saved.
    float pelvis_offset{};
    float left_weight{};
    float right_weight{};
    ground_state left_ground;
    ground_state right_ground;
};

/**
 * @brief A sphere spring bones collide with, attached to an entity (usually a bone).
 */
struct spring_bone_collider
{
    entt::handle entity;
    float radius{0.1f};
    /// The sphere's center from the entity, along the entity's axes, in world units.
    math::vec3 offset{0.0f, 0.0f, 0.0f};
};

/**
 * @brief Secondary motion for a chain of bones hanging from the bone it sits on (tails, ears,
 * ponytails, cloth strips): every joint is a damped particle pulled back to its animated direction,
 * pushed by gravity and kept out of colliders, simulated in world space at a fixed rate.
 */
class spring_bone_component : public component_crtp<spring_bone_component, owned_component>
{
public:
    /// Pull back toward the animated direction, per second.
    float stiffness{1.5f};
    /// Fraction of velocity lost per simulation step, 0..1.
    float damping{0.2f};
    /// World-space gravity acceleration applied to the joints.
    math::vec3 gravity{0.0f, -2.0f, 0.0f};
    /// Joint radius used against colliders.
    float radius{0.02f};
    /// Bones the spring turns: this one and those after it, following the first child at every level.
    int chain_length{8};
    float weight{1.0f};
    std::vector<spring_bone_collider> colliders;

    ik_debug_state debug;

    // Runtime state, not saved.
    struct joint_state
    {
        math::vec3 tail{};
        math::vec3 previous_tail{};
        /// Bone heads the last two steps were simulated against: the pose shows each tail from its
        /// own head, so a moving character does not pull the chain back between steps.
        math::vec3 head{};
        math::vec3 previous_head{};
    };
    std::vector<joint_state> joints;
    math::vec3 last_root_position{};
    float time_accumulator{};
    bool simulated{};

    /// Drops the simulation state; the chain restarts from its animated pose (after a teleport).
    void reset_simulation()
    {
        joints.clear();
        simulated = false;
        time_accumulator = 0.0f;
    }
};

} // namespace unravel
