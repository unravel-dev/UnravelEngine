#pragma once

#include "animation_pose.h"
#include <engine/rendering/model.h>

namespace unravel
{

auto blend(const math::transform& lhs, const math::transform& rhs, float factor) -> math::transform;

void blend_poses(const animation_pose& pose1, const animation_pose& pose2, float factor, animation_pose& result_pose);

auto blend_additive(const math::transform& base,
                    const math::transform& additive,
                    const math::transform& ref,
                    float weight) -> math::transform;
void blend_poses_additive(const animation_pose& base,
                          const animation_pose& additive,
                          const animation_pose& ref_pose,
                          float weight,
                          animation_pose& result);

struct blend_space_point
{
    std::vector<float> parameters;     // The parameter values for this point
    asset_handle<animation_clip> clip; // The animation clip associated with this point
};

class blend_space_def
{
public:
    using parameter_t = float;
    using parameters_t = std::vector<parameter_t>;

    // Add an animation clip to the blend space at specified parameter values
    void add_clip(const parameters_t& params, const asset_handle<animation_clip>& clip);

    // Compute the blending weights for the current parameters
    void compute_blend(const parameters_t& current_params,
                       std::vector<std::pair<asset_handle<animation_clip>, float>>& out_clips) const;

    // Get the number of parameters in the blend space
    auto get_parameter_count() const -> size_t;

private:
    void compute_blend_1d(const parameters_t& current_params,
                          std::vector<std::pair<asset_handle<animation_clip>, float>>& out_clips) const;

    void compute_blend_2d(const parameters_t& current_params,
                          std::vector<std::pair<asset_handle<animation_clip>, float>>& out_clips) const;

    std::vector<blend_space_point> points_;
    size_t parameter_count_{0};
};

} // namespace unravel
