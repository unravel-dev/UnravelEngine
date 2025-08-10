#pragma once

#include <engine/animation/animation.h>
#include <math/math.h>

namespace unravel
{

struct animation_pose
{
    struct node_desc
    {
        size_t index{};

    };

    struct node
    {
        node_desc desc{};
        math::transform transform{};
    };

    struct root_motion_result
    {
        int root_position_node_index{-1};
        math::vec3 root_position_weights{1.0f, 1.0f, 1.0f};
        math::vec3 bone_position_weights{0.0f, 0.0f, 0.0f};

        int root_rotation_node_index{-1};
        float root_rotation_weight{1.0f};
        float bone_rotation_weight{0.0f};

        math::transform root_transform_delta;
    };

    struct root_motion_state
    {
        math::vec3 root_position_at_time;
        animation_channel::seconds_t root_position_time{};

        math::quat root_rotation_at_time;
        animation_channel::seconds_t root_rotation_time{};
    };

    std::vector<node> nodes;

    root_motion_result motion_result;
    root_motion_state motion_state;

};


} // namespace unravel
