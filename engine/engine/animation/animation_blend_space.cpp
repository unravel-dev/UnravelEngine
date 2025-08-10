#include "animation_player.h"
#include <hpp/utility/overload.hpp>
namespace unravel
{

// Computes an additive blend between a base and an additive transform,
// using a reference transform. The additive transform is assumed to be authored
// relative to the reference pose. The result is:
//   result = base + weight * (additive - ref)
// For rotations, we compute the delta rotation and then slerp from identity.
auto blend_additive(const math::transform& base,
                    const math::transform& additive,
                    const math::transform& ref,
                    float weight) -> math::transform
{
    math::transform result;
    // Translation: base + weight*(additive - ref)
    result.set_translation(base.get_translation() + weight * (additive.get_translation() - ref.get_translation()));

    // Rotation: Compute delta = additive.rotation * inverse(ref.rotation)
    math::quat additive_delta = additive.get_rotation() * glm::inverse(ref.get_rotation());
    // Interpolate from identity to the delta
    math::quat weighted_delta = math::slerp(math::identity<math::quat>(), additive_delta, weight);
    // Apply the weighted delta to the base rotation
    result.set_rotation(math::normalize(weighted_delta * base.get_rotation()));

    // Scale: base + weight*(additive - ref)
    result.set_scale(base.get_scale() + weight * (additive.get_scale() - ref.get_scale()));

    return result;
}

/**
 * Additively blends the base pose with an additive pose using a reference pose.
 * Each pose's nodes are sorted by node_index.
 *
 * For each node in the base pose:
 *   - If a matching node exists in the additive pose (by index), then:
 *       result.transform = base.transform + weight * (additive.transform - ref.transform)
 *   - Otherwise, the base node is copied.
 *
 * Root motion is blended similarly.
 */
void blend_poses_by_node_index_sorted_additive(const animation_pose& base,
                                               const animation_pose& additive,
                                               const animation_pose& ref_pose,
                                               float weight,
                                               animation_pose& result)
{
    result.nodes.clear();
    // Reserve based on the ref pose since it is the most complete.
    result.nodes.reserve(ref_pose.nodes.size());

    // Blend the root transform delta using additive blending.
    result.motion_result.root_transform_delta = blend_additive(base.motion_result.root_transform_delta,
                                                               additive.motion_result.root_transform_delta,
                                                               ref_pose.motion_result.root_transform_delta,
                                                               weight);
    // For weights, you might choose to leave them as-is or blend them differently.
    result.motion_result.root_position_weights = base.motion_result.root_position_weights;
    result.motion_result.bone_position_weights = base.motion_result.bone_position_weights;

    result.motion_result.root_rotation_weight = base.motion_result.root_rotation_weight;
    result.motion_result.bone_rotation_weight = base.motion_result.bone_rotation_weight;

    result.motion_result.root_position_node_index = base.motion_result.root_position_node_index;
    result.motion_result.root_rotation_node_index = base.motion_result.root_rotation_node_index;

    // We'll use indices to iterate through base and additive poses.
    size_t i_base = 0;
    size_t i_add = 0;

    // Iterate over each node in the reference pose.
    for(const auto& ref_node : ref_pose.nodes)
    {
        animation_pose::node blended_node;
        blended_node.desc = ref_node.desc;

        // Default to the ref node's transform if no corresponding node is found.
        math::transform base_transform = ref_node.transform;
        math::transform additive_transform = ref_node.transform;

        // Advance the base index until we find a node with an index >= ref_node.desc.index.
        while(i_base < base.nodes.size() && base.nodes[i_base].desc.index < ref_node.desc.index)
        {
            ++i_base;
        }
        // If we found an exact match in the base pose, use its transform.
        if(i_base < base.nodes.size() && base.nodes[i_base].desc.index == ref_node.desc.index)
        {
            base_transform = base.nodes[i_base].transform;
        }

        // Do the same for the additive pose.
        while(i_add < additive.nodes.size() && additive.nodes[i_add].desc.index < ref_node.desc.index)
        {
            ++i_add;
        }
        if(i_add < additive.nodes.size() && additive.nodes[i_add].desc.index == ref_node.desc.index)
        {
            additive_transform = additive.nodes[i_add].transform;
        }

        // Blend additively:
        // The idea is that the additive animation was authored as an offset relative to the reference pose.
        // So the delta is (additive_transform - ref_node.transform) and we add that (scaled by weight) onto base.
        blended_node.transform = blend_additive(base_transform, additive_transform, ref_node.transform, weight);

        result.nodes.push_back(blended_node);
    }
}

void blend_poses_additive(const animation_pose& base,
                          const animation_pose& additive,
                          const animation_pose& ref_pose,
                          float weight,
                          animation_pose& result)
{
    blend_poses_by_node_index_sorted_additive(base, additive, ref_pose, weight, result);
}

auto blend(const math::transform& lhs, const math::transform& rhs, float factor) -> math::transform
{
    math::transform result;
    result.set_translation(math::lerp(lhs.get_translation(), rhs.get_translation(), factor));
    result.set_rotation(math::slerp(lhs.get_rotation(), rhs.get_rotation(), factor));
    result.set_scale(math::lerp(lhs.get_scale(), rhs.get_scale(), factor));
    return result;
}

auto blend(const animation_pose::root_motion_result& r1, const animation_pose::root_motion_result& r2, float factor)
    -> animation_pose::root_motion_result
{
    animation_pose::root_motion_result result;
    result.root_transform_delta = blend(r1.root_transform_delta, r2.root_transform_delta, factor);
    result.root_position_weights = r1.root_position_weights * r2.root_position_weights;
    result.bone_position_weights = r1.bone_position_weights * r2.bone_position_weights;

    result.root_rotation_weight = r1.root_rotation_weight * r2.root_rotation_weight;
    result.bone_rotation_weight = r1.bone_rotation_weight * r2.bone_rotation_weight;

    if(r1.root_position_node_index == -1)
    {
        result.root_position_node_index = r2.root_position_node_index;
    }
    else if(r2.root_position_node_index == -1)
    {
        result.root_position_node_index = r1.root_position_node_index;
    }
    else
    {
        result.root_position_node_index = factor < 0.5f ? r1.root_position_node_index : r2.root_position_node_index;
    }

    if(r1.root_rotation_node_index == -1)
    {
        result.root_rotation_node_index = r2.root_rotation_node_index;
    }
    else if(r2.root_rotation_node_index == -1)
    {
        result.root_rotation_node_index = r1.root_rotation_node_index;
    }
    else
    {
        result.root_rotation_node_index = factor < 0.5f ? r1.root_rotation_node_index : r2.root_rotation_node_index;
    }
    return result;
}

/**
 * Blend two poses that are each sorted by node_index.
 *
 * This runs in O(n1 + n2) by merging in one pass.
 */
void blend_poses_by_node_index_sorted(const animation_pose& pose1,
                                      const animation_pose& pose2,
                                      float factor,
                                      animation_pose& result)
{
    result.nodes.clear();
    result.nodes.reserve(pose1.nodes.size() + pose2.nodes.size());

    size_t i1 = 0;
    size_t i2 = 0;

    result.motion_result = blend(pose1.motion_result, pose2.motion_result, factor);

    while(i1 < pose1.nodes.size() && i2 < pose2.nodes.size())
    {
        const auto& node1 = pose1.nodes[i1];
        const auto& node2 = pose2.nodes[i2];

        if(node1.desc.index < node2.desc.index)
        {
            // node1 is "missing" in pose2, so copy node1
            result.nodes.push_back(node1);
            i1++;
        }
        else if(node1.desc.index > node2.desc.index)
        {
            // node2 is "missing" in pose1, so copy node2
            result.nodes.push_back(node2);
            i2++;
        }
        else
        {
            auto& node = result.nodes.emplace_back();
            // node1.index == node2.index -> blend
            node.desc = node1.desc;
            node.transform = blend(node1.transform, node2.transform, factor);

            i1++;
            i2++;
        }
    }

    // Copy the remaining nodes in pose1
    while(i1 < pose1.nodes.size())
    {
        result.nodes.push_back(pose1.nodes[i1]);
        i1++;
    }

    // Copy the remaining nodes in pose2
    while(i2 < pose2.nodes.size())
    {
        result.nodes.push_back(pose2.nodes[i2]);
        i2++;
    }
}

void blend_poses(const animation_pose& pose1, const animation_pose& pose2, float factor, animation_pose& result_pose)
{
    blend_poses_by_node_index_sorted(pose1, pose2, factor, result_pose);
}

void blend_poses_by_node_index_sorted_multiway(const std::vector<animation_pose>& poses,
                                               const std::vector<float>& weights,
                                               animation_pose& result)
{
    // 1) If there's only 1 pose, it's trivial
    if(poses.size() == 1)
    {
        result = poses[0];
        return;
    }

    // We'll assume each pose is sorted by node.index in ascending order
    // We'll keep a pointer array "idx[]" for each pose
    size_t k = poses.size();
    std::vector<size_t> idx(k, 0);

    result.nodes.clear();

    // We'll do a loop while there's at least one pose not at end
    while(true)
    {
        // 2) Among all poses that are not finished, find the smallest node.index
        size_t min_index = (size_t)-1; // sentinel for "none"
        bool all_finished = true;

        // Collect all unique indices that appear for this iteration
        // e.g. we might see multiple poses have the same current index
        // or some might have bigger ones
        for(size_t p = 0; p < k; ++p)
        {
            if(idx[p] < poses[p].nodes.size())
            {
                all_finished = false;
                size_t node_index = poses[p].nodes[idx[p]].desc.index;
                if(min_index == (size_t)-1 || node_index < min_index)
                {
                    min_index = node_index;
                }
            }
        }

        // If all are finished, break
        if(all_finished)
        {
            break;
        }

        // 3) Collect transforms from all poses that have this minIndex
        float total_weight{0.0f};
        math::transform accum_transform{}; // maybe identity or something
        bool first_transform_set{false};

        for(size_t p = 0; p < k; ++p)
        {
            if(idx[p] < poses[p].nodes.size())
            {
                const auto& node = poses[p].nodes[idx[p]];
                if(node.desc.index == min_index)
                {
                    // We want to blend node.transform into accumTransform
                    float w = weights[p];
                    if(!first_transform_set)
                    {
                        accum_transform = node.transform; // first
                        total_weight = w;
                        first_transform_set = true;

                        result.motion_result = poses[p].motion_result;
                    }
                    else
                    {
                        // Blend accumTransform w/ node.transform
                        float factor = w / (total_weight + w);
                        accum_transform = blend(accum_transform, node.transform, factor);
                        result.motion_result = blend(result.motion_result, poses[p].motion_result, factor);

                        total_weight += w;
                    }
                    // advance pointer in pose p
                    idx[p]++;
                }
            }
        }

        // 4) Push the final blended node into result
        auto& out = result.nodes.emplace_back();
        out.desc.index = min_index;
        out.transform = accum_transform;
        // name can be optional or from whichever pose you prefer
        // out.name = ?
    }

    // Now result has the union of all node_index across all poses, blended by weight.
}

void blend_poses(const std::vector<animation_pose>& poses,
                 const std::vector<float>& weights,
                 animation_pose& result_pose)
{
    blend_poses_by_node_index_sorted_multiway(poses, weights, result_pose);
}

void blend_space_def::add_clip(const parameters_t& params, const asset_handle<animation_clip>& clip)
{
    points_.emplace_back(blend_space_point{params, clip});
    parameter_count_ = params.size(); // Ensure all points have the same number of parameters
}

void blend_space_def::compute_blend(const parameters_t& current_params,
                                    std::vector<std::pair<asset_handle<animation_clip>, float>>& out_clips) const
{
    // Clear the output vector
    out_clips.clear();

    if(parameter_count_ == 1)
    {
        // 1D linear interpolation
        // (see example below)
        compute_blend_1d(current_params, out_clips);
        return;
    }
    if(parameter_count_ == 2)
    {
        compute_blend_2d(current_params, out_clips);
        return;
    }

    // Implement 3D or N-D as needed
}

void blend_space_def::compute_blend_1d(const parameters_t& current_params,
                                       std::vector<std::pair<asset_handle<animation_clip>, float>>& out_clips) const
{
    // current_params[0] is the single parameter (e.g., "speed")
    float param = current_params[0];

    // Gather all unique parameter values from points_
    std::set<float> unique_values;
    for(const auto& point : points_)
    {
        unique_values.insert(point.parameters[0]);
    }

    // Turn into a sorted vector
    std::vector<float> sorted_values(unique_values.begin(), unique_values.end());

    // If there's only 1 or 0 unique values, there's no blending, just use that clip
    if(sorted_values.size() <= 1)
    {
        if(!points_.empty())
        {
            // Assume they're all the same param -> 100% weight on the first
            out_clips.emplace_back(points_.front().clip, 1.0f);
        }
        return;
    }

    // 1) Find which interval param falls into
    //    e.g. if sortedValues = [0.0, 2.0, 5.0], and param = 1.5
    //    that’s between indices 0 and 1
    auto find_index_1d = [&](float p)
    {
        for(size_t i = 0; i < sorted_values.size() - 1; ++i)
        {
            if(p >= sorted_values[i] && p <= sorted_values[i + 1])
                return i;
        }
        // clamp if out of range
        return sorted_values.size() - 2;
    };

    size_t idx = find_index_1d(param);
    float v0 = sorted_values[idx];
    float v1 = sorted_values[idx + 1];

    // 2) Interpolation factor
    float t = 0.0f;
    if(fabs(v1 - v0) > 1e-5f) // avoid divide by zero
        t = (param - v0) / (v1 - v0);

    t = math::clamp(t, 0.0f, 1.0f);
    // 3) Find the exact clip(s) that correspond to v0 and v1
    //    We’ll pick the *closest* clip for each param value (since we might have multiple points at the same param)
    const blend_space_point* p0 = nullptr;
    const blend_space_point* p1 = nullptr;

    // We'll store whichever points match v0 and v1
    // (If you had multiple clips at the same param, you'd either pick one or store them all—depends on design.)
    for(const auto& point : points_)
    {
        if(fabs(point.parameters[0] - v0) < 1e-5f)
            p0 = &point;
        if(fabs(point.parameters[0] - v1) < 1e-5f)
            p1 = &point;
    }

    // 4) If we found both endpoints, output them with weight
    //    Typically you'll have 2 clips if param is within range,
    //    or if param < v0 or param > v1 you'll effectively clamp to one clip.
    if(p0 && p1 && p0 != p1)
    {
        float w0 = 1.0f - t;
        float w1 = t;
        // Add them if both weights are > 0, or clamp if out of range
        out_clips.emplace_back(p0->clip, w0);
        out_clips.emplace_back(p1->clip, w1);
    }
    else if(p0) // param is out of range or there's only one valid endpoint
    {
        // 100% to p0
        out_clips.emplace_back(p0->clip, 1.0f);
    }
    else if(p1)
    {
        // 100% to p1
        out_clips.emplace_back(p1->clip, 1.0f);
    }
}

void blend_space_def::compute_blend_2d(const parameters_t& current_params,
                                       std::vector<std::pair<asset_handle<animation_clip>, float>>& out_clips) const
{
    // Clear the output vector
    out_clips.clear();

    // For simplicity, we'll handle a 2D blend space with bilinear interpolation
    if(parameter_count_ != 2)
    {
        // Implement support for other dimensions as needed
        return;
    }

    // Find the four closest points for bilinear interpolation
    // This involves finding the rectangle (grid cell) that the current parameters fall into

    // Collect all parameter values along each axis
    std::set<float> param0_values;
    std::set<float> param1_values;
    for(const auto& point : points_)
    {
        param0_values.insert(point.parameters[0]);
        param1_values.insert(point.parameters[1]);
    }

    // Convert sets to vectors for indexing
    std::vector<float> param0_vector(param0_values.begin(), param0_values.end());
    std::vector<float> param1_vector(param1_values.begin(), param1_values.end());

    // Find indices along each axis
    auto find_index = [](const std::vector<float>& values, float param) -> size_t
    {
        for(size_t i = 0; i < values.size() - 1; ++i)
        {
            if(param >= values[i] && param <= values[i + 1])
            {
                return i;
            }
        }
        return values.size() - 2; // Return last index if beyond range
    };

    size_t index0 = find_index(param0_vector, current_params[0]);
    size_t index1 = find_index(param1_vector, current_params[1]);

    // Get the parameter values at the grid corners
    float p00 = param0_vector[index0];
    float p01 = param0_vector[index0 + 1];
    float p10 = param1_vector[index1];
    float p11 = param1_vector[index1 + 1];

    // Collect the four corner points
    std::array<const blend_space_point*, 4> corner_points = {nullptr, nullptr, nullptr, nullptr};

    for(const auto& point : points_)
    {
        const auto& params = point.parameters;
        if(params[0] == p00 && params[1] == p10)
            corner_points[0] = &point; // Bottom-left
        if(params[0] == p01 && params[1] == p10)
            corner_points[1] = &point; // Bottom-right
        if(params[0] == p00 && params[1] == p11)
            corner_points[2] = &point; // Top-left
        if(params[0] == p01 && params[1] == p11)
            corner_points[3] = &point; // Top-right
    }

    // Ensure all corner points are found
    for(const auto* cp : corner_points)
    {
        if(!cp)
            return; // Cannot interpolate without all corner points
    }

    // Compute interpolation factors
    float tx = (current_params[0] - p00) / (p01 - p00);
    float ty = (current_params[1] - p10) / (p11 - p10);

    // Compute weights
    float w_bl = (1 - tx) * (1 - ty); // Bottom-left
    float w_br = tx * (1 - ty);       // Bottom-right
    float w_tl = (1 - tx) * ty;       // Top-left
    float w_tr = tx * ty;             // Top-right

    // Output the clips and their weights
    out_clips.emplace_back(corner_points[0]->clip, w_bl);
    out_clips.emplace_back(corner_points[1]->clip, w_br);
    out_clips.emplace_back(corner_points[2]->clip, w_tl);
    out_clips.emplace_back(corner_points[3]->clip, w_tr);
}

auto blend_space_def::get_parameter_count() const -> size_t
{
    return parameter_count_;
}

} // namespace unravel
