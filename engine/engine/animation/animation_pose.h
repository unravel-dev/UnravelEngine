#pragma once

#include <engine/animation/animation.h>
#include <math/math.h>

namespace unravel
{

/**
 * @brief Animation retargeting mode for bone matching.
 */
enum class animation_retargeting_mode
{
    name_based,   ///< Flexible, works with different skeletons (uses cached hash map). Component default.
    index_based,  ///< Fast, requires exact skeleton match.
};

struct animation_pose
{
    struct node_desc
    {
        size_t index{};
        std::string name;

    };

    struct node
    {
        node_desc desc{};
        math::transform transform{};
    };

    struct root_motion_result
    {
        int root_position_node_index{-1};
        std::string root_position_node_name;
        math::vec3 root_position_weights{1.0f, 1.0f, 1.0f};
        math::vec3 bone_position_weights{0.0f, 0.0f, 0.0f};

        int root_rotation_node_index{-1};
        std::string root_rotation_node_name;
        float root_rotation_weight{1.0f};
        float bone_rotation_weight{0.0f};

        math::transform root_transform_delta;
    };

    struct root_motion_state
    {
        math::vec3 root_position_at_time{};
        /// Clip loop count at the last position sample; the difference to the
        /// current loop count tells how many wraps the delta must account for.
        uint64_t root_position_loop_count{};
        bool root_position_initialized{};

        math::quat root_rotation_at_time{math::identity<math::quat>()};
        uint64_t root_rotation_loop_count{};
        bool root_rotation_initialized{};
    };

    std::vector<node> nodes;

    root_motion_result motion_result;
    root_motion_state motion_state;

};


} // namespace unravel
