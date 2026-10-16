#include "rig_system.h"

#include <engine/animation/ecs/components/animation_component.h>
#include <engine/animation/ecs/components/ik_components.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/engine.h>
#include <engine/physics/ecs/systems/physics_system.h>
#include <engine/play_mode.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/systems/ik_solvers.h>

#include <hpp/span.hpp>
#include <logging/logging.h>

#include <concurrency/parallel.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <optional>
#include <tuple>

namespace unravel
{

namespace
{
/// Spring bones simulate at a fixed rate so their feel does not change with the frame rate.
constexpr float SPRING_STEP = 1.0f / 60.0f;
/// A frame longer than this many steps drops the rest instead of catching up (hitches, breakpoints).
constexpr int MAX_SPRING_STEPS = 4;
/// A chain root that moves further than this in one frame was teleported, in meters at least...
constexpr float SPRING_TELEPORT_DISTANCE = 2.0f;
/// ...and in lengths of the chain, so a large rig moving fast is not mistaken for a teleport.
constexpr float SPRING_TELEPORT_CHAIN_LENGTHS = 4.0f;
/// Vectors shorter than this have no usable direction (a bone, an offset, an axis).
constexpr float MIN_DIRECTION_LENGTH = 1e-4f;
/// Smallest range a fade is spread over: a zero range is a step instead of a division by zero.
constexpr float MIN_FADE_RANGE = 1e-4f;
/// Pelvis offsets smaller than this, in meters, are not applied.
constexpr float MIN_PELVIS_OFFSET = 1e-4f;
/// Range, in degrees, over which a look-at fades out as its target nears max_turn_angle: the look
/// is gone at max_turn_angle and complete this far before it.
constexpr float LOOK_AT_FADE_DEGREES = 20.0f;
/// Look directions closer to opposite than this cosine turn about the character's up axis.
constexpr float OPPOSITE_DIRECTION_DOT = -0.999f;
/// Weight below which a modifier does not run.
constexpr float MIN_WEIGHT = 1e-3f;
/// With fewer character groups than this the modifiers run on the calling thread: the dispatch
/// would cost more than it saves (measured with foot and look-at IK per character: about 30).
constexpr size_t MIN_PARALLEL_GROUPS = 32;
/// Root lookups are cheaper than a group's solve, so they go parallel only for more candidates.
constexpr size_t MIN_PARALLEL_ROOT_LOOKUPS = 256;
/// Knee pole height below the pelvis, as a fraction of the pole's forward distance.
constexpr float KNEE_POLE_DROP = 0.35f;
/// A knee closer than this to the hip-to-foot line (meters) leaves the animated bend undefined.
constexpr float MIN_KNEE_BEND = 0.01f;
/// Steepest ground a planted foot tilts onto, in degrees; steeper hits (edges, walls) tilt it no more.
constexpr float MAX_FOOT_TILT_DEGREES = 45.0f;
/// Inputs from scripts are held to the ranges the inspector offers.
constexpr int MAX_CHAIN_IK_BONES = 32;
constexpr int MAX_IK_ITERATIONS = 100;
constexpr int MAX_LOOK_AT_BONES = 8;
constexpr int MAX_SPRING_BONES = 64;
const math::vec3 WORLD_UP{0.0f, 1.0f, 0.0f};
const math::vec3 WORLD_FORWARD{0.0f, 0.0f, 1.0f};

/// The modifiers, in the order they run among modifiers whose highest joints are equally deep.
enum class rig_modifier : uint8_t
{
    foot_ik,
    two_bone_ik,
    chain_ik,
    look_at,
    spring_bone,
};

/// One modifier to run this frame and the joints it may write.
struct rig_job
{
    rig_modifier kind{};
    entt::entity entity{entt::null};
    /// Hierarchy depth of the highest joint the modifier writes: shallower jobs run first.
    int depth{};
    /// The job's joints in its group's joint list (or the serial path's), its highest joint first.
    size_t first{};
    size_t count{};
};

struct saved_pose
{
    entt::handle entity;
    /// Local transform before the modifiers ran.
    math::transform input;
    /// Local transform the modifiers left, for deferred restores.
    math::transform solved;
};

/// Per-bone input of a spring solve, reused from chain to chain.
struct spring_scratch
{
    std::vector<math::quat> input_local;
    std::vector<math::vec3> rest_axis;
    std::vector<float> lengths;
};

/// Scratch one thread reuses from job to job.
struct rig_scratch
{
    std::vector<entt::handle> touched;
    std::vector<entt::handle> leg_chain;
    spring_scratch spring;
};

/// A modifier component that runs this frame, and the group it runs in.
struct rig_candidate
{
    entt::entity root{entt::null};
    rig_modifier kind{};
    entt::entity entity{entt::null};
};

/**
 * @brief The modifiers of one character - with any character nested in its bones - run in order
 * on one thread. A group writes only inside its root's hierarchy, so groups run in parallel.
 */
struct rig_group
{
    entt::entity root{entt::null};
    /// The group's candidates in rig_scene_state::candidates.
    size_t first{};
    size_t count{};
    /// Roots of the other groups it reads (a target on another character), sorted: it runs after
    /// the parallel groups and after those groups.
    std::vector<entt::entity> references;
    std::vector<rig_job> jobs;
    std::vector<entt::handle> joints;
    std::vector<saved_pose> saved;
    std::vector<saved_pose> deferred;
};

/// Rig state of one scene, kept in its registry's context so it goes away with the scene.
struct rig_scene_state
{
    /// Input poses the serial path saved and held; the groups keep their own (rig_group).
    std::vector<saved_pose> saved;
    std::vector<saved_pose> deferred;
    // Scratch reused from frame to frame.
    std::vector<rig_candidate> candidates;
    std::vector<rig_group> groups;
    /// The groups' roots, sorted, and every ancestor of a root (entities no group may write).
    std::vector<entt::entity> roots;
    std::vector<entt::entity> root_ancestors;
    std::vector<size_t> parallel_groups;
    std::vector<size_t> waiting_groups;
    /// The serial path: every job of the frame in one list.
    std::vector<rig_job> jobs;
    std::vector<entt::handle> joints;
    rig_scratch scratch;
};

/// What every modifier of a frame shares; read only while the modifiers run.
struct rig_frame
{
    entt::registry& registry;
    float dt{};
    /// The physics world when the registry is the scene being played: ground probes need it.
    const physics_system* physics{};
};

auto get_state(entt::registry& registry) -> rig_scene_state&
{
    auto& ctx = registry.ctx();
    if(auto* state = ctx.find<rig_scene_state>())
    {
        return *state;
    }
    return ctx.emplace<rig_scene_state>();
}

auto get_transform(entt::handle entity) -> transform_component*
{
    return entity && entity.valid() ? entity.try_get<transform_component>() : nullptr;
}

auto is_active(entt::handle entity) -> bool
{
    return entity && entity.valid() && entity.all_of<active_component>();
}

auto is_same_transform(const math::transform& lhs, const math::transform& rhs) -> bool
{
    return lhs.get_position() == rhs.get_position() && lhs.get_rotation() == rhs.get_rotation() &&
           lhs.get_scale() == rhs.get_scale() && lhs.get_skew() == rhs.get_skew() &&
           lhs.get_perspective() == rhs.get_perspective();
}

template<typename Vector>
auto is_finite(const Vector& value) -> bool
{
    for(int i = 0; i < Vector::length(); ++i)
    {
        if(!std::isfinite(value[i]))
        {
            return false;
        }
    }
    return true;
}

/// A 0..1 setting; anything else a script may write (NaN, infinity) counts as 0.
auto to_unit(float value) -> float
{
    return std::isfinite(value) ? math::clamp(value, 0.0f, 1.0f) : 0.0f;
}

/// Exponential smoothing factor for a time constant: frame-rate independent.
auto get_smoothing_alpha(float dt, float time_constant) -> float
{
    // Zero, negative or NaN follows at once.
    if(!(time_constant > 0.0f))
    {
        return 1.0f;
    }
    return 1.0f - std::exp(-std::max(dt, 0.0f) / time_constant);
}

/// The scene being played: its solved pose is held through the render.
auto is_playing_scene(const entt::registry& registry) -> bool
{
    auto& ctx = engine::context();
    if(!ctx.has<play_mode>() || !ctx.get_cached<play_mode>().is_active() || !ctx.has<ecs>())
    {
        return false;
    }
    return ctx.get_cached<ecs>().get_scene().registry.get() == &registry;
}

/// The physics world queries see, when it is this registry's: the scene being played.
auto find_physics(entt::registry& registry) -> const physics_system*
{
    auto* physics = registry.ctx().find<physics_system*>();
    return physics != nullptr ? *physics : nullptr;
}


/// Multiplier from an animation curve the character's animation plays; 1 without a curve name.
auto get_curve_weight(entt::handle bone, const std::string& curve) -> float
{
    if(curve.empty())
    {
        return 1.0f;
    }
    const auto character = find_rig_character(bone);
    const auto* animation = character ? character.try_get<animation_component>() : nullptr;
    if(animation == nullptr)
    {
        return 0.0f;
    }
    return to_unit(animation->get_player().get_curve_value(get_animation_curve_id(curve), 0.0f));
}


/// Hierarchy depth of an entity, counted from its root.
auto get_depth(entt::handle entity) -> int
{
    int depth = 0;
    for(auto* transform = get_transform(entity); transform != nullptr; transform = get_transform(transform->get_parent()))
    {
        ++depth;
    }
    return depth;
}

void append_positions(hpp::span<const entt::handle> joints, std::vector<math::vec3>& out)
{
    for(const auto& joint : joints)
    {
        if(const auto* transform = get_transform(joint))
        {
            out.push_back(transform->get_position_global());
        }
    }
}

/// Clears the debug state, keeping the joint list's storage.
void reset_debug(ik_debug_state& debug)
{
    debug.solved = false;
    debug.reached = false;
    debug.effective_weight = 0.0f;
    debug.goal = {};
    debug.joints.clear();
}

void fill_debug(ik_debug_state& debug,
                const ik_result& result,
                float weight,
                const math::vec3& goal,
                hpp::span<const entt::handle> joints)
{
    debug.solved = result.applied;
    debug.reached = result.reached;
    debug.effective_weight = weight;
    debug.goal = goal;
    append_positions(joints, debug.joints);
}

// A modifier that does not run (inactive, nothing to pose) starts over from the animated pose
// when it runs again, fading in rather than snapping to where it left off.
void reset_runtime(two_bone_ik_component& component)
{
    component.goal = {};
    reset_debug(component.debug);
}

void reset_runtime(chain_ik_component& component)
{
    component.goal = {};
    reset_debug(component.debug);
}

void reset_runtime(look_at_ik_component& component)
{
    component.smoothed_weight = 0.0f;
    component.has_smoothed_target = false;
    reset_debug(component.debug);
}

void reset_runtime(foot_ik_component& component)
{
    component.pelvis_offset = 0.0f;
    component.left_weight = 0.0f;
    component.right_weight = 0.0f;
    reset_debug(component.left_debug);
    reset_debug(component.right_debug);
}

void reset_runtime(spring_bone_component& component)
{
    component.reset_simulation();
    reset_debug(component.debug);
}

// A modifier switched off (weight 0) whose output has faded out writes nothing, so it is left out
// of the frame: no pose save, no solve, no ground probes for a foot rig.
auto is_resting(const two_bone_ik_component& component) -> bool
{
    return to_unit(component.weight) < MIN_WEIGHT && component.goal.weight < MIN_WEIGHT;
}

auto is_resting(const chain_ik_component& component) -> bool
{
    return to_unit(component.weight) < MIN_WEIGHT && component.goal.weight < MIN_WEIGHT;
}

auto is_resting(const look_at_ik_component& component) -> bool
{
    return to_unit(component.weight) < MIN_WEIGHT && component.smoothed_weight < MIN_WEIGHT;
}

auto is_resting(const foot_ik_component& component) -> bool
{
    return to_unit(component.weight) < MIN_WEIGHT && component.left_weight < MIN_WEIGHT &&
           component.right_weight < MIN_WEIGHT && std::fabs(component.pelvis_offset) < MIN_PELVIS_OFFSET;
}

auto is_resting(const spring_bone_component& component) -> bool
{
    return to_unit(component.weight) < MIN_WEIGHT;
}

auto make_pole(entt::handle pole) -> ik_pole
{
    const auto* transform = get_transform(pole);
    return transform ? ik_pole::from_point(transform->get_position_global()) : ik_pole{};
}

/**
 * @brief Moves the goal of a reaching modifier (two-bone or chain IK) with its target and returns
 * the weight to solve with. The goal follows the target at once; only where it starts (at the
 * animated effector) or the target it leaves eases over, as an offset that decays. Without a target
 * the last goal holds while the weight fades out.
 */
template<typename Component>
auto update_reach_goal(Component& component, entt::handle effector, float dt) -> float
{
    auto& goal = component.goal;
    const auto* target = get_transform(component.target);
    const bool has_target = target != nullptr && is_finite(target->get_position_global()) &&
                            is_finite(target->get_rotation_global());
    const float desired =
        has_target ? to_unit(component.weight) * get_curve_weight(effector, component.weight_curve) : 0.0f;
    const float alpha = get_smoothing_alpha(dt, component.smoothing);
    goal.weight = math::mix(goal.weight, desired, alpha);
    if(goal.weight < MIN_WEIGHT && desired < MIN_WEIGHT)
    {
        goal = {};
        return 0.0f;
    }
    if(!has_target)
    {
        // A target that comes back eases over from the held goal.
        goal.target = {};
        return goal.valid ? goal.weight : 0.0f;
    }
    if(!goal.valid)
    {
        const auto* effector_transform = get_transform(effector);
        goal.position = effector_transform->get_position_global();
        goal.rotation = effector_transform->get_rotation_global();
        goal.target = {};
        goal.valid = true;
    }
    const math::vec3 target_position = target->get_position_global();
    const math::quat target_rotation = target->get_rotation_global();
    if(goal.target != component.target)
    {
        goal.position_offset = goal.position - target_position;
        goal.rotation_offset = math::normalize(goal.rotation * glm::inverse(target_rotation));
        goal.target = component.target;
    }
    goal.position_offset = math::mix(goal.position_offset, math::vec3(0.0f), alpha);
    goal.rotation_offset = math::normalize(math::slerp(goal.rotation_offset, math::identity<math::quat>(), alpha));
    goal.position = target_position + goal.position_offset;
    goal.rotation = math::normalize(goal.rotation_offset * target_rotation);
    return goal.weight;
}

/// The effector also takes the goal's rotation, by rotation_weight (a hand gripping a handle).
template<typename Component>
void apply_rotation_goal(const Component& component, entt::handle effector, const ik_result& result, float weight)
{
    const float rotation_weight = to_unit(component.rotation_weight);
    if(result && rotation_weight > 0.0f)
    {
        ik_set_rotation(effector, component.goal.rotation, weight * rotation_weight);
    }
}

void run_two_bone(two_bone_ik_component& component, entt::handle bone, hpp::span<const entt::handle> joints, float dt)
{
    reset_debug(component.debug);
    const float weight = update_reach_goal(component, bone, dt);
    if(weight < MIN_WEIGHT)
    {
        return;
    }
    const math::vec3 goal = component.goal.position;
    const auto result = ik_set_position_two_bone(bone, goal, make_pole(component.pole), weight, to_unit(component.soften));
    apply_rotation_goal(component, bone, result, weight);
    fill_debug(component.debug, result, weight, goal, joints);
}

void run_chain(chain_ik_component& component, entt::handle bone, hpp::span<const entt::handle> joints, float dt)
{
    reset_debug(component.debug);
    const float weight = update_reach_goal(component, bone, dt);
    if(weight < MIN_WEIGHT)
    {
        return;
    }
    ik_solver_params params;
    params.max_iterations = std::clamp(component.iterations, 1, MAX_IK_ITERATIONS);
    params.threshold = component.tolerance;
    params.weight = weight;
    const math::vec3 goal = component.goal.position;
    const auto pole = make_pole(component.pole);
    const auto bones = size_t(std::clamp(component.chain_length, 1, MAX_CHAIN_IK_BONES));
    const auto result = component.solver == chain_ik_solver::ccd ? ik_set_position_ccd(bone, goal, pole, bones, params)
                                                                 : ik_set_position_fabrik(bone, goal, pole, bones, params);
    apply_rotation_goal(component, bone, result, weight);
    fill_debug(component.debug, result, weight, goal, joints);
}

/// Fades the look out as the target swings behind the character, instead of twisting the neck round.
auto get_turn_fade(const look_at_ik_component& component, const math::vec3& eye, const math::vec3& character_forward) -> float
{
    math::vec3 to_target = component.smoothed_target - eye;
    to_target.y = 0.0f;
    math::vec3 forward = character_forward;
    forward.y = 0.0f;
    if(math::length(to_target) <= MIN_DIRECTION_LENGTH || math::length(forward) <= MIN_DIRECTION_LENGTH)
    {
        return 1.0f;
    }
    const float cos_angle = math::clamp(math::dot(math::normalize(to_target), math::normalize(forward)), -1.0f, 1.0f);
    const float angle = math::degrees(std::acos(cos_angle));
    return math::clamp((component.max_turn_angle - angle) / LOOK_AT_FADE_DEGREES, 0.0f, 1.0f);
}

/// The entity whose model owns a skinned bone, or nothing for plain entities.
auto find_bone_model(entt::handle bone) -> entt::handle
{
    if(!bone || !bone.valid() || !bone.all_of<bone_component>())
    {
        return {};
    }
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

/// The armature index of a model's bone entity, or nothing.
auto find_armature_index(const model_component& model, entt::handle entity) -> std::optional<size_t>
{
    for(const auto& node : model.get_bind_pose().nodes)
    {
        if(model.get_armature_by_index(node.desc.index) == entity)
        {
            return node.desc.index;
        }
    }
    return std::nullopt;
}

/// A skinned bone's rotation in its model's bind pose, relative to the model entity; nothing for
/// plain entities or bones the bind pose does not hold.
auto find_bind_rotation(entt::handle bone) -> std::optional<math::quat>
{
    const entt::handle model_entity = find_bone_model(bone);
    if(!model_entity)
    {
        return std::nullopt;
    }
    const auto& model = model_entity.get<model_component>();
    math::quat rotation = math::identity<math::quat>();
    for(entt::handle current = bone; current && current.valid() && current != model_entity;)
    {
        const auto index = find_armature_index(model, current);
        const auto* node = index ? model.get_bind_pose().find_node(*index) : nullptr;
        if(node == nullptr)
        {
            return std::nullopt;
        }
        rotation = math::normalize(node->transform.get_rotation() * rotation);
        auto* transform = current.try_get<transform_component>();
        current = transform ? transform->get_parent() : entt::handle{};
    }
    return rotation;
}

/// The bone-local axis a bone faces the character's forward with: read from the model's bind pose,
/// so an animated twist (an aiming chest) does not pick another axis; plain rigs use the pose now.
auto resolve_facing_axis(entt::handle bone, const math::vec3& character_forward) -> math::vec3
{
    const auto bind_rotation = find_bind_rotation(bone);
    const auto* model_transform = get_transform(find_bone_model(bone));
    if(!bind_rotation || model_transform == nullptr)
    {
        return ik_resolve_facing_axis_local(bone, character_forward);
    }
    const math::vec3 model_forward = glm::inverse(model_transform->get_rotation_global()) * character_forward;
    return ik_snap_to_cardinal_axis(glm::inverse(*bind_rotation) * model_forward);
}

/// Facing axes are resolved when the look-at first runs, again when its bones or forward axis change.
void resolve_look_at_axes(look_at_ik_component& component,
                          hpp::span<const entt::handle> bones,
                          const math::vec3& character_forward)
{
    const bool is_current = component.resolved_axes.size() == bones.size() &&
                            component.resolved_forward_axis == component.forward_axis &&
                            std::equal(bones.begin(), bones.end(), component.resolved_bones.begin(), component.resolved_bones.end());
    if(is_current)
    {
        return;
    }
    component.resolved_bones.assign(bones.begin(), bones.end());
    component.resolved_forward_axis = component.forward_axis;
    component.resolved_axes.clear();
    const float axis_length = math::length(component.forward_axis);
    for(const auto& bone : bones)
    {
        component.resolved_axes.push_back(axis_length > MIN_DIRECTION_LENGTH ? component.forward_axis / axis_length
                                                                             : resolve_facing_axis(bone, character_forward));
    }
}

/// Turns a unit direction toward another by `amount` of the angle between them, about the up axis
/// when they are opposite (a look swapped across the head turns it, it does not flip over it).
auto turn_direction(const math::vec3& from, const math::vec3& to, float amount) -> math::vec3
{
    const float cos_angle = math::clamp(math::dot(from, to), -1.0f, 1.0f);
    math::vec3 axis = math::cross(from, to);
    if(math::length(axis) < MIN_DIRECTION_LENGTH)
    {
        if(cos_angle > OPPOSITE_DIRECTION_DOT)
        {
            return to;
        }
        axis = WORLD_UP;
    }
    return math::normalize(math::angleAxis(std::acos(cos_angle) * amount, math::normalize(axis)) * from);
}

void run_look_at(look_at_ik_component& component, entt::handle tip, hpp::span<const entt::handle> bones, float dt)
{
    reset_debug(component.debug);
    const math::vec3 character_forward = get_rig_character_forward(tip);
    const math::vec3 eye = get_transform(tip)->get_position_global();
    // Target, smoothed; without one the look fades out toward the animated pose.
    float desired_weight = to_unit(component.weight) * get_curve_weight(tip, component.weight_curve);
    const float alpha = get_smoothing_alpha(dt, component.smoothing);
    const auto* target = get_transform(component.target);
    const math::vec3 goal = target ? target->get_position_global() + component.target_offset : math::vec3{};
    const float goal_distance = math::length(goal - eye);
    if(target != nullptr && is_finite(goal) && goal_distance > MIN_DIRECTION_LENGTH)
    {
        // The look turns toward the target and the distance follows it.
        const math::vec3 goal_direction = (goal - eye) / goal_distance;
        component.smoothed_direction = component.has_smoothed_target
                                           ? turn_direction(component.smoothed_direction, goal_direction, alpha)
                                           : goal_direction;
        component.smoothed_target = eye + component.smoothed_direction * goal_distance;
        component.has_smoothed_target = true;
    }
    else
    {
        desired_weight = 0.0f;
    }
    if(component.has_smoothed_target)
    {
        desired_weight *= get_turn_fade(component, eye, character_forward);
    }
    component.smoothed_weight = math::mix(component.smoothed_weight, desired_weight, alpha);
    component.debug.effective_weight = component.smoothed_weight;
    component.debug.goal = component.smoothed_target;
    // A bone limit of 0 holds the bones where the animation has them.
    if(component.smoothed_weight < MIN_WEIGHT || !component.has_smoothed_target || component.max_bone_angle <= 0.0f)
    {
        return;
    }
    resolve_look_at_axes(component, bones, character_forward);
    // The lowest bone takes root_share of the turn, the tip completes it.
    const float root_share = to_unit(component.root_share);
    ik_aim_params params;
    params.max_angle_radians = math::radians(component.max_bone_angle);
    bool reached = false;
    for(size_t i = 0; i < bones.size(); ++i)
    {
        const float share = bones.size() == 1 ? 1.0f : math::mix(root_share, 1.0f, float(i) / float(bones.size() - 1));
        params.forward_axis = component.resolved_axes[i];
        params.weight = share * component.smoothed_weight;
        reached = ik_aim_at_position(bones[i], component.smoothed_target, params).reached;
    }
    component.debug.solved = true;
    component.debug.reached = reached;
    append_positions(bones, component.debug.joints);
}

/// Whether `entity` is `character` or under it.
auto is_part_of(entt::handle entity, entt::handle character) -> bool
{
    for(entt::handle current = entity; current && current.valid();)
    {
        if(current == character)
        {
            return true;
        }
        auto* transform = current.try_get<transform_component>();
        current = transform ? transform->get_parent() : entt::handle{};
    }
    return false;
}

/// Nearest hit straight down from `origin` that is not the character itself.
auto probe_ground(const physics_system& physics, const math::vec3& origin, float distance, int layers, entt::handle character)
    -> hpp::optional<raycast_hit>
{
    hpp::optional<raycast_hit> nearest;
    for(const auto& hit : physics.ray_cast_all(origin, -WORLD_UP, distance, layers, false))
    {
        if((!nearest || hit.distance < nearest->distance) &&
           !is_part_of(entt::handle(*character.registry(), hit.entity), character))
        {
            nearest = hit;
        }
    }
    return nearest;
}

struct foot_goal
{
    bool valid{};
    math::vec3 position{};
    math::vec3 normal{WORLD_UP};
    float vertical_offset{};
    float weight{};
};

/// 1 below `start`, 0 above `end`, linear in between.
auto fade_out(float value, float start, float end) -> float
{
    return 1.0f - math::clamp((value - start) / std::max(end - start, MIN_FADE_RANGE), 0.0f, 1.0f);
}

/// The ground normal a planted foot tilts onto: a hit facing down (a back face) counts as flat,
/// and steeper ground than MAX_FOOT_TILT_DEGREES tilts the foot that far only.
auto limit_foot_tilt(const math::vec3& normal) -> math::vec3
{
    if(math::length(normal) <= MIN_DIRECTION_LENGTH || math::dot(normal, WORLD_UP) <= 0.0f)
    {
        return WORLD_UP;
    }
    const math::vec3 unit = math::normalize(normal);
    const float max_tilt = math::radians(MAX_FOOT_TILT_DEGREES);
    const float tilt = std::acos(math::clamp(math::dot(unit, WORLD_UP), -1.0f, 1.0f));
    if(tilt <= max_tilt)
    {
        return unit;
    }
    const math::vec3 axis = math::cross(WORLD_UP, unit);
    return math::length(axis) > MIN_DIRECTION_LENGTH ? math::angleAxis(max_tilt, math::normalize(axis)) * WORLD_UP
                                                     : WORLD_UP;
}

/// Eases the ground height under a foot like the weights do; a jump larger than the IK reaches
/// (a teleport, a ledge) is taken at once.
auto smooth_ground_height(const foot_ik_component& component, foot_ik_component::ground_state& ground, float height, float alpha)
    -> float
{
    if(!ground.valid || std::fabs(height - ground.height) > component.drop_end)
    {
        ground.height = height;
        ground.valid = true;
        return height;
    }
    ground.height = math::mix(ground.height, height, alpha);
    return ground.height;
}

auto plan_foot(const foot_ik_component& component,
               entt::handle foot,
               float root_height,
               const physics_system* physics,
               entt::handle character,
               foot_ik_component::ground_state& ground,
               float alpha) -> foot_goal
{
    foot_goal goal;
    const auto* transform = get_transform(foot);
    if(transform == nullptr || physics == nullptr)
    {
        return goal;
    }
    const math::vec3 foot_position = transform->get_position_global();
    // Feet the animation lifts (swing) keep their animated path.
    const float height_above_root = foot_position.y - root_height;
    float weight = fade_out(height_above_root, component.lift_start, component.lift_end);
    if(weight <= 0.0f)
    {
        return goal;
    }
    const auto hit = probe_ground(*physics,
                                  foot_position + WORLD_UP * component.probe_up,
                                  component.probe_up + component.probe_down,
                                  component.ground_layers.mask,
                                  character);
    if(!hit)
    {
        return goal;
    }
    // Keep the animated height above the ground: on flat ground at root height the goal is the
    // animated foot itself, so the IK only acts where the ground differs.
    const float animated_clearance = std::max(height_above_root, 0.0f);
    const float ground_height = smooth_ground_height(component, ground, hit->point.y, alpha);
    goal.position = math::vec3(foot_position.x, ground_height + animated_clearance, foot_position.z);
    goal.normal = limit_foot_tilt(hit->normal);
    // Ground far below the foot (a ledge, a downhill swing) fades the IK out instead of slamming the
    // foot - and through the pelvis, the whole body - down onto it.
    weight *= fade_out(foot_position.y - goal.position.y, component.drop_start, component.drop_end);
    if(weight <= 0.0f)
    {
        return goal;
    }
    goal.valid = true;
    goal.weight = weight;
    goal.vertical_offset = (goal.position.y - foot_position.y) * weight;
    return goal;
}

struct leg_solve
{
    entt::handle foot;
    foot_goal goal;
    float weight{};
    /// -1 for the left leg, 1 for the right: the side the knee leans out to.
    float side{};
    /// World rotation of the foot in the animated pose, before the pelvis moved.
    math::quat animated_rotation{math::identity<math::quat>()};
    /// The side the animated knee bends to (from the hip-to-foot line toward the knee), before the
    /// pelvis moved; nothing for a straight leg.
    std::optional<math::vec3> animated_bend;
};

/// The side the animated knee bends to, read before the pelvis moves; nothing for a straight leg.
auto find_animated_bend(entt::handle foot, std::vector<entt::handle>& chain) -> std::optional<math::vec3>
{
    chain.clear();
    collect_rig_chain(foot, RIG_TWO_BONE_JOINTS, chain);
    if(chain.size() != size_t(RIG_TWO_BONE_JOINTS))
    {
        return std::nullopt;
    }
    const math::vec3 hip = get_transform(chain[0])->get_position_global();
    const math::vec3 knee = get_transform(chain[1])->get_position_global();
    const math::vec3 ankle = get_transform(chain[2])->get_position_global();
    const math::vec3 leg = ankle - hip;
    const float leg_length = math::length(leg);
    if(leg_length <= MIN_DIRECTION_LENGTH)
    {
        return std::nullopt;
    }
    const math::vec3 along = leg / leg_length;
    const math::vec3 bend = (knee - hip) - along * math::dot(knee - hip, along);
    const float bend_length = math::length(bend);
    return bend_length > MIN_KNEE_BEND ? std::optional<math::vec3>(bend / bend_length) : std::nullopt;
}

void solve_leg(const foot_ik_component& component,
               const transform_component& root,
               const transform_component& pelvis,
               const leg_solve& leg,
               std::vector<entt::handle>& chain,
               ik_debug_state& debug)
{
    debug.effective_weight = leg.weight;
    if(!leg.goal.valid || leg.weight < MIN_WEIGHT || get_transform(leg.foot) == nullptr)
    {
        return;
    }
    // The knee keeps the animated bend, so a planted foot on flat ground keeps its animated leg. A
    // straight leg has no bend of its own: its pole hangs off the pelvis (never off the foot, which
    // tilts with slopes and swings both knees toward the midline), biased outward.
    ik_pole pole = leg.animated_bend ? ik_pole::from_direction(*leg.animated_bend) : ik_pole{};
    if(!leg.animated_bend)
    {
        const math::vec3 forward = math::normalize(root.get_z_axis_global());
        const math::vec3 right = math::normalize(root.get_x_axis_global());
        pole = ik_pole::from_point(pelvis.get_position_global() + forward * component.knee_forward +
                                   right * (leg.side * component.knee_outward) -
                                   WORLD_UP * (component.knee_forward * KNEE_POLE_DROP));
    }
    const auto result = ik_set_position_two_bone(leg.foot, leg.goal.position, pole, leg.weight);
    if(result)
    {
        // The planted foot keeps its animated world rotation, tilted onto the slope; the solve
        // alone swings it with the shin.
        const math::quat slope = math::from_to_rotation(WORLD_UP, leg.goal.normal);
        const math::quat tilt = math::slerp(math::identity<math::quat>(), slope, to_unit(component.align_to_ground));
        ik_set_rotation(leg.foot, math::normalize(tilt * leg.animated_rotation), leg.weight);
    }
    debug.solved = result.applied;
    debug.reached = result.reached;
    debug.goal = leg.goal.position;
    chain.clear();
    collect_rig_chain(leg.foot, RIG_TWO_BONE_JOINTS, chain);
    append_positions(chain, debug.joints);
}

auto get_rotation_global(entt::handle entity) -> math::quat
{
    const auto* transform = get_transform(entity);
    return transform ? transform->get_rotation_global() : math::identity<math::quat>();
}

void run_foot_ik(foot_ik_component& component, entt::handle character, const rig_frame& frame, rig_scratch& scratch)
{
    reset_debug(component.left_debug);
    reset_debug(component.right_debug);
    const auto& root = *get_transform(character);
    auto& pelvis = *get_transform(component.pelvis);
    const float weight = to_unit(component.weight) * get_curve_weight(character, component.weight_curve);
    const float alpha = get_smoothing_alpha(frame.dt, component.smoothing);
    const float root_height = root.get_position_global().y;
    leg_solve left{component.left_foot,
                   plan_foot(component, component.left_foot, root_height, frame.physics, character, component.left_ground, alpha),
                   0.0f,
                   -1.0f};
    leg_solve right{component.right_foot,
                    plan_foot(component, component.right_foot, root_height, frame.physics, character, component.right_ground, alpha),
                    0.0f,
                    1.0f};
    component.left_weight = math::mix(component.left_weight, left.goal.valid ? left.goal.weight * weight : 0.0f, alpha);
    component.right_weight = math::mix(component.right_weight, right.goal.valid ? right.goal.weight * weight : 0.0f, alpha);
    left.weight = component.left_weight;
    right.weight = component.right_weight;
    left.animated_rotation = get_rotation_global(component.left_foot);
    right.animated_rotation = get_rotation_global(component.right_foot);
    left.animated_bend = find_animated_bend(component.left_foot, scratch.leg_chain);
    right.animated_bend = find_animated_bend(component.right_foot, scratch.leg_chain);

    // The pelvis drops to the lowest planted foot, never further than max_pelvis_drop.
    float pelvis_target = 0.0f;
    for(const auto* leg : {&left, &right})
    {
        if(leg->goal.valid)
        {
            pelvis_target = std::min(pelvis_target, leg->goal.vertical_offset * weight);
        }
    }
    pelvis_target = std::max(pelvis_target, -std::max(component.max_pelvis_drop, 0.0f));
    component.pelvis_offset = math::mix(component.pelvis_offset, pelvis_target, alpha);
    if(std::fabs(component.pelvis_offset) > MIN_PELVIS_OFFSET)
    {
        pelvis.set_position_global(pelvis.get_position_global() + WORLD_UP * component.pelvis_offset);
    }
    solve_leg(component, root, pelvis, left, scratch.leg_chain, component.left_debug);
    solve_leg(component, root, pelvis, right, scratch.leg_chain, component.right_debug);
}

/// The input pose of this frame: each bone's local rotation, rest direction toward its tail and length.
void capture_spring_input(hpp::span<const entt::handle> chain, spring_scratch& scratch)
{
    const size_t simulated = chain.size() - 1;
    scratch.input_local.resize(simulated);
    scratch.rest_axis.resize(simulated);
    scratch.lengths.resize(simulated);
    for(size_t i = 0; i < simulated; ++i)
    {
        const auto* transform = get_transform(chain[i]);
        const auto* child = get_transform(chain[i + 1]);
        scratch.input_local[i] = transform->get_rotation_local();
        const math::vec3 offset = child->get_position_local();
        const float local_length = math::length(offset);
        scratch.rest_axis[i] = local_length > MIN_DIRECTION_LENGTH ? offset / local_length : WORLD_UP;
        scratch.lengths[i] = math::length(child->get_position_global() - transform->get_position_global());
    }
}

/// Puts every joint at its animated position with no velocity.
void restart_spring(spring_bone_component& component, hpp::span<const entt::handle> chain, const math::vec3& root_position)
{
    component.joints.resize(chain.size() - 1);
    for(size_t i = 0; i < component.joints.size(); ++i)
    {
        auto& joint = component.joints[i];
        joint.tail = get_transform(chain[i + 1])->get_position_global();
        joint.previous_tail = joint.tail;
        joint.head = get_transform(chain[i])->get_position_global();
        joint.previous_head = joint.head;
    }
    component.simulated = true;
    component.time_accumulator = 0.0f;
    component.last_root_position = root_position;
}

/// Restarts on the first run, after the chain changed, and when its root jumped (a teleport).
auto is_spring_restart_needed(const spring_bone_component& component,
                              size_t simulated,
                              const math::vec3& root_position,
                              const spring_scratch& scratch) -> bool
{
    if(!component.simulated || component.joints.size() != simulated)
    {
        return true;
    }
    float chain_length = 0.0f;
    for(const float length : scratch.lengths)
    {
        chain_length += length;
    }
    const float teleport_distance = std::max(SPRING_TELEPORT_DISTANCE, chain_length * SPRING_TELEPORT_CHAIN_LENGTHS);
    return math::length(root_position - component.last_root_position) > teleport_distance;
}

/// Fixed steps to simulate this frame; a hitch longer than MAX_SPRING_STEPS drops the rest.
auto advance_spring_clock(spring_bone_component& component, float dt) -> int
{
    component.time_accumulator += std::max(dt, 0.0f);
    const int steps = int(component.time_accumulator / SPRING_STEP);
    if(steps > MAX_SPRING_STEPS)
    {
        component.time_accumulator = 0.0f;
        return MAX_SPRING_STEPS;
    }
    component.time_accumulator -= float(steps) * SPRING_STEP;
    return steps;
}

/// Bone i's animated rotation: its input local rotation on top of its parent's current pose.
auto get_spring_rest_rotation(hpp::span<const entt::handle> chain, const spring_scratch& scratch, size_t i) -> math::quat
{
    const auto* transform = get_transform(chain[i]);
    const auto* parent = get_transform(transform->get_parent());
    const math::quat parent_rotation = parent ? parent->get_rotation_global() : math::identity<math::quat>();
    return math::normalize(parent_rotation * scratch.input_local[i]);
}

/// Rotates bone i so its tail points at `tail`, blended over its animated rotation by `weight`.
void aim_spring_bone(hpp::span<const entt::handle> chain, const spring_scratch& scratch, size_t i, const math::vec3& tail, float weight)
{
    auto* transform = get_transform(chain[i]);
    const math::vec3 to_tail = tail - transform->get_position_global();
    if(math::length(to_tail) < MIN_DIRECTION_LENGTH)
    {
        return;
    }
    const math::quat rest_rotation = get_spring_rest_rotation(chain, scratch, i);
    const math::vec3 rest_direction = math::normalize(rest_rotation * scratch.rest_axis[i]);
    const math::quat swing = math::from_to_rotation(rest_direction, math::normalize(to_tail));
    const math::quat simulated_rotation = math::normalize(swing * rest_rotation);
    transform->set_rotation_global(math::normalize(math::slerp(rest_rotation, simulated_rotation, weight)));
}

/**
 * @brief One fixed step of a joint: Verlet that carries the velocity (damped), pulls toward the
 * animated direction and falls, then keeps the bone length and stays out of the colliders.
 */
auto step_spring_joint(const spring_bone_component& component,
                       const math::vec3& head,
                       const math::vec3& rest_direction,
                       float length,
                       const spring_bone_component::joint_state& joint) -> math::vec3
{
    const auto constrain = [&](const math::vec3& point)
    {
        const math::vec3 offset = point - head;
        const float distance = math::length(offset);
        return distance > MIN_DIRECTION_LENGTH ? head + offset * (length / distance) : head + rest_direction * length;
    };
    const float damping = to_unit(component.damping);
    math::vec3 next = constrain(joint.tail + (joint.tail - joint.previous_tail) * (1.0f - damping) +
                                rest_direction * (component.stiffness * SPRING_STEP * length) +
                                component.gravity * (SPRING_STEP * SPRING_STEP));
    for(const auto& collider : component.colliders)
    {
        const auto* collider_transform = get_transform(collider.entity);
        if(collider_transform == nullptr)
        {
            continue;
        }
        const math::vec3 center =
            collider_transform->get_position_global() + collider_transform->get_rotation_global() * collider.offset;
        const float reach = collider.radius + component.radius;
        const math::vec3 away = next - center;
        const float distance = math::length(away);
        if(distance < reach && distance > MIN_DIRECTION_LENGTH)
        {
            next = constrain(center + away * (reach / distance));
        }
    }
    // A setting that is not a number (from a script) leaves the joint on its animated direction
    // rather than locking the chain into NaN.
    return is_finite(next) ? next : head + rest_direction * length;
}

void run_spring(spring_bone_component& component, hpp::span<const entt::handle> chain, spring_scratch& scratch, float dt)
{
    reset_debug(component.debug);
    // chain[i] rotates so that chain[i + 1] (its tail) follows the simulated joint i.
    const size_t simulated = chain.size() - 1;
    capture_spring_input(chain, scratch);
    const math::vec3 root_position = get_transform(chain.front())->get_position_global();
    if(is_spring_restart_needed(component, simulated, root_position, scratch))
    {
        restart_spring(component, chain, root_position);
    }
    const int steps = advance_spring_clock(component, dt);
    const float weight = to_unit(component.weight);
    // The chain root moved over the whole frame: each step sees it part way, so several steps in
    // one frame do not take the whole move at the first one.
    const math::vec3 root_motion = root_position - component.last_root_position;
    for(int step = 0; step < steps; ++step)
    {
        const math::vec3 root_shift = root_motion * (float(step + 1) / float(steps) - 1.0f);
        for(size_t i = 0; i < simulated; ++i)
        {
            const math::vec3 head = get_transform(chain[i])->get_position_global() + root_shift;
            const math::vec3 rest_direction =
                math::normalize(get_spring_rest_rotation(chain, scratch, i) * scratch.rest_axis[i]);
            auto& joint = component.joints[i];
            const math::vec3 next = step_spring_joint(component, head, rest_direction, scratch.lengths[i], joint);
            joint.previous_tail = joint.tail;
            joint.previous_head = joint.head;
            joint.tail = next;
            joint.head = head;
            // Rotate at once: the next joint's head is this bone's simulated tail (the bone sits
            // at the frame's root, so it aims at the tail as seen from there).
            aim_spring_bone(chain, scratch, i, joint.tail - root_shift, weight);
        }
    }
    // The pose shows the joints part way between their last two steps, so frames faster than the
    // step rate move smoothly instead of holding a step and jumping. Each tail is taken from the
    // head it was simulated against, so a moving character does not drag the chain back.
    const float blend = math::clamp(component.time_accumulator / SPRING_STEP, 0.0f, 1.0f);
    for(size_t i = 0; i < simulated; ++i)
    {
        const auto& joint = component.joints[i];
        const math::vec3 offset = math::mix(joint.previous_tail - joint.previous_head, joint.tail - joint.head, blend);
        aim_spring_bone(chain, scratch, i, get_transform(chain[i])->get_position_global() + offset, weight);
    }
    component.last_root_position = root_position;
    component.debug.solved = true;
    component.debug.effective_weight = weight;
    component.debug.goal = root_position;
    append_positions(chain, component.debug.joints);
}

/**
 * @brief The group a modifier on `entity` runs in: the character it belongs to, or the outermost
 * animated character that one is nested in (a prop rig parented to another character's bone).
 */
auto get_rig_group_root(entt::handle entity) -> entt::entity
{
    entt::handle root = find_rig_character(entity);
    for(entt::handle current = root; current && current.valid();)
    {
        const auto* transform = current.try_get<transform_component>();
        current = transform ? transform->get_parent() : entt::handle{};
        if(current && current.valid() && current.all_of<animation_component>())
        {
            root = current;
        }
    }
    return root ? root.entity() : entity.entity();
}

/// Adds a candidate for every component of a type that runs this frame; one that does not
/// (inactive, resting) has its runtime state reset. Roots are found afterwards (find_rig_roots).
template<typename Component>
void gather_rig_candidates(entt::registry& registry, rig_modifier kind, std::vector<rig_candidate>& out)
{
    registry.view<Component>().each(
        [&](entt::entity entity, Component& component)
        {
            const entt::handle owner(registry, entity);
            if(is_resting(component) || !is_active(owner))
            {
                reset_runtime(component);
                return;
            }
            out.push_back({entt::null, kind, entity});
        });
}

/// Fills every candidate's group root: hierarchy walks that only read, spread over the pool.
void find_rig_roots(entt::registry& registry, std::vector<rig_candidate>& candidates)
{
    poolstl::for_each_par_if(candidates.size() >= MIN_PARALLEL_ROOT_LOOKUPS,
                             candidates.begin(),
                             candidates.end(),
                             [&](rig_candidate& candidate)
                             {
                                 candidate.root = get_rig_group_root(entt::handle(registry, candidate.entity));
                             });
}

/// Appends the joints a modifier may write, highest first.
void collect_rig_job_joints(rig_modifier kind, entt::handle owner, std::vector<entt::handle>& joints)
{
    switch(kind)
    {
        case rig_modifier::foot_ik:
        {
            const auto& component = owner.get<foot_ik_component>();
            if(get_transform(owner) != nullptr && get_transform(component.pelvis) != nullptr)
            {
                joints.push_back(component.pelvis);
                collect_rig_chain(component.left_foot, RIG_TWO_BONE_JOINTS, joints);
                collect_rig_chain(component.right_foot, RIG_TWO_BONE_JOINTS, joints);
            }
            break;
        }
        case rig_modifier::two_bone_ik:
            collect_rig_chain(owner, RIG_TWO_BONE_JOINTS, joints);
            break;
        case rig_modifier::chain_ik:
        {
            const int chain_length = std::clamp(owner.get<chain_ik_component>().chain_length, 1, MAX_CHAIN_IK_BONES);
            collect_rig_chain(owner, chain_length + 1, joints);
            break;
        }
        case rig_modifier::look_at:
            collect_rig_chain(owner, std::clamp(owner.get<look_at_ik_component>().chain_length, 1, MAX_LOOK_AT_BONES), joints);
            break;
        case rig_modifier::spring_bone:
        {
            const size_t first = joints.size();
            collect_rig_spring_chain(owner, std::clamp(owner.get<spring_bone_component>().chain_length, 1, MAX_SPRING_BONES), joints);
            // A bone without a child has nothing to swing.
            if(joints.size() - first < 2)
            {
                joints.resize(first);
            }
            break;
        }
    }
}

void reset_runtime(rig_modifier kind, entt::handle owner)
{
    switch(kind)
    {
        case rig_modifier::foot_ik:
            reset_runtime(owner.get<foot_ik_component>());
            break;
        case rig_modifier::two_bone_ik:
            reset_runtime(owner.get<two_bone_ik_component>());
            break;
        case rig_modifier::chain_ik:
            reset_runtime(owner.get<chain_ik_component>());
            break;
        case rig_modifier::look_at:
            reset_runtime(owner.get<look_at_ik_component>());
            break;
        case rig_modifier::spring_bone:
            reset_runtime(owner.get<spring_bone_component>());
            break;
    }
}

/// Calls `visit` with every entity a modifier reads besides its own joints: targets, poles, colliders.
template<typename Visit>
void for_each_rig_reference(rig_modifier kind, entt::handle owner, const Visit& visit)
{
    switch(kind)
    {
        case rig_modifier::foot_ik:
            break;
        case rig_modifier::two_bone_ik:
        {
            const auto& component = owner.get<two_bone_ik_component>();
            visit(component.target);
            visit(component.pole);
            break;
        }
        case rig_modifier::chain_ik:
        {
            const auto& component = owner.get<chain_ik_component>();
            visit(component.target);
            visit(component.pole);
            break;
        }
        case rig_modifier::look_at:
            visit(owner.get<look_at_ik_component>().target);
            break;
        case rig_modifier::spring_bone:
            for(const auto& collider : owner.get<spring_bone_component>().colliders)
            {
                visit(collider.entity);
            }
            break;
    }
}

auto is_listed_entity(const std::vector<entt::entity>& sorted, entt::entity entity) -> bool
{
    return std::binary_search(sorted.begin(), sorted.end(), entity);
}

/// The group root `entity` is or sits under, or null.
auto find_rig_group_root(entt::handle entity, const std::vector<entt::entity>& roots) -> entt::entity
{
    for(entt::handle current = entity; current && current.valid();)
    {
        if(is_listed_entity(roots, current.entity()))
        {
            return current.entity();
        }
        const auto* transform = current.try_get<transform_component>();
        current = transform ? transform->get_parent() : entt::handle{};
    }
    return entt::null;
}

/// Parents first: a modifier sees the final pose of every joint above the ones it writes.
void sort_rig_jobs(std::vector<rig_job>& jobs)
{
    std::sort(jobs.begin(),
              jobs.end(),
              [](const rig_job& lhs, const rig_job& rhs)
              {
                  return std::tie(lhs.depth, lhs.kind, lhs.entity) < std::tie(rhs.depth, rhs.kind, rhs.entity);
              });
}

/**
 * @brief Sorts the candidates into groups. World transforms resolve lazily into caches, so the ones
 * groups read but no group writes - the roots' ancestors and the targets - are resolved here, on
 * one thread, and the groups only read them. A group that reads another group's entities is
 * flagged to run after the others.
 */
void build_rig_groups(entt::registry& registry, rig_scene_state& state)
{
    auto& candidates = state.candidates;
    std::sort(candidates.begin(),
              candidates.end(),
              [](const rig_candidate& lhs, const rig_candidate& rhs)
              {
                  return std::tie(lhs.root, lhs.kind, lhs.entity) < std::tie(rhs.root, rhs.kind, rhs.entity);
              });
    state.roots.clear();
    size_t group_count = 0;
    for(size_t begin = 0; begin < candidates.size();)
    {
        size_t end = begin + 1;
        while(end < candidates.size() && candidates[end].root == candidates[begin].root)
        {
            ++end;
        }
        if(state.groups.size() <= group_count)
        {
            state.groups.emplace_back();
        }
        auto& group = state.groups[group_count++];
        group.root = candidates[begin].root;
        group.first = begin;
        group.count = end - begin;
        group.references.clear();
        group.jobs.clear();
        group.joints.clear();
        group.saved.clear();
        state.roots.push_back(group.root);
        begin = end;
    }
    state.groups.resize(group_count);

    state.root_ancestors.clear();
    for(const auto root : state.roots)
    {
        const auto* transform = get_transform(entt::handle(registry, root));
        if(transform == nullptr)
        {
            continue;
        }
        transform->get_transform_global();
        for(entt::handle parent = transform->get_parent(); parent && parent.valid();)
        {
            state.root_ancestors.push_back(parent.entity());
            const auto* parent_transform = parent.try_get<transform_component>();
            parent = parent_transform ? parent_transform->get_parent() : entt::handle{};
        }
    }
    std::sort(state.root_ancestors.begin(), state.root_ancestors.end());
    state.root_ancestors.erase(std::unique(state.root_ancestors.begin(), state.root_ancestors.end()),
                               state.root_ancestors.end());

    for(auto& group : state.groups)
    {
        for(size_t i = group.first; i < group.first + group.count; ++i)
        {
            const auto& candidate = candidates[i];
            for_each_rig_reference(candidate.kind,
                               entt::handle(registry, candidate.entity),
                               [&](entt::handle reference)
                               {
                                   const auto* transform = get_transform(reference);
                                   if(transform == nullptr)
                                   {
                                       return;
                                   }
                                   transform->get_transform_global();
                                   const auto reference_root = find_rig_group_root(reference, state.roots);
                                   if(reference_root != entt::null && reference_root != group.root)
                                   {
                                       group.references.push_back(reference_root);
                                   }
                               });
        }
        std::sort(group.references.begin(), group.references.end());
        group.references.erase(std::unique(group.references.begin(), group.references.end()), group.references.end());
    }
}

/**
 * @brief Collects a group's jobs, shallowest joints first. False when a job would write outside the
 * group's hierarchy, another group's root or above a root: the frame then runs on one thread.
 */
auto collect_rig_group(entt::registry& registry, const rig_scene_state& state, rig_group& group) -> bool
{
    bool is_contained = true;
    const entt::handle root(registry, group.root);
    for(size_t i = group.first; i < group.first + group.count; ++i)
    {
        const auto& candidate = state.candidates[i];
        const entt::handle owner(registry, candidate.entity);
        const size_t first = group.joints.size();
        collect_rig_job_joints(candidate.kind, owner, group.joints);
        if(group.joints.size() == first)
        {
            reset_runtime(candidate.kind, owner);
            continue;
        }
        for(size_t j = first; j < group.joints.size() && is_contained; ++j)
        {
            const auto joint = group.joints[j];
            is_contained = is_part_of(joint, root) && !is_listed_entity(state.root_ancestors, joint.entity()) &&
                           (joint.entity() == group.root || !is_listed_entity(state.roots, joint.entity()));
        }
        group.jobs.push_back({candidate.kind, candidate.entity, get_depth(group.joints[first]), first, group.joints.size() - first});
    }
    sort_rig_jobs(group.jobs);
    return is_contained;
}

/// Saves the local transform of every entity the jobs may write, so after_skinning() can hand it back.
void save_input_pose(const std::vector<entt::handle>& joints, std::vector<entt::handle>& touched, std::vector<saved_pose>& saved)
{
    touched.assign(joints.begin(), joints.end());
    std::sort(touched.begin(),
              touched.end(),
              [](const entt::handle& lhs, const entt::handle& rhs)
              {
                  return lhs.entity() < rhs.entity();
              });
    touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
    for(const auto& entity : touched)
    {
        if(const auto* transform = get_transform(entity))
        {
            saved.push_back({entity, transform->get_transform_local(), {}});
        }
    }
}

void run_job(const rig_frame& frame, const std::vector<entt::handle>& all_joints, const rig_job& job, rig_scratch& scratch)
{
    const entt::handle owner(frame.registry, job.entity);
    const hpp::span<const entt::handle> joints(all_joints.data() + job.first, job.count);
    switch(job.kind)
    {
        case rig_modifier::foot_ik:
            run_foot_ik(owner.get<foot_ik_component>(), owner, frame, scratch);
            break;
        case rig_modifier::two_bone_ik:
            run_two_bone(owner.get<two_bone_ik_component>(), owner, joints, frame.dt);
            break;
        case rig_modifier::chain_ik:
            run_chain(owner.get<chain_ik_component>(), owner, joints, frame.dt);
            break;
        case rig_modifier::look_at:
            run_look_at(owner.get<look_at_ik_component>(), owner, joints, frame.dt);
            break;
        case rig_modifier::spring_bone:
            run_spring(owner.get<spring_bone_component>(), joints, scratch.spring, frame.dt);
            break;
    }
}

/// Saves a group's input pose and runs its jobs in order.
void run_rig_group(const rig_frame& frame, rig_group& group, rig_scratch& scratch)
{
    save_input_pose(group.joints, scratch.touched, group.saved);
    for(const auto& job : group.jobs)
    {
        run_job(frame, group.joints, job, scratch);
    }
}

/**
 * @brief Hands back the input pose of every entity a modifier wrote. While the scene plays the
 * solved pose is held through the render instead: the poses move to `deferred`, and
 * restore_deferred() hands them back before the next frame animates.
 */
void hand_back_poses(std::vector<saved_pose>& saved, bool holds_solved_pose, std::vector<saved_pose>& deferred)
{
    for(auto& pose : saved)
    {
        auto* transform = get_transform(pose.entity);
        // Entities no modifier wrote (no weight, no target) are left alone: setting them would
        // dirty their hierarchy every frame.
        if(transform == nullptr || is_same_transform(transform->get_transform_local(), pose.input))
        {
            continue;
        }
        if(holds_solved_pose)
        {
            pose.solved = transform->get_transform_local();
            deferred.push_back(pose);
            continue;
        }
        transform->set_transform_local(pose.input);
    }
    saved.clear();
}

/// Hands back held input poses, unless something else moved the entity since.
void restore_poses(std::vector<saved_pose>& deferred)
{
    for(const auto& pose : deferred)
    {
        auto* transform = get_transform(pose.entity);
        if(transform != nullptr && is_same_transform(transform->get_transform_local(), pose.solved))
        {
            transform->set_transform_local(pose.input);
        }
    }
    deferred.clear();
}

/// The scratch of the calling thread.
auto get_rig_thread_scratch() -> rig_scratch&
{
    thread_local rig_scratch scratch;
    return scratch;
}

/**
 * @brief Runs the groups that read another character, each after the groups it reads, so a rig
 * aiming at another character sees that character's solved pose (through any number of them).
 * Groups reading each other run in root order.
 */
void run_waiting_groups(const rig_frame& frame, rig_scene_state& state)
{
    auto& pending = state.waiting_groups;
    pending.clear();
    for(size_t i = 0; i < state.groups.size(); ++i)
    {
        if(!state.groups[i].references.empty())
        {
            pending.push_back(i);
        }
    }
    const auto reads_pending_group = [&](size_t index)
    {
        return std::any_of(pending.begin(),
                           pending.end(),
                           [&](size_t other)
                           {
                               const auto& references = state.groups[index].references;
                               return other != index &&
                                      std::binary_search(references.begin(), references.end(), state.groups[other].root);
                           });
    };
    while(!pending.empty())
    {
        auto ready = std::find_if(pending.begin(),
                                  pending.end(),
                                  [&](size_t index)
                                  {
                                      return !reads_pending_group(index);
                                  });
        if(ready == pending.end())
        {
            ready = pending.begin();
        }
        const size_t index = *ready;
        pending.erase(ready);
        run_rig_group(frame, state.groups[index], state.scratch);
    }
}

/// Runs every job of the frame on this thread in one list, shallowest joints first.
void run_rigs_serially(const rig_frame& frame, rig_scene_state& state)
{
    state.jobs.clear();
    state.joints.clear();
    for(const auto& group : state.groups)
    {
        const size_t offset = state.joints.size();
        state.joints.insert(state.joints.end(), group.joints.begin(), group.joints.end());
        for(auto job : group.jobs)
        {
            job.first += offset;
            state.jobs.push_back(job);
        }
    }
    sort_rig_jobs(state.jobs);
    save_input_pose(state.joints, state.scratch.touched, state.saved);
    for(const auto& job : state.jobs)
    {
        run_job(frame, state.joints, job, state.scratch);
    }
}
} // namespace

auto find_rig_character(entt::handle bone) -> entt::handle
{
    const bool is_skinned = bone && bone.valid() && bone.all_of<bone_component>();
    entt::handle top;
    for(entt::handle current = bone; current && current.valid();)
    {
        if(current.all_of<animation_component>() || (is_skinned && current.all_of<model_component>()))
        {
            return current;
        }
        top = current;
        auto* transform = current.try_get<transform_component>();
        current = transform ? transform->get_parent() : entt::handle{};
    }
    return top;
}

auto get_rig_character_forward(entt::handle bone) -> math::vec3
{
    const auto* transform = get_transform(find_rig_character(bone));
    return transform ? math::normalize(transform->get_z_axis_global()) : WORLD_FORWARD;
}

void collect_rig_chain(entt::handle tip, int joints, std::vector<entt::handle>& out)
{
    const size_t first = out.size();
    const bool bones_only = tip && tip.valid() && tip.all_of<bone_component>();
    for(entt::handle current = tip; get_transform(current) != nullptr && int(out.size() - first) < std::max(joints, 1);
        current = ik_get_chain_parent(current, bones_only))
    {
        out.push_back(current);
    }
    std::reverse(out.begin() + std::ptrdiff_t(first), out.end());
}

void collect_rig_spring_chain(entt::handle root, int length, std::vector<entt::handle>& out)
{
    const size_t first = out.size();
    const bool bones_only = root && root.valid() && root.all_of<bone_component>();
    for(entt::handle current = root; current && int(out.size() - first) <= std::max(length, 1);)
    {
        const auto* transform = get_transform(current);
        if(transform == nullptr)
        {
            break;
        }
        out.push_back(current);
        const auto& children = transform->get_children();
        const auto next = std::find_if(children.begin(),
                                       children.end(),
                                       [&](const entt::handle& child)
                                       {
                                           return get_transform(child) != nullptr &&
                                                  (!bones_only || child.all_of<bone_component>());
                                       });
        current = next != children.end() ? *next : entt::handle{};
    }
}

auto collect_rig_chain_positions(entt::handle tip, int joints) -> std::vector<math::vec3>
{
    std::vector<entt::handle> chain;
    collect_rig_chain(tip, joints, chain);
    std::vector<math::vec3> positions;
    append_positions(chain, positions);
    return positions;
}

auto collect_rig_spring_chain_positions(entt::handle root, int length) -> std::vector<math::vec3>
{
    std::vector<entt::handle> chain;
    collect_rig_spring_chain(root, length, chain);
    std::vector<math::vec3> positions;
    append_positions(chain, positions);
    return positions;
}

auto rig_system::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);
    return true;
}

