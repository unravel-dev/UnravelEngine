#include "model_system.h"
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/model_component.h>

#include <engine/ecs/ecs.h>
#include <engine/events.h>
#include <engine/profiler/profiler.h>

#include <hpp/small_vector.hpp>
#include <logging/logging.h>

#define POOLSTL_STD_SUPPLEMENT 1
#include <poolstl/poolstl.hpp>

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

// Advanced CCD IK solver with unreachable target handling and non-linear weighting.
auto ccdik_advanced(ik_vector<transform_component*>& chain,
                    math::vec3 target,
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
                return true; // Target reached.
            }
        }
    }
    return false; // Target not reached within the iteration limit.
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

    // STEP 4: Update bone rotations (mirroring CCDIK logic).
    for(size_t i = 0; i < n - 1; ++i)
    {
        transform_component* bone = chain[i];
        transform_component* child = chain[i + 1];

        // Current direction: where the bone is pointing right now
        math::vec3 current_pos = bone->get_position_global();
        math::vec3 child_pos = child->get_position_global();
        math::vec3 current_dir = math::normalize(child_pos - current_pos);

        // Desired direction: where it *should* be pointing now after FABRIK
        math::vec3 desired_dir = math::normalize(positions[i + 1] - positions[i]);

        // If the vectors are too short or nearly aligned, skip
        if(math::length(current_dir) < 1e-5f || math::length(desired_dir) < 1e-5f)
        {
            continue;
        }

        float dot = math::clamp(math::dot(current_dir, desired_dir), -1.f, 1.f);
        if(dot > 0.9999f) // Almost aligned
        {
            continue;
        }

        math::vec3 axis = math::cross(current_dir, desired_dir);
        if(math::length(axis) < 1e-5f)
        {
            continue;
        }

        axis = math::normalize(axis);
        float angle = math::acos(dot);
        math::quat rotation_delta = math::angleAxis(angle, axis);

        // Convert rotation from world to local space
        auto parent = bone->get_parent();
        transform_component* parent_trans = parent ? parent.try_get<transform_component>() : nullptr;
        math::quat parent_global_rot =
            (parent_trans) ? parent_trans->get_rotation_global() : math::identity<math::quat>();

        math::quat local_rotation_delta = glm::inverse(parent_global_rot) * rotation_delta * parent_global_rot;

        // Apply the rotation delta to the local rotation
        math::quat new_local = math::normalize(local_rotation_delta * bone->get_rotation_local());
        bone->set_rotation_local(new_local);
    }

    // (Optionally update the end effector orientation if desired.)

    return true;
}

// Helper functions to transform points and vectors in homogeneous coordinates.
inline glm::vec3 transform_point(const glm::mat4& mat, const glm::vec3& point)
{
    return glm::vec3(mat * glm::vec4(point, 1.0f));
}

inline glm::vec3 transform_vector(const glm::mat4& mat, const glm::vec3& vec)
{
    return glm::vec3(mat * glm::vec4(vec, 0.0f));
}

