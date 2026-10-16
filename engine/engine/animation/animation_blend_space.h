#pragma once

#include "animation_pose.h"
#include <engine/rendering/model.h>

namespace unravel
{

auto blend(const math::transform& lhs, const math::transform& rhs, float factor) -> math::transform;

auto blend(const animation_pose::root_motion_result& r1, const animation_pose::root_motion_result& r2, float factor)
    -> animation_pose::root_motion_result;

/**
 * @brief Blends two poses sorted by node index. A bone only one pose animates keeps that pose's
 * value, with its coverage scaled by that pose's share: what the other pose leaves to the layers
 * below shows through instead of the bone jumping. `result_pose` must not alias either input.
 */
void blend_poses(const animation_pose& pose1, const animation_pose& pose2, float factor, animation_pose& result_pose);

/**
 * @brief Weighted multiway blend of poses sorted by node index, with the same coverage rule as
 * the pairwise blend. Weights need not be normalized. `result_pose` must not alias any input.
 */
void blend_poses(const std::vector<animation_pose>& poses,
                 const std::vector<float>& weights,
                 animation_pose& result_pose);

/**
 * @brief The value a node stands for once its coverage is resolved: the node blended over `below`
 * (what the layers below give the bone, or its rest pose) by its coverage; the node's own
 * transform when it is fully covered or nothing lies below.
 */
auto resolve_coverage(const animation_pose::node& node, const animation_pose::node* below) -> math::transform;

/**
 * @brief Replaces `pose` by its change from `reference`, node by node: translation and scale
 * differences, rotation `pose * inverse(reference)`. Nodes missing from the reference keep an
 * identity change. Curves are left untouched.
 */
void make_pose_additive(animation_pose& pose, const animation_pose& reference);

/**
 * @brief Layers `layer` over `base` into `result`.
 *
 * Override replaces each bone by `weight x mask[bone] x coverage`; additive adds the layer's
 * per-bone change (a pose made with make_pose_additive) scaled the same way. Bones the layer does
 * not animate keep the base pose; a bone only the layer animates blends from its `ref_pose` (rest)
 * value, so it fades in and out with the layer instead of popping. Base bones are resolved against
 * `ref_pose` by their coverage, so every bone of `result` is fully covered. Curves the layer has
 * override or add by `weight`; the others keep the base. Root motion comes from the base. `result`
 * must not alias any input.
 */
void blend_layer(const animation_pose& base,
                 const animation_pose& layer,
                 animation_layer_blend_mode mode,
                 float weight,
                 const std::vector<float>& bone_mask,
                 const animation_pose& ref_pose,
                 animation_pose& result);

/// Blends two sorted curve lists; a curve one side lacks counts as 0 there (a crossfade fades it).
void blend_curves(const std::vector<animation_curve_value>& lhs,
                  const std::vector<animation_curve_value>& rhs,
                  float factor,
                  std::vector<animation_curve_value>& result);

struct blend_space_point
{
    friend auto operator==(const blend_space_point&, const blend_space_point&) -> bool = default;

    std::vector<float> parameters;     // The parameter values for this point
    asset_handle<animation_clip> clip; // The animation clip associated with this point
};

/**
 * @brief One clip of a blend and its share, normalized over the blend.
 */
struct blend_space_weight
{
    asset_handle<animation_clip> clip;
    float weight{};
    /// Index of the point the clip belongs to: stable while points enter and leave the blend.
    size_t point{};
};

/**
 * @brief How a two-parameter blend space weighs its points.
 */
enum class blend_space_2d_mode : uint8_t
{
    /// Gradient band interpolation on the raw parameters. Suits parameters of unrelated units,
    /// such as speed and lean.
    freeform_cartesian,
    /// Gradient band interpolation in polar space: points are compared by direction and magnitude,
    /// so motions that differ in direction blend through the arc between them. Suits velocity
    /// (strafe / forward / back) layouts with an idle point at the origin.
    freeform_directional,
};

class blend_space_def
{
public:
    /// Same points (parameters and clips) and the same 2D mode.
    friend auto operator==(const blend_space_def&, const blend_space_def&) -> bool = default;

    using parameter_t = float;
    using parameters_t = std::vector<parameter_t>;

    // Add an animation clip to the blend space at specified parameter values
    void add_clip(const parameters_t& params, const asset_handle<animation_clip>& clip);

    // Compute the blending weights for the current parameters
    void compute_blend(const parameters_t& current_params,
                       std::vector<blend_space_weight>& out_clips) const;

    // Get the number of parameters in the blend space
    auto get_parameter_count() const -> size_t;

    void set_2d_mode(blend_space_2d_mode mode);
    auto get_2d_mode() const -> blend_space_2d_mode;

    auto get_points() const -> const std::vector<blend_space_point>&;

private:
    void compute_blend_1d(const parameters_t& current_params,
                          std::vector<blend_space_weight>& out_clips) const;

    void compute_blend_2d(const parameters_t& current_params,
                          std::vector<blend_space_weight>& out_clips) const;

    std::vector<blend_space_point> points_;
    size_t parameter_count_{0};
    blend_space_2d_mode mode_2d_{blend_space_2d_mode::freeform_cartesian};
};

} // namespace unravel
