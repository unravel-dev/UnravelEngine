#pragma once
#include <engine/engine_export.h>

#include <engine/animation/animation_humanoid.h>

#include <entt/core/hashed_string.hpp>
#include <hpp/string_view.hpp>
#include <math/curve.h>
#include <math/math.h>

#include <chrono>
#include <string>
#include <vector>

namespace unravel
{

/**
 * @brief Struct representing a node animation.
 *
 * This struct contains animation data for a specific node, including position, rotation, and scaling keys.
 */
struct animation_channel
{
    using seconds_t = std::chrono::duration<float>;

    /**
     * @brief Struct representing a keyframe for animation.
     *
     * @tparam T The type of value stored in the keyframe.
     */
    template<typename T>
    struct key
    {
        friend auto operator==(const key& lhs, const key& rhs) -> bool = default;
        auto operator=(const key& rhs) -> key& = default;
        /// The time of the keyframe.
        seconds_t time = seconds_t(0);

        /// The value of the keyframe.
        T value = {};
    };

    friend auto operator==(const animation_channel& lhs, const animation_channel& rhs) -> bool = default;


    auto get_position_keys_count() const -> size_t
    {
        return position_keys.size();
    }

    auto get_rotation_keys_count() const -> size_t
    {
        return rotation_keys.size();
    }

    auto get_scaling_keys_count() const -> size_t
    {
        return scaling_keys.size();
    }

    /// The name of the node affected by this animation. The node must exist and it must be unique.
    std::string node_name;

    size_t node_index{};

    /// The position keys of this animation channel. Positions are specified as 3D vector.
    std::vector<key<math::vec3>> position_keys;

    /// The rotation keys of this animation channel. Rotations are given as quaternions.
    std::vector<key<math::quat>> rotation_keys;

    /// The scaling keys of this animation channel. Scalings are specified as 3D vector.
    std::vector<key<math::vec3>> scaling_keys;

    /// Source skeleton the keys were authored against, for rest-aligned retargeting. Empty for clips
    /// imported before it was recorded (has_rest false): those play by copying local transforms.
    bool has_rest{};
    /// The node's local transform in the source file's rest (bind) pose.
    math::transform rest_local{};
    /// Index of the channel animating the nearest ancestor of the node, or -1.
    int parent_channel{-1};
    /// Transform from that ancestor (or the source scene root when there is none) to the node's
    /// parent: the nodes in between are not animated, so their rest transforms hold.
    math::transform parent_offset{};
};


struct root_motion_params
{

    auto get_apply_root_motion() const -> bool
    {
        return !keep_position_y || !keep_position_xz || !keep_rotation;
    }

    bool keep_position_y{};
    bool keep_position_xz{};
    bool keep_rotation{};

    bool keep_in_place{};

    std::string position_node_name;
    int position_node_index{-1};

    std::string rotation_node_name;
    int rotation_node_index{-1};
};

/**
 * @brief A named marker on a clip's timeline, reported to scripts when playback crosses it.
 *
 * Positions are normalized (0 = first key, 1 = last key) so a marker stays on the same frame when the
 * source is re-exported at a different length.
 */
struct animation_event
{
    friend auto operator==(const animation_event& lhs, const animation_event& rhs) -> bool = default;

    /// Name scripts dispatch on, for example "footstep".
    std::string name;
    /// Normalized position on the clip, 0..1.
    float position{};
    std::string string_parameter;
    float float_parameter{};
    int int_parameter{};
    /**
     * @brief Smallest effective weight at which the event fires.
     *
     * The effective weight is the clip's share of the final pose: layer weight x crossfade weight (and,
     * in a blend space, the space's own weight - only its highest weighted clip reports events). The
     * default of one half makes a crossfade report a marker from exactly one side instead of both.
     */
    float min_weight{0.5f};
};

/**
 * @brief A named float track on a clip's timeline, sampled and blended with the pose.
 *
 * Curve positions are normalized like event positions. Scripts read the blended value by name, a
 * controller float parameter of the same name follows it, and IK components can scale their weight
 * by it.
 */
struct animation_curve
{
    friend auto operator==(const animation_curve& lhs, const animation_curve& rhs) -> bool = default;

    std::string name;
    math::curve<float> curve;
};

/**
 * @brief A node name without its namespace: the part after the last ':'. Files of one rig family
 * prefix the same bones differently ("mixamorig:Hips", "mixamorig1:Hips").
 */
inline auto get_name_without_namespace(hpp::string_view name) -> hpp::string_view
{
    const size_t separator = name.rfind(':');
    return separator == hpp::string_view::npos ? name : name.substr(separator + 1);
}

/**
 * @brief Stable identifier of a curve name, used to blend and look up curve values without strings.
 */
inline auto get_animation_curve_id(hpp::string_view name) -> uint32_t
{
    return uint32_t(entt::hashed_string::value(name.data(), name.size()));
}

/**
 * @brief Struct representing an animation.
 *
 * This struct contains data for an entire animation, including the name, duration, and node animation channels.
 */
struct animation_clip
{
    using seconds_t = animation_channel::seconds_t;

    /// The name of the animation_clip. Usually empty if the modeling package supports only a single animation_clip channel.
    std::string name;

    /// Duration of the animation_clip in seconds.
    seconds_t duration = seconds_t(0);

    /// The node animation_clip channels. Each channel affects a single node.
    std::vector<animation_channel> channels;

    root_motion_params root_motion;

    /// Timeline markers, sorted by position.
    std::vector<animation_event> events;

    /// Float tracks blended with the pose.
    std::vector<animation_curve> curves;

    /// Humanoid roles set by hand for the skeleton the clip was made on (its model's import
    /// settings), over the ones found from its shape.
    std::vector<humanoid_bone_override> humanoid_overrides;
};

/**
 * @brief Fills the lazily computed parts of a clip's rest transforms.
 *
 * A math::transform computes its matrix (or its components) on first read, into its own storage.
 * One clip is read by every character playing it, from pool threads: resolving those caches once,
 * before the clip is shared, keeps the reads from writing concurrently.
 */
inline void resolve_rest_transforms(animation_clip& clip)
{
    for(const auto& channel : clip.channels)
    {
        for(const math::transform* transform : {&channel.rest_local, &channel.parent_offset})
        {
            (void)transform->get_matrix();
            (void)transform->get_position();
        }
    }
}

} // namespace unravel