/// @brief Two-bone IK solver.
/// @param start_joint                    Pointer to the first (start) joint.
/// @param mid_joint                      Pointer to the second (mid) joint.
/// @param end_joint                      Pointer to the end effector.
/// @param target                       Global target position.
/// @param mid_axis                     Desired bending axis (global, normalized).
/// @param pole_vector                  Reference pole vector (global).
/// @param twist_angle                  Optional twist (in radians).
/// @param weight                       IK weight in [0, 1].
/// @param soften                       Softening factor (0 = rigid, 1 = full softening).
/// @return                             true Flag indicating if the target is nearly reached..
bool solve_two_bone_ik_impl(transform_component* start_joint,
                            transform_component* mid_joint,
                            transform_component* end_joint,
                            const glm::vec3& target,
                            const glm::vec3& mid_axis,    // desired bending axis (global)
                            const glm::vec3& pole_vector, // reference pole vector (global)
                            float twist_angle,
                            float weight,
                            float soften)
{
    // --- 1. Retrieve Global Transforms and Positions ---
    glm::mat4 start_transform = start_joint->get_transform_global();
    glm::mat4 mid_transform = mid_joint->get_transform_global();
    glm::mat4 end_transform = end_joint->get_transform_global();

    glm::vec3 start_pos = start_joint->get_position_global();
    glm::vec3 mid_pos = mid_joint->get_position_global();
    glm::vec3 end_pos = end_joint->get_position_global();

    // --- 2. Compute Inverse Transforms ---
    glm::mat4 inv_start = glm::inverse(start_transform);
    glm::mat4 inv_mid = glm::inverse(mid_transform);

    // --- 3. Set Up Constant Data ---

    // Transform positions into mid joint space.
    glm::vec3 start_ms = transform_point(inv_mid, start_pos);
    glm::vec3 end_ms = transform_point(inv_mid, end_pos);
    // In mid joint space, the mid joint is the origin.
    glm::vec3 start_mid_ms = -start_ms; // vector from mid joint to start joint.
    glm::vec3 mid_end_ms = end_ms;      // vector from mid joint to end effector.

    // Transform positions into start joint space.
    glm::vec3 mid_ss = transform_point(inv_start, mid_pos);
    glm::vec3 end_ss = transform_point(inv_start, end_pos);
    glm::vec3 start_mid_ss = mid_ss;        // start joint is at the origin.
    glm::vec3 mid_end_ss = end_ss - mid_ss; // vector from mid joint to end effector.
    glm::vec3 start_end_ss = end_ss;        // vector from start joint to end effector.

    float start_mid_ss_len2 = glm::dot(start_mid_ss, start_mid_ss);
    float mid_end_ss_len2 = glm::dot(mid_end_ss, mid_end_ss);
    float start_end_ss_len2 = glm::dot(start_end_ss, start_end_ss);

    // --- 4. Soften the Target Position ---
    // Transform target into start joint space.
    glm::vec3 start_target_ss_orig = transform_point(inv_start, target);
    float start_target_ss_orig_len = glm::length(start_target_ss_orig);
    float start_target_ss_orig_len2 = start_target_ss_orig_len * start_target_ss_orig_len;

    // Compute bone lengths in start joint space.
    float l0 = std::sqrt(start_mid_ss_len2); // upper bone length
    float l1 = std::sqrt(mid_end_ss_len2);   // lower bone length
    float chain_length = l0 + l1;
    float bone_diff = fabs(l0 - l1);

    // Compute softening parameters.
    float da = chain_length * glm::clamp(soften, 0.0f, 1.0f);
    float ds = chain_length - da;

    glm::vec3 start_target_ss;
    float start_target_ss_len2;
    bool target_softened = false;
    if(start_target_ss_orig_len > da && start_target_ss_orig_len > bone_diff && ds > 1e-4f)
    {
        float alpha = (start_target_ss_orig_len - da) / ds;
        // Exponential-like blend: ratio = 1 - (3^4)/((alpha+3)^4)
        float ratio = 1.0f - (std::pow(3.0f, 4.0f) / std::pow(alpha + 3.0f, 4.0f));
        float new_target_len = da + ds - ds * ratio;
        start_target_ss = glm::normalize(start_target_ss_orig) * new_target_len;
        start_target_ss_len2 = new_target_len * new_target_len;
        target_softened = true;
    }
    else
    {
        start_target_ss = start_target_ss_orig;
        start_target_ss_len2 = start_target_ss_orig_len2;
    }

    // --- 5. Compute the Mid Joint (Knee) Correction ---
    // Compute the "corrected" knee angle using the law of cosines.
    float cos_corrected = (start_mid_ss_len2 + mid_end_ss_len2 - start_target_ss_len2) / (2.0f * l0 * l1);
    cos_corrected = glm::clamp(cos_corrected, -1.0f, 1.0f);
    float corrected_angle = std::acos(cos_corrected);

    // Compute the "initial" knee angle from the original effector position.
    float cos_initial = (start_mid_ss_len2 + mid_end_ss_len2 - start_end_ss_len2) / (2.0f * l0 * l1);
    cos_initial = glm::clamp(cos_initial, -1.0f, 1.0f);
    float initial_angle = std::acos(cos_initial);

    // Adjust the sign of the initial angle based on the bending direction.
    glm::vec3 mid_axis_ms = glm::normalize(glm::mat3(inv_mid) * mid_axis);
    glm::vec3 bent_side_ref = glm::cross(start_mid_ms, mid_axis_ms);
    if(glm::dot(bent_side_ref, mid_end_ms) < 0.0f)
    {
        initial_angle = -initial_angle;
    }

    float angle_delta = corrected_angle - initial_angle;
    // Mid joint correction quaternion (rotation about the (global) mid_axis).
    glm::quat mid_rot = glm::angleAxis(angle_delta, glm::normalize(mid_axis));

    // --- 6. Compute the Start Joint Correction ---
    // Predict the effector position given the mid correction.
    glm::vec3 rotated_mid_end_ms = glm::rotate(mid_rot, mid_end_ms);
    glm::vec3 rotated_mid_end_global = glm::mat3(mid_transform) * rotated_mid_end_ms;
    glm::vec3 mid_end_ss_final = glm::mat3(inv_start) * rotated_mid_end_global;
    glm::vec3 start_end_ss_final = start_mid_ss + mid_end_ss_final;

    // Compute the rotation aligning the predicted effector direction to the softened target.
    glm::quat end_to_target_rot_ss = glm::rotation(start_end_ss_final, start_target_ss);
    glm::quat start_rot_ss = end_to_target_rot_ss;

    // Compute the "rotate-plane" correction if the target direction is valid.
    if(glm::length(start_target_ss) > 1e-4f)
    {
        // Transform the pole vector into start joint space.
        glm::vec3 pole_ss = glm::normalize(glm::mat3(inv_start) * pole_vector);
        // Compute the reference plane normal (cross of target and pole).
        glm::vec3 ref_plane_normal_ss = glm::normalize(glm::cross(start_target_ss, pole_ss));
        // Compute mid_axis in start joint space.
        glm::vec3 mid_axis_ss = glm::normalize(glm::mat3(inv_start) * (glm::mat3(mid_transform) * mid_axis));
        // Joint chain plane normal (rotated by end_to_target rotation).
        glm::vec3 joint_plane_normal_ss = glm::rotate(end_to_target_rot_ss, mid_axis_ss);

        float rotate_plane_cos_angle =
            glm::dot(glm::normalize(ref_plane_normal_ss), glm::normalize(joint_plane_normal_ss));
        rotate_plane_cos_angle = glm::clamp(rotate_plane_cos_angle, -1.0f, 1.0f);

        // Rotation axis is along the softened target direction (flip if needed).
        glm::vec3 rotate_plane_axis_ss = glm::normalize(start_target_ss);
        if(glm::dot(joint_plane_normal_ss, pole_ss) < 0.0f)
        {
            rotate_plane_axis_ss = -rotate_plane_axis_ss;
        }
        glm::quat rotate_plane_ss = glm::angleAxis(std::acos(rotate_plane_cos_angle), rotate_plane_axis_ss);

        // Apply twist if provided.
        if(fabs(twist_angle) > 1e-5f)
        {
            glm::quat twist_ss = glm::angleAxis(twist_angle, glm::normalize(start_target_ss));
            start_rot_ss = twist_ss * rotate_plane_ss * end_to_target_rot_ss;
        }
        else
        {
            start_rot_ss = rotate_plane_ss * end_to_target_rot_ss;
        }
    }

    // --- 7. Weighting and Final Output ---
    // Force the scalar (w) to be positive.
    if(start_rot_ss.w < 0.0f)
        start_rot_ss = -start_rot_ss;
    if(mid_rot.w < 0.0f)
        mid_rot = -mid_rot;

    // Blend with identity if weight is less than 1.
    if(weight < 1.0f)
    {
        start_rot_ss = glm::slerp(glm::identity<glm::quat>(), start_rot_ss, weight);
        mid_rot = glm::slerp(glm::identity<glm::quat>(), mid_rot, weight);
    }

    // 'reached' is set when softening was applied and weight is full.
    bool reached = target_softened && (weight >= 1.0f);

    if(reached)
    {
        mid_joint->set_rotation_local(glm::normalize(mid_rot));
        start_joint->set_rotation_local(glm::normalize(start_rot_ss));
    }

    return reached;
}

