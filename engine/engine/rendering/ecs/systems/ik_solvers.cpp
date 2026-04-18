#include "ik_solvers.h"
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/model_component.h>

#include <hpp/small_vector.hpp>
#include <algorithm>
#include <cmath>

namespace unravel
{
template<typename T>
using ik_vector = hpp::small_vector<T>;

auto bones_collect(entt::handle end_effector, size_t num_bones_in_chain) -> ik_vector<transform_component*>
{
    bool skinned = end_effector.all_of<bone_component>();

    ik_vector<transform_component*> chain;
    transform_component* current = end_effector.try_get<transform_component>();
    chain.push_back(current);

    // Collect bones from the end effector upward.
    while(current != nullptr && chain.size() < num_bones_in_chain + 1)
    {
        auto parent = current->get_parent();
        if(parent)
        {
            current = parent.try_get<transform_component>();

            if(skinned)
            {
                if(auto bone = parent.try_get<bone_component>())
                {
                    if(bone->bone_index == 0)
                    {
                        break;
                    }

                    chain.push_back(current);
                }
            }
            else
            {
                chain.push_back(current);
            }
        }
        else
        {
            break;
        }
    }
    // The chain was built from end effector upward; reverse it to have it from root to end effector.
    std::reverse(chain.begin(), chain.end());
    return chain;
}

// Computes the global position of the bone's "end" (tip).
auto get_end_position(transform_component* comp) -> math::vec3
{
    return comp->get_position_global();
}

// -----------------------------------------------------------------------------
// Pole constraint helper.
//
// Pivots every intermediate joint in `positions` around the (root -> end) axis
// so that it lies in the half-plane that contains the `pole` vector. This is
// the canonical way to disambiguate "which side does the knee bend to" in a
// position-based IK solver: the chain remains the same length and the end
// effector stays on target, but the bend direction is forced to match the pole.
//
// Pass a zero-length pole to disable the constraint.
// -----------------------------------------------------------------------------
void apply_pole_constraint(ik_vector<math::vec3>& positions, const math::vec3& pole)
{
    if(glm::dot(pole, pole) < 1e-10f)
    {
        return;
    }

    const size_t n = positions.size();
    if(n < 3)
    {
        return;
    }

    const math::vec3 root = positions.front();
    const math::vec3 end = positions.back();

    math::vec3 axis = end - root;
    const float axis_len = math::length(axis);
    if(axis_len < 1e-5f)
    {
        return;
    }
    axis /= axis_len;

    // Component of the pole direction perpendicular to the root->end axis.
    // That's the target "knee side" half-plane direction.
    math::vec3 pole_dir = pole - root;
    math::vec3 pole_perp = pole_dir - glm::dot(pole_dir, axis) * axis;
    const float pole_perp_len = math::length(pole_perp);
    if(pole_perp_len < 1e-5f)
    {
        // Pole is collinear with the leg: can't disambiguate. Leave chain alone.
        return;
    }
    pole_perp /= pole_perp_len;

    for(size_t i = 1; i + 1 < n; ++i)
    {
        const math::vec3 rel = positions[i] - root;
        const float along = glm::dot(rel, axis);
        const math::vec3 along_vec = along * axis;
        const math::vec3 perp = rel - along_vec;
        const float perp_len = math::length(perp);
        if(perp_len < 1e-5f)
        {
            continue;
        }
        // Keep the joint's distance from the root->end line but rotate it onto
        // the pole side.
        positions[i] = root + along_vec + pole_perp * perp_len;
    }
}

// -----------------------------------------------------------------------------
// Re-derive bone rotations from a set of target joint positions.
//
// Shared by FABRIK (after its forward/backward pass) and by CCD's pole
// post-processing. For each bone we compute the shortest-arc rotation that
// aligns the current bone direction with the desired direction implied by the
// new joint positions, then convert that correction into local space and
// compose it with the bone's existing local rotation.
// -----------------------------------------------------------------------------
void update_rotations_from_positions(ik_vector<transform_component*>& chain,
                                     const ik_vector<math::vec3>& positions)
{
    const size_t n = chain.size();
    for(size_t i = 0; i + 1 < n; ++i)
    {
        transform_component* bone = chain[i];
        transform_component* child = chain[i + 1];

        const math::vec3 current_pos = bone->get_position_global();
        const math::vec3 child_pos = child->get_position_global();
        math::vec3 current_dir = child_pos - current_pos;
        math::vec3 desired_dir = positions[i + 1] - positions[i];

        if(math::length(current_dir) < 1e-5f || math::length(desired_dir) < 1e-5f)
        {
            continue;
        }

        current_dir = math::normalize(current_dir);
        desired_dir = math::normalize(desired_dir);

        const float dot = glm::clamp(glm::dot(current_dir, desired_dir), -1.f, 1.f);
        if(dot > 0.9999f)
        {
            continue;
        }

        math::vec3 axis = math::cross(current_dir, desired_dir);
        if(math::length(axis) < 1e-5f)
        {
            continue;
        }
        axis = math::normalize(axis);

        const float angle = math::acos(dot);
        const math::quat rotation_delta = math::angleAxis(angle, axis);

        auto parent = bone->get_parent();
        transform_component* parent_trans = parent ? parent.try_get<transform_component>() : nullptr;
        const math::quat parent_global_rot =
            (parent_trans) ? parent_trans->get_rotation_global() : math::identity<math::quat>();

        const math::quat local_rotation_delta = glm::inverse(parent_global_rot) * rotation_delta * parent_global_rot;
        bone->set_rotation_local(math::normalize(local_rotation_delta * bone->get_rotation_local()));
    }
}

// Advanced CCD IK solver with unreachable target handling and non-linear weighting.
auto ccdik_advanced(ik_vector<transform_component*>& chain,
                    math::vec3 target,
                    const math::vec3& pole,
                    float threshold = 0.001f,
                    int maxIterations = 10,
                    float damping_error_threshold = 0.5f,
                    float weight_exponent = 1.0f) -> bool
{
    transform_component* end_effector = chain.back();
    const size_t chain_size = chain.size();

    // ----- Unreachable Target Handling -----

    // We approximate bone lengths as the distance from each bone to the end effector.
    float max_reach = 0.f;
    for(size_t i = 0; i < chain.size() - 1; ++i)
    {
        math::vec3 diff = chain[i + 1]->get_position_global() - chain[i]->get_position_global();
        max_reach += math::length(diff);
    }
    // Clamp the target if it lies outside the reachable sphere.
    math::vec3 base_position = chain.front()->get_position_global();
    math::vec3 target_dir = target - base_position;
    float target_dist = math::length(target_dir);
    if(target_dist > max_reach)
    {
        target_dir = math::normalize(target_dir);
        target = base_position + target_dir * (max_reach - 0.001f);
    }
    // ------------------------------------------

    // Main CCD iteration loop.
    for(int iter = 0; iter < maxIterations; ++iter)
    {
        // Traverse the chain from the bone before the end effector to the root.
        for(int i = static_cast<int>(chain_size) - 2; i >= 0; --i)
        {
            transform_component* bone = chain[i];
            math::vec3 bone_pos = bone->get_position_global();
            math::vec3 current_end_pos = get_end_position(end_effector);

            // Compute direction vectors from the current bone to the end effector and target.
            math::vec3 to_end = current_end_pos - bone_pos;
            math::vec3 to_target = target - bone_pos;

            float len_to_end = math::length(to_end);
            float len_to_target = math::length(to_target);

            // Skip this bone if either vector is degenerate.
            if(len_to_end < math::epsilon<float>() || len_to_target < math::epsilon<float>())
            {
                continue;
            }

            to_end = math::normalize(to_end);
            to_target = math::normalize(to_target);

            // Calculate the angle between the current direction and the target direction.
            float cos_angle = math::clamp(math::dot(to_end, to_target), -1.0f, 1.0f);
            float angle = math::acos(cos_angle);

            // Skip tiny adjustments.
            if(std::fabs(angle) < 1e-3f)
            {
                continue;
            }

            // Determine the rotation axis in global space.
            math::vec3 rotation_axis = math::cross(to_end, to_target);
            if(math::length(rotation_axis) < 1e-4f)
            {
                continue;
            }
            rotation_axis = math::normalize(rotation_axis);

            // ----- Dynamic Damping -----
            // Scale the rotation angle based on the current global error.
            float global_error = math::length(target - current_end_pos);
            float damping_factor = math::clamp(global_error / damping_error_threshold, 0.0f, 1.0f);
            float damped_angle = angle * damping_factor;
            // ---------------------------

            // Compute the rotation quaternion (in global space) for the damped angle.
            math::quat rotation_delta = math::angleAxis(damped_angle, rotation_axis);

            // Convert the global rotation to the bone's local space.
            auto parent = bone->get_parent();
            transform_component* parent_trans = parent ? parent.try_get<transform_component>() : nullptr;
            math::quat parent_global_rot =
                (parent_trans) ? parent_trans->get_rotation_global() : math::identity<math::quat>();
            math::quat local_rotation_delta = glm::inverse(parent_global_rot) * rotation_delta * parent_global_rot;

            // ----- Non-Linear Weighting -----
            // Bones closer to the end effector have more influence.
            // Using a non-linear weighting function allows more control over influence falloff.
            float t = float(i + 1) / float(chain_size); // Linear ratio [0,1]
            float weight = std::pow(t, weight_exponent);
            math::quat weighted_local_rotation_delta =
                glm::slerp(math::identity<math::quat>(), local_rotation_delta, weight);
            // ---------------------------------

            // (Optional: Here you could enforce joint limits on the resulting rotation.)

            // Update the bone's local rotation.
            bone->set_rotation_local(math::normalize(weighted_local_rotation_delta * bone->get_rotation_local()));

            // Check overall error after applying the rotation.
            current_end_pos = get_end_position(end_effector);
            float current_error = math::length(target - current_end_pos);
            if(current_error < threshold)
            {
                // Target reached; still enforce the pole constraint below before returning.
                iter = maxIterations;
                break;
            }
        }
    }

    // Post-process: enforce the pole constraint by pivoting intermediate joints
    // into the (root, end, pole) half-plane and re-deriving rotations.
    if(glm::dot(pole, pole) > 1e-10f && chain_size >= 3)
    {
        ik_vector<math::vec3> positions(chain_size);
        for(size_t i = 0; i < chain_size; ++i)
        {
            positions[i] = chain[i]->get_position_global();
        }
        apply_pole_constraint(positions, pole);
        update_rotations_from_positions(chain, positions);
    }

    const float final_error = math::length(target - get_end_position(end_effector));
    return final_error < threshold;
}

// FABRIK IK Solver (Advanced)
// Uses the chain’s rest configuration to compute per‐bone rest directions,
// then iteratively updates joint positions and finally adjusts bone rotations
// so that each bone’s tip aligns with its new child joint position.
//
// The chain is provided as a vector of transform_component pointers,
// ordered from the root (index 0) to the end effector (last element).
//
// NOTE: This implementation assumes that, before IK, the bone positions
// in the chain (obtained via get_position_global()) reflect the rest pose.
// If your system stores a separate rest offset (or tip offset), you can substitute that.
auto fabrik(ik_vector<transform_component*>& chain,
            const math::vec3& target,
            const math::vec3& pole,
            float threshold = 0.001f,
            int max_iterations = 10) -> bool
{
    const size_t n = chain.size();
    if(n < 2)
        return false; // Need at least two joints

    // STEP 1: Capture the original (rest) joint positions.
    ik_vector<math::vec3> orig_positions(n);
    for(size_t i = 0; i < n; ++i)
    {
        orig_positions[i] = chain[i]->get_position_global();
    }

    // STEP 2: Initialize the working positions for IK.
    ik_vector<math::vec3> positions = orig_positions;

    // Compute bone lengths from the rest positions.
    ik_vector<float> bone_lengths(n - 1, 0.f);
    float total_length = 0.f;
    for(size_t i = 0; i < n - 1; ++i)
    {
        bone_lengths[i] = math::length(orig_positions[i + 1] - orig_positions[i]);
        total_length += bone_lengths[i];
    }

    // Store the root position.
    const math::vec3 root_pos = positions[0];

    // STEP 3: Handle unreachable target.
    if(math::length(target - root_pos) > total_length)
    {
        // Target is unreachable: stretch the chain toward the target.
        math::vec3 dir = math::normalize(target - root_pos);
        for(size_t i = 0; i < n - 1; ++i)
        {
            positions[i + 1] = positions[i] + dir * bone_lengths[i];
        }
    }
    else
    {
        // Target is reachable: perform iterative forward and backward passes.
        for(int iter = 0; iter < max_iterations; ++iter)
        {
            // BACKWARD REACHING: Set the end effector to the target.
            positions[n - 1] = target;
            for(int i = static_cast<int>(n) - 2; i >= 0; --i)
            {
                float r = math::length(positions[i + 1] - positions[i]);
                float lambda = bone_lengths[i] / r;
                positions[i] = (1 - lambda) * positions[i + 1] + lambda * positions[i];
            }

            // FORWARD REACHING: Reset the root and move joints forward.
            positions[0] = root_pos;
            for(size_t i = 0; i < n - 1; ++i)
            {
                float r = math::length(positions[i + 1] - positions[i]);
                float lambda = bone_lengths[i] / r;
                positions[i + 1] = (1 - lambda) * positions[i] + lambda * positions[i + 1];
            }

            // Check if the end effector is within threshold of the target.
            if(math::length(positions[n - 1] - target) < threshold)
            {
                break;
            }
        }
    }

    // STEP 3.5: Enforce the pole constraint before deriving rotations.
    // Done after position convergence so the end effector stays at the target.
    apply_pole_constraint(positions, pole);

    // STEP 4: Derive bone rotations from the final joint positions.
    update_rotations_from_positions(chain, positions);

    // (Optionally update the end effector orientation if desired.)

    return true;
}

// -----------------------------------------------------------------------------
// Analytical two-bone IK solver.
//
// Given three joints (start / mid / end), a target and a world-space pole, we
// solve the knee/elbow position directly via the law of cosines. Unlike the
// previous implementation this:
//   * Always writes the result when weight > 0 (the old version silently threw
//     away the solution whenever the target was reachable).
//   * Uses the pole vector correctly as a bending-plane hint (the old version
//     passed the pole as both the pole AND the bend axis, which are supposed
//     to be perpendicular, producing garbage).
//   * Requires no fallback to FABRIK.
//
// A zero-length pole keeps the mid joint in its current bending half-plane.
// -----------------------------------------------------------------------------
auto solve_two_bone_ik(transform_component* start_joint,
                       transform_component* mid_joint,
                       transform_component* end_joint,
                       const math::vec3& target,
                       const math::vec3& pole,
                       float weight,
                       float soften) -> bool
{
    if(weight <= 0.f)
    {
        return false;
    }

    const math::vec3 a = start_joint->get_position_global();
    const math::vec3 b = mid_joint->get_position_global();
    const math::vec3 c = end_joint->get_position_global();

    const float l1 = math::length(b - a);
    const float l2 = math::length(c - b);
    if(l1 < 1e-5f || l2 < 1e-5f)
    {
        return false;
    }

    math::vec3 at = target - a;
    float d = math::length(at);
    if(d < 1e-5f)
    {
        return false;
    }

    // Clamp target distance to the analytically solvable range. `soften` pulls
    // the maximum reach in slightly so full extension never produces a locked
    // knee (which tends to look bad and introduces numerical noise).
    const float soft_t = glm::clamp(soften, 0.f, 1.f);
    const float max_d = (l1 + l2) * (1.f - 0.001f * soft_t);
    const float min_d = std::max(std::fabs(l1 - l2) * 1.001f, 1e-4f);
    d = glm::clamp(d, min_d, max_d);

    const math::vec3 at_dir = at / math::length(at);
    const math::vec3 c_new = a + at_dir * d;

    // Cosine law for the hip angle between AC and AB.
    float cos_a = (l1 * l1 + d * d - l2 * l2) / (2.f * l1 * d);
    cos_a = glm::clamp(cos_a, -1.f, 1.f);
    const float sin_a = std::sqrt(std::max(0.f, 1.f - cos_a * cos_a));

    // Select the bend direction: pole side if provided, else keep the current
    // bend direction to avoid popping.
    auto perpendicularize = [&](const math::vec3& v) -> math::vec3
    {
        return v - glm::dot(v, at_dir) * at_dir;
    };

    math::vec3 knee_dir(0.f);
    bool resolved = false;

    if(glm::dot(pole, pole) > 1e-6f)
    {
        math::vec3 pp = perpendicularize(pole - a);
        const float ppl = math::length(pp);
        if(ppl > 1e-5f)
        {
            knee_dir = pp / ppl;
            resolved = true;
        }
    }

    if(!resolved)
    {
        math::vec3 cur = perpendicularize(b - a);
        const float cl = math::length(cur);
        if(cl > 1e-5f)
        {
            knee_dir = cur / cl;
            resolved = true;
        }
    }

    if(!resolved)
    {
        // Last-ditch fallback when the current pose is perfectly collinear.
        knee_dir = math::cross(at_dir, math::vec3(0, 1, 0));
        if(math::length(knee_dir) < 1e-5f)
        {
            knee_dir = math::cross(at_dir, math::vec3(1, 0, 0));
        }
        knee_dir = math::normalize(knee_dir);
    }

    const math::vec3 b_new = a + at_dir * (l1 * cos_a) + knee_dir * (l1 * sin_a);

    // Save original local rotations so we can blend by weight.
    const math::quat start_local_orig = start_joint->get_rotation_local();
    const math::quat mid_local_orig = mid_joint->get_rotation_local();

    ik_vector<transform_component*> chain;
    chain.push_back(start_joint);
    chain.push_back(mid_joint);
    chain.push_back(end_joint);

    ik_vector<math::vec3> positions;
    positions.push_back(a);
    positions.push_back(b_new);
    positions.push_back(c_new);

    update_rotations_from_positions(chain, positions);

    if(weight < 1.f)
    {
        start_joint->set_rotation_local(
            math::normalize(glm::slerp(start_local_orig, start_joint->get_rotation_local(), weight)));
        mid_joint->set_rotation_local(
            math::normalize(glm::slerp(mid_local_orig, mid_joint->get_rotation_local(), weight)));
    }

    const float final_error = math::length(target - end_joint->get_position_global());
    return final_error < 0.01f;
}

//--------------------------------------
// Public API entry points.
//--------------------------------------

auto ik_set_position_ccd(entt::handle end_effector,
                         const math::vec3& target,
                         const math::vec3& pole,
                         size_t num_bones_in_chain,
                         int max_iterations,
                         float threshold) -> bool
{
    auto bones = bones_collect(end_effector, num_bones_in_chain);
    return ccdik_advanced(bones, target, pole, threshold, max_iterations);
}

auto ik_set_position_fabrik(entt::handle end_effector,
                            const math::vec3& target,
                            const math::vec3& pole,
                            size_t num_bones_in_chain,
                            int max_iterations,
                            float threshold) -> bool
{
    auto bones = bones_collect(end_effector, num_bones_in_chain);
    return fabrik(bones, target, pole, threshold, max_iterations);
}

auto ik_set_position_two_bone(entt::handle end_effector,
                              const math::vec3& target,
                              const math::vec3& pole,
                              float weight,
                              float soften) -> bool
{
    // The analytical two-bone solver converges in a single call. If the chain
    // could not be built (e.g. the end effector has fewer than two parents) we
    // fall back to FABRIK, which at least still honors the pole.
    auto bones = bones_collect(end_effector, 2);
    if(bones.size() == 3)
    {
        return solve_two_bone_ik(bones[0], bones[1], bones[2], target, pole, weight, soften);
    }

    return fabrik(bones, target, pole, 0.001f, 10);
}

auto ik_look_at_position(entt::handle end_effector, const math::vec3& target, float weight) -> bool
{
    auto bones = bones_collect(end_effector, 0);

    auto bone = bones.front();

    // 1) compute the desired “look at” rotation
    math::vec3 eye = bone->get_position_global();
    math::transform lookM = math::lookAt(eye, target, bone->get_y_axis_global());
    lookM = math::inverse(lookM);
    math::quat desired = lookM.get_rotation();

    // 2) fetch current rotation
    math::quat current = bone->get_rotation_global();

    // 3) slerp toward desired by boneWeight
    math::quat blended = math::slerp(current, desired, weight);

    // 4) apply
    bone->set_rotation_global(blended);

    // bone->look_at(target, bone->get_y_axis_global());
    return true;
}
} // namespace unravel