auto rig_system::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);
    return true;
}

void rig_system::before_skinning(entt::registry& registry, delta_t dt)
{
    APP_SCOPE_PERF("Animation/Rig Modifiers");
    restore_deferred(registry);
    auto& state = get_state(registry);
    state.saved.clear();
    state.candidates.clear();
    gather_rig_candidates<foot_ik_component>(registry, rig_modifier::foot_ik, state.candidates);
    gather_rig_candidates<two_bone_ik_component>(registry, rig_modifier::two_bone_ik, state.candidates);
    gather_rig_candidates<chain_ik_component>(registry, rig_modifier::chain_ik, state.candidates);
    gather_rig_candidates<look_at_ik_component>(registry, rig_modifier::look_at, state.candidates);
    gather_rig_candidates<spring_bone_component>(registry, rig_modifier::spring_bone, state.candidates);
    if(state.candidates.empty())
    {
        state.groups.clear();
        return;
    }
    find_rig_roots(registry, state.candidates);
    build_rig_groups(registry, state);

    const bool is_parallel = state.groups.size() >= MIN_PARALLEL_GROUPS;
    std::atomic<bool> is_contained{true};
    poolstl::for_each_par_if(is_parallel,
                             state.groups.begin(),
                             state.groups.end(),
                             [&](rig_group& group)
                             {
                                 if(!collect_rig_group(registry, state, group))
                                 {
                                     is_contained.store(false, std::memory_order_relaxed);
                                 }
                             });
    // Ground probes from several threads are safe: between its steps the physics world only
    // answers read-only queries.
    const rig_frame frame{registry, std::chrono::duration<float>(dt).count(), find_physics(registry)};
    if(!is_contained.load(std::memory_order_relaxed))
    {
        // A rig writes outside its own character: one thread keeps every write in one order.
        run_rigs_serially(frame, state);
        return;
    }

    state.parallel_groups.clear();
    for(size_t i = 0; i < state.groups.size(); ++i)
    {
        if(state.groups[i].references.empty())
        {
            state.parallel_groups.push_back(i);
        }
    }
    poolstl::for_each_par_if(is_parallel && state.parallel_groups.size() >= MIN_PARALLEL_GROUPS,
                             state.parallel_groups.begin(),
                             state.parallel_groups.end(),
                             [&](size_t index)
                             {
                                 APP_SCOPE_PERF_THREAD("Animation/Rig Characters", "Pool Thread");
                                 run_rig_group(frame, state.groups[index], get_rig_thread_scratch());
                             });
    run_waiting_groups(frame, state);
}