auto solve_two_bone_ik_weighted(transform_component* start_joint,
                                transform_component* mid_joint,
                                transform_component* end_joint,
                                const math::vec3& target,
                                float weight = 1.0f,
                                float soften = 1.0f,
                                const math::vec3& pole = math::vec3(0, 0, 1),
                                float twist_angle = 0.f) -> bool
{
    math::vec3 mid_axis = mid_joint->get_z_axis_global();
    return solve_two_bone_ik_impl(start_joint, mid_joint, end_joint, target, pole, pole, twist_angle, weight, soften);
}

//--------------------------------------
// CCD IK Solver Implementation (Parent-Chain Version)
//--------------------------------------
/*
   This overload of CCDIK takes an end effector and a maximum chain length.
   It builds the IK chain by following parent upward until it reaches the specified count.
*/
auto ik_set_position_ccd(entt::handle end_effector,
                         const math::vec3& target,
                         size_t num_bones_in_chain,
                         float threshold,
                         int max_iterations) -> bool
{
    auto bones = bones_collect(end_effector, num_bones_in_chain);
    return ccdik_advanced(bones, target, threshold, max_iterations);
}

auto ik_set_position_fabrik(entt::handle end_effector,
                            const math::vec3& target,
                            size_t num_bones_in_chain,
                            float threshold,
                            int max_iterations) -> bool
{
    auto bones = bones_collect(end_effector, num_bones_in_chain);
    return fabrik(bones, target, threshold, max_iterations);
}

