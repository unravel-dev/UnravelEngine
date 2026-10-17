#include "animation_player.h"
#include <base/hash.hpp>
#include <hpp/utility/overload.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace unravel
{

namespace
{
/// Horizontal start-to-end displacement below which a root channel counts as
/// non-traveling for keep_in_place (idles, dances): its full animation is kept
/// instead of projecting out the travel direction.
constexpr float IN_PLACE_MIN_TRAVEL = 1e-2f;

/// Clip bindings kept per player before the cache is dropped and rebuilt. A character plays a
/// few dozen clips at most; the bound only matters for tools that cycle through whole libraries.
constexpr size_t MAX_CACHED_BINDINGS = 128;

/// Rest-pose fits: point sets spread less than this (meters) have no direction, and sets whose
/// points stay within this fraction of their length from one line are taken as that line.
constexpr float MIN_FIT_SPREAD = 1e-5f;
constexpr float MIN_FIT_PLANARITY = 1e-3f;

/**
 * @brief Interpolates between keyframes to find the appropriate value at the current time.
 * @tparam T The type of value being interpolated (e.g., vec3 or quat).
 * @param keys The list of keyframes.
 * @param time The current animation time.
 * @return The interpolated value.
 */
template<typename T>
auto interpolate(const std::vector<animation_channel::key<T>>& keys, animation_player::seconds_t time) -> T
{
    if(keys.empty())
    {
        return {}; // Return default value if there are no keys
    }

    // Do binary search for keyframe
    int high = (int)keys.size(), low = -1;
    while(high - low > 1)
    {
        int probe = (high + low) / 2;
        if(keys[probe].time < time)
        {
            low = probe;
        }
        else
        {
            high = probe;
        }
    }

    if(low == -1)
    {
        // Before first key, return first key
        return keys.front().value;
    }

    if(high == (int)keys.size())
    {
        // Beyond last key, return last key
        return keys.back().value;
    }

    const auto& key1 = keys[low];
    const auto& key2 = keys[low + 1];

    // Importers can emit keys with identical timestamps - avoid dividing by zero.
    const float denom = key2.time.count() - key1.time.count();
    if(denom <= std::numeric_limits<float>::epsilon())
    {
        return key2.value;
    }

    // Compute the interpolation factor (0.0 to 1.0)
    float factor = (time.count() - key1.time.count()) / denom;
    factor = math::clamp(factor, 0.0f, 1.0f);

    // Perform the interpolation
    if constexpr(std::is_same_v<T, math::vec3>)
    {
        return math::lerp(key1.value, key2.value, factor);
    }
    else if constexpr(std::is_same_v<T, math::quat>)
    {
        return math::slerp(key1.value, key2.value, factor);
    }

    return {};
}

/**
 * @brief Whether a marker at normalized `position` lies in the cycle window.
 *
 * The window runs over cycle positions (loop count + progress), so a marker is crossed once per
 * cycle at `k + position`. Only the first crossing is reported, so a long hitch reports a marker
 * once rather than once per skipped loop.
 */
auto is_marker_in_window(float position, double begin, double end, bool inclusive) -> bool
{
    if(end < begin)
    {
        return false;
    }
    const double marker = double(math::clamp(position, 0.0f, 1.0f));
    // Cycles count from 0: a last-frame marker (1.0) belongs to the end of the first cycle, never to
    // a cycle before playback began - which would put it on the starting instant.
    double candidate = std::max(std::ceil(begin - marker), 0.0) + marker;
    if(candidate < begin || (!inclusive && candidate <= begin))
    {
        candidate += 1.0;
    }
    return candidate <= end;
}

auto is_root_channel(const animation_channel& channel, const std::string& node_name, int node_index) -> bool
{
    if(!node_name.empty())
    {
        return channel.node_name == node_name;
    }
    return node_index >= 0 && int(channel.node_index) == node_index;
}

auto is_same_motion(const animation_state& state,
                    const asset_handle<animation_clip>& clip,
                    const std::shared_ptr<blend_space_def>& blend_space) -> bool
{
    if(blend_space)
    {
        return state.blend_space == blend_space;
    }
    return !state.blend_space && clip && state.clip == clip;
}

/**
 * @brief Samples one channel's local transform; a track without keys takes its part of `fallback`.
 * The default fallback is no change: a value-initialized quat is all zeros (NaN after normalize) and
 * a zero scale collapses the bone. Rest-aligned sampling passes the channel's rest pose, which is
 * what a bone without keys holds.
 */
auto sample_channel(const animation_channel& channel,
                    animation_player::seconds_t time,
                    const math::transform& fallback = math::transform{}) -> math::transform
{
    math::transform local;
    local.set_position(channel.position_keys.empty() ? fallback.get_position() : interpolate(channel.position_keys, time));
    local.set_rotation(channel.rotation_keys.empty() ? fallback.get_rotation() : interpolate(channel.rotation_keys, time));
    local.set_scale(channel.scaling_keys.empty() ? fallback.get_scale() : interpolate(channel.scaling_keys, time));
    return local;
}

/// Minimum rest height (scene units) a node needs for its height to scale retargeted motion.
constexpr float MIN_RETARGET_HEIGHT = 1e-2f;

/// Below this length a rotation's turn about the up axis is undefined (it is upside down about a
/// horizontal axis) and counts as no turn.
constexpr float MIN_HEADING_LENGTH = 1e-6f;

/**
 * @brief Rest transform of the frame a channel's keys live in - its parent's - in the clip root.
 *
 * The clip root frame is upright (+Y up); the parent frame of a root-motion node need not be: an
 * FBX armature commonly stands Z-up below a node that turns it upright.
 */
auto get_parent_rest(const animation_clip& clip, size_t channel_index) -> math::transform
{
    const auto& channels = clip.channels;
    math::transform parent = channels[channel_index].parent_offset;
    // Channels are sorted parents first, so every ancestor has a lower index.
    for(size_t child = channel_index;
        channels[child].parent_channel >= 0 && size_t(channels[child].parent_channel) < child;)
    {
        child = size_t(channels[child].parent_channel);
        parent = channels[child].parent_offset * channels[child].rest_local * parent;
    }
    return parent;
}

/// A displacement carried into the frame's parent: rotated and scaled, not translated. Offsets are
/// lengths in their frame - an FBX armature in centimeters stands under a node scaled by 100.
auto transform_offset(const math::transform& frame, const math::vec3& offset) -> math::vec3
{
    return frame.transform_coord(offset) - frame.get_position();
}

/// The inverse of transform_offset: a displacement in the frame's parent carried into the frame.
auto inverse_transform_offset(const math::transform& frame, const math::vec3& offset) -> math::vec3
{
    return frame.inverse_transform_coord(offset) - frame.inverse_transform_coord(math::vec3(0.0f));
}

/// Scale ratios this close to each other stretch every axis alike.
constexpr float UNIFORM_SCALE_RATIO_EPSILON = 1e-5f;

/**
 * @brief A bone's per-axis scale change carried onto a bone whose axes are turned by
 * `source_to_target` (source local to target local): each target axis stretches as the physical
 * direction it points along does in the source. Bones of different conventions (one along +Y,
 * one along +X) stretch along the same direction.
 */
auto carry_scale_ratio(const math::vec3& ratio, const math::quat& source_to_target) -> math::vec3
{
    if(std::fabs(ratio.x - ratio.y) <= UNIFORM_SCALE_RATIO_EPSILON && std::fabs(ratio.x - ratio.z) <= UNIFORM_SCALE_RATIO_EPSILON)
    {
        return ratio;
    }
    const math::mat3 turn = glm::mat3_cast(source_to_target);
    math::vec3 carried;
    for(int axis = 0; axis < 3; ++axis)
    {
        // The target axis in source coordinates: row `axis` of the turn.
        const math::vec3 direction(turn[0][axis], turn[1][axis], turn[2][axis]);
        carried[axis] = math::length(ratio * direction);
    }
    return carried;
}

/// Whether every channel recorded its source rest pose (clips imported with it).
auto has_rest_pose(const animation_clip& clip) -> bool
{
    return !clip.channels.empty() && std::all_of(clip.channels.begin(),
                                                 clip.channels.end(),
                                                 [](const animation_channel& channel)
                                                 {
                                                     return channel.has_rest;
                                                 });
}

/// The parent channel of channel `c` when it comes earlier (channels are sorted parents first), or -1.
auto get_parent_channel(const animation_clip& clip, size_t c) -> int
{
    const int parent = clip.channels[c].parent_channel;
    return parent >= 0 && size_t(parent) < c ? parent : -1;
}

/// Distances and angles below these count as a key sitting on the rest pose.
constexpr float REST_POSITION_EPSILON = 1e-5f;
constexpr float REST_ROTATION_EPSILON = 1e-6f;
constexpr float REST_SCALE_EPSILON = 1e-5f;

auto is_near(const math::vec3& lhs, const math::vec3& rhs, float epsilon) -> bool
{
    const math::vec3 difference = lhs - rhs;
    return math::dot(difference, difference) <= epsilon * epsilon;
}

/// Whether a channel keeps its rest scale for the whole clip (a track without keys does).
auto holds_rest_scale(const animation_channel& channel) -> bool
{
    return std::all_of(channel.scaling_keys.begin(),
                       channel.scaling_keys.end(),
                       [&](const auto& key)
                       {
                           return is_near(key.value, channel.rest_local.get_scale(), REST_SCALE_EPSILON);
                       });
}

/// Whether a channel holds its rest pose for the whole clip.
auto holds_rest_pose(const animation_channel& channel) -> bool
{
    const auto& rest = channel.rest_local;
    return holds_rest_scale(channel) &&
           std::all_of(channel.position_keys.begin(),
                       channel.position_keys.end(),
                       [&](const auto& key)
                       {
                           return is_near(key.value, rest.get_position(), REST_POSITION_EPSILON);
                       }) &&
           std::all_of(channel.rotation_keys.begin(),
                       channel.rotation_keys.end(),
                       [&](const auto& key)
                       {
                           return std::fabs(math::dot(key.value, rest.get_rotation())) >= 1.0f - REST_ROTATION_EPSILON;
                       });
}

/// Identity of a clip's source skeleton: its channels' names, parents and rest poses.
auto hash_source_skeleton(const animation_clip& clip) -> size_t
{
    size_t seed = clip.channels.size();
    const auto combine_vec3 = [&](const math::vec3& value)
    {
        utils::hash_combine(seed, value.x);
        utils::hash_combine(seed, value.y);
        utils::hash_combine(seed, value.z);
    };
    for(const auto& channel : clip.channels)
    {
        utils::hash_combine(seed, channel.node_name);
        utils::hash_combine(seed, channel.parent_channel);
        for(const auto* transform : {&channel.rest_local, &channel.parent_offset})
        {
            combine_vec3(transform->get_position());
            combine_vec3(transform->get_scale());
            const math::quat rotation = transform->get_rotation();
            utils::hash_combine(seed, rotation.x);
            utils::hash_combine(seed, rotation.y);
            utils::hash_combine(seed, rotation.z);
            utils::hash_combine(seed, rotation.w);
        }
    }
    return seed;
}

/// Blends source poses channel by channel with the incremental weights blend_poses() uses, so the
/// blend matches blending the poses after a linear retarget.
void blend_source_poses(const std::vector<std::vector<math::transform>>& poses,
                        const std::vector<float>& weights,
                        std::vector<math::transform>& result)
{
    result = poses.front();
    float total_weight = weights.front();
    for(size_t p = 1; p < poses.size(); ++p)
    {
        const float denom = total_weight + weights[p];
        const float factor = denom > 0.0f ? weights[p] / denom : 0.0f;
        for(size_t c = 0; c < result.size(); ++c)
        {
            result[c] = blend(result[c], poses[p][c], factor);
        }
        total_weight += weights[p];
    }
}

/// Resets the root motion a sample reports before it is taken.
void clear_root_motion(animation_pose& pose)
{
    pose.motion_result.root_position_node_index = -1;
    pose.motion_result.root_rotation_node_index = -1;
    pose.motion_result.root_transform_delta = {};
}

/// The frame a channel's local transform lives in, from its parent's global in `globals`.
auto get_parent_global(const animation_clip& clip, const std::vector<math::transform>& globals, size_t c) -> math::transform
{
    const int parent = get_parent_channel(clip, c);
    const auto& offset = clip.channels[c].parent_offset;
    return parent >= 0 ? globals[size_t(parent)] * offset : offset;
}

/**
 * @brief Whether a rest-aligned retarget turns and scales every channel's local transform by
 * constants only. A bone's retargeted rotation depends on its source global relative to the source
 * channel of its nearest mapped target ancestor; when every channel between the two holds its rest
 * pose (all the way up when there is no such ancestor) and no scale animates, that relation is its
 * own local transform between two constant frames. Slerp and lerp keep constant frames, so clips
 * of one skeleton then blend the same before the retarget as after it.
 */
auto is_linear_retarget(const animation_clip& clip,
                        const std::vector<int>& channel_targets,
                        const std::vector<int>& target_channels,
                        const animation_target_skeleton& target) -> bool
{
    if(!std::all_of(clip.channels.begin(), clip.channels.end(), holds_rest_scale))
    {
        return false;
    }
    for(size_t c = 0; c < clip.channels.size(); ++c)
    {
        const int t = channel_targets[c];
        if(t < 0)
        {
            continue;
        }
        int ancestor_channel = -1;
        for(int bone = t, parent = target.parents[size_t(t)]; parent >= 0 && parent < bone;
            bone = parent, parent = target.parents[size_t(parent)])
        {
            if(size_t(parent) < target_channels.size() && target_channels[size_t(parent)] >= 0)
            {
                ancestor_channel = target_channels[size_t(parent)];
                break;
            }
        }
        int between = get_parent_channel(clip, c);
        for(; between >= 0 && between != ancestor_channel; between = get_parent_channel(clip, size_t(between)))
        {
            if(!holds_rest_pose(clip.channels[size_t(between)]))
            {
                return false;
            }
        }
        if(between != ancestor_channel)
        {
            return false;
        }
    }
    return true;
}

/// Rest transform of every channel in the clip root frame. Channels are sorted parents first.
auto compute_source_rest_globals(const animation_clip& clip) -> std::vector<math::transform>
{
    std::vector<math::transform> rest(clip.channels.size());
    for(size_t c = 0; c < clip.channels.size(); ++c)
    {
        rest[c] = get_parent_global(clip, rest, c) * clip.channels[c].rest_local;
    }
    return rest;
}

/// The humanoid roles of the clip's source skeleton, as channel indices.
auto find_channel_roles(const animation_clip& clip, const std::vector<math::transform>& source_rest) -> humanoid_map
{
    std::vector<std::string> names;
    std::vector<int> parents;
    std::vector<math::vec3> positions;
    for(size_t c = 0; c < clip.channels.size(); ++c)
    {
        names.push_back(clip.channels[c].node_name);
        parents.push_back(clip.channels[c].parent_channel);
        positions.push_back(source_rest[c].get_position());
    }
    return find_humanoid_bones(names, parents, positions);
}

/**
 * @brief The target bone of every channel by humanoid role, -1 where there is none.
 *
 * Lets a clip play on a humanoid whose bones are named differently: both skeletons are read for
 * their roles (hips, spine, arms, legs...) and each source bone lands on the target bone with its role.
 */
auto map_channels_by_role(const animation_clip& clip,
                          const std::vector<math::transform>& source_rest,
                          const animation_target_skeleton& target) -> std::vector<int>
{
    std::vector<int> targets(clip.channels.size(), -1);
    if(count_humanoid_bones(target.humanoid) == 0)
    {
        return targets;
    }
    const humanoid_map source = find_channel_roles(clip, source_rest);
    for(size_t role = 0; role < HUMANOID_BONE_COUNT; ++role)
    {
        if(source[role] >= 0 && target.humanoid[role] >= 0)
        {
            targets[size_t(source[role])] = target.humanoid[role];
        }
    }
    return targets;
}

/// The channel a clip's root motion names, or -1.
auto find_root_channel(const animation_clip& clip, const std::string& node_name, int node_index) -> int
{
    for(size_t c = 0; c < clip.channels.size(); ++c)
    {
        if(is_root_channel(clip.channels[c], node_name, node_index))
        {
            return int(c);
        }
    }
    return -1;
}

/// The part of an upright-frame rotation that turns about +Y (a swing-twist split): its heading.
auto get_heading(const math::quat& rotation) -> math::quat
{
    const float length = std::sqrt(rotation.w * rotation.w + rotation.y * rotation.y);
    if(length < MIN_HEADING_LENGTH)
    {
        return math::identity<math::quat>();
    }
    return math::quat(rotation.w / length, 0.0f, rotation.y / length, 0.0f);
}

/// A heading (a turn about +Y, see get_heading) applied `count` times.
auto repeat_heading(const math::quat& heading, uint64_t count) -> math::quat
{
    const float half_angle = std::atan2(heading.y, heading.w) * float(count);
    return math::quat(std::cos(half_angle), 0.0f, std::sin(half_angle), 0.0f);
}

/// Jacobi sweeps of the rotation fit's 4x4 eigen solve; it converges quadratically, in a handful.
constexpr int ROTATION_FIT_SWEEPS = 32;

/**
 * @brief Rotation that best maps one point set onto another (Horn 1987, closed form).
 *
 * The point sets are centred, so translation and uniform scale do not matter. The rotation is the
 * eigenvector of the largest eigenvalue of Horn's symmetric 4x4 matrix, found by a cyclic Jacobi
 * eigen solve.
 */
/**
 * @brief For points on one line (or fewer than three) the best rotation is free about that line:
 * the shortest arc between the two sets' directions, or nothing. Returns nothing for a point set
 * that spans a plane.
 */
auto fit_degenerate_rotation(const std::vector<math::vec3>& from,
                             const std::vector<math::vec3>& to,
                             size_t count,
                             const math::vec3& from_center,
                             const math::vec3& to_center) -> std::optional<math::quat>
{
    size_t farthest = 0;
    float farthest_length = 0.0f;
    for(size_t i = 0; i < count; ++i)
    {
        const float length = math::length(from[i] - from_center);
        if(length > farthest_length)
        {
            farthest_length = length;
            farthest = i;
        }
    }
    if(farthest_length <= MIN_FIT_SPREAD)
    {
        return math::identity<math::quat>();
    }
    const math::vec3 axis = (from[farthest] - from_center) / farthest_length;
    float off_axis = 0.0f;
    for(size_t i = 0; i < count; ++i)
    {
        const math::vec3 offset = from[i] - from_center;
        off_axis = std::max(off_axis, math::length(offset - axis * math::dot(offset, axis)));
    }
    if(count >= 3 && off_axis > farthest_length * MIN_FIT_PLANARITY)
    {
        return std::nullopt;
    }
    const math::vec3 target_axis = to[farthest] - to_center;
    if(math::length(target_axis) <= MIN_FIT_SPREAD)
    {
        return math::identity<math::quat>();
    }
    return math::normalize(math::from_to_rotation(axis, math::normalize(target_axis)));
}

auto fit_rotation(const std::vector<math::vec3>& from, const std::vector<math::vec3>& to) -> math::quat
{
    const size_t count = std::min(from.size(), to.size());
    if(count == 0)
    {
        return math::identity<math::quat>();
    }
    math::vec3 from_center(0.0f);
    math::vec3 to_center(0.0f);
    for(size_t i = 0; i < count; ++i)
    {
        from_center += from[i];
        to_center += to[i];
    }
    from_center /= float(count);
    to_center /= float(count);
    if(const auto degenerate = fit_degenerate_rotation(from, to, count, from_center, to_center))
    {
        return *degenerate;
    }
    // Cross-covariance S[a][b] = sum(from_a * to_b).
    float s[3][3] = {};
    for(size_t i = 0; i < count; ++i)
    {
        const math::vec3 a = from[i] - from_center;
        const math::vec3 b = to[i] - to_center;
        for(int r = 0; r < 3; ++r)
        {
            for(int c = 0; c < 3; ++c)
            {
                s[r][c] += a[r] * b[c];
            }
        }
    }
    const float n[4][4] = {
        {s[0][0] + s[1][1] + s[2][2], s[1][2] - s[2][1], s[2][0] - s[0][2], s[0][1] - s[1][0]},
        {s[1][2] - s[2][1], s[0][0] - s[1][1] - s[2][2], s[0][1] + s[1][0], s[2][0] + s[0][2]},
        {s[2][0] - s[0][2], s[0][1] + s[1][0], -s[0][0] + s[1][1] - s[2][2], s[1][2] + s[2][1]},
        {s[0][1] - s[1][0], s[2][0] + s[0][2], s[1][2] + s[2][1], -s[0][0] - s[1][1] + s[2][2]},
    };
    // Cyclic Jacobi eigen solve of the symmetric matrix: exact for any rotation, including the half
    // turn a power iteration started at identity could never reach.
    float a[4][4];
    float e[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    for(int r = 0; r < 4; ++r)
    {
        for(int c = 0; c < 4; ++c)
        {
            a[r][c] = n[r][c];
        }
    }
    for(int sweep = 0; sweep < ROTATION_FIT_SWEEPS; ++sweep)
    {
        float off = 0.0f;
        for(int r = 0; r < 4; ++r)
        {
            for(int c = r + 1; c < 4; ++c)
            {
                off += a[r][c] * a[r][c];
            }
        }
        if(off < 1e-20f)
        {
            break;
        }
        for(int p = 0; p < 3; ++p)
        {
            for(int q = p + 1; q < 4; ++q)
            {
                if(std::fabs(a[p][q]) < 1e-20f)
                {
                    continue;
                }
                const float theta = (a[q][q] - a[p][p]) / (2.0f * a[p][q]);
                const float t = (theta >= 0.0f ? 1.0f : -1.0f) / (std::fabs(theta) + std::sqrt(theta * theta + 1.0f));
                const float cs = 1.0f / std::sqrt(t * t + 1.0f);
                const float sn = t * cs;
                for(int k = 0; k < 4; ++k)
                {
                    const float akp = a[k][p];
                    const float akq = a[k][q];
                    a[k][p] = cs * akp - sn * akq;
                    a[k][q] = sn * akp + cs * akq;
                }
                for(int k = 0; k < 4; ++k)
                {
                    const float apk = a[p][k];
                    const float aqk = a[q][k];
                    a[p][k] = cs * apk - sn * aqk;
                    a[q][k] = sn * apk + cs * aqk;
                }
                for(int k = 0; k < 4; ++k)
                {
                    const float ekp = e[k][p];
                    const float ekq = e[k][q];
                    e[k][p] = cs * ekp - sn * ekq;
                    e[k][q] = sn * ekp + cs * ekq;
                }
            }
        }
    }
    int best = 0;
    for(int i = 1; i < 4; ++i)
    {
        if(a[i][i] > a[best][best])
        {
            best = i;
        }
    }
    const float v[4] = {e[0][best], e[1][best], e[2][best], e[3][best]};
    // Horn's eigenvector is (w, x, y, z).
    return math::normalize(math::quat(v[0], v[1], v[2], v[3]));
}
} // namespace

auto find_clip_humanoid_bones(const animation_clip& clip) -> humanoid_map
{
    return has_rest_pose(clip) ? find_channel_roles(clip, compute_source_rest_globals(clip)) : make_empty_humanoid_map();
}

void animation_target_skeleton::compute_humanoid_bones()
{
    std::vector<math::vec3> positions;
    positions.reserve(rest_global.size());
    for(const auto& rest : rest_global)
    {
        positions.push_back(rest.get_position());
    }
    humanoid = find_humanoid_bones(names, parents, positions);
}

void animation_target_skeleton::set_bones(std::vector<std::string> bone_names,
                                          std::vector<int> bone_parents,
                                          const animation_pose& bind_pose)
{
    names = std::move(bone_names);
    parents = std::move(bone_parents);
    bind_local.assign(parents.size(), math::transform{});
    for(const auto& node : bind_pose.nodes)
    {
        if(node.desc.index < bind_local.size())
        {
            bind_local[node.desc.index] = node.transform;
        }
    }
}

void animation_target_skeleton::compute_rest_globals()
{
    rest_global.resize(bind_local.size());
    for(size_t i = 0; i < bind_local.size(); ++i)
    {
        const int parent = i < parents.size() ? parents[i] : -1;
        rest_global[i] = parent >= 0 && size_t(parent) < i ? rest_global[size_t(parent)] * bind_local[i] : bind_local[i];
    }
}

auto animation_player::get_layer(size_t index) -> animation_layer&
{
    if(index >= layers_.size())
    {
        layers_.resize(index + 1);
    }
    return layers_[index];
}

void animation_player::clear_motion(animation_layer& layer)
{
    layer.current_state = {};
    layer.target_state = {};
    layer.blend_pose = {};
    layer.blend_pose_valid = false;
    layer.blending_state = {};
    layer.motion_weight = 1.0f;
    layer.motion_weight_rate = 0.0f;
    layer.stopping = false;
}

void animation_player::clear(size_t layer_idx)
{
    clear_motion(get_layer(layer_idx));
}

void animation_player::blend_to(size_t layer_idx,
                                const asset_handle<animation_clip>& clip,
                                seconds_t duration,
                                bool loop,
                                bool phase_sync,
                                const blend_easing_t& easing)
{
    animation_play_params params;
    params.duration = duration;
    params.loop = loop;
    params.phase_sync = phase_sync;
    params.easing = easing;
    play_clip(layer_idx, clip, params);
}

void animation_player::play_clip(size_t layer_idx,
                                 const asset_handle<animation_clip>& clip,
                                 const animation_play_params& params)
{
    if(!clip)
    {
        // A null clip means "stop animating this layer" - clear the in-flight
        // crossfade target as well, not just the current state.
        stop_motion(layer_idx, params.duration);
        return;
    }

    animation_state state;
    state.clip = clip;
    state.loop = params.loop;
    state.speed = params.speed;
    start_motion(layer_idx, std::move(state), params);
}

void animation_player::play_blend_space(size_t layer_idx,
                                        const std::shared_ptr<blend_space_def>& blend_space,
                                        const animation_play_params& params)
{
    if(!blend_space)
    {
        stop_motion(layer_idx, params.duration);
        return;
    }

    animation_state state;
    state.blend_space = blend_space;
    state.loop = params.loop;
    state.speed = params.speed;
    start_motion(layer_idx, std::move(state), params);
}

void animation_player::stop_motion(size_t layer_idx, seconds_t duration)
{
    auto& layer = get_layer(layer_idx);
    if(layer_idx == 0 || !(duration.count() > 0.0f) || !std::isfinite(duration.count()) || !layer.has_motion() ||
       layer.motion_weight <= 0.0f)
    {
        clear_motion(layer);
        return;
    }
    // Upper layers fade their whole contribution out, then clear.
    layer.stopping = true;
    layer.motion_weight_rate = -1.0f / duration.count();
}

namespace
{
/// Whether the layer shows its crossfade's blend pose (a target that was never posed was never seen).
auto is_showing_blend(const animation_player::animation_layer& layer) -> bool
{
    return layer.target_state.is_valid() && layer.blend_pose_valid;
}

/// Whether the layer adds its change on top of the layers below (the base layer never does).
auto is_additive_layer(size_t layer_index, const animation_player::animation_layer& layer) -> bool
{
    return layer_index > 0 && layer.blend_mode == animation_layer_blend_mode::additive;
}

/// Makes `state` the layer's only motion, at `progress`, with no crossfade.
void switch_to(animation_player::animation_layer& layer, animation_state&& state, float progress)
{
    layer.current_state = {};
    layer.current_state.state = std::move(state);
    layer.current_state.state.set_progress(progress);
    layer.current_state.state.reset_event_window();
    layer.target_state = {};
    layer.blending_state = {};
    layer.blend_pose_valid = false;
}

/// A crossfade into a one-shot clip ends by the time the clip does at its playback rate; looping
/// clips and blend spaces take the requested length.
auto clamp_crossfade_duration(const animation_state& state, animation_player::seconds_t duration)
    -> animation_player::seconds_t
{
    if(state.blend_space || state.loop || state.speed <= 0.0f)
    {
        return duration;
    }
    const auto clip = state.clip.try_get();
    if(!clip)
    {
        return duration;
    }
    return std::min(duration, animation_player::seconds_t(clip->duration.count() / state.speed));
}

/// Freezes the pose the layer shows as its crossfade source. The frozen pose is static, so it moves
/// the entity no further: its share of the crossfade holds its feet where they are, and the motion
/// the entity takes is the target's share.
void freeze_shown_pose(animation_player::animation_layer& layer)
{
    animation_pose frozen = is_showing_blend(layer) ? std::move(layer.blend_pose) : std::move(layer.current_state.pose);
    layer.current_state = {};
    layer.current_state.pose = std::move(frozen);
    layer.current_state.pose.motion_state = {};
    layer.current_state.pose.motion_result.root_transform_delta = {};
}

/// Points tracking that has no baseline yet at where the time step began on the clip: the step
/// that starts a state, follows a seek or brings a culled character back moves the entity too.
void set_baseline_position(animation_pose::root_motion_state& motion_state,
                           animation_player::seconds_t time,
                           uint64_t loop_count)
{
    motion_state.baseline_time = time;
    motion_state.baseline_loop_count = loop_count;
}

/// Resets root-motion tracking of a layer state, so its next sample starts a new baseline instead
/// of measuring a delta across a jump in time.
void reset_root_motion_tracking(animation_player::animation_layer_state& layer_state)
{
    layer_state.pose.motion_state = {};
    for(auto& pose : layer_state.state.blend_poses)
    {
        pose.motion_state = {};
    }
    layer_state.state.point_motion_states.assign(layer_state.state.point_motion_states.size(), {});
}
} // namespace

void animation_player::start_motion(size_t layer_idx, animation_state&& new_state, const animation_play_params& params)
{
    auto& layer = get_layer(layer_idx);
    const auto& clip = new_state.clip;
    const auto& blend_space = new_state.blend_space;
    // A length that is not a number (bad script input) switches instead of starting a crossfade
    // that never ends.
    const seconds_t requested = std::isfinite(params.duration.count()) ? params.duration : seconds_t(0);
    const seconds_t duration = clamp_crossfade_duration(new_state, requested);

    if(layer.stopping)
    {
        // A new motion cancels a pending fade out and brings the layer back.
        layer.stopping = false;
        layer.motion_weight_rate = duration.count() > 0.0f ? 1.0f / duration.count() : 0.0f;
        if(layer.motion_weight_rate == 0.0f)
        {
            layer.motion_weight = 1.0f;
        }
    }

    if(!layer.has_motion())
    {
        // Nothing to crossfade from: the motion starts right away. Upper layers fade their whole
        // contribution in instead, so starting an action on a layer does not pop.
        switch_to(layer, std::move(new_state), params.start_progress);
        if(layer_idx > 0 && duration.count() > 0.0f)
        {
            layer.motion_weight = 0.0f;
            layer.motion_weight_rate = 1.0f / duration.count();
        }
        else
        {
            layer.motion_weight = 1.0f;
            layer.motion_weight_rate = 0.0f;
        }
        return;
    }

    if(!params.restart)
    {
        // Already blending TO this motion - nothing to do (but keep the flags fresh).
        if(layer.target_state.is_valid() && is_same_motion(layer.target_state.state, clip, blend_space))
        {
            layer.target_state.state.loop = params.loop;
            layer.target_state.state.speed = params.speed;
            return;
        }

        // Caller wants to go back to the motion we're currently blending AWAY from.
        if(is_same_motion(layer.current_state.state, clip, blend_space))
        {
            if(!layer.target_state.is_valid())
            {
                // Already fully on this motion.
                layer.current_state.state.loop = params.loop;
                layer.current_state.state.speed = params.speed;
                return;
            }
            reverse_crossfade(layer, params, duration);
            return;
        }
    }

    // Fresh blend from what the layer shows to the requested motion. Phase sync follows the motion
    // the layer is heading for: an interrupted crossfade's target.
    const auto& sync_source = layer.target_state.is_valid() ? layer.target_state.state : layer.current_state.state;
    const float progress = params.phase_sync ? sync_source.get_progress() : params.start_progress;

    if(duration.count() <= 0.0f)
    {
        // A zero-length crossfade is a switch: nothing to blend, so the new motion becomes current
        // now and reports as playing right away.
        switch_to(layer, std::move(new_state), progress);
        return;
    }

    // A crossfade in flight freezes the pose on screen as the new source, so cutting into it does
    // not pop; so does a restart of the motion the layer plays (two live copies would fire its
    // events twice). Another motion's source keeps playing into the new one, and a crossfade
    // target that was never posed was never seen.
    if(is_showing_blend(layer) || (params.restart && is_same_motion(layer.current_state.state, clip, blend_space)))
    {
        freeze_shown_pose(layer);
    }

    layer.blend_pose_valid = false;
    layer.target_state = {};
    layer.target_state.state = std::move(new_state);
    layer.target_state.state.set_progress(progress);
    layer.target_state.state.reset_event_window();

    layer.blending_state.state = blend_over_time{duration};
    layer.blending_state.easing = params.easing;
    layer.blending_state.start_weight = 0.0f;
}

void animation_player::reverse_crossfade(animation_layer& layer, const animation_play_params& params, seconds_t duration)
{
    if(!layer.blend_pose_valid)
    {
        // The target was never shown: going back is staying on the current motion.
        layer.target_state = {};
        layer.blending_state = {};
        layer.current_state.state.loop = params.loop;
        layer.current_state.state.speed = params.speed;
        return;
    }
    // Swap current and target so the layer blends back toward the requested motion, from the
    // weight on screen: the remaining fade takes the requested duration with the requested easing.
    const float shown_target_weight = get_blend_weight(layer);
    std::swap(layer.current_state, layer.target_state);
    layer.target_state.state.loop = params.loop;
    layer.target_state.state.speed = params.speed;
    layer.blending_state.state = blend_over_time{duration};
    layer.blending_state.easing = params.easing;
    layer.blending_state.start_weight = 1.0f - shown_target_weight;
}

void animation_player::finish_crossfade(animation_layer& layer)
{
    layer.current_state = std::move(layer.target_state);
    layer.target_state = {};
    layer.blending_state = {};
    layer.blend_pose_valid = false;
}

void animation_player::set_blend_space(size_t layer_idx, const std::shared_ptr<blend_space_def>& blend_space, bool loop)
{
    auto& layer = get_layer(layer_idx);
    layer.current_state.state.loop = loop;
    if(layer.current_state.state.blend_space == blend_space)
    {
        return;
    }
    animation_state state;
    state.blend_space = blend_space;
    state.loop = loop;
    auto parameters = std::move(layer.current_state.parameters);
    switch_to(layer, std::move(state), 0.0f);
    layer.current_state.parameters = std::move(parameters);
}

void animation_player::set_blend_space_parameters(size_t layer_idx, const std::vector<float>& params)
{
    auto& layer = get_layer(layer_idx);
    layer.current_state.parameters = params;
}

void animation_player::set_state_parameters(size_t layer_idx, bool target, hpp::span<const float> params)
{
    auto& layer = get_layer(layer_idx);
    auto& state = target ? layer.target_state : layer.current_state;
    // Assigned in place: the state keeps its storage from frame to frame.
    state.parameters.assign(params.begin(), params.end());
}

void animation_player::set_state_speed(size_t layer_idx, bool target, float speed)
{
    auto& layer = get_layer(layer_idx);
    auto& state = target ? layer.target_state : layer.current_state;
    state.state.speed = std::max(0.0f, speed);
}

void animation_player::set_layer_weight(size_t layer_idx, float weight)
{
    if(std::isfinite(weight))
    {
        get_layer(layer_idx).weight = math::clamp(weight, 0.0f, 1.0f);
    }
}

auto animation_player::get_layer_weight(size_t layer_idx) const -> float
{
    // The base layer always plays at full weight.
    if(layer_idx == 0)
    {
        return 1.0f;
    }
    return layer_idx < layers_.size() ? layers_[layer_idx].weight : 0.0f;
}

void animation_player::set_layer_blend_mode(size_t layer_idx, animation_layer_blend_mode mode)
{
    auto& layer = get_layer(layer_idx);
    if(layer.blend_mode == mode)
    {
        return;
    }
    layer.blend_mode = mode;
    // Additive and override states sample different poses; the cached references stay valid.
}

auto animation_player::get_layer_blend_mode(size_t layer_idx) const -> animation_layer_blend_mode
{
    if(layer_idx >= layers_.size())
    {
        return animation_layer_blend_mode::override_pose;
    }
    return layers_[layer_idx].blend_mode;
}

void animation_player::set_layer_bone_mask(size_t layer_idx, std::vector<float> weights)
{
    get_layer(layer_idx).bone_mask = std::move(weights);
}

auto animation_player::get_layer_bone_mask(size_t layer_idx) const -> const std::vector<float>&
{
    static const std::vector<float> empty;
    if(layer_idx >= layers_.size())
    {
        return empty;
    }
    return layers_[layer_idx].bone_mask;
}

auto animation_player::play() -> bool
{
    if(playing_)
    {
        return false;
    }
    playing_ = true;
    paused_ = false;

    return true;
}

void animation_player::pause()
{
    paused_ = true;
}

void animation_player::resume()
{
    paused_ = false;
}

void animation_player::stop()
{
    playing_ = false;
    paused_ = false;
    fired_events_.clear();

    for(auto& layer : layers_)
    {
        for(auto* layer_state : {&layer.current_state, &layer.target_state})
        {
            auto& state = layer_state->state;
            state.elapsed = seconds_t(0);
            state.phase = 0.0f;
            state.loop_count = 0;
            state.begin_step();
            state.reset_event_window();
            // Otherwise the first sample of the next playback measures a delta against the stale
            // end-of-play position and teleports the entity.
            reset_root_motion_tracking(*layer_state);
        }
    }
}

void animation_player::update_motion_weight(animation_layer& layer, seconds_t delta_time)
{
    if(layer.motion_weight_rate == 0.0f)
    {
        return;
    }
    layer.motion_weight = math::clamp(layer.motion_weight + layer.motion_weight_rate * delta_time.count(), 0.0f, 1.0f);
    if(layer.motion_weight_rate > 0.0f && layer.motion_weight >= 1.0f)
    {
        layer.motion_weight_rate = 0.0f;
    }
    else if(layer.motion_weight_rate < 0.0f && layer.motion_weight <= 0.0f)
    {
        if(layer.stopping)
        {
            clear_motion(layer);
            return;
        }
        layer.motion_weight_rate = 0.0f;
    }
}

auto animation_player::update_time(seconds_t delta_time, bool force) -> bool
{
    fired_events_.clear();

    const bool refresh = pose_refresh_requested_;
    pose_refresh_requested_ = false;
    // `force` steps time even when stopped or paused (editor frame stepping).
    // A requested refresh also gets through, but with zero delta: it re-samples
    // poses at the current position without advancing it (editor seeking).
    if(!is_playing() && !force)
    {
        if(!refresh)
        {
            return false;
        }
        delta_time = seconds_t(0);
    }

    bool any_valid = false;
    for(auto& layer : layers_)
    {
        if(layer.has_motion())
        {
            any_valid = true;
            break;
        }
    }

    if(!any_valid)
    {
        return false;
    }

    for(size_t layer_index = 0; layer_index < layers_.size(); ++layer_index)
    {
        auto& layer = layers_[layer_index];
        update_motion_weight(layer, delta_time);

        update_state(delta_time, layer.current_state);

        update_state(delta_time, layer.target_state);

        // update overtime parameters
        hpp::visit(hpp::overload(
                       [&](blend_over_time& state)
                       {
                           state.elapsed += delta_time;
                       },
                       [](auto& state)
                       {

                       }),
                   layer.blending_state.state);
        // A crossfade ends in the time step, not when poses are sampled: a character whose poses
        // are skipped (renderer-based culling) still leaves its transitions.
        if(layer.target_state.is_valid() && get_blend_progress(layer) >= 1.0f)
        {
            finish_crossfade(layer);
        }

        const float layer_weight = (layer_index == 0 ? 1.0f : layer.weight) * layer.motion_weight;
        if(layer.target_state.is_valid())
        {
            const float target_weight = get_blend_weight(layer);
            collect_events(layer_index, layer.current_state.state, layer_weight * (1.0f - target_weight));
            collect_events(layer_index, layer.target_state.state, layer_weight * target_weight);
        }
        else
        {
            collect_events(layer_index, layer.current_state.state, layer_weight);
        }
    }

    return true;
}

void animation_player::collect_events(size_t layer_index, const animation_state& state, float weight)
{
    if(!state.event_window_valid || weight <= 0.0f)
    {
        return;
    }

    // A blend space reports the markers of its highest weighted clip only: every clip shares one
    // phase, so reporting them all would repeat each footstep once per blended clip.
    const asset_handle<animation_clip>* source = nullptr;
    const animation_clip* clip = nullptr;
    if(state.blend_space)
    {
        float best_weight = -1.0f;
        for(size_t i = 0; i < state.blend_clips.size() && i < state.blend_loaded_clips.size(); ++i)
        {
            if(state.blend_clips[i].weight > best_weight)
            {
                best_weight = state.blend_clips[i].weight;
                source = &state.blend_clips[i].clip;
                clip = state.blend_loaded_clips[i].get();
            }
        }
    }
    else if(state.clip)
    {
        source = &state.clip;
        clip = state.loaded_clip.get();
    }
    if(source == nullptr || clip == nullptr || clip->events.empty())
    {
        return;
    }

    for(const auto& event : clip->events)
    {
        if(weight + 1e-6f < event.min_weight)
        {
            continue;
        }
        if(is_marker_in_window(event.position,
                               state.event_window_begin,
                               state.event_window_end,
                               state.event_window_inclusive))
        {
            auto& hit = fired_events_.emplace_back();
            hit.event = event;
            hit.clip = *source;
            hit.layer = layer_index;
            hit.weight = weight;
        }
    }
}

void animation_player::update_poses(const animation_pose& ref_pose,
                                    animation_retargeting_mode retargeting_mode,
                                    bool extract_root_motion,
                                    const update_callback_t& set_transform_callback)
{
    // Without a skeleton to resolve names against, both modes sample onto the clip's own indices.
    static const animation_skeleton_binding identity_binding{};
    update_poses(ref_pose, identity_binding, extract_root_motion, set_transform_callback);
}

void animation_player::update_poses(const animation_pose& ref_pose,
                                    const animation_skeleton_binding& binding,
                                    bool extract_root_motion,
                                    const update_callback_t& set_transform_callback)
{
    if(layers_.empty())
    {
        return;
    }

    for(size_t layer_index = 0; layer_index < layers_.size(); ++layer_index)
    {
        auto& layer = layers_[layer_index];
        const bool additive = is_additive_layer(layer_index, layer);
        // Root motion belongs to the base layer; upper layers never move the entity.
        const bool layer_extracts = extract_root_motion && layer_index == 0;

        update_pose(layer.current_state, binding, layer_extracts, additive);

        // The crossfade shows once its target has a pose (its clips loaded).
        if(update_pose(layer.target_state, binding, layer_extracts, additive) && !layer.target_state.pose.nodes.empty())
        {
            blend_poses(layer.current_state.pose, layer.target_state.pose, get_blend_weight(layer), layer.blend_pose);
            layer.blend_pose_valid = true;
        }
    }

    const animation_pose* final_pose = nullptr;
    if(layers_.size() == 1)
    {
        final_pose = layers_.front().get_final_pose();
    }
    else
    {
        final_pose = &compose_layers(ref_pose);
    }

    final_curves_ = final_pose->curves;
    write_final_pose(*final_pose, ref_pose, set_transform_callback);
    reset_unwritten_bones(*final_pose, ref_pose, set_transform_callback);
}

void animation_player::write_final_pose(const animation_pose& final_pose,
                                        const animation_pose& ref_pose,
                                        const update_callback_t& set_transform_callback)
{
    for(const auto& node : final_pose.nodes)
    {
        // A bone only part of a blend animates is completed by its rest pose.
        const math::transform transform =
            node.coverage >= 1.0f ? node.transform : resolve_coverage(node, ref_pose.find_node(node.desc.index));
        set_transform_callback(node.desc, transform, final_pose.motion_result);
    }
}

auto animation_player::is_layer_pose_pending(const animation_layer& layer) -> bool
{
    const auto& state = layer.current_state.state;
    if(state.clip)
    {
        return !state.loaded_clip;
    }
    if(state.blend_space)
    {
        return std::none_of(state.blend_loaded_clips.begin(),
                            state.blend_loaded_clips.end(),
                            [](const std::shared_ptr<animation_clip>& clip)
                            {
                                return clip != nullptr;
                            });
    }
    return false;
}

void animation_player::reset_unwritten_bones(const animation_pose& final_pose,
                                             const animation_pose& ref_pose,
                                             const update_callback_t& set_transform_callback)
{
    // While a layer's clips load nothing new is posed: the bones keep what they show.
    if(std::any_of(layers_.begin(), layers_.end(), &animation_player::is_layer_pose_pending))
    {
        return;
    }
    size_t hash = final_pose.nodes.size();
    for(const auto& node : final_pose.nodes)
    {
        utils::hash_combine(hash, node.desc.index);
    }
    if(hash == written_bones_hash_ && written_bones_.size() == final_pose.nodes.size())
    {
        return;
    }
    // A bone no motion writes any more stands at rest, as a crossfade or a fading layer leaves it.
    // A rest write moves no entity: it carries no root motion.
    static const animation_pose::root_motion_result no_root_motion{};
    for(const size_t bone : written_bones_)
    {
        if(final_pose.find_node(bone) != nullptr)
        {
            continue;
        }
        if(const auto* rest = ref_pose.find_node(bone))
        {
            set_transform_callback(rest->desc, rest->transform, no_root_motion);
        }
    }
    written_bones_.clear();
    for(const auto& node : final_pose.nodes)
    {
        written_bones_.push_back(node.desc.index);
    }
    written_bones_hash_ = hash;
}

auto animation_player::compose_layers(const animation_pose& ref_pose) -> const animation_pose&
{
    const auto& base_layer = layers_.front();
    // The base pose is used in place until a layer blends onto it: on most frames no upper layer
    // contributes, and a copy of the whole pose would be thrown away.
    const animation_pose* composed = base_layer.has_motion() ? base_layer.get_final_pose() : &ref_pose;

    for(size_t layer_index = 1; layer_index < layers_.size(); ++layer_index)
    {
        const auto& layer = layers_[layer_index];
        const float weight = layer.weight * layer.motion_weight;
        if(weight <= 0.0f || !layer.has_motion())
        {
            continue;
        }
        const animation_pose* layer_pose = layer.get_final_pose();
        if(layer_pose->nodes.empty() && layer_pose->curves.empty())
        {
            continue;
        }
        blend_layer(*composed, *layer_pose, layer.blend_mode, weight, layer.bone_mask, ref_pose, compose_scratch_);
        std::swap(composed_pose_, compose_scratch_);
        composed = &composed_pose_;
    }
    return *composed;
}

auto animation_player::update_pose(animation_layer_state& layer,
                                   const animation_skeleton_binding& binding,
                                   bool extract_root_motion,
                                   bool additive) -> bool
{
    auto& state = layer.state;
    auto& pose = layer.pose;

    if(state.blend_space)
    {
        // Weights are refreshed by every time step; compute them here only when the state was
        // never stepped (a pose refresh right after the state started).
        if(state.blend_clips.empty())
        {
            state.blend_space->compute_blend(layer.parameters, state.blend_clips);
        }
        if(state.blend_loaded_clips.size() != state.blend_clips.size())
        {
            state.blend_loaded_clips.clear();
            for(const auto& clip_weight : state.blend_clips)
            {
                state.blend_loaded_clips.push_back(clip_weight.clip.try_get());
            }
        }
        if(std::none_of(state.blend_loaded_clips.begin(),
                        state.blend_loaded_clips.end(),
                        [](const std::shared_ptr<animation_clip>& clip)
                        {
                            return clip != nullptr;
                        }))
        {
            // Nothing loaded yet: the last pose stays, and it moves the entity no further.
            clear_root_motion(pose);
            return true;
        }
        // Each point keeps its own root-motion tracking; one that left the blend starts over when
        // it comes back, so a delta never spans the frames it was out.
        state.point_motion_states.resize(state.blend_space->get_points().size());
        for(size_t point = 0; point < state.point_motion_states.size(); ++point)
        {
            const bool is_blended = std::any_of(state.blend_clips.begin(),
                                                state.blend_clips.end(),
                                                [&](const blend_space_weight& clip_weight)
                                                {
                                                    return clip_weight.point == point;
                                                });
            if(!is_blended)
            {
                state.point_motion_states[point] = {};
            }
        }
        // Every clip is sampled at the shared phase, so clips of different lengths stay in step.
        state.blend_poses.resize(state.blend_clips.size());
        state.blend_weights.resize(state.blend_clips.size());
        if(!additive && sample_blend_in_source_space(state, binding, extract_root_motion, pose))
        {
            return true;
        }
        for(size_t i = 0; i < state.blend_clips.size(); ++i)
        {
            const auto& clip_weight = state.blend_clips[i];
            const auto& clip = state.blend_loaded_clips[i];
            auto& point_pose = state.blend_poses[i];
            state.blend_weights[i] = clip_weight.weight;
            if(!clip)
            {
                // A point still loading takes no share, so the loaded ones carry the whole blend.
                state.blend_weights[i] = 0.0f;
                point_pose.nodes.clear();
                point_pose.curves.clear();
                clear_root_motion(point_pose);
                continue;
            }
            auto& motion_state = state.point_motion_states[clip_weight.point];
            point_pose.motion_state = motion_state;
            set_baseline_position(point_pose.motion_state, clip->duration * state.step_begin_phase, state.step_begin_loop_count);
            const seconds_t clip_time = clip->duration * state.phase;
            sample_animation(clip, clip_time, state.loop_count, binding, extract_root_motion, !additive, point_pose);
            motion_state = point_pose.motion_state;
            sample_curves(*clip, state.phase, point_pose);
            if(additive)
            {
                convert_to_additive(layer, clip, clip_weight.point, binding, point_pose);
            }
        }

        if(state.blend_poses.empty())
        {
            pose.nodes.clear();
            pose.curves.clear();
            return true;
        }

        // Multiway merge into a pose that does not alias any input - blending
        // in place would clear the accumulator before it is read.
        blend_poses(state.blend_poses, state.blend_weights, pose);
        return true;
    }

    if(state.clip)
    {
        // A state started after this frame's time step resolves its clip here, and the progress
        // it was started at.
        if(!state.loaded_clip)
        {
            state.loaded_clip = state.clip.try_get();
        }
        const auto& clip = state.loaded_clip;
        if(!clip)
        {
            // Still loading (or reloading): the last pose stays, and it moves the entity no further.
            clear_root_motion(pose);
            return true;
        }
        if(state.pending_progress)
        {
            state.set_progress(*state.pending_progress);
        }
        set_baseline_position(pose.motion_state, state.step_begin_elapsed, state.step_begin_loop_count);
        sample_animation(clip, state.elapsed, state.loop_count, binding, extract_root_motion, !additive, pose);
        sample_curves(*clip, state.get_progress(), pose);
        if(additive)
        {
            convert_to_additive(layer, clip, 0, binding, pose);
        }
        return true;
    }

    return false;
}

namespace
{
/// Whether `cached` refers to `clip` itself: compared by control block, so a newer clip that reuses
/// a freed one's address never matches, and no reference count is touched.
auto is_same_clip(const std::weak_ptr<const animation_clip>& cached, const std::shared_ptr<animation_clip>& clip) -> bool
{
    return !cached.owner_before(clip) && !clip.owner_before(cached) && !cached.expired();
}
} // namespace

void animation_player::convert_to_additive(animation_layer_state& layer_state,
                                           const std::shared_ptr<animation_clip>& clip,
                                           size_t reference_index,
                                           const animation_skeleton_binding& binding,
                                           animation_pose& pose)
{
    if(layer_state.reference_poses.size() <= reference_index)
    {
        layer_state.reference_poses.resize(reference_index + 1);
    }
    auto& reference = layer_state.reference_poses[reference_index];
    if(!is_same_clip(reference.clip, clip) || reference.binding_version != binding.version || reference.pose.nodes.empty())
    {
        const seconds_t first_key_time{0.0f};
        sample_animation(clip, first_key_time, 0, binding, false, false, reference.pose);
        reference.clip = clip;
        reference.binding_version = binding.version;
    }
    make_pose_additive(pose, reference.pose);
}

void animation_player::update_state(seconds_t delta_time, animation_layer_state& layer_state)
{
    auto& state = layer_state.state;
    if(!state.clip && !state.blend_space)
    {
        state.event_window_valid = false;
        return;
    }

    const seconds_t step = delta_time * std::max(0.0f, state.speed);

    if(state.blend_space)
    {
        // The weights decide how long one cycle takes, so they are computed with the time step.
        state.blend_space->compute_blend(layer_state.parameters, state.blend_clips);
        state.blend_loaded_clips.clear();
        float weighted_duration = 0.0f;
        float total_weight = 0.0f;
        for(const auto& clip_weight : state.blend_clips)
        {
            const auto& clip = state.blend_loaded_clips.emplace_back(clip_weight.clip.try_get());
            if(clip)
            {
                weighted_duration += clip->duration.count() * clip_weight.weight;
                total_weight += clip_weight.weight;
            }
        }
        state.blend_duration = seconds_t(total_weight > 0.0f ? weighted_duration / total_weight : 0.0f);
        state.begin_step();
        if(state.blend_duration.count() > 0.0f)
        {
            state.phase += step.count() / state.blend_duration.count();
            if(state.phase > 1.0f)
            {
                if(state.loop)
                {
                    const float wraps = std::floor(state.phase);
                    state.loop_count += uint64_t(wraps);
                    state.phase -= wraps;
                }
                else
                {
                    state.phase = 1.0f;
                }
            }
        }
    }
    else
    {
        state.loaded_clip = state.clip.try_get();
        if(!state.loaded_clip)
        {
            // Still loading: time starts once the clip is there.
            state.event_window_valid = false;
            return;
        }
        const auto duration = get_state_duration(state);
        if(state.pending_progress)
        {
            state.elapsed = duration * *state.pending_progress;
            state.pending_progress.reset();
        }
        state.begin_step();
        state.elapsed += step;
        if(duration > seconds_t(0) && state.elapsed > duration)
        {
            if(state.loop)
            {
                // Count every wrap so root motion stays exact even when several loops pass between
                // two pose samples; wraps and remainder come from one division, so the cycle count
                // and the time on the clip never disagree by a cycle.
                const double length = duration.count();
                double elapsed = state.elapsed.count();
                double wraps = std::floor(elapsed / length);
                elapsed -= wraps * length;
                if(elapsed < 0.0)
                {
                    elapsed += length;
                    wraps -= 1.0;
                }
                else if(elapsed >= length)
                {
                    elapsed -= length;
                    wraps += 1.0;
                }
                state.loop_count += uint64_t(std::max(wraps, 0.0));
                state.elapsed = seconds_t(float(elapsed));
            }
            else
            {
                state.elapsed = duration;
            }
        }
    }

    if(delta_time.count() > 0.0f)
    {
        state.event_window_begin = state.event_cursor;
        state.event_window_end = state.get_cycle_position();
        state.event_window_inclusive = state.event_cursor_inclusive;
        state.event_window_valid = true;
        state.event_cursor = state.event_window_end;
        state.event_cursor_inclusive = false;
    }
    else
    {
        state.event_window_valid = false;
    }
}

auto animation_player::get_state_duration(const animation_state& state) -> seconds_t
{
    if(state.blend_space)
    {
        return state.blend_duration;
    }
    if(state.clip)
    {
        const auto clip = state.loaded_clip ? state.loaded_clip : state.clip.try_get();
        return clip ? clip->duration : seconds_t(0);
    }
    return seconds_t(0);
}

auto animation_player::get_blend_progress(const animation_layer& layer) const -> float
{
    return hpp::visit(hpp::overload(
                          [](const hpp::monostate& state)
                          {
                              return 0.0f;
                          },
                          [](const auto& state)
                          {
                              return state.get_progress();
                          }),
                      layer.blending_state.state);
}

auto animation_player::get_blend_weight(const animation_layer& layer) const -> float
{
    if(!layer.target_state.is_valid())
    {
        return 0.0f;
    }
    return compute_blend_factor(layer, get_blend_progress(layer));
}

auto animation_player::compute_blend_factor(const animation_layer& layer, float normalized_blend_time) const -> float
{
    // Check if blending is complete
    if(normalized_blend_time >= 1.0f)
    {
        // Blending complete
        return 1.0f;
    }

    // Apply the easing function
    const float eased = layer.blending_state.easing ? layer.blending_state.easing(normalized_blend_time)
                                                    : normalized_blend_time;
    const float start_weight = layer.blending_state.start_weight;
    return math::clamp(start_weight + (1.0f - start_weight) * eased, 0.0f, 1.0f);
}

namespace
{
/**
 * @brief The target bone a root channel's motion is reported against: its own, or for a root the
 * target lacks (only rest-aligned bindings map by role) the bone of its first mapped descendant,
 * shallowest first. -1 without one.
 */
template<typename Binding>
auto find_root_motion_target(const animation_clip& clip, const Binding& binding, int root_channel) -> int
{
    if(root_channel < 0)
    {
        return -1;
    }
    if(binding.channel_targets[size_t(root_channel)] >= 0 || !binding.rest_aligned)
    {
        return binding.channel_targets[size_t(root_channel)];
    }
    // Channels are ordered parents first: the first mapped one below the root is the shallowest.
    for(size_t c = size_t(root_channel) + 1; c < clip.channels.size(); ++c)
    {
        int ancestor = get_parent_channel(clip, c);
        while(ancestor > root_channel)
        {
            ancestor = get_parent_channel(clip, size_t(ancestor));
        }
        if(ancestor == root_channel && binding.channel_targets[c] >= 0)
        {
            return binding.channel_targets[c];
        }
    }
    return -1;
}
} // namespace

namespace
{
/// The segments whose rest directions tell rest poses apart: the spine from the hips up, and each
/// limb bone to the next joint of its chain.
constexpr std::array<std::pair<humanoid_bone, humanoid_bone>, 9> REST_SEGMENTS{{
    {humanoid_bone::hips, humanoid_bone::upper_chest},
    {humanoid_bone::left_upper_arm, humanoid_bone::left_lower_arm},
    {humanoid_bone::left_lower_arm, humanoid_bone::left_hand},
    {humanoid_bone::right_upper_arm, humanoid_bone::right_lower_arm},
    {humanoid_bone::right_lower_arm, humanoid_bone::right_hand},
    {humanoid_bone::left_upper_leg, humanoid_bone::left_lower_leg},
    {humanoid_bone::left_lower_leg, humanoid_bone::left_foot},
    {humanoid_bone::right_upper_leg, humanoid_bone::right_lower_leg},
    {humanoid_bone::right_lower_leg, humanoid_bone::right_foot},
}};

/// Below this length a rest segment or a rotation axis has no direction.
constexpr float MIN_REST_DIRECTION_LENGTH = 1e-4f;
/// A recorded rest rotation this far (degrees) from the bone's bind rotation is another skeleton's.
constexpr float REST_MISMATCH_DEGREES = 1.0f;
/// So is a rest offset this far (a share of its length) from the bind offset.
constexpr float REST_MISMATCH_LENGTH_SHARE = 0.01f;

auto get_degrees_between(const math::vec3& lhs, const math::vec3& rhs) -> float
{
    const float cosine = math::clamp(math::dot(math::normalize(lhs), math::normalize(rhs)), -1.0f, 1.0f);
    return math::degrees(std::acos(cosine));
}

auto get_degrees_between(const math::quat& lhs, const math::quat& rhs) -> float
{
    const float cosine = math::clamp(std::fabs(math::dot(math::normalize(lhs), math::normalize(rhs))), 0.0f, 1.0f);
    return math::degrees(2.0f * std::acos(cosine));
}

/// How a channel the binding sent to `bone` matched it.
auto get_match_kind(const animation_channel& channel,
                    int bone,
                    const animation_skeleton_binding& binding,
                    const animation_target_skeleton& skeleton) -> animation_binding_report::match_kind
{
    using match_kind = animation_binding_report::match_kind;
    if(bone < 0)
    {
        return match_kind::none;
    }
    if(!binding.resolve)
    {
        return match_kind::index;
    }
    // The name lookup found another bone or none: the humanoid role decided.
    if(binding.resolve(channel) != bone)
    {
        return match_kind::role;
    }
    const bool is_named = size_t(bone) < skeleton.names.size();
    if(is_named && channel.node_name != skeleton.names[size_t(bone)] &&
       get_name_without_namespace(channel.node_name) == get_name_without_namespace(skeleton.names[size_t(bone)]))
    {
        return match_kind::name_without_namespace;
    }
    return match_kind::name;
}

/// The channel's recorded rest local transform is not the bone's bind transform: the clip was made
/// on another skeleton (or another export of this one).
auto is_rest_mismatched(const animation_channel& channel, const math::transform& bind_local) -> bool
{
    if(!channel.has_rest)
    {
        return false;
    }
    const math::vec3 rest_offset = channel.rest_local.get_position();
    const math::vec3 bind_offset = bind_local.get_position();
    const float length = std::max(math::length(bind_offset), MIN_REST_DIRECTION_LENGTH);
    return get_degrees_between(channel.rest_local.get_rotation(), bind_local.get_rotation()) > REST_MISMATCH_DEGREES ||
           math::length(rest_offset - bind_offset) > REST_MISMATCH_LENGTH_SHARE * length;
}
} // namespace

auto animation_player::describe_binding(const std::shared_ptr<animation_clip>& clip,
                                        const animation_skeleton_binding& binding,
                                        const animation_target_skeleton& skeleton) -> animation_binding_report
{
    animation_binding_report report;
    if(!clip)
    {
        return report;
    }
    animation_player probe;
    const clip_binding& entry = probe.find_binding(clip, binding);
    report.has_source_rest = has_rest_pose(*clip);
    report.is_rest_aligned = entry.rest_aligned;
    std::vector<bool> reached(skeleton.names.size(), false);
    for(size_t c = 0; c < clip->channels.size(); ++c)
    {
        const auto& channel = clip->channels[c];
        auto& out = report.channels.emplace_back();
        out.name = channel.node_name;
        out.bone = c < entry.channel_targets.size() ? entry.channel_targets[c] : -1;
        out.match = get_match_kind(channel, out.bone, binding, skeleton);
        if(out.bone < 0 || size_t(out.bone) >= reached.size())
        {
            continue;
        }
        reached[size_t(out.bone)] = true;
        if(size_t(out.bone) < skeleton.bind_local.size() && is_rest_mismatched(channel, skeleton.bind_local[size_t(out.bone)]))
        {
            ++report.rest_mismatches;
        }
    }
    for(size_t bone = 0; bone < reached.size(); ++bone)
    {
        if(!reached[bone])
        {
            report.unreached_bones.push_back(int(bone));
        }
    }
    report.root_position_channel = entry.root_position_channel;
    report.root_position_bone = entry.root_position_target;
    report.root_rotation_channel = entry.root_rotation_channel;
    report.root_rotation_bone = entry.root_rotation_target;
    if(entry.rest_aligned)
    {
        describe_rest_aligned(*clip, entry, skeleton, report);
    }
    return report;
}

void animation_player::describe_rest_aligned(const animation_clip& clip,
                                             const clip_binding& entry,
                                             const animation_target_skeleton& skeleton,
                                             animation_binding_report& report)
{
    report.height_ratio = entry.height_ratio;
    const math::quat frame = math::normalize(entry.frame_rotation);
    const math::vec3 axis(frame.x, frame.y, frame.z);
    report.frame_degrees = math::degrees(2.0f * std::atan2(math::length(axis), std::fabs(frame.w)));
    if(math::length(axis) > MIN_REST_DIRECTION_LENGTH)
    {
        report.frame_axis = math::normalize(frame.w < 0.0f ? -axis : axis);
    }
    const humanoid_map source_roles = find_channel_roles(clip, entry.source_rest_global);
    const auto source_position = [&](humanoid_bone role)
    {
        return entry.source_rest_global[size_t(source_roles[size_t(role)])].get_position();
    };
    const auto target_position = [&](humanoid_bone role)
    {
        return skeleton.rest_global[size_t(skeleton.humanoid[size_t(role)])].get_position();
    };
    const auto has_role = [&](humanoid_bone role)
    {
        const int source = source_roles[size_t(role)];
        const int target = skeleton.humanoid[size_t(role)];
        return source >= 0 && size_t(source) < entry.source_rest_global.size() && target >= 0 &&
               size_t(target) < skeleton.rest_global.size();
    };
    for(const auto& [from, to] : REST_SEGMENTS)
    {
        if(!has_role(from) || !has_role(to))
        {
            continue;
        }
        const math::vec3 source_direction = frame * (source_position(to) - source_position(from));
        const math::vec3 target_direction = target_position(to) - target_position(from);
        if(math::length(source_direction) <= MIN_REST_DIRECTION_LENGTH || math::length(target_direction) <= MIN_REST_DIRECTION_LENGTH)
        {
            continue;
        }
        report.rest_differences.push_back({from, get_degrees_between(source_direction, target_direction)});
    }
}

auto animation_player::find_binding(const std::shared_ptr<animation_clip>& clip_ptr, const animation_skeleton_binding& binding)
    -> const clip_binding&
{
    const animation_clip& clip = *clip_ptr;
    clip_binding* entry = nullptr;
    for(auto& candidate : bindings_)
    {
        if(is_same_clip(candidate.clip, clip_ptr) && candidate.channel_count == clip.channels.size())
        {
            if(candidate.version == binding.version)
            {
                return candidate;
            }
            entry = &candidate;
            break;
        }
    }
    if(entry == nullptr)
    {
        if(bindings_.size() >= MAX_CACHED_BINDINGS)
        {
            bindings_.clear();
        }
        entry = &bindings_.emplace_back();
    }

    entry->clip = clip_ptr;
    entry->channel_count = clip.channels.size();
    entry->version = binding.version;
    entry->ordered_channels.clear();
    entry->ordered_channels.reserve(clip.channels.size());

    // Rest-aligned retargeting needs the source rest pose of every channel and the target's.
    const auto& target = binding.rest_aligned_target;
    const bool has_source_rest = target && has_rest_pose(clip);
    std::vector<math::transform> source_rest;
    if(has_source_rest)
    {
        source_rest = compute_source_rest_globals(clip);
    }

    std::vector<int> resolved(clip.channels.size(), -1);
    bool all_resolved = true;
    for(size_t channel_index = 0; channel_index < clip.channels.size(); ++channel_index)
    {
        const auto& channel = clip.channels[channel_index];
        resolved[channel_index] = binding.resolve ? binding.resolve(channel) : int(channel.node_index);
        all_resolved = all_resolved && resolved[channel_index] >= 0;
    }
    // Bones the target names differently are matched by humanoid role, rest to rest.
    if(!all_resolved && has_source_rest)
    {
        const auto by_role = map_channels_by_role(clip, source_rest, *target);
        for(size_t channel_index = 0; channel_index < clip.channels.size(); ++channel_index)
        {
            if(resolved[channel_index] < 0)
            {
                resolved[channel_index] = by_role[channel_index];
            }
        }
    }
    for(size_t channel_index = 0; channel_index < clip.channels.size(); ++channel_index)
    {
        if(resolved[channel_index] >= 0)
        {
            entry->ordered_channels.emplace_back(uint32_t(resolved[channel_index]), uint32_t(channel_index));
        }
    }
    // Ascending target index, which the merge-based blends require; when two channels land on the
    // same bone the first one wins.
    std::stable_sort(entry->ordered_channels.begin(),
                     entry->ordered_channels.end(),
                     [](const auto& lhs, const auto& rhs)
                     {
                         return lhs.first < rhs.first;
                     });
    entry->ordered_channels.erase(std::unique(entry->ordered_channels.begin(),
                                              entry->ordered_channels.end(),
                                              [](const auto& lhs, const auto& rhs)
                                              {
                                                  return lhs.first == rhs.first;
                                              }),
                                  entry->ordered_channels.end());

    entry->channel_targets.assign(clip.channels.size(), -1);
    entry->target_channels.clear();
    entry->max_target_index = 0;
    for(const auto& [target_index, channel_index] : entry->ordered_channels)
    {
        entry->channel_targets[channel_index] = int(target_index);
        if(entry->target_channels.size() <= target_index)
        {
            entry->target_channels.resize(target_index + 1, -1);
        }
        entry->target_channels[target_index] = int(channel_index);
        entry->max_target_index = std::max<size_t>(entry->max_target_index, target_index);
    }
    entry->root_position_channel = find_root_channel(clip, clip.root_motion.position_node_name, clip.root_motion.position_node_index);
    entry->root_rotation_channel = find_root_channel(clip, clip.root_motion.rotation_node_name, clip.root_motion.rotation_node_index);
    entry->root_position_frame = make_root_motion_frame(clip, entry->root_position_channel);
    entry->root_rotation_frame = make_root_motion_frame(clip, entry->root_rotation_channel);
    entry->position_turns_with_rotation_root = false;
    for(int c = entry->root_position_channel >= 0 ? get_parent_channel(clip, size_t(entry->root_position_channel)) : -1;
        c >= 0 && entry->root_rotation_channel >= 0;
        c = get_parent_channel(clip, size_t(c)))
    {
        entry->position_turns_with_rotation_root |= c == entry->root_rotation_channel;
    }

    entry->rest_aligned = has_source_rest && !entry->ordered_channels.empty() &&
                          entry->max_target_index < target->parents.size() &&
                          entry->max_target_index < target->rest_global.size();
    entry->source_rest_global.clear();
    entry->height_ratio = 1.0f;
    entry->frame_rotation = math::identity<math::quat>();
    entry->source_skeleton_hash = 0;
    entry->is_linear_retarget = false;
    if(entry->rest_aligned)
    {
        entry->source_rest_global = std::move(source_rest);
        entry->source_skeleton_hash = hash_source_skeleton(clip);
        entry->is_linear_retarget = is_linear_retarget(clip, entry->channel_targets, entry->target_channels, *target);
        // Both rest poses are the same physical pose, but each skeleton may stand in its own world
        // frame - an importer's facing correction on the model's root node, say. Joint positions do
        // not depend on how bones label their axes, so the rotation that best lays the source rest
        // joints onto the target's is that frame difference; it turns source world directions into
        // target world directions.
        std::vector<math::vec3> source_points;
        std::vector<math::vec3> target_points;
        for(const auto& [target_index, channel_index] : entry->ordered_channels)
        {
            source_points.push_back(entry->source_rest_global[channel_index].get_position());
            target_points.push_back(target->rest_global[target_index].get_position());
        }
        entry->frame_rotation = fit_rotation(source_points, target_points);

        // Motion scales with the hips' height, so a shorter skeleton takes shorter steps. Taken
        // from the skeletons alone (not from a clip's root motion), so every clip of one skeleton
        // scales alike. Without hips on both sides: the node that carries the motion (or the first
        // child of a root-motion root at floor level).
        const auto source_height_of = [&](size_t c)
        {
            return (entry->frame_rotation * entry->source_rest_global[c].get_position()).y;
        };
        const auto source_roles = find_channel_roles(clip, entry->source_rest_global);
        const int source_hips = source_roles[size_t(humanoid_bone::hips)];
        const int target_hips = target->humanoid[size_t(humanoid_bone::hips)];
        const float source_hips_height = source_hips >= 0 ? source_height_of(size_t(source_hips)) : 0.0f;
        const float target_hips_height = target_hips >= 0 ? target->rest_global[size_t(target_hips)].get_position().y : 0.0f;
        if(source_hips_height > MIN_RETARGET_HEIGHT && target_hips_height > MIN_RETARGET_HEIGHT)
        {
            entry->height_ratio = target_hips_height / source_hips_height;
        }
        else if(entry->root_position_channel >= 0 && entry->channel_targets[size_t(entry->root_position_channel)] >= 0)
        {
            const auto c = size_t(entry->root_position_channel);
            const int t = entry->channel_targets[c];
            float source_height = source_height_of(c);
            float target_height = target->rest_global[size_t(t)].get_position().y;
            for(size_t child = c + 1; child < clip.channels.size() && source_height < MIN_RETARGET_HEIGHT; ++child)
            {
                const int child_target = entry->channel_targets[child];
                if(clip.channels[child].parent_channel == int(c) && child_target >= 0)
                {
                    source_height = source_height_of(child);
                    target_height = target->rest_global[size_t(child_target)].get_position().y;
                }
            }
            if(source_height > MIN_RETARGET_HEIGHT && target_height > MIN_RETARGET_HEIGHT)
            {
                entry->height_ratio = target_height / source_height;
            }
        }
    }
    entry->root_position_target = find_root_motion_target(clip, *entry, entry->root_position_channel);
    entry->root_rotation_target = find_root_motion_target(clip, *entry, entry->root_rotation_channel);
    return *entry;
}

auto animation_player::make_root_motion_frame(const animation_clip& clip, int channel_index) -> root_motion_frame
{
    root_motion_frame frame;
    if(channel_index < 0 || !clip.channels[size_t(channel_index)].has_rest)
    {
        // Clips imported without rest data are taken as upright and keep their first frame.
        return frame;
    }
    const math::transform parent = get_parent_rest(clip, size_t(channel_index));
    frame.up = math::normalize(parent.get_rotation());
    const math::transform rest = parent * clip.channels[size_t(channel_index)].rest_local;
    frame.locks_to_rest = rest.get_position().y < MIN_RETARGET_HEIGHT;
    return frame;
}

void animation_player::sample_curves(const animation_clip& anim_clip, float progress, animation_pose& pose) const
{
    pose.curves.clear();
    if(anim_clip.curves.empty())
    {
        return;
    }
    pose.curves.reserve(anim_clip.curves.size());
    for(const auto& curve : anim_clip.curves)
    {
        pose.curves.push_back({get_animation_curve_id(curve.name), curve.curve.sample(progress)});
    }
    std::sort(pose.curves.begin(),
              pose.curves.end(),
              [](const animation_curve_value& lhs, const animation_curve_value& rhs)
              {
                  return lhs.id < rhs.id;
              });
}

void animation_player::sample_root_position(const animation_clip& anim_clip,
                                            const animation_channel& channel,
                                            uint32_t target_index,
                                            seconds_t time,
                                            uint64_t loop_count,
                                            bool extract_root_motion,
                                            const root_motion_frame& frame,
                                            math::transform& local,
                                            animation_pose& pose) const
{
    const auto& root_motion = anim_clip.root_motion;
    const math::vec3 position = local.get_position();
    const math::quat& up_frame = frame.up;

    // Root motion is reported against the TARGET skeleton index the node is written to.
    pose.motion_result.root_position_node_index = int(target_index);

    pose.motion_result.root_position_weights = {1.0f, 1.0f, 1.0f};
    pose.motion_result.bone_position_weights = {0.0f, 0.0f, 0.0f};

    auto& motion_state = pose.motion_state;
    if(loop_count < motion_state.root_position_loop_count)
    {
        // The state restarted under the tracking: measure from here.
        motion_state.root_position_initialized = false;
    }
    if(!motion_state.root_position_initialized)
    {
        // First sample: measured from where the time step began, not from the clip start, so a
        // phase-synced start does not move the entity from the start to that phase.
        motion_state.root_position_initialized = true;
        motion_state.root_position_at_time = interpolate(channel.position_keys, motion_state.baseline_time);
        motion_state.root_position_loop_count = std::min(motion_state.baseline_loop_count, loop_count);
    }

    auto delta_position = position - motion_state.root_position_at_time;

    const auto wraps = loop_count - motion_state.root_position_loop_count;
    if(wraps > 0)
    {
        // Time wraps from the clip's end to its start, which keys need not sit on.
        const math::vec3 clip_start_pos = interpolate(channel.position_keys, seconds_t(0));
        const math::vec3 clip_end_pos = interpolate(channel.position_keys, anim_clip.duration);
        delta_position += float(wraps) * (clip_end_pos - clip_start_pos);
    }
    // Kept axes are up and horizontal in the clip turned upright, not the keys' own axes (a Z-up
    // export keeps its height on Z): the delta loses them there, and moves the entity whole.
    {
        math::vec3 upright_delta = up_frame * delta_position;
        if(root_motion.keep_position_y || root_motion.keep_in_place)
        {
            upright_delta.y = 0.0f;
        }
        if(root_motion.keep_position_xz || root_motion.keep_in_place)
        {
            upright_delta.x = 0.0f;
            upright_delta.z = 0.0f;
        }
        delta_position = glm::inverse(up_frame) * upright_delta;
    }

    if(root_motion.keep_position_y || root_motion.keep_position_xz || root_motion.keep_in_place)
    {
        pose.motion_result.bone_position_weights = {1.0f, 1.0f, 1.0f};
    }

    // Unless keep_position_xz explicitly leaves the raw travel in the
    // pose, replace the pose's horizontal position with a value that is
    // meaningful WITHOUT its weight mask, and write it at bone weight 1.
    // Pose blending knows nothing about per-pose weights, so a raw
    // traveling position masked by a zero weight would leak into any
    // crossfade against a clip whose weight is 1, whenever clips disagree
    // on root-motion settings.
    //
    // keep_in_place discards the clip's TRAVEL, not the bone's
    // animation: subtract the start-to-end displacement accrued
    // linearly over the key range, then remove EVERY remaining
    // component along the travel direction - mocap travel is not
    // constant-speed (a Mixamo jog's hip speed swings ~30% within the
    // loop), and the linear part alone leaves that asymmetry as a
    // fore-aft lunge played once per loop. The lateral sway stays in
    // the pose, the residual matches at both key-range ends (no seam
    // across loop wraps), and a non-traveling channel (idle, dance)
    // keeps its full animation. Extraction instead takes the FULL
    // per-frame displacement - sway included - into the root delta, so
    // its pose complement is the constant clip-start baseline; adding
    // the sway there too would play it twice. The baseline is only
    // valid while the caller actually applies the extracted delta -
    // when root motion is not applied the delta is discarded, so
    // pinning would erase the authored motion; keep the raw channel in
    // the pose instead.
    //
    // Vertical motion follows the same rule: an extracted height is
    // replaced by the clip-start height, so the whole position is
    // written and the bone weights are uniform. Per-axis bone weights
    // are applied in the bone's parent frame, which need not be upright.
    //
    // Up and horizontal are the clip's: keys live in the node's parent
    // frame, so the split is made in that frame turned upright.
    if(root_motion.keep_in_place || extract_root_motion)
    {
        pose.motion_result.bone_position_weights = {1.0f, 1.0f, 1.0f};

        const auto& first_key = channel.position_keys.front();
        const auto& last_key = channel.position_keys.back();
        const float key_span = (last_key.time - first_key.time).count();
        const math::vec3 sampled = up_frame * position;
        const math::vec3 first = up_frame * first_key.value;
        const math::vec3 baseline = frame.locks_to_rest ? up_frame * channel.rest_local.get_position() : first;
        math::vec3 pose_position = root_motion.keep_position_xz ? sampled : baseline;
        if(root_motion.keep_in_place && key_span > 0.0f)
        {
            const float progress = math::clamp((time - first_key.time).count() / key_span, 0.0f, 1.0f);
            math::vec3 travel = up_frame * last_key.value - first;
            travel.y = 0.0f;
            math::vec3 residual = sampled - first - travel * progress;
            const float travel_len = math::length(travel);
            if(travel_len > IN_PLACE_MIN_TRAVEL)
            {
                const math::vec3 travel_dir = travel / travel_len;
                residual -= travel_dir * math::dot(residual, travel_dir);
            }
            pose_position = first + residual;
        }
        pose_position.y = root_motion.keep_position_y || root_motion.keep_in_place ? sampled.y : baseline.y;
        local.set_position(glm::inverse(up_frame) * pose_position);
    }

    motion_state.root_position_at_time = position;
    motion_state.root_position_loop_count = loop_count;
    pose.motion_result.root_transform_delta.set_position(delta_position);
}

auto animation_player::sample_root_rotation(const animation_clip& anim_clip,
                                            const animation_channel& channel,
                                            uint32_t target_index,
                                            uint64_t loop_count,
                                            bool extract_root_motion,
                                            const root_motion_frame& frame,
                                            math::transform& local,
                                            animation_pose& pose) const -> math::quat
{
    const auto& root_motion = anim_clip.root_motion;
    const math::quat rotation = local.get_rotation();
    const math::quat& up_frame = frame.up;

    pose.motion_result.root_rotation_node_index = int(target_index);

    pose.motion_result.root_rotation_weight = {1.0f};
    pose.motion_result.bone_rotation_weight = {0.0f};

    auto& motion_state = pose.motion_state;
    if(loop_count < motion_state.root_rotation_loop_count)
    {
        motion_state.root_rotation_initialized = false;
    }
    if(!motion_state.root_rotation_initialized)
    {
        motion_state.root_rotation_initialized = true;
        motion_state.root_rotation_at_time = interpolate(channel.rotation_keys, motion_state.baseline_time);
        motion_state.root_rotation_loop_count = std::min(motion_state.baseline_loop_count, loop_count);
    }

    // Only the turn about the clip's up axis moves the entity: a hips node also leans and rolls,
    // and tilting the whole character by that would lay it over. Headings are measured with the
    // keys turned upright (see get_parent_rest); they all turn about +Y, so they commute, and the
    // deltas telescope: the turns the entity takes add up to the heading the pose gives up.
    const math::quat up_frame_inverse = glm::inverse(up_frame);
    const auto heading_of = [&](const math::quat& key_rotation)
    {
        return get_heading(up_frame * key_rotation);
    };
    const math::quat heading = heading_of(rotation);
    const math::quat previous_heading = heading_of(motion_state.root_rotation_at_time);
    math::quat delta_heading;
    const auto wraps = loop_count - motion_state.root_rotation_loop_count;
    if(wraps == 0)
    {
        delta_heading = heading * glm::inverse(previous_heading);
    }
    else
    {
        // Time wraps from the clip's end to its start, which keys need not sit on. The delta
        // across the wrap(s), in playback order: previous sample -> clip end, then (wraps - 1)
        // full loops, then clip start -> current sample.
        const math::quat clip_start_heading = heading_of(interpolate(channel.rotation_keys, seconds_t(0)));
        const math::quat clip_end_heading = heading_of(interpolate(channel.rotation_keys, anim_clip.duration));
        const auto loop_heading_offset = clip_end_heading * glm::inverse(clip_start_heading);
        delta_heading = clip_end_heading * glm::inverse(previous_heading);
        delta_heading = repeat_heading(loop_heading_offset, wraps - 1) * delta_heading;
        delta_heading = (heading * glm::inverse(clip_start_heading)) * delta_heading;
    }
    // Reported in the keys' own frame, like the position delta.
    const math::quat delta_rotation = math::normalize(up_frame_inverse * delta_heading * up_frame);

    if(root_motion.keep_rotation)
    {
        pose.motion_result.root_rotation_weight = 0.0f;
        pose.motion_result.bone_rotation_weight = 1.0f;
    }

    math::quat pose_turn = math::identity<math::quat>();
    if(root_motion.keep_in_place)
    {
        pose.motion_result.root_rotation_weight = 0.0f;
        pose.motion_result.bone_rotation_weight = 1.0f;
    }
    else if(extract_root_motion && !root_motion.keep_rotation)
    {
        // Extraction takes the FULL per-frame rotation change into the
        // root delta, so the pose's own rotation must be the constant
        // clip-start baseline written at bone weight 1 - not the raw
        // traveling rotation masked by a zero weight. Pose blending
        // ignores per-pose weights, so a masked raw value would leak
        // into crossfades against kept-rotation clips, as with position.
        // The baseline only holds while the delta is applied - with root
        // motion off the raw rotation stays in the pose.
        //
        // The baseline is the clip-start HEADING: the lean and roll
        // that are not extracted stay in the pose.
        pose.motion_result.bone_rotation_weight = 1.0f;
        const math::quat upright = up_frame * rotation;
        const math::quat start_heading =
            heading_of(frame.locks_to_rest ? channel.rest_local.get_rotation() : channel.rotation_keys.front().value);
        local.set_rotation(math::normalize(up_frame_inverse * start_heading * glm::inverse(heading) * upright));
        // The entity has turned by what the clip turned from the baseline up to the last sample:
        // a step the clip takes in its own frame is that much less turned in the entity's.
        pose_turn = math::normalize(start_heading * glm::inverse(previous_heading));
    }

    motion_state.root_rotation_at_time = rotation;
    motion_state.root_rotation_loop_count = loop_count;
    pose.motion_result.root_transform_delta.set_rotation(delta_rotation);
    return pose_turn;
}

void animation_player::rebase_root_translation(const clip_binding& clip_binding, const math::quat& pose_turn, animation_pose& pose)
{
    if(clip_binding.position_turns_with_rotation_root || pose.motion_result.root_position_node_index < 0)
    {
        return;
    }
    const math::quat& up_frame = clip_binding.root_position_frame.up;
    const math::vec3 delta = pose.motion_result.root_transform_delta.get_position();
    pose.motion_result.root_transform_delta.set_position(glm::inverse(up_frame) * (pose_turn * (up_frame * delta)));
}

void animation_player::sample_animation(const std::shared_ptr<animation_clip>& anim_clip,
                                        seconds_t time,
                                        uint64_t loop_count,
                                        const animation_skeleton_binding& binding,
                                        bool extract_root_motion,
                                        bool track_root_motion,
                                        animation_pose& pose)
{
    if(!anim_clip)
    {
        return;
    }
    const auto& clip_binding = find_binding(anim_clip, binding);
    pose.nodes.clear();
    pose.nodes.reserve(clip_binding.ordered_channels.size());
    clear_root_motion(pose);

    if(clip_binding.rest_aligned)
    {
        sample_rest_aligned(*anim_clip, clip_binding, *binding.rest_aligned_target, time, loop_count,
                            extract_root_motion, track_root_motion, pose);
        return;
    }

    math::quat pose_turn = math::identity<math::quat>();
    for(const auto& [target_index, channel_index] : clip_binding.ordered_channels)
    {
        const auto& channel = anim_clip->channels[channel_index];
        auto& node = pose.nodes.emplace_back();
        node.desc.index = target_index;
        node.transform = sample_channel(channel, time);

        if(!track_root_motion)
        {
            continue;
        }
        if(!channel.position_keys.empty() && int(channel_index) == clip_binding.root_position_channel)
        {
            sample_root_position(*anim_clip, channel, target_index, time, loop_count, extract_root_motion,
                                 clip_binding.root_position_frame, node.transform, pose);
        }
        if(!channel.rotation_keys.empty() && int(channel_index) == clip_binding.root_rotation_channel)
        {
            pose_turn = sample_root_rotation(*anim_clip, channel, target_index, loop_count, extract_root_motion,
                                             clip_binding.root_rotation_frame, node.transform, pose);
        }
    }
    rebase_root_translation(clip_binding, pose_turn, pose);
}

void animation_player::sample_rest_aligned(const animation_clip& anim_clip,
                                           const clip_binding& clip_binding,
                                           const animation_target_skeleton& target,
                                           seconds_t time,
                                           uint64_t loop_count,
                                           bool extract_root_motion,
                                           bool track_root_motion,
                                           animation_pose& pose)
{
    auto& source_local = retarget_scratch_.source_local;
    const auto roots =
        sample_source_pose(anim_clip, clip_binding, time, loop_count, extract_root_motion, track_root_motion, source_local, pose);
    retarget_source_pose(anim_clip, clip_binding, target, source_local, pose);
    convert_root_delta(anim_clip, clip_binding, target, roots, pose);
}

auto animation_player::sample_source_pose(const animation_clip& anim_clip,
                                          const clip_binding& clip_binding,
                                          seconds_t time,
                                          uint64_t loop_count,
                                          bool extract_root_motion,
                                          bool track_root_motion,
                                          std::vector<math::transform>& source_local,
                                          animation_pose& pose) const -> root_channels
{
    const auto& channels = anim_clip.channels;
    source_local.resize(channels.size());

    // Sample every channel - unmapped ancestors still carry their children, and an unmapped root
    // still moves the entity.
    root_channels roots;
    math::quat pose_turn = math::identity<math::quat>();
    for(size_t c = 0; c < channels.size(); ++c)
    {
        const auto& channel = channels[c];
        source_local[c] = sample_channel(channel, time, channel.rest_local);
        if(!track_root_motion)
        {
            continue;
        }
        if(!channel.position_keys.empty() && int(c) == clip_binding.root_position_channel && clip_binding.root_position_target >= 0)
        {
            sample_root_position(anim_clip, channel, uint32_t(clip_binding.root_position_target), time, loop_count,
                                 extract_root_motion, clip_binding.root_position_frame, source_local[c], pose);
            roots.position = int(c);
        }
        if(!channel.rotation_keys.empty() && int(c) == clip_binding.root_rotation_channel && clip_binding.root_rotation_target >= 0)
        {
            pose_turn = sample_root_rotation(anim_clip, channel, uint32_t(clip_binding.root_rotation_target), loop_count,
                                             extract_root_motion, clip_binding.root_rotation_frame, source_local[c], pose);
            roots.rotation = int(c);
        }
    }
    rebase_root_translation(clip_binding, pose_turn, pose);
    return roots;
}

void animation_player::retarget_source_pose(const animation_clip& anim_clip,
                                            const clip_binding& clip_binding,
                                            const animation_target_skeleton& target,
                                            const std::vector<math::transform>& source_local,
                                            animation_pose& pose)
{
    const auto& channels = anim_clip.channels;
    auto& source_parent_global = retarget_scratch_.source_parent_global;
    auto& source_global = retarget_scratch_.source_global;
    source_parent_global.resize(channels.size());
    source_global.resize(channels.size());
    for(size_t c = 0; c < channels.size(); ++c)
    {
        source_parent_global[c] = get_parent_global(anim_clip, source_global, c);
        source_global[c] = source_parent_global[c] * source_local[c];
    }

    // Target globals in index order: parents precede children in the armature.
    auto& target_global = retarget_scratch_.target_global;
    const size_t bone_count = std::min(target.parents.size(), clip_binding.max_target_index + 1);
    target_global.resize(bone_count);
    pose.nodes.reserve(pose.nodes.size() + clip_binding.ordered_channels.size());
    for(size_t t = 0; t < bone_count; ++t)
    {
        const int parent = target.parents[t];
        const math::transform parent_global =
            parent >= 0 && size_t(parent) < t ? target_global[size_t(parent)] : math::transform{};
        const int c = t < clip_binding.target_channels.size() ? clip_binding.target_channels[t] : -1;
        if(c < 0)
        {
            target_global[t] = parent_global * target.bind_local[t];
            continue;
        }
        const auto& channel = channels[size_t(c)];
        const auto& source_rest_global = clip_binding.source_rest_global[size_t(c)];

        // The bone turns in the world by what the source bone turned from ITS rest pose, carried
        // from the source root frame into the target's.
        const math::quat& frame = clip_binding.frame_rotation;
        const math::quat world_delta = frame *
                                       (source_global[size_t(c)].get_rotation() * glm::inverse(source_rest_global.get_rotation())) *
                                       glm::inverse(frame);
        const math::quat global_rotation = math::normalize(world_delta * target.rest_global[t].get_rotation());

        // Lengths stay the target's: only the offset from the source rest moves, carried through
        // the parents' frames and scaled by the skeletons' height ratio.
        const math::vec3 source_offset = source_local[size_t(c)].get_position() - channel.rest_local.get_position();
        math::vec3 world_offset = frame * transform_offset(source_parent_global[size_t(c)], source_offset);
        // Ancestors the target lacks (a floor root above the hips) carry this bone with them:
        // their moves from rest reach it as part of its own.
        for(int u = get_parent_channel(anim_clip, size_t(c)); u >= 0 && clip_binding.channel_targets[size_t(u)] < 0;
            u = get_parent_channel(anim_clip, size_t(u)))
        {
            const math::vec3 ancestor_offset = source_local[size_t(u)].get_position() - channels[size_t(u)].rest_local.get_position();
            world_offset += frame * transform_offset(source_parent_global[size_t(u)], ancestor_offset);
        }
        const math::vec3 target_offset = inverse_transform_offset(parent_global, world_offset) * clip_binding.height_ratio;

        math::transform local;
        local.set_rotation(math::normalize(glm::inverse(parent_global.get_rotation()) * global_rotation));
        local.set_position(target.bind_local[t].get_position() + target_offset);
        const math::vec3 rest_scale = channel.rest_local.get_scale();
        const math::vec3 scale_ratio(rest_scale.x != 0.0f ? source_local[size_t(c)].get_scale().x / rest_scale.x : 1.0f,
                                     rest_scale.y != 0.0f ? source_local[size_t(c)].get_scale().y / rest_scale.y : 1.0f,
                                     rest_scale.z != 0.0f ? source_local[size_t(c)].get_scale().z / rest_scale.z : 1.0f);
        const math::quat source_to_target =
            glm::inverse(target.rest_global[t].get_rotation()) * frame * source_rest_global.get_rotation();
        local.set_scale(target.bind_local[t].get_scale() * carry_scale_ratio(scale_ratio, source_to_target));
        target_global[t] = parent_global * local;

        auto& node = pose.nodes.emplace_back();
        node.desc.index = t;
        node.transform = local;
    }
}

void animation_player::convert_root_delta(const animation_clip& anim_clip,
                                          const clip_binding& clip_binding,
                                          const animation_target_skeleton& target,
                                          const root_channels& roots,
                                          animation_pose& pose) const
{
    // The extracted root delta was measured in the source root node's parent frame: carry it into
    // the frame of the target bone it is reported against, rest to rest. Both skeletons stand
    // upright, so only the heading of the frame fit applies: a fit tilted by differing proportions
    // would tilt the entity's turns and lift its travel.
    const auto source_parent_rest = [&](int c)
    {
        return get_parent_global(anim_clip, clip_binding.source_rest_global, size_t(c));
    };
    const auto target_parent_rest = [&](int t) -> math::transform
    {
        const int parent = t >= 0 ? target.parents[size_t(t)] : -1;
        return parent >= 0 && parent < t ? target.rest_global[size_t(parent)] : math::transform{};
    };
    const math::quat frame_heading = get_heading(clip_binding.frame_rotation);
    if(roots.position >= 0)
    {
        const math::transform from = source_parent_rest(roots.position);
        const math::transform to = target_parent_rest(clip_binding.root_position_target);
        const math::vec3 world = frame_heading * transform_offset(from, pose.motion_result.root_transform_delta.get_position());
        pose.motion_result.root_transform_delta.set_position(inverse_transform_offset(to, world) * clip_binding.height_ratio);
    }
    if(roots.rotation >= 0)
    {
        const math::transform from = source_parent_rest(roots.rotation);
        const math::transform to = target_parent_rest(clip_binding.root_rotation_target);
        const math::quat frame = glm::inverse(to.get_rotation()) * frame_heading * from.get_rotation();
        pose.motion_result.root_transform_delta.set_rotation(
            math::normalize(frame * pose.motion_result.root_transform_delta.get_rotation() * glm::inverse(frame)));
    }
}

auto animation_player::sample_blend_in_source_space(animation_state& state,
                                                    const animation_skeleton_binding& binding,
                                                    bool extract_root_motion,
                                                    animation_pose& pose) -> bool
{
    const auto& clips = state.blend_loaded_clips;
    if(!binding.rest_aligned_target || clips.size() < 2)
    {
        return false;
    }
    size_t skeleton_hash = 0;
    for(size_t i = 0; i < clips.size(); ++i)
    {
        if(!clips[i])
        {
            return false;
        }
        const auto& clip_binding = find_binding(clips[i], binding);
        // Equal channel counts make the channel-by-channel blend safe even if two skeletons
        // ever shared a hash.
        const bool is_same_skeleton = i == 0 || (clip_binding.source_skeleton_hash == skeleton_hash &&
                                                 clips[i]->channels.size() == clips.front()->channels.size());
        if(!clip_binding.rest_aligned || !clip_binding.is_linear_retarget || !is_same_skeleton)
        {
            return false;
        }
        skeleton_hash = clip_binding.source_skeleton_hash;
    }

    const auto& target = *binding.rest_aligned_target;
    auto& point_source_local = retarget_scratch_.point_source_local;
    point_source_local.resize(clips.size());
    for(size_t i = 0; i < clips.size(); ++i)
    {
        const auto& clip = clips[i];
        const auto& clip_weight = state.blend_clips[i];
        auto& point_pose = state.blend_poses[i];
        auto& motion_state = state.point_motion_states[clip_weight.point];
        state.blend_weights[i] = clip_weight.weight;
        // A point's pose carries its root motion and curves; its bones come from the one retarget.
        point_pose.nodes.clear();
        clear_root_motion(point_pose);
        point_pose.motion_state = motion_state;
        set_baseline_position(point_pose.motion_state, clip->duration * state.step_begin_phase, state.step_begin_loop_count);
        const auto& clip_binding = find_binding(clip, binding);
        const auto roots = sample_source_pose(*clip,
                                              clip_binding,
                                              clip->duration * state.phase,
                                              state.loop_count,
                                              extract_root_motion,
                                              true,
                                              point_source_local[i],
                                              point_pose);
        convert_root_delta(*clip, clip_binding, target, roots, point_pose);
        motion_state = point_pose.motion_state;
        sample_curves(*clip, state.phase, point_pose);
    }

    // Curves and root motion blend as they always do; the bones are the retarget of the blend.
    blend_poses(state.blend_poses, state.blend_weights, pose);
    blend_source_poses(point_source_local, state.blend_weights, retarget_scratch_.source_local);
    retarget_source_pose(*clips.front(), find_binding(clips.front(), binding), target, retarget_scratch_.source_local, pose);
    return true;
}

auto animation_player::get_fired_events() const -> const std::vector<animation_event_hit>&
{
    return fired_events_;
}

void animation_player::clear_fired_events()
{
    fired_events_.clear();
}

auto animation_player::get_curve_values() const -> const std::vector<animation_curve_value>&
{
    return final_curves_;
}

auto animation_player::find_curve_value(uint32_t curve_id) const -> std::optional<float>
{
    auto it = std::lower_bound(final_curves_.begin(),
                               final_curves_.end(),
                               curve_id,
                               [](const animation_curve_value& lhs, uint32_t id)
                               {
                                   return lhs.id < id;
                               });
    if(it == final_curves_.end() || it->id != curve_id)
    {
        return std::nullopt;
    }
    return it->value;
}

auto animation_player::get_curve_value(uint32_t curve_id, float fallback) const -> float
{
    return find_curve_value(curve_id).value_or(fallback);
}

auto animation_player::has_curve(uint32_t curve_id) const -> bool
{
    return find_curve_value(curve_id).has_value();
}

auto animation_player::is_playing() const -> bool
{
    return playing_ && !paused_;
}

auto animation_player::is_paused() const -> bool
{
    return paused_;
}

auto animation_player::get_layers() const -> const std::vector<animation_layer>&
{
    return layers_;
}

void animation_player::seek(size_t layer_idx, float progress, bool seek_target)
{
    // Deliberately no get_layer(): seeking must not create layers.
    if(layer_idx >= layers_.size())
    {
        return;
    }
    auto& layer = layers_[layer_idx];
    auto& layer_state = seek_target ? layer.target_state : layer.current_state;
    auto& state = layer_state.state;
    if(!state.blend_space && get_state_duration(state) <= seconds_t(0))
    {
        return;
    }
    state.set_progress(progress);
    // A seek jumps: markers between the old and new position must not fire, and root motion must
    // not move the entity by the jump.
    state.reset_event_window(false);
    reset_root_motion_tracking(layer_state);
}

void animation_player::reset_root_motion()
{
    for(auto& layer : layers_)
    {
        reset_root_motion_tracking(layer.current_state);
        reset_root_motion_tracking(layer.target_state);
    }
}

void animation_player::request_pose_refresh()
{
    pose_refresh_requested_ = true;
}

} // namespace unravel
