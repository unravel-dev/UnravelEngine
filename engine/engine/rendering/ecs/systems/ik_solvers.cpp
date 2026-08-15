#include "ik_solvers.h"
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/model_component.h>

#include <algorithm>
#include <cmath>
#include <glm/gtc/epsilon.hpp>
#include <hpp/small_vector.hpp>

namespace unravel
{
template<typename T>
using ik_vector = hpp::small_vector<T>;

namespace
{
/// Absolute epsilon for world-space length comparisons, in engine units.
constexpr float IK_EPSILON = 1e-5f;

/// Squared-length below which the legacy zero-vector pole counts as "disabled".
constexpr float IK_LEGACY_POLE_EPSILON = 1e-10f;

/// Dot product above which two unit directions are close enough that applying
/// the correction would be pure numerical noise (~0.03 degrees).
constexpr float IK_DIRECTION_ALIGNED_DOT = 1.0f - 1e-7f;

/// Cap on a single CCD bone step so one bone cannot flip past the target,
/// without shrinking the step as the error falls (that prevented the chain from
/// converging within the default iteration count).
constexpr float CCD_MAX_STEP_RADIANS = 0.5235987756f; // 30 degrees

/// Fraction of chain length eased before lockout when soften == 1.
constexpr float TWO_BONE_SOFTEN_FRACTION = 0.15f;

/// Fraction of chain length held in reserve when an out-of-reach target is
/// clamped onto the reach sphere. Relative rather than absolute so it behaves
/// the same on a 0.1-unit rig and a 100-unit one; it keeps the chain off the
/// singular fully-straight pose, where the bend plane is undefined.
constexpr float REACH_MARGIN_FRACTION = 1e-3f;

/// Relative tolerance for "this scale is uniform enough to compose rotations".
constexpr float UNIFORM_SCALE_TOLERANCE = 1e-4f;

auto is_finite(const math::vec3& v) -> bool
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

auto is_finite(const math::quat& q) -> bool
{
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}

auto safe_normalize(const math::vec3& v, const math::vec3& fallback) -> math::vec3
{
    const float len = math::length(v);
    if(len < IK_EPSILON)
    {
        return fallback;
    }
    return v / len;
}

/// Any unit vector perpendicular to `v`, which must already be normalized.
/// Used where the mathematically correct axis is undefined (exactly antiparallel
/// directions, a fully straight chain) and the solver still has to pick one.
auto any_perpendicular(const math::vec3& v) -> math::vec3
{
    const math::vec3 reference =
        (std::fabs(v.y) < 0.9f) ? math::vec3(0.0f, 1.0f, 0.0f) : math::vec3(1.0f, 0.0f, 0.0f);
    return math::normalize(math::cross(reference, v));
}

/**
 * @brief True when rot(parent * local) == rot(parent) * rot(local) holds.
 *
 * The transform stack composes full matrices, so that identity only holds for a
 * uniform, positive parent scale. Mirrored or non-uniformly scaled rigs need the
 * slower path that decomposes properly.
 */
auto has_uniform_positive_scale(const math::vec3& scale) -> bool
{
    if(!(scale.x > 0.0f) || !(scale.y > 0.0f) || !(scale.z > 0.0f))
    {
        return false;
    }
    const float tolerance = UNIFORM_SCALE_TOLERANCE * scale.x + UNIFORM_SCALE_TOLERANCE;
    return std::fabs(scale.x - scale.y) <= tolerance && std::fabs(scale.x - scale.z) <= tolerance;
}

/// Writes a world-space orientation onto a bone, taking the cheap quaternion
/// route when the parent scale allows it (see has_uniform_positive_scale).
void set_bone_rotation_global(transform_component* bone, const math::quat& desired_global)
{
    auto parent = bone->get_parent();
    transform_component* parent_transform = parent ? parent.try_get<transform_component>() : nullptr;
    if(parent_transform == nullptr)
    {
        bone->set_rotation_local(math::normalize(desired_global));
        return;
    }
    const auto& parent_global = parent_transform->get_transform_global();
    if(!has_uniform_positive_scale(parent_global.get_scale()))
    {
        bone->set_rotation_global(math::normalize(desired_global));
        return;
    }
    bone->set_rotation_local(math::normalize(glm::inverse(parent_global.get_rotation()) * desired_global));
}

void apply_world_rotation_delta(transform_component* bone, const math::quat& rotation_delta)
{
    set_bone_rotation_global(bone, math::normalize(rotation_delta * bone->get_rotation_global()));
}

/// Place `from` a distance `length` toward `toward`. If the segment collapses,
/// keep traveling along `fallback_dir` so FABRIK never divides by zero.
auto place_at_length(const math::vec3& from, const math::vec3& toward, float length, const math::vec3& fallback_dir)
    -> math::vec3
{
    const math::vec3 delta = toward - from;
    const float distance = math::length(delta);
    if(distance < IK_EPSILON)
    {
        return from + safe_normalize(fallback_dir, math::vec3(0.0f, 1.0f, 0.0f)) * length;
    }
    return from + (delta / distance) * length;
}

/**
 * @brief Collects the joint chain from `end_effector` up through its parents.
 *
 * `num_bones_in_chain` counts bones, so the result holds one more joint than
 * that, ordered root-first.
 *
 * On a skinned effector every collected joint must itself carry a
 * bone_component. That is also what terminates the walk at the skeleton root:
 * the armature container above it is not a skinned bone. The previous
 * implementation instead skipped non-bone ancestors and kept walking, which
 * produced a chain of non-adjacent joints - every direction derived from such a
 * chain spans a gap and is wrong. It also stopped at `bone_index == 0`, but that
 * index addresses the mesh's skin influence list (pruned by remove_empty_bones),
 * so index 0 is whichever bone the skinning data happened to list first, not the
 * skeleton root.
 */
auto collect_chain(entt::handle end_effector, size_t num_bones_in_chain) -> ik_vector<transform_component*>
{
    ik_vector<transform_component*> chain;
    if(!end_effector || num_bones_in_chain == 0)
    {
        return chain;
    }
    transform_component* current = end_effector.try_get<transform_component>();
    if(current == nullptr)
    {
        return chain;
    }
    const bool require_bone_ancestors = end_effector.all_of<bone_component>();

    chain.push_back(current);
    while(chain.size() < num_bones_in_chain + 1)
    {
        auto parent = current->get_parent();
        if(!parent)
        {
            break;
        }
        if(require_bone_ancestors && !parent.all_of<bone_component>())
        {
            break;
        }
        transform_component* parent_transform = parent.try_get<transform_component>();
        if(parent_transform == nullptr)
        {
            break;
        }
        chain.push_back(parent_transform);
        current = parent_transform;
    }
    std::reverse(chain.begin(), chain.end());
    return chain;
}

using ik_pose_snapshot = ik_vector<math::quat>;

auto capture_local_rotations(const ik_vector<transform_component*>& chain) -> ik_pose_snapshot
{
    ik_pose_snapshot snapshot;
    for(const transform_component* bone : chain)
    {
        snapshot.push_back(bone->get_rotation_local());
    }
    return snapshot;
}

/**
 * @brief Interpolates the solved pose back toward the captured (animated) one.
 *
 * Local rotation space on purpose: this is a pose interpolation, the same thing
 * Unity and Unreal call the IK alpha. The effector therefore does not travel
 * linearly toward the target as the weight ramps - it follows the arc between
 * the two poses, which is what reads correctly when IK fades in over animation.
 */
void blend_toward_snapshot(const ik_vector<transform_component*>& chain,
                           const ik_pose_snapshot& snapshot,
                           float weight)
{
    if(weight >= 1.0f)
    {
        return;
    }
    const size_t count = std::min(chain.size(), snapshot.size());
    for(size_t i = 0; i < count; ++i)
    {
        chain[i]->set_rotation_local(
            math::normalize(math::slerp(snapshot[i], chain[i]->get_rotation_local(), weight)));
    }
}

/**
 * @brief Rotates the intermediate joints about the (root -> end) axis so the
 *        chain bends toward the pole.
 *
 * One angle for the whole chain, not one per joint. Rotating each joint onto the
 * pole half-plane individually changes the distance between adjacent joints, so
 * the position set stops describing a chain of fixed-length bones and the
 * effector misses the target - visible on any chain with two or more
 * intermediate joints. A single rotation about a common axis is an isometry, so
 * every bone length survives it, and because the axis passes through the root
 * and end points those two stay put.
 */
void apply_pole_constraint(ik_vector<math::vec3>& positions, const ik_pole& pole)
{
    if(!pole.is_enabled())
    {
        return;
    }
    const size_t count = positions.size();
    if(count < 3)
    {
        return;
    }

    const math::vec3 root = positions.front();
    const math::vec3 end = positions.back();
    math::vec3 axis = end - root;
    const float axis_len = math::length(axis);
    if(axis_len < IK_EPSILON)
    {
        return;
    }
    axis /= axis_len;

    const auto flatten = [&axis](const math::vec3& v) -> math::vec3
    {
        return v - glm::dot(v, axis) * axis;
    };

    const math::vec3 pole_offset = (pole.mode == ik_pole_mode::direction) ? pole.value : (pole.value - root);
    const math::vec3 pole_flat = flatten(pole_offset);
    const float pole_flat_len = math::length(pole_flat);
    if(pole_flat_len < IK_EPSILON)
    {
        return;
    }
    const math::vec3 pole_dir = pole_flat / pole_flat_len;

    // The first intermediate joint defines the chain's current bend plane.
    const math::vec3 bend_flat = flatten(positions[1] - root);
    const float bend_flat_len = math::length(bend_flat);
    if(bend_flat_len < IK_EPSILON)
    {
        // Straight chain: there is no bend plane to rotate.
        return;
    }
    const math::vec3 bend_dir = bend_flat / bend_flat_len;

    const float cos_angle = glm::clamp(glm::dot(bend_dir, pole_dir), -1.0f, 1.0f);
    const float sin_angle = glm::dot(math::cross(bend_dir, pole_dir), axis);
    const float angle = std::atan2(sin_angle, cos_angle);
    if(std::fabs(angle) < IK_EPSILON)
    {
        return;
    }

    const math::quat rotation = math::angleAxis(angle, axis);
    for(size_t i = 1; i + 1 < count; ++i)
    {
        positions[i] = root + rotation * (positions[i] - root);
    }
}

/**
 * @brief Re-derives bone rotations from a set of target joint positions.
 *
 * For each bone we compute the shortest-arc rotation that aligns the current
 * bone direction with the direction implied by the new joint positions, then
 * apply it in world space and let the transform stack convert it to local. Bones
 * are processed root-first, so each one reads its parent's already-corrected
 * position. Twist about the bone axis is untouched.
 *
 * Note that the effector (the last joint) is never rotated here - it has no
 * child to aim at. Callers that need a specific effector orientation apply
 * ik_set_rotation afterwards.
 */
void update_rotations_from_positions(const ik_vector<transform_component*>& chain,
                                     const ik_vector<math::vec3>& positions)
{
    const size_t count = std::min(chain.size(), positions.size());
    for(size_t i = 0; i + 1 < count; ++i)
    {
        transform_component* bone = chain[i];
        const math::vec3 bone_pos = bone->get_position_global();
        const math::vec3 child_pos = chain[i + 1]->get_position_global();
        const math::vec3 current_delta = child_pos - bone_pos;
        const math::vec3 desired_delta = positions[i + 1] - positions[i];
        if(math::length(current_delta) < IK_EPSILON || math::length(desired_delta) < IK_EPSILON)
        {
            continue;
        }
        const math::vec3 current_dir = math::normalize(current_delta);
        const math::vec3 desired_dir = math::normalize(desired_delta);
        if(glm::dot(current_dir, desired_dir) > IK_DIRECTION_ALIGNED_DOT)
        {
            continue;
        }
        apply_world_rotation_delta(bone, math::from_to_rotation(current_dir, desired_dir));
    }
}

auto measure_chain_length(const ik_vector<transform_component*>& chain) -> float
{
    float total = 0.0f;
    for(size_t i = 0; i + 1 < chain.size(); ++i)
    {
        total += math::length(chain[i + 1]->get_position_global() - chain[i]->get_position_global());
    }
    return total;
}

auto make_result(const ik_vector<transform_component*>& chain, const math::vec3& target, float tolerance) -> ik_result
{
    ik_result result;
    result.applied = true;
    result.distance = math::length(target - chain.back()->get_position_global());
    result.reached = result.distance <= std::max(tolerance, IK_EPSILON);
    return result;
}

auto solve_ccd(const ik_vector<transform_component*>& chain,
               const math::vec3& target,
               const ik_pole& pole,
               const ik_solver_params& params) -> ik_result
{
    const size_t count = chain.size();
    if(count < 2)
    {
        return {};
    }
    transform_component* end_effector = chain.back();
    const float max_reach = measure_chain_length(chain);
    if(max_reach < IK_EPSILON)
    {
        return {};
    }

    const math::vec3 base_position = chain.front()->get_position_global();
    math::vec3 solve_target = target;
    const math::vec3 to_target = target - base_position;
    const float target_distance = math::length(to_target);
    if(target_distance > max_reach && target_distance > IK_EPSILON)
    {
        solve_target = base_position + (to_target / target_distance) * (max_reach * (1.0f - REACH_MARGIN_FRACTION));
    }

    const ik_pose_snapshot snapshot = capture_local_rotations(chain);
    const int max_iterations = std::max(1, params.max_iterations);
    const float threshold = std::max(0.0f, params.threshold);

    bool converged = false;
    for(int iteration = 0; iteration < max_iterations && !converged; ++iteration)
    {
        // End-to-root sweep: rotating the joints nearest the effector first is
        // what gives CCD its fast early convergence.
        for(size_t index = count - 1; index-- > 0;)
        {
            transform_component* bone = chain[index];
            const math::vec3 bone_pos = bone->get_position_global();
            const math::vec3 effector_pos = end_effector->get_position_global();
            const math::vec3 to_effector = effector_pos - bone_pos;
            const math::vec3 to_goal = solve_target - bone_pos;
            const float effector_len = math::length(to_effector);
            const float goal_len = math::length(to_goal);
            if(effector_len < IK_EPSILON || goal_len < IK_EPSILON)
            {
                continue;
            }
            const math::vec3 effector_dir = to_effector / effector_len;
            const math::vec3 goal_dir = to_goal / goal_len;
            const float cos_angle = glm::clamp(glm::dot(effector_dir, goal_dir), -1.0f, 1.0f);
            if(cos_angle > IK_DIRECTION_ALIGNED_DOT)
            {
                continue;
            }
            math::vec3 rotation_axis = math::cross(effector_dir, goal_dir);
            const float axis_len = math::length(rotation_axis);
            rotation_axis = (axis_len < IK_EPSILON) ? any_perpendicular(effector_dir) : (rotation_axis / axis_len);

            const float angle = std::min(std::acos(cos_angle), CCD_MAX_STEP_RADIANS);
            apply_world_rotation_delta(bone, math::angleAxis(angle, rotation_axis));

            if(math::length(solve_target - end_effector->get_position_global()) < threshold)
            {
                converged = true;
                break;
            }
        }
    }

    if(pole.is_enabled() && count >= 3)
    {
        ik_vector<math::vec3> positions;
        for(const transform_component* bone : chain)
        {
            positions.push_back(bone->get_position_global());
        }
        apply_pole_constraint(positions, pole);
        update_rotations_from_positions(chain, positions);
    }

    blend_toward_snapshot(chain, snapshot, params.weight);

    return make_result(chain, target, threshold);
}

auto solve_fabrik(const ik_vector<transform_component*>& chain,
                  const math::vec3& target,
                  const ik_pole& pole,
                  const ik_solver_params& params) -> ik_result
{
    const size_t count = chain.size();
    if(count < 2)
    {
        return {};
    }

    ik_vector<math::vec3> positions;
    for(const transform_component* bone : chain)
    {
        positions.push_back(bone->get_position_global());
    }

    ik_vector<float> bone_lengths(count - 1, 0.0f);
    float total_length = 0.0f;
    for(size_t i = 0; i + 1 < count; ++i)
    {
        bone_lengths[i] = math::length(positions[i + 1] - positions[i]);
        total_length += bone_lengths[i];
    }
    if(total_length < IK_EPSILON)
    {
        return {};
    }

    const math::vec3 root_pos = positions[0];
    const ik_pose_snapshot snapshot = capture_local_rotations(chain);
    const int max_iterations = std::max(1, params.max_iterations);
    const float threshold = std::max(0.0f, params.threshold);

    if(math::length(target - root_pos) > total_length)
    {
        // Out of reach: the optimal pose is the fully extended chain.
        const math::vec3 direction = safe_normalize(target - root_pos, math::vec3(0.0f, 1.0f, 0.0f));
        for(size_t i = 0; i + 1 < count; ++i)
        {
            positions[i + 1] = positions[i] + direction * bone_lengths[i];
        }
    }
    else
    {
        for(int iteration = 0; iteration < max_iterations; ++iteration)
        {
            // Backward pass: pin the effector to the target, walk to the root.
            positions[count - 1] = target;
            for(size_t i = count - 1; i-- > 0;)
            {
                const math::vec3 fallback =
                    (i + 2 < count) ? (positions[i + 1] - positions[i + 2]) : (positions[i] - positions[i + 1]);
                positions[i] = place_at_length(positions[i + 1], positions[i], bone_lengths[i], fallback);
            }
            // Forward pass: pin the root back, walk to the effector.
            positions[0] = root_pos;
            for(size_t i = 0; i + 1 < count; ++i)
            {
                const math::vec3 fallback = (i > 0) ? (positions[i] - positions[i - 1]) : (target - root_pos);
                positions[i + 1] = place_at_length(positions[i], positions[i + 1], bone_lengths[i], fallback);
            }
            if(math::length(positions[count - 1] - target) < threshold)
            {
                break;
            }
        }
    }

    apply_pole_constraint(positions, pole);
    update_rotations_from_positions(chain, positions);
    blend_toward_snapshot(chain, snapshot, params.weight);

    return make_result(chain, target, threshold);
}

/**
 * @brief Which side of the (start -> target) line the mid joint bends toward.
 *
 * Preference order: the pole if it resolves, then the chain's current bend
 * plane (so a disabled pole leaves the animated bend alone), then any
 * perpendicular - a fully straight chain aimed at a collinear target has no
 * defined bend plane, and returning a deterministic axis beats returning NaN.
 */
auto resolve_bend_direction(const math::vec3& start,
                            const math::vec3& mid,
                            const math::vec3& target_dir,
                            const ik_pole& pole) -> math::vec3
{
    const auto flatten = [&target_dir](const math::vec3& v) -> math::vec3
    {
        return v - glm::dot(v, target_dir) * target_dir;
    };

    if(pole.is_enabled())
    {
        const math::vec3 offset = (pole.mode == ik_pole_mode::direction) ? pole.value : (pole.value - start);
        const math::vec3 flat = flatten(offset);
        const float len = math::length(flat);
        if(len > IK_EPSILON)
        {
            return flat / len;
        }
    }

    const math::vec3 current = flatten(mid - start);
    const float current_len = math::length(current);
    if(current_len > IK_EPSILON)
    {
        return current / current_len;
    }

    return any_perpendicular(target_dir);
}

auto solve_two_bone(const ik_vector<transform_component*>& chain,
                    const math::vec3& target,
                    const ik_pole& pole,
                    float weight,
                    float soften) -> ik_result
{
    if(chain.size() != 3)
    {
        return {};
    }
    const math::vec3 start_pos = chain[0]->get_position_global();
    const math::vec3 mid_pos = chain[1]->get_position_global();
    const math::vec3 end_pos = chain[2]->get_position_global();
    const float upper_length = math::length(mid_pos - start_pos);
    const float lower_length = math::length(end_pos - mid_pos);
    if(upper_length < IK_EPSILON || lower_length < IK_EPSILON)
    {
        return {};
    }

    math::vec3 to_target = target - start_pos;
    float distance = math::length(to_target);
    if(distance < IK_EPSILON)
    {
        // Target sits on the root joint: aim along the current upper bone so the
        // pose stays defined instead of collapsing.
        to_target = mid_pos - start_pos;
        distance = math::length(to_target);
        if(distance < IK_EPSILON)
        {
            to_target = math::vec3(0.0f, 0.0f, 1.0f);
            distance = 1.0f;
        }
    }
    const math::vec3 target_dir = to_target / distance;

    const float hard_max = upper_length + lower_length;
    const float soft_fraction = glm::clamp(soften, 0.0f, 1.0f);
    const float soft_region = hard_max * TWO_BONE_SOFTEN_FRACTION * soft_fraction;
    if(soft_region > IK_EPSILON && distance > hard_max - soft_region)
    {
        // Exponential ease so reach approaches the limit asymptotically instead
        // of snapping to a locked joint. Costs reach, hence soften defaults to 0.
        const float t = (distance - (hard_max - soft_region)) / soft_region;
        distance = (hard_max - soft_region) + soft_region * (1.0f - std::exp(-t));
    }
    const float min_distance = std::max(std::fabs(upper_length - lower_length) * 1.001f, IK_EPSILON);
    distance = glm::clamp(distance, min_distance, hard_max * (1.0f - REACH_MARGIN_FRACTION));

    // Law of cosines: angle at the start joint between the chain axis and the
    // upper bone, for a triangle with sides (upper, lower, distance).
    float cos_start = (upper_length * upper_length + distance * distance - lower_length * lower_length) /
                      (2.0f * upper_length * distance);
    cos_start = glm::clamp(cos_start, -1.0f, 1.0f);
    const float sin_start = std::sqrt(std::max(0.0f, 1.0f - cos_start * cos_start));

    const math::vec3 bend_dir = resolve_bend_direction(start_pos, mid_pos, target_dir, pole);

    ik_vector<math::vec3> positions;
    positions.push_back(start_pos);
    positions.push_back(start_pos + target_dir * (upper_length * cos_start) + bend_dir * (upper_length * sin_start));
    positions.push_back(start_pos + target_dir * distance);

    const ik_pose_snapshot snapshot = capture_local_rotations(chain);
    update_rotations_from_positions(chain, positions);
    blend_toward_snapshot(chain, snapshot, weight);

    // The solve is analytical, so a reachable target is hit exactly; the
    // tolerance only has to absorb the reach margin and float error.
    return make_result(chain, target, hard_max * REACH_MARGIN_FRACTION * 2.0f);
}

/**
 * @brief Rejects goals that would write NaN into the skeleton.
 *
 * A single non-finite local rotation propagates through the skinning palette and
 * blanks the whole mesh, and nothing downstream can recover it - so bad input is
 * refused at the boundary rather than clamped.
 */
auto is_goal_finite(const math::vec3& target, const ik_pole& pole, float weight) -> bool
{
    if(!is_finite(target) || !std::isfinite(weight))
    {
        return false;
    }
    return !pole.is_enabled() || is_finite(pole.value);
}

auto find_facing_adjustment_rotation(entt::handle entity) -> math::quat
{
    entt::handle current = entity;
    while(current)
    {
        if(auto* model = current.try_get<model_component>())
        {
            return model->get_facing_adjustment_rotation();
        }
        auto* trans = current.try_get<transform_component>();
        if(trans == nullptr)
        {
            break;
        }
        current = trans->get_parent();
    }
    return math::identity<math::quat>();
}
} // namespace

auto ik_pole::from_legacy_point(const math::vec3& point) -> ik_pole
{
    if(!is_finite(point) || glm::dot(point, point) < IK_LEGACY_POLE_EPSILON)
    {
        return ik_pole{};
    }
    return from_point(point);
}

//--------------------------------------
// Public API entry points.
//--------------------------------------

auto ik_set_position_ccd(entt::handle end_effector,
                         const math::vec3& target,
                         const ik_pole& pole,
                         size_t num_bones_in_chain,
                         const ik_solver_params& params) -> ik_result
{
    if(!is_goal_finite(target, pole, params.weight))
    {
        return {};
    }
    ik_solver_params sanitized = params;
    sanitized.weight = glm::clamp(params.weight, 0.0f, 1.0f);
    if(sanitized.weight <= 0.0f)
    {
        return {};
    }
    const auto chain = collect_chain(end_effector, num_bones_in_chain);
    if(chain.size() < 2)
    {
        return {};
    }
    return solve_ccd(chain, target, pole, sanitized);
}

auto ik_set_position_fabrik(entt::handle end_effector,
                            const math::vec3& target,
                            const ik_pole& pole,
                            size_t num_bones_in_chain,
                            const ik_solver_params& params) -> ik_result
{
    if(!is_goal_finite(target, pole, params.weight))
    {
        return {};
    }
    ik_solver_params sanitized = params;
    sanitized.weight = glm::clamp(params.weight, 0.0f, 1.0f);
    if(sanitized.weight <= 0.0f)
    {
        return {};
    }
    const auto chain = collect_chain(end_effector, num_bones_in_chain);
    if(chain.size() < 2)
    {
        return {};
    }
    return solve_fabrik(chain, target, pole, sanitized);
}

auto ik_set_position_two_bone(entt::handle end_effector,
                              const math::vec3& target,
                              const ik_pole& pole,
                              float weight,
                              float soften) -> ik_result
{
    if(!is_goal_finite(target, pole, weight) || !std::isfinite(soften))
    {
        return {};
    }
    const float blend = glm::clamp(weight, 0.0f, 1.0f);
    if(blend <= 0.0f)
    {
        return {};
    }
    // Exactly three joints or nothing: silently falling back to another solver
    // would change the reach, softening and blend semantics without telling the
    // caller, which is worse than reporting that the chain is too short.
    const auto chain = collect_chain(end_effector, 2);
    if(chain.size() != 3)
    {
        return {};
    }
    return solve_two_bone(chain, target, pole, blend, soften);
}

auto ik_set_rotation(entt::handle bone, const math::quat& rotation, float weight) -> ik_result
{
    ik_result result;
    if(!bone || !is_finite(rotation) || !std::isfinite(weight))
    {
        return result;
    }
    const float blend = glm::clamp(weight, 0.0f, 1.0f);
    if(blend <= 0.0f)
    {
        return result;
    }
    auto* transform = bone.try_get<transform_component>();
    if(transform == nullptr)
    {
        return result;
    }
    const math::quat desired = math::normalize(rotation);
    if(!is_finite(desired))
    {
        return result;
    }
    transform->set_rotation_global(math::normalize(math::slerp(transform->get_rotation_global(), desired, blend)));
    result.applied = true;
    result.reached = blend >= 1.0f;
    return result;
}

auto ik_aim_at_position(entt::handle bone, const math::vec3& target, const ik_aim_params& params) -> ik_result
{
    ik_result result;
    if(!bone || !is_finite(target) || !std::isfinite(params.weight))
    {
        return result;
    }
    const float blend = glm::clamp(params.weight, 0.0f, 1.0f);
    if(blend <= 0.0f)
    {
        return result;
    }
    auto* transform = bone.try_get<transform_component>();
    if(transform == nullptr)
    {
        return result;
    }

    const math::vec3 eye = transform->get_position_global();
    const math::vec3 to_target = target - eye;
    const float distance = math::length(to_target);
    if(distance < IK_EPSILON)
    {
        return result;
    }
    const math::vec3 goal_forward = to_target / distance;

    const math::quat current_rotation = transform->get_rotation_global();
    const math::vec3 local_forward = safe_normalize(params.forward_axis, math::vec3(0.0f, 0.0f, 1.0f));
    const math::vec3 current_forward = math::normalize(current_rotation * local_forward);

    // Cone limit: clamp the swing itself so the bone stops at the limit, rather
    // than making the caller gate the whole solve on and off (which pops).
    math::vec3 desired_forward = goal_forward;
    if(params.max_angle_radians > 0.0f)
    {
        const float cos_limit = std::cos(params.max_angle_radians);
        const float cos_angle = glm::clamp(glm::dot(current_forward, goal_forward), -1.0f, 1.0f);
        if(cos_angle < cos_limit)
        {
            math::vec3 axis = math::cross(current_forward, goal_forward);
            const float axis_len = math::length(axis);
            axis = (axis_len < IK_EPSILON) ? any_perpendicular(current_forward) : (axis / axis_len);
            desired_forward = math::normalize(math::angleAxis(params.max_angle_radians, axis) * current_forward);
        }
    }

    math::quat aimed = math::normalize(math::from_to_rotation(current_forward, desired_forward) * current_rotation);

    // Roll is only forced when the caller supplies an up reference. Without one
    // we leave the existing twist about the aim axis alone: synthesising a roll
    // from a basis is exactly what pops when the up reference happens to line up
    // with the aim direction.
    const float world_up_len = math::length(params.world_up);
    if(world_up_len > IK_EPSILON && is_finite(params.world_up))
    {
        const math::vec3 local_up = safe_normalize(params.up_axis, math::vec3(0.0f, 1.0f, 0.0f));
        const auto flatten = [&desired_forward](const math::vec3& v) -> math::vec3
        {
            return v - glm::dot(v, desired_forward) * desired_forward;
        };
        const math::vec3 current_up_flat = flatten(math::normalize(aimed * local_up));
        const math::vec3 desired_up_flat = flatten(params.world_up / world_up_len);
        if(math::length(current_up_flat) > IK_EPSILON && math::length(desired_up_flat) > IK_EPSILON)
        {
            const math::vec3 from = math::normalize(current_up_flat);
            const math::vec3 to = math::normalize(desired_up_flat);
            const float cos_roll = glm::clamp(glm::dot(from, to), -1.0f, 1.0f);
            const float sin_roll = glm::dot(math::cross(from, to), desired_forward);
            aimed = math::normalize(math::angleAxis(std::atan2(sin_roll, cos_roll), desired_forward) * aimed);
        }
    }

    transform->set_rotation_global(math::normalize(math::slerp(current_rotation, aimed, blend)));

    const math::vec3 achieved = math::normalize(transform->get_rotation_global() * local_forward);
    const float achieved_dot = glm::clamp(glm::dot(achieved, goal_forward), -1.0f, 1.0f);
    result.applied = true;
    result.distance = std::acos(achieved_dot);
    result.reached = achieved_dot > IK_DIRECTION_ALIGNED_DOT;
    return result;
}

auto ik_look_at_position(entt::handle bone, const math::vec3& target, float weight) -> ik_result
{
    ik_aim_params params;
    params.weight = weight;
    return ik_aim_at_position(bone, target, params);
}

auto ik_resolve_bone_axis_local(entt::handle bone) -> math::vec3
{
    const math::vec3 fallback(0.0f, 0.0f, 1.0f);
    if(!bone)
    {
        return fallback;
    }
    auto* transform = bone.try_get<transform_component>();
    if(transform == nullptr)
    {
        return fallback;
    }
    for(const auto& child : transform->get_children())
    {
        auto* child_transform = child.try_get<transform_component>();
        if(child_transform == nullptr)
        {
            continue;
        }
        // A child's local position is already expressed in this bone's space, so
        // it is the bone-local direction the bone visually runs along.
        const math::vec3 offset = child_transform->get_position_local();
        const float length = math::length(offset);
        if(length < IK_EPSILON)
        {
            continue;
        }
        return offset / length;
    }
    return fallback;
}

auto ik_resolve_facing_axis_local(entt::handle bone, const math::vec3& world_direction, bool snap_to_cardinal)
    -> math::vec3
{
    const math::vec3 fallback(0.0f, 0.0f, 1.0f);
    if(!bone || !is_finite(world_direction))
    {
        return fallback;
    }
    auto* transform = bone.try_get<transform_component>();
    if(transform == nullptr)
    {
        return fallback;
    }
    const float length = math::length(world_direction);
    if(length < IK_EPSILON)
    {
        return fallback;
    }

    // rotation^-1 maps a world direction into the bone's own frame, so this is
    // literally "the local axis that is currently aimed that way".
    const math::vec3 local = glm::inverse(transform->get_rotation_global()) * (world_direction / length);
    if(!is_finite(local))
    {
        return fallback;
    }
    if(!snap_to_cardinal)
    {
        return safe_normalize(local, fallback);
    }

    const float abs_x = std::fabs(local.x);
    const float abs_y = std::fabs(local.y);
    const float abs_z = std::fabs(local.z);
    if(abs_x >= abs_y && abs_x >= abs_z)
    {
        return math::vec3(local.x < 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f);
    }
    if(abs_y >= abs_z)
    {
        return math::vec3(0.0f, local.y < 0.0f ? -1.0f : 1.0f, 0.0f);
    }
    return math::vec3(0.0f, 0.0f, local.z < 0.0f ? -1.0f : 1.0f);
}

auto ik_get_facing_adjustment_rotation(entt::handle end_effector) -> math::quat
{
    return find_facing_adjustment_rotation(end_effector);
}
} // namespace unravel