auto ik_set_position_two_bone(entt::handle end_effector,
                              const math::vec3& target,
                              const math::vec3& forward,
                              float weight,
                              float soften,
                              int max_iterations) -> bool
{
    auto bones = bones_collect(end_effector, 2);
    if(bones.size() == 3)
    {
        auto root = bones[0];
        auto joint = bones[1];
        auto end = bones[2];

        for(int i = 0; i < max_iterations; ++i)
        {
            if(solve_two_bone_ik_weighted(root, joint, end, target, weight, soften, forward))
            {
                return true;
            }
        }
    }

    return fabrik(bones, target, 0.001f, max_iterations);
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

auto model_system::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);
    auto& ev = ctx.get_cached<events>();

    ev.on_play_begin.connect(sentinel_, 1000, this, &model_system::on_play_begin);

    return true;
}

auto model_system::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
}

void model_system::on_play_begin(rtti::context& ctx)
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();

    auto view = scn.registry->view<model_component>();

    // this pass can create new entities so we cannot parallelize it
    std::for_each(view.begin(),
                  view.end(),
                  [&](entt::entity entity)
                  {
                      auto& model_comp = view.get<model_component>(entity);
                      model_comp.init_armature(true);
                  });
}

void model_system::on_frame_update(scene& scn, delta_t dt)
{
    APP_SCOPE_PERF("Model/System Update");

    auto view = scn.registry->view<transform_component, model_component, active_component>();

    // this pass can create new entities so we cannot parallelize it
    std::for_each(view.begin(),
                  view.end(),
                  [&](entt::entity entity)
                  {
                      auto& model_comp = view.get<model_component>(entity);
                      model_comp.init_armature(false);
                  });
}

void model_system::on_frame_before_render(scene& scn, delta_t dt)
{
    APP_SCOPE_PERF("Model/Skinning");
    auto view = scn.registry->view<transform_component, model_component, active_component>();

    // this code should be thread safe as each task works with a whole hierarchy and
    // there is no interleaving between tasks.
    std::for_each(std::execution::par,
                  view.begin(),
                  view.end(),
                  [&](entt::entity entity)
                  {
                      auto& transform_comp = view.get<transform_component>(entity);
                      auto& model_comp = view.get<model_component>(entity);

                      if(model_comp.was_used_last_frame())
                      {
                          model_comp.update_armature();
                      }

                      model_comp.update_world_bounds(transform_comp.get_transform_global());
                  });
}

void model_system::on_play_begin(hpp::span<const entt::handle> entities, delta_t dt)
{
    for(auto entity : entities)
    {
        if(auto model_comp = entity.try_get<model_component>())
        {
            model_comp->init_armature(false);

            auto& transform_comp = entity.get<transform_component>();

            model_comp->update_world_bounds(transform_comp.get_transform_global());
        }
    }
}

} // namespace unravel