void rig_system::after_skinning(entt::registry& registry)
{
    auto* state = registry.ctx().find<rig_scene_state>();
    if(state == nullptr)
    {
        return;
    }
    APP_SCOPE_PERF("Animation/Rig Hand Back");
    const bool holds_solved_pose = is_playing_scene(registry);
    hand_back_poses(state->saved, holds_solved_pose, state->deferred);
    // Each group wrote only inside its own hierarchy, so the groups hand back side by side.
    poolstl::for_each_par_if(state->groups.size() >= MIN_PARALLEL_GROUPS,
                             state->groups.begin(),
                             state->groups.end(),
                             [&](rig_group& group)
                             {
                                 hand_back_poses(group.saved, holds_solved_pose, group.deferred);
                             });
}

void rig_system::restore_deferred(entt::registry& registry)
{
    auto* state = registry.ctx().find<rig_scene_state>();
    if(state == nullptr)
    {
        return;
    }
    APP_SCOPE_PERF("Animation/Rig Restore");
    restore_poses(state->deferred);
    poolstl::for_each_par_if(state->groups.size() >= MIN_PARALLEL_GROUPS,
                             state->groups.begin(),
                             state->groups.end(),
                             [](rig_group& group)
                             {
                                 restore_poses(group.deferred);
                             });
}

} // namespace unravel
