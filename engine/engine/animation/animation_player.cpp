#include "animation_player.h"
#include <hpp/utility/overload.hpp>
namespace unravel
{

namespace
{
/**
 * @brief Interpolates between keyframes to find the appropriate value at the current time.
 *  * @tparam T The type of value being interpolated (e.g., vec3 or quat).
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

    // Compute the interpolation factor (0.0 to 1.0)
    float factor = (time.count() - key1.time.count()) / (key2.time.count() - key1.time.count());

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

} // namespace
auto animation_player::get_layer(size_t index) -> animation_layer&
{
    if(index >= layers_.size())
    {
        layers_.resize(index + 1);
    }
    return layers_[index];
}
void animation_player::clear(size_t layer_idx)
{
    auto& layer = get_layer(layer_idx);
    layer = {};
}

void animation_player::blend_to(size_t layer_idx,
                                const asset_handle<animation_clip>& clip,
                                seconds_t duration,
                                bool loop,
                                bool phase_sync,
                                const blend_easing_t& easing)
{
    auto& layer = get_layer(layer_idx);
    if(!clip)
    {
        if(layer.current_state.state.clip)
        {
            layer.current_state = {};
        }
    }

    layer.target_state.state.loop = loop;

    if(layer.target_state.state.clip == clip)
    {
        return;
    }

    if(layer.current_state.state.clip == clip)
    {
        return;
    }

    layer.target_state.state.clip = clip;
    auto phase = phase_sync ? layer.current_state.state.get_progress() : 0.0f;
    layer.target_state.state.set_progress(phase);


    if(duration > clip.get()->duration)
    {
        duration = clip.get()->duration;
    }

    // Set blending parameters
    layer.blending_state.state = blend_over_time{duration};
    layer.blending_state.easing = easing;
}

void animation_player::set_blend_space(size_t layer_idx, const std::shared_ptr<blend_space_def>& blend_space, bool loop)
{
    auto& layer = get_layer(layer_idx);

    layer.current_state.state.loop = loop;

    if(layer.current_state.state.blend_space == blend_space)
    {
        return;
    }

    layer.current_state.state.blend_space = blend_space;
    layer.current_state.state.elapsed = seconds_t(0);
    // Clear target state if any
    layer.target_state = {};
    layer.blending_state = {};
}

void animation_player::set_blend_space_parameters(size_t layer_idx, const std::vector<float>& params)
{
    auto& layer = get_layer(layer_idx);
    layer.current_state.parameters = params;
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

    for(auto& layer : layers_)
    {
        layer.current_state.state.elapsed = seconds_t(0);
        layer.target_state.state.elapsed = seconds_t(0);
    }
}

auto animation_player::update_time(seconds_t delta_time, bool force) -> bool
{
    if(!is_playing())
    {
        return false;
    }

    bool any_valid = false;
    for(auto& layer : layers_)
    {
        any_valid |= layer.current_state.is_valid();
        any_valid |= layer.target_state.is_valid();

        if(any_valid)
        {
            break;
        }
    }

    if(!any_valid)
    {
        return false;
    }

    // Update times
    if(playing_ && !paused_)
    {
        for(auto& layer : layers_)
        {
            update_state(delta_time, layer.current_state.state);

            update_state(delta_time, layer.target_state.state);

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
        }

        return true;
    }
    return false;
}

void animation_player::update_poses(const animation_pose& ref_pose, const update_callback_t& set_transform_callback)
{
    if(layers_.empty())
    {
        return;
    }

    for(auto& layer : layers_)
    {
        // Update current layer
        update_pose(layer.current_state);

        // Update target layer
        if(update_pose(layer.target_state))
        {
            // Compute blend factor
            float blend_progress = get_blend_progress(layer);
            float blend_factor = compute_blend_factor(layer, blend_progress);

            // Blend poses
            blend_poses(layer.current_state.pose, layer.target_state.pose, blend_factor, layer.blend_pose);

            // Check if blending is finished
            if(blend_progress >= 1.0f)
            {
                // Switch to target animation or blend space
                layer.current_state = layer.target_state;
                layer.target_state = {};
                layer.blending_state = {};
            }
        }
    }

    if(layers_.size() == 1)
    {
        auto final_pose = layers_.front().get_final_pose();

        // Apply the final pose using the callback
        for(const auto& node : final_pose->nodes)
        {
            set_transform_callback(node.desc, node.transform, final_pose->motion_result);
        }
    }
    else
    {
        animation_pose final_pose{};
        blend_poses_additive(*layers_[0].get_final_pose(), *layers_[1].get_final_pose(), ref_pose, 1.0f, final_pose);

        for(size_t i = 2; i < layers_.size(); ++i)
        {
            animation_pose next_final_pose{};
            blend_poses_additive(final_pose, *layers_[i].get_final_pose(), ref_pose, 1.0f, next_final_pose);

            final_pose = next_final_pose;
        }

        // Apply the final pose using the callback
        for(const auto& node : final_pose.nodes)
        {
            set_transform_callback(node.desc, node.transform, final_pose.motion_result);
        }
    }
}

auto animation_player::update_pose(animation_layer_state& layer) -> bool
{
    auto& state = layer.state;
    auto& pose = layer.pose;
    auto& parameters = layer.parameters;

    if(state.blend_space)
    {
        // Compute blending weights based on current parameters (e.g., speed and direction)
        state.blend_space->compute_blend(parameters, state.blend_clips);

        // Sample animations and blend poses
        state.blend_poses.resize(state.blend_clips.size());
        for(size_t i = 0; i < state.blend_clips.size(); ++i)
        {
            const auto& clip_weight_pair = state.blend_clips[i];
            sample_animation(clip_weight_pair.first.get().get(), state.elapsed, state.blend_poses[i]);
        }

        // Blend all poses based on their weights
        pose.nodes.clear();
        if(!state.blend_poses.empty())
        {
            // Initialize with the first pose
            pose = state.blend_poses[0];
            float total_weight = state.blend_clips[0].second;

            for(size_t i = 1; i < state.blend_poses.size(); ++i)
            {
                blend_poses(pose,
                            state.blend_poses[i],
                            state.blend_clips[i].second / (total_weight + state.blend_clips[i].second),
                            pose);
                total_weight += state.blend_clips[i].second;
            }
        }
        return true;
    }

    if(state.clip)
    {
        sample_animation(state.clip.get().get(), state.elapsed, pose);
        return true;
    }

    return false;
}

void animation_player::update_state(seconds_t delta_time, animation_state& state)
{
    if(state.clip)
    {
        state.elapsed += delta_time;
        auto target_anim = state.clip.get();
        if(target_anim)
        {
            if(state.elapsed > target_anim->duration)
            {
                if(state.loop)
                {
                    state.elapsed = seconds_t(std::fmod(state.elapsed.count(), target_anim->duration.count()));
                }
                else
                {
                    state.elapsed = target_anim->duration;
                }
            }
        }

    }
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

auto animation_player::compute_blend_factor(const animation_layer& layer, float normalized_blend_time) noexcept -> float
{
    float blend_factor = 0.0f;

    // Apply the easing function
    blend_factor = layer.blending_state.easing(normalized_blend_time);

    // Check if blending is complete
    if(normalized_blend_time >= 1.0f)
    {
        // Blending complete
        blend_factor = 1.0f;
    }

    return blend_factor;
}

void animation_player::sample_animation(const animation_clip* anim_clip,
                                        seconds_t time,
                                        animation_pose& pose) const noexcept
{
    if(!anim_clip)
    {
        return;
    }
    pose.nodes.clear();
    pose.nodes.reserve(anim_clip->channels.size());

    for(const auto& channel : anim_clip->channels)
    {
        math::vec3 position = interpolate(channel.position_keys, time);
        math::quat rotation = interpolate(channel.rotation_keys, time);
        math::vec3 scaling = interpolate(channel.scaling_keys, time);

        auto& node = pose.nodes.emplace_back();
        node.desc.index = channel.node_index;
        // node.desc.name = channel.node_name;
        node.transform.set_position(position);
        node.transform.set_rotation(rotation);
        node.transform.set_scale(scaling);

        bool processed = false;

        if(int(node.desc.index) == anim_clip->root_motion.position_node_index)
        {
            pose.motion_result.root_position_node_index = anim_clip->root_motion.position_node_index;

            const auto& clip_start_pos = channel.position_keys.front().value;
            const auto& clip_end_pos = channel.position_keys.back().value;

            pose.motion_result.root_position_weights = {1.0f, 1.0f, 1.0f};
            pose.motion_result.bone_position_weights = {0.0f, 0.0f, 0.0f};

            if(pose.motion_state.root_position_time == seconds_t(0))
            {
                pose.motion_state.root_position_time = time;
                pose.motion_state.root_position_at_time = clip_start_pos;
            }

            auto delta_position = position - pose.motion_state.root_position_at_time;

            if(time < pose.motion_state.root_position_time)
            {
                auto loop_pos_offset = clip_end_pos - clip_start_pos;
                delta_position = delta_position + loop_pos_offset;
            }



            if(anim_clip->root_motion.keep_position_y)
            {
                pose.motion_result.root_position_weights.y = 0.0f;
                pose.motion_result.bone_position_weights.y = 1.0f;

            }

            if(anim_clip->root_motion.keep_position_xz)
            {
                pose.motion_result.root_position_weights.x = 0.0f;
                pose.motion_result.root_position_weights.z = 0.0f;

                pose.motion_result.bone_position_weights.x = 1.0f;
                pose.motion_result.bone_position_weights.z = 1.0f;
            }

            if(anim_clip->root_motion.keep_in_place)
            {
                pose.motion_result.root_position_weights.y = 0.0f;
                pose.motion_result.root_position_weights.x = 0.0f;
                pose.motion_result.root_position_weights.z = 0.0f;

                pose.motion_result.bone_position_weights.y = 1.0f;
                pose.motion_result.bone_position_weights.x = 0.0f;
                pose.motion_result.bone_position_weights.z = 0.0f;
            }

            pose.motion_state.root_position_time = time;
            pose.motion_state.root_position_at_time = position;
            pose.motion_result.root_transform_delta.set_position(delta_position);
        }

        if(int(node.desc.index) == anim_clip->root_motion.rotation_node_index)
        {
            pose.motion_result.root_rotation_node_index = anim_clip->root_motion.rotation_node_index;

            const auto& clip_start_rotation = channel.rotation_keys.front().value;
            const auto& clip_end_rotation = channel.rotation_keys.back().value;

            pose.motion_result.root_rotation_weight = {1.0f};
            pose.motion_result.bone_rotation_weight = {0.0f};

            if(pose.motion_state.root_rotation_time == seconds_t(0))
            {
                pose.motion_state.root_rotation_time = time;

                pose.motion_state.root_rotation_at_time = clip_start_rotation;
            }

            auto delta_rotation = rotation * glm::inverse(pose.motion_state.root_rotation_at_time);

            if(time < pose.motion_state.root_rotation_time)
            {
                auto loop_rotation_offset = clip_end_rotation * glm::inverse(clip_start_rotation);
                delta_rotation = loop_rotation_offset * delta_rotation;
            }

            if(anim_clip->root_motion.keep_rotation)
            {
                pose.motion_result.root_rotation_weight = 0.0f;
                pose.motion_result.bone_rotation_weight = 1.0f;

            }

            if(anim_clip->root_motion.keep_in_place)
            {
                pose.motion_result.root_rotation_weight = 0.0f;
                pose.motion_result.bone_rotation_weight = 1.0f;
            }

            pose.motion_state.root_rotation_time = time;
            pose.motion_state.root_rotation_at_time = rotation;
            pose.motion_result.root_transform_delta.set_rotation(delta_rotation);
        }
    }
}

auto animation_player::is_playing() const -> bool
{
    return playing_ && !paused_;
}

auto animation_player::is_paused() const -> bool
{
    return paused_;
}

} // namespace unravel
