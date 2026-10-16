#include "animation_player.h"
#include <hpp/utility/overload.hpp>
#include <logging/logging.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace unravel
{

auto blend(const math::transform& lhs, const math::transform& rhs, float factor) -> math::transform
{
    math::transform result;
    result.set_translation(math::lerp(lhs.get_translation(), rhs.get_translation(), factor));
    result.set_rotation(math::slerp(lhs.get_rotation(), rhs.get_rotation(), factor));
    result.set_scale(math::lerp(lhs.get_scale(), rhs.get_scale(), factor));
    return result;
}

namespace
{
auto has_root_position(const animation_pose::root_motion_result& r) -> bool
{
    return r.root_position_node_index >= 0;
}

auto has_root_rotation(const animation_pose::root_motion_result& r) -> bool
{
    return r.root_rotation_node_index >= 0;
}

/// The rest pose of bone `index`, or nullptr. `cursor` walks the index-sorted pose forward.
auto find_ref_node(const animation_pose& ref_pose, size_t index, size_t& cursor) -> const animation_pose::node*
{
    while(cursor < ref_pose.nodes.size() && ref_pose.nodes[cursor].desc.index < index)
    {
        ++cursor;
    }
    return cursor < ref_pose.nodes.size() && ref_pose.nodes[cursor].desc.index == index ? &ref_pose.nodes[cursor] : nullptr;
}

/// The change of a bone that does not move: no translation, rotation or scale difference.
auto make_no_change() -> math::transform
{
    math::transform delta;
    delta.set_translation(math::vec3(0.0f));
    delta.set_rotation(math::identity<math::quat>());
    delta.set_scale(math::vec3(0.0f));
    return delta;
}

/// Weight of a new entry in a running weighted average that so far holds `total`.
auto get_incremental_factor(float total, float weight) -> float
{
    const float denom = total + weight;
    return denom > 0.0f ? weight / denom : 0.0f;
}

/// A bone only one of two poses animates: that pose's value, covered by its share of the blend.
void append_one_sided_node(const animation_pose::node& node, float share, animation_pose& result)
{
    auto& out = result.nodes.emplace_back(node);
    out.coverage = node.coverage * share;
}
} // namespace

auto blend(const animation_pose::root_motion_result& r1, const animation_pose::root_motion_result& r2, float factor)
    -> animation_pose::root_motion_result
{
    animation_pose::root_motion_result result;
    result.root_transform_delta = blend(r1.root_transform_delta, r2.root_transform_delta, factor);

    // Weights of a pose without root motion are meaningless defaults - use the
    // other pose's weights outright, and lerp only when both have root motion.
    // Multiplying them could zero BOTH the root and the bone weight of an axis,
    // leaving it neither animated nor moved.
    const bool r1_has_position = has_root_position(r1);
    const bool r2_has_position = has_root_position(r2);
    if(r1_has_position && r2_has_position)
    {
        result.root_position_weights = math::lerp(r1.root_position_weights, r2.root_position_weights, factor);
        result.bone_position_weights = math::lerp(r1.bone_position_weights, r2.bone_position_weights, factor);
    }
    else if(r1_has_position)
    {
        result.root_position_weights = r1.root_position_weights;
        result.bone_position_weights = r1.bone_position_weights;
    }
    else if(r2_has_position)
    {
        result.root_position_weights = r2.root_position_weights;
        result.bone_position_weights = r2.bone_position_weights;
    }

    const bool r1_has_rotation = has_root_rotation(r1);
    const bool r2_has_rotation = has_root_rotation(r2);
    if(r1_has_rotation && r2_has_rotation)
    {
        result.root_rotation_weight = math::lerp(r1.root_rotation_weight, r2.root_rotation_weight, factor);
        result.bone_rotation_weight = math::lerp(r1.bone_rotation_weight, r2.bone_rotation_weight, factor);
    }
    else if(r1_has_rotation)
    {
        result.root_rotation_weight = r1.root_rotation_weight;
        result.bone_rotation_weight = r1.bone_rotation_weight;
    }
    else if(r2_has_rotation)
    {
        result.root_rotation_weight = r2.root_rotation_weight;
        result.bone_rotation_weight = r2.bone_rotation_weight;
    }

    if(!r1_has_position)
    {
        result.root_position_node_index = r2.root_position_node_index;
    }
    else if(!r2_has_position)
    {
        result.root_position_node_index = r1.root_position_node_index;
    }
    else
    {
        result.root_position_node_index = factor < 0.5f ? r1.root_position_node_index : r2.root_position_node_index;
    }

    if(!r1_has_rotation)
    {
        result.root_rotation_node_index = r2.root_rotation_node_index;
    }
    else if(!r2_has_rotation)
    {
        result.root_rotation_node_index = r1.root_rotation_node_index;
    }
    else
    {
        result.root_rotation_node_index = factor < 0.5f ? r1.root_rotation_node_index : r2.root_rotation_node_index;
    }
    return result;
}

namespace
{
/**
 * Blends two poses that are each sorted by node_index in one O(n1 + n2) merge. Each pose counts by
 * its share and by how much of it animates the bone.
 */
void blend_poses_by_node_index_sorted(const animation_pose& pose1,
                                      const animation_pose& pose2,
                                      float factor,
                                      animation_pose& result)
{
    result.nodes.clear();
    result.nodes.reserve(pose1.nodes.size() + pose2.nodes.size());
    result.motion_result = blend(pose1.motion_result, pose2.motion_result, factor);
    blend_curves(pose1.curves, pose2.curves, factor, result.curves);

    size_t i1 = 0;
    size_t i2 = 0;
    while(i1 < pose1.nodes.size() || i2 < pose2.nodes.size())
    {
        if(i2 == pose2.nodes.size() || (i1 < pose1.nodes.size() && pose1.nodes[i1].desc.index < pose2.nodes[i2].desc.index))
        {
            append_one_sided_node(pose1.nodes[i1++], 1.0f - factor, result);
            continue;
        }
        if(i1 == pose1.nodes.size() || pose2.nodes[i2].desc.index < pose1.nodes[i1].desc.index)
        {
            append_one_sided_node(pose2.nodes[i2++], factor, result);
            continue;
        }
        const auto& node1 = pose1.nodes[i1++];
        const auto& node2 = pose2.nodes[i2++];
        const float weight1 = node1.coverage * (1.0f - factor);
        const float weight2 = node2.coverage * factor;
        auto& node = result.nodes.emplace_back();
        node.desc = node1.desc;
        node.transform = blend(node1.transform, node2.transform, get_incremental_factor(weight1, weight2));
        node.coverage = weight1 + weight2;
    }
}

void blend_poses_by_node_index_sorted_multiway(const std::vector<animation_pose>& poses,
                                               const std::vector<float>& weights,
                                               animation_pose& result)
{
    result.nodes.clear();
    result.curves.clear();
    result.motion_result = {};

    if(poses.empty() || weights.size() < poses.size())
    {
        return;
    }

    if(poses.size() == 1)
    {
        result = poses[0];
        return;
    }

    float weight_sum{0.0f};
    for(size_t p = 0; p < poses.size(); ++p)
    {
        weight_sum += weights[p];
    }

    // Curves: a weighted sum over every pose, normalized by the total weight, with a curve a pose
    // lacks counting as 0 there - the same rule the pairwise blend uses.
    const float inverse_sum = weight_sum > 0.0f ? 1.0f / weight_sum : 0.0f;
    for(size_t p = 0; p < poses.size(); ++p)
    {
        const float w = weights[p] * inverse_sum;
        for(const auto& curve_value : poses[p].curves)
        {
            auto it = std::lower_bound(result.curves.begin(),
                                       result.curves.end(),
                                       curve_value.id,
                                       [](const animation_curve_value& lhs, uint32_t id)
                                       {
                                           return lhs.id < id;
                                       });
            if(it == result.curves.end() || it->id != curve_value.id)
            {
                it = result.curves.insert(it, animation_curve_value{curve_value.id, 0.0f});
            }
            it->value += curve_value.value * w;
        }
    }

    // Root motion results, with incremental weight normalization.
    float motion_weight{0.0f};
    for(size_t p = 0; p < poses.size(); ++p)
    {
        const float w = weights[p];
        if(p == 0)
        {
            result.motion_result = poses[p].motion_result;
            motion_weight = w;
            continue;
        }
        result.motion_result = blend(result.motion_result, poses[p].motion_result, get_incremental_factor(motion_weight, w));
        motion_weight += w;
    }

    // Bones: every pose is sorted by node index; a cursor per pose (reused per thread: the pool
    // blends one character's poses per task, every frame) walks them all in one merge.
    const size_t k = poses.size();
    thread_local std::vector<size_t> idx;
    idx.assign(k, 0);
    constexpr size_t NO_INDEX = static_cast<size_t>(-1);
    while(true)
    {
        size_t min_index = NO_INDEX;
        for(size_t p = 0; p < k; ++p)
        {
            if(idx[p] < poses[p].nodes.size())
            {
                min_index = std::min(min_index, poses[p].nodes[idx[p]].desc.index);
            }
        }
        if(min_index == NO_INDEX)
        {
            break;
        }

        // The poses that animate this bone, each by its weight and its coverage; the share the
        // others hold is left to what lies below (the bone's coverage).
        float covered_weight{0.0f};
        math::transform accum_transform{};
        const animation_pose::node_desc* first_desc{nullptr};
        for(size_t p = 0; p < k; ++p)
        {
            if(idx[p] >= poses[p].nodes.size() || poses[p].nodes[idx[p]].desc.index != min_index)
            {
                continue;
            }
            const auto& node = poses[p].nodes[idx[p]++];
            const float w = weights[p] * node.coverage;
            if(first_desc == nullptr)
            {
                accum_transform = node.transform;
                covered_weight = w;
                first_desc = &node.desc;
                continue;
            }
            accum_transform = blend(accum_transform, node.transform, get_incremental_factor(covered_weight, w));
            covered_weight += w;
        }

        auto& out = result.nodes.emplace_back();
        out.desc = *first_desc;
        out.transform = accum_transform;
        out.coverage = weight_sum > 0.0f ? std::min(covered_weight / weight_sum, 1.0f) : 1.0f;
    }
}
} // namespace

void blend_poses(const animation_pose& pose1, const animation_pose& pose2, float factor, animation_pose& result_pose)
{
    blend_poses_by_node_index_sorted(pose1, pose2, factor, result_pose);
}

void blend_poses(const std::vector<animation_pose>& poses,
                 const std::vector<float>& weights,
                 animation_pose& result_pose)
{
    blend_poses_by_node_index_sorted_multiway(poses, weights, result_pose);
}

auto resolve_coverage(const animation_pose::node& node, const animation_pose::node* below) -> math::transform
{
    if(below == nullptr || node.coverage >= 1.0f)
    {
        return node.transform;
    }
    return blend(below->transform, node.transform, std::max(node.coverage, 0.0f));
}

void blend_curves(const std::vector<animation_curve_value>& lhs,
                  const std::vector<animation_curve_value>& rhs,
                  float factor,
                  std::vector<animation_curve_value>& result)
{
    result.clear();
    result.reserve(lhs.size() + rhs.size());
    size_t il = 0;
    size_t ir = 0;
    while(il < lhs.size() || ir < rhs.size())
    {
        if(ir == rhs.size() || (il < lhs.size() && lhs[il].id < rhs[ir].id))
        {
            result.push_back({lhs[il].id, lhs[il].value * (1.0f - factor)});
            ++il;
        }
        else if(il == lhs.size() || rhs[ir].id < lhs[il].id)
        {
            result.push_back({rhs[ir].id, rhs[ir].value * factor});
            ++ir;
        }
        else
        {
            result.push_back({lhs[il].id, math::lerp(lhs[il].value, rhs[ir].value, factor)});
            ++il;
            ++ir;
        }
    }
}

void make_pose_additive(animation_pose& pose, const animation_pose& reference)
{
    size_t ir = 0;
    for(auto& node : pose.nodes)
    {
        while(ir < reference.nodes.size() && reference.nodes[ir].desc.index < node.desc.index)
        {
            ++ir;
        }
        math::transform delta = make_no_change();
        if(ir < reference.nodes.size() && reference.nodes[ir].desc.index == node.desc.index)
        {
            const auto& ref = reference.nodes[ir].transform;
            delta.set_translation(node.transform.get_translation() - ref.get_translation());
            delta.set_rotation(math::normalize(node.transform.get_rotation() * glm::inverse(ref.get_rotation())));
            delta.set_scale(node.transform.get_scale() - ref.get_scale());
        }
        node.transform = delta;
    }
    pose.motion_result.root_transform_delta = {};
}

namespace
{
auto get_mask_weight(const std::vector<float>& bone_mask, size_t index) -> float
{
    if(bone_mask.empty())
    {
        return 1.0f;
    }
    return index < bone_mask.size() ? bone_mask[index] : 0.0f;
}

auto apply_additive_delta(const math::transform& base, const math::transform& delta, float weight) -> math::transform
{
    math::transform result;
    result.set_translation(base.get_translation() + delta.get_translation() * weight);
    const math::quat weighted = math::slerp(math::identity<math::quat>(), delta.get_rotation(), weight);
    result.set_rotation(math::normalize(weighted * base.get_rotation()));
    result.set_scale(base.get_scale() + delta.get_scale() * weight);
    return result;
}

auto combine_node(const math::transform& base,
                  const math::transform& layer,
                  animation_layer_blend_mode mode,
                  float weight) -> math::transform
{
    if(mode == animation_layer_blend_mode::additive)
    {
        return apply_additive_delta(base, layer, weight);
    }
    return blend(base, layer, weight);
}

void combine_curves(const std::vector<animation_curve_value>& base,
                    const std::vector<animation_curve_value>& layer,
                    animation_layer_blend_mode mode,
                    float weight,
                    std::vector<animation_curve_value>& result)
{
    result.clear();
    result.reserve(base.size() + layer.size());
    const bool additive = mode == animation_layer_blend_mode::additive;
    size_t ib = 0;
    size_t il = 0;
    while(ib < base.size() || il < layer.size())
    {
        if(il == layer.size() || (ib < base.size() && base[ib].id < layer[il].id))
        {
            // A curve the layer does not have keeps the base value, as a bone it does not animate does.
            result.push_back(base[ib]);
            ++ib;
        }
        else if(ib == base.size() || layer[il].id < base[ib].id)
        {
            result.push_back({layer[il].id, layer[il].value * weight});
            ++il;
        }
        else
        {
            const float value = additive ? base[ib].value + layer[il].value * weight
                                         : math::lerp(base[ib].value, layer[il].value, weight);
            result.push_back({base[ib].id, value});
            ++ib;
            ++il;
        }
    }
}

} // namespace

void blend_layer(const animation_pose& base,
                 const animation_pose& layer,
                 animation_layer_blend_mode mode,
                 float weight,
                 const std::vector<float>& bone_mask,
                 const animation_pose& ref_pose,
                 animation_pose& result)
{
    result.nodes.clear();
    result.nodes.reserve(std::max(base.nodes.size(), layer.nodes.size()));
    result.motion_result = base.motion_result;
    result.motion_state = base.motion_state;
    combine_curves(base.curves, layer.curves, mode, weight, result.curves);

    size_t ib = 0;
    size_t il = 0;
    size_t ir = 0;
    while(ib < base.nodes.size() || il < layer.nodes.size())
    {
        if(il == layer.nodes.size() || (ib < base.nodes.size() && base.nodes[ib].desc.index < layer.nodes[il].desc.index))
        {
            const auto& base_node = base.nodes[ib++];
            auto& out = result.nodes.emplace_back(base_node);
            out.transform = resolve_coverage(base_node, find_ref_node(ref_pose, base_node.desc.index, ir));
            out.coverage = 1.0f;
            continue;
        }
        const auto& layer_node = layer.nodes[il++];
        const float bone_weight =
            weight * get_mask_weight(bone_mask, layer_node.desc.index) * std::clamp(layer_node.coverage, 0.0f, 1.0f);
        const auto* rest = find_ref_node(ref_pose, layer_node.desc.index, ir);
        if(ib == base.nodes.size() || layer_node.desc.index < base.nodes[ib].desc.index)
        {
            // A bone only the layer animates: an override layer owns it by its weight, from the
            // bone's rest pose; an additive change has no base to apply to and is dropped.
            if(mode == animation_layer_blend_mode::override_pose && bone_weight > 0.0f)
            {
                auto& out = result.nodes.emplace_back(layer_node);
                out.coverage = 1.0f;
                if(rest != nullptr)
                {
                    out.transform = combine_node(rest->transform, layer_node.transform, mode, bone_weight);
                }
            }
            continue;
        }
        const auto& base_node = base.nodes[ib++];
        const math::transform base_value = resolve_coverage(base_node, rest);
        auto& out = result.nodes.emplace_back();
        out.desc = base_node.desc;
        out.transform = bone_weight > 0.0f ? combine_node(base_value, layer_node.transform, mode, bone_weight) : base_value;
    }
}

namespace
{
/// Points of a 1D blend closer than this count as one value.
constexpr float BLEND_1D_EPSILON = 1e-5f;
} // namespace

void blend_space_def::add_clip(const parameters_t& params, const asset_handle<animation_clip>& clip)
{
    if(!points_.empty() && params.size() != parameter_count_)
    {
        APPLOG_WARNING("blend_space_def::add_clip: point with {} parameters rejected, blend space has {}",
                       params.size(),
                       parameter_count_);
        return;
    }
    points_.emplace_back(blend_space_point{params, clip});
    parameter_count_ = params.size(); // Ensure all points have the same number of parameters
}

void blend_space_def::compute_blend(const parameters_t& current_params,
                                    std::vector<blend_space_weight>& out_clips) const
{
    // Clear the output vector
    out_clips.clear();

    if(points_.empty())
    {
        return;
    }

    if(current_params.size() < parameter_count_)
    {
        // Parameters were never (fully) supplied - fall back to the first
        // point instead of reading out of bounds / silently freezing.
        out_clips.push_back({points_.front().clip, 1.0f, 0});
        return;
    }

    if(parameter_count_ == 1)
    {
        compute_blend_1d(current_params, out_clips);
        return;
    }
    if(parameter_count_ == 2)
    {
        compute_blend_2d(current_params, out_clips);
        return;
    }

    // Blend spaces are 1D or 2D; any other point layout plays its first point (controller
    // validation reports it).
    out_clips.push_back({points_.front().clip, 1.0f, 0});
}

void blend_space_def::compute_blend_1d(const parameters_t& current_params,
                                       std::vector<blend_space_weight>& out_clips) const
{
    const float param = current_params[0];
    // The points on either side of the parameter: the highest at or below it and the lowest at or
    // above it. Of several points on one value the last one counts.
    int below = -1;
    int above = -1;
    for(size_t i = 0; i < points_.size(); ++i)
    {
        const float value = points_[i].parameters[0];
        if(value <= param && (below < 0 || value >= points_[size_t(below)].parameters[0]))
        {
            below = int(i);
        }
        if(value >= param && (above < 0 || value <= points_[size_t(above)].parameters[0]))
        {
            above = int(i);
        }
    }
    if(below < 0 && above < 0)
    {
        // Not a number: no point compares; play the first.
        out_clips.push_back({points_.front().clip, 1.0f, 0});
        return;
    }
    // Outside the range the nearest end plays alone.
    if(below < 0 || above < 0 || below == above)
    {
        const size_t point = size_t(below >= 0 ? below : above);
        out_clips.push_back({points_[point].clip, 1.0f, point});
        return;
    }
    const float v0 = points_[size_t(below)].parameters[0];
    const float v1 = points_[size_t(above)].parameters[0];
    const float t = v1 - v0 > BLEND_1D_EPSILON ? math::clamp((param - v0) / (v1 - v0), 0.0f, 1.0f) : 0.0f;
    out_clips.push_back({points_[size_t(below)].clip, 1.0f - t, size_t(below)});
    out_clips.push_back({points_[size_t(above)].clip, t, size_t(above)});
}

namespace
{
/// Weight of the angular term against the magnitude term in polar gradient bands. Two points of
/// equal speed 90 degrees apart meet half way at any value; larger values make a point's influence
/// fall off faster with direction than with speed.
constexpr float DIRECTIONAL_ANGLE_SCALE = 2.0f;

/// Points closer than this (in parameter units) count as coincident.
constexpr float BLEND_POINT_EPSILON = 1e-5f;

/// Two points whose directions differ from a half turn by less than this (radians) are opposite.
constexpr float OPPOSITE_ANGLE_EPSILON = 1e-4f;

struct vec2_param
{
    float x{};
    float y{};
};

auto dot2(const vec2_param& a, const vec2_param& b) -> float
{
    return a.x * b.x + a.y * b.y;
}

auto length2d(const vec2_param& a) -> float
{
    return std::sqrt(dot2(a, a));
}

/// Signed angle from `from` to `to`, counter-clockwise positive.
auto signed_angle(const vec2_param& from, const vec2_param& to) -> float
{
    const float cross = from.x * to.y - from.y * to.x;
    return std::atan2(cross, dot2(from, to));
}

/**
 * @brief Gradient band influence of point i on a pair (i, j), in raw parameter space.
 */
auto cartesian_band(const vec2_param& pi, const vec2_param& pj, const vec2_param& p) -> float
{
    const vec2_param pij{pj.x - pi.x, pj.y - pi.y};
    const float len2 = dot2(pij, pij);
    if(len2 < BLEND_POINT_EPSILON * BLEND_POINT_EPSILON)
    {
        return 1.0f;
    }
    const vec2_param pip{p.x - pi.x, p.y - pi.y};
    return 1.0f - dot2(pip, pij) / len2;
}

/**
 * @brief Gradient band influence in polar space (magnitude difference, scaled angle).
 *
 * A point at the origin has no direction: its pairs compare magnitudes only, so an idle at the
 * centre fades out radially, and an outer point's pair with it ramps up radially along its own
 * direction.
 */
auto directional_band(const vec2_param& pi, const vec2_param& pj, const vec2_param& p) -> float
{
    const float mi = length2d(pi);
    const float mj = length2d(pj);
    const float mp = length2d(p);
    const float average = (mi + mj) * 0.5f;
    if(average < BLEND_POINT_EPSILON)
    {
        return 1.0f;
    }
    const bool i_has_direction = mi > BLEND_POINT_EPSILON;
    const bool j_has_direction = mj > BLEND_POINT_EPSILON;
    const bool p_has_direction = mp > BLEND_POINT_EPSILON;

    float angle_ij = 0.0f;
    float angle_ip = 0.0f;
    if(i_has_direction && j_has_direction)
    {
        angle_ij = signed_angle(pi, pj);
        angle_ip = p_has_direction ? signed_angle(pi, p) : 0.0f;
        if(std::fabs(std::fabs(angle_ij) - math::pi<float>()) < OPPOSITE_ANGLE_EPSILON)
        {
            // Opposite points have no side: compare unsigned angles, so the result does not hinge
            // on the sign of a zero cross product.
            angle_ij = std::fabs(angle_ij);
            angle_ip = std::fabs(angle_ip);
        }
        else
        {
            // Measure the point's angle on the branch around the pair, so a point just past the
            // half turn is not read as a full turn the other way.
            const float middle = angle_ij * 0.5f;
            if(angle_ip < middle - math::pi<float>())
            {
                angle_ip += math::two_pi<float>();
            }
            else if(angle_ip >= middle + math::pi<float>())
            {
                angle_ip -= math::two_pi<float>();
            }
        }
    }

    const vec2_param pij{(mj - mi) / average, angle_ij * DIRECTIONAL_ANGLE_SCALE};
    const vec2_param pip{(mp - mi) / average, angle_ip * DIRECTIONAL_ANGLE_SCALE};
    const float len2 = dot2(pij, pij);
    if(len2 < BLEND_POINT_EPSILON * BLEND_POINT_EPSILON)
    {
        return 1.0f;
    }
    return 1.0f - dot2(pip, pij) / len2;
}
} // namespace

/**
 * @brief Freeform 2D weights by gradient band interpolation.
 *
 * Every point's influence is the minimum, over every other point, of a linear band that is 1 at the
 * point and 0 at the other one; influences are clamped at 0 and normalized. This works for any
 * layout - no grid, triangulation or corner points needed - is exact on every point, never goes
 * negative, and changes continuously with the parameters (Johansen 2009, the scheme behind
 * freeform 2D blend trees). The directional variant measures the bands in polar space.
 */
void blend_space_def::compute_blend_2d(const parameters_t& current_params,
                                       std::vector<blend_space_weight>& out_clips) const
{
    out_clips.clear();
    if(parameter_count_ != 2 || points_.empty())
    {
        return;
    }

    const vec2_param p{current_params[0], current_params[1]};
    if(points_.size() == 1)
    {
        out_clips.push_back({points_.front().clip, 1.0f, 0});
        return;
    }

    const bool directional = mode_2d_ == blend_space_2d_mode::freeform_directional;
    float total = 0.0f;
    out_clips.reserve(points_.size());
    for(size_t i = 0; i < points_.size(); ++i)
    {
        const vec2_param pi{points_[i].parameters[0], points_[i].parameters[1]};
        float influence = 1.0f;
        for(size_t j = 0; j < points_.size() && influence > 0.0f; ++j)
        {
            if(i == j)
            {
                continue;
            }
            const vec2_param pj{points_[j].parameters[0], points_[j].parameters[1]};
            const float band = directional ? directional_band(pi, pj, p) : cartesian_band(pi, pj, p);
            influence = std::min(influence, band);
        }
        influence = std::max(influence, 0.0f);
        if(influence > 0.0f)
        {
            out_clips.push_back({points_[i].clip, influence, i});
            total += influence;
        }
    }

    if(total <= 0.0f)
    {
        // Every band excluded the input (only possible for degenerate layouts): nearest point.
        out_clips.clear();
        size_t best = 0;
        float best_distance = std::numeric_limits<float>::max();
        for(size_t i = 0; i < points_.size(); ++i)
        {
            const vec2_param offset{points_[i].parameters[0] - p.x, points_[i].parameters[1] - p.y};
            const float distance = dot2(offset, offset);
            if(distance < best_distance)
            {
                best_distance = distance;
                best = i;
            }
        }
        out_clips.push_back({points_[best].clip, 1.0f, best});
        return;
    }

    for(auto& clip_weight : out_clips)
    {
        clip_weight.weight /= total;
    }
}

void blend_space_def::set_2d_mode(blend_space_2d_mode mode)
{
    mode_2d_ = mode;
}

auto blend_space_def::get_2d_mode() const -> blend_space_2d_mode
{
    return mode_2d_;
}

auto blend_space_def::get_points() const -> const std::vector<blend_space_point>&
{
    return points_;
}

auto blend_space_def::get_parameter_count() const -> size_t
{
    return parameter_count_;
}

} // namespace unravel
