#pragma once

#include <engine/animation/animation.h>
#include <math/math.h>

#include <algorithm>

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

/**
 * @brief How sampled clip poses are carried onto the playing skeleton.
 */
enum class animation_pose_retargeting : uint8_t
{
    /// Local transforms are written as authored. Right for clips exported with the model's own
    /// skeleton (same root frame, units and proportions).
    copy,
    /// Every bone turns in the world by what its source bone turned from the source rest pose, bone
    /// lengths stay the target's and motion scales with hip height. Plays clips from other exports
    /// (another root frame or unit scale) and on characters of other proportions, as long as both
    /// skeletons rest in the same pose. Clips imported without a recorded rest pose fall back to copy.
    rest_aligned,
};

/**
 * @brief How a layer above the base layer combines with the layers below it.
 */
enum class animation_layer_blend_mode : uint8_t
{
    /// The layer's pose replaces the pose below it, per bone, by layer weight x mask weight.
    override_pose,
    /// The layer's change from its clips' first frame is added on top of the pose below it.
    additive,
};

/**
 * @brief Blended value of one animation curve, keyed by get_animation_curve_id.
 */
struct animation_curve_value
{
    uint32_t id{};
    float value{};
};

struct animation_pose
{
    struct node_desc
    {
        /// Target skeleton index: the armature index the node is written to.
        size_t index{};
    };

    struct node
    {
        node_desc desc{};
        math::transform transform{};
        /// Share of a blend that animates the bone, 0..1. The rest of the bone's value comes from
        /// below: the layers under this one, the rest pose for the base layer, no change in an
        /// additive layer. Sampled poses animate their bones fully.
        float coverage{1.0f};
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
        math::vec3 root_position_at_time{};
        /// Clip loop count at the last position sample; the difference to the
        /// current loop count tells how many wraps the delta must account for.
        uint64_t root_position_loop_count{};
        bool root_position_initialized{};

        math::quat root_rotation_at_time{math::identity<math::quat>()};
        uint64_t root_rotation_loop_count{};
        bool root_rotation_initialized{};

        /// Where on the clip a baseline not taken yet is measured from: the time and loop count
        /// the time step before the sample began at, so the step that starts the tracking moves
        /// the entity too.
        animation_clip::seconds_t baseline_time{};
        uint64_t baseline_loop_count{};
    };

    /// The node of bone `index`, or nullptr. Nodes are sorted by index.
    [[nodiscard]] auto find_node(size_t index) const -> const node*
    {
        const auto it = std::lower_bound(nodes.begin(),
                                         nodes.end(),
                                         index,
                                         [](const node& lhs, size_t bone)
                                         {
                                             return lhs.desc.index < bone;
                                         });
        return it != nodes.end() && it->desc.index == index ? &*it : nullptr;
    }

    /// Nodes sorted by desc.index, each bone at most once.
    std::vector<node> nodes;

    /// Curve values sorted by id. A curve a pose lacks counts as 0 when poses blend, so a curve
    /// fades in and out across a crossfade instead of jumping.
    std::vector<animation_curve_value> curves;

    root_motion_result motion_result;
    root_motion_state motion_state;

};


} // namespace unravel
