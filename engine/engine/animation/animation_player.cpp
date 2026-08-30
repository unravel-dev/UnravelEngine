#include "animation_player.h"
#include <hpp/utility/overload.hpp>

#include <limits>

namespace unravel
{

namespace
{
/// Horizontal start-to-end displacement below which a root channel counts as
/// non-traveling for keep_in_place (idles, dances): its full animation is kept
/// instead of projecting out the travel direction.
constexpr float IN_PLACE_MIN_TRAVEL = 1e-2f;

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
        // A null clip means "stop animating this layer" - clear the in-flight
        // crossfade target as well, not just the current state.
        layer = {};
        return;
    }

    // Already blending TO this clip - nothing to do (but keep loop flag fresh).
    if(layer.target_state.state.clip == clip)
    {
        layer.target_state.state.loop = loop;
        return;
    }

    // Caller wants to go back to the clip we're currently blending AWAY from.
    if(layer.current_state.state.clip == clip)
    {
        // No active crossfade? Already fully on this clip, nothing to do.
        if(!layer.target_state.state.clip)
        {
            layer.current_state.state.loop = loop;
            return;
        }

        // Active crossfade - reverse direction. Swap current<->target so we
        // are now blending BACK toward the caller's requested clip, and
        // flip the blend progress so the visible pose stays continuous.
        std::swap(layer.current_state, layer.target_state);
        layer.current_state.state.loop = loop;

        if(auto* bot = std::get_if<blend_over_time>(&layer.blending_state.state))
        {
            float progress_before = 0.0f;
            if(bot->duration.count() > 0.0f)
            {
                progress_before = float(bot->elapsed.count() / bot->duration.count());
                progress_before = std::clamp(progress_before, 0.0f, 1.0f);
            }

            if(progress_before > 1e-4f)
            {
                // Stretch `duration` so remaining wall-clock time == caller's
                // requested duration, while starting at mirrored progress
                // (1 - progress_before) for visual continuity.
                auto stretched = seconds_t(duration.count() / progress_before);
                bot->duration = stretched;
                bot->elapsed  = seconds_t(stretched.count() * (1.0f - progress_before));
            }
            else
            {
                bot->duration = duration;
                bot->elapsed  = seconds_t(0);
            }
        }
        else
        {
            // Shouldn't happen given target was set, but be safe.
            layer.blending_state.state = blend_over_time{duration};
        }
        layer.blending_state.easing = easing;
        return;
    }

    // Fresh blend: start a new crossfade from current to the requested clip.
    auto phase = phase_sync ? layer.current_state.state.get_progress() : 0.0f;

    // If a crossfade is already in flight, freeze the currently visible
    // blended pose as the new source so retargeting mid-blend does not pop.
    if(layer.target_state.is_valid())
    {
        layer.current_state.state = {};
        layer.current_state.parameters.clear();
        layer.current_state.pose = layer.blend_pose;
        // The frozen pose is static: zero its root-motion delta so the last
        // frame's motion is not re-applied every frame while it fades out.
        layer.current_state.pose.motion_result.root_transform_delta = {};
        layer.current_state.pose.motion_state = {};
    }

    layer.target_state = {};
    layer.target_state.state.clip = clip;
    layer.target_state.state.loop = loop;
    layer.target_state.state.set_progress(phase);

    auto clip_ptr = clip.get();
    if(clip_ptr && duration > clip_ptr->duration)
    {
        duration = clip_ptr->duration;
    }

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
    layer.current_state.state.loop_count = 0;
    layer.current_state.state.blend_clips.clear();
    layer.current_state.state.blend_poses.clear();
    layer.current_state.state.blend_weights.clear();
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
        layer.current_state.state.loop_count = 0;
        // Reset root-motion tracking, otherwise the first sample of the next
        // playback computes a delta against the stale end-of-play position and
        // teleports the entity.
        layer.current_state.pose.motion_state = {};
        layer.target_state.state.elapsed = seconds_t(0);
        layer.target_state.state.loop_count = 0;
        layer.target_state.pose.motion_state = {};
    }
}

auto animation_player::update_time(seconds_t delta_time, bool force) -> bool
{
    // `force` steps time even when stopped or paused (editor frame stepping).
    if(!is_playing() && !force)
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

void animation_player::update_poses(const animation_pose& ref_pose,
                                    animation_retargeting_mode retargeting_mode,
                                    bool extract_root_motion,
                                    const update_callback_t& set_transform_callback)
{
    if(layers_.empty())
    {
        return;
    }

    for(auto& layer : layers_)
    {
        // Update current layer
        update_pose(layer.current_state, retargeting_mode, extract_root_motion);

        // Update target layer
        if(update_pose(layer.target_state, retargeting_mode, extract_root_motion))
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

auto animation_player::update_pose(animation_layer_state& layer,
                                   animation_retargeting_mode retargeting_mode,
                                   bool extract_root_motion) -> bool
{
    auto& state = layer.state;
    auto& pose = layer.pose;
    auto& parameters = layer.parameters;

    if(state.blend_space)
    {
        // Compute blending weights based on current parameters (e.g., speed and direction)
        state.blend_space->compute_blend(parameters, state.blend_clips);

        // Sample animations and blend poses. The per-clip poses persist across
        // frames so each keeps its own root-motion tracking state.
        state.blend_poses.resize(state.blend_clips.size());
        state.blend_weights.resize(state.blend_clips.size());
        for(size_t i = 0; i < state.blend_clips.size(); ++i)
        {
            const auto& clip_weight_pair = state.blend_clips[i];
            sample_animation(clip_weight_pair.first.get().get(),
                             state.elapsed,
                             state.loop_count,
                             retargeting_mode,
                             extract_root_motion,
                             state.blend_poses[i]);
            state.blend_weights[i] = clip_weight_pair.second;
        }

        if(state.blend_poses.empty())
        {
            pose.nodes.clear();
            return true;
        }

        // Multiway merge into a pose that does not alias any input - blending
        // in place would clear the accumulator before it is read.
        blend_poses(state.blend_poses, state.blend_weights, pose);
        return true;
    }

    if(state.clip)
    {
        sample_animation(state.clip.get().get(), state.elapsed, state.loop_count, retargeting_mode, extract_root_motion, pose);
        return true;
    }

    return false;
}

void animation_player::update_state(seconds_t delta_time, animation_state& state)
{
    if(!state.clip && !state.blend_space)
    {
        return;
    }
    state.elapsed += delta_time;
    const auto duration = get_state_duration(state);
    if(duration <= seconds_t(0))
    {
        // Blend-space duration is unknown until the first pose update fills
        // blend_clips - keep accumulating and wrap on a later frame.
        return;
    }
    if(state.elapsed > duration)
    {
        if(state.loop)
        {
            // Count every wrap so root motion stays exact even when several
            // loops pass between two pose samples.
            state.loop_count += uint64_t(state.elapsed.count() / duration.count());
            state.elapsed = seconds_t(std::fmod(state.elapsed.count(), duration.count()));
        }
        else
        {
            state.elapsed = duration;
        }
    }
}

auto animation_player::get_state_duration(const animation_state& state) -> seconds_t
{
    if(state.clip)
    {
        auto clip = state.clip.get();
        return clip ? clip->duration : seconds_t(0);
    }
    // A blend space plays as long as its longest active clip; shorter clips
    // clamp to their last key when sampled beyond their duration.
    seconds_t duration{0};
    for(const auto& clip_weight_pair : state.blend_clips)
    {
        auto clip = clip_weight_pair.first.get();
        if(clip)
        {
            duration = std::max(duration, clip->duration);
        }
    }
    return duration;
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

auto animation_player::compute_blend_factor(const animation_layer& layer, float normalized_blend_time) -> float
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
                                        uint64_t loop_count,
                                        animation_retargeting_mode retargeting_mode,
                                        bool extract_root_motion,
                                        animation_pose& pose) const
{
    if(!anim_clip)
    {
        return;
    }
    pose.nodes.clear();
    pose.nodes.reserve(anim_clip->channels.size());

    for(const auto& channel : anim_clip->channels)
    {
        // Empty tracks need explicit defaults: a value-initialized quat is all
        // zeros (NaN after normalize) and a zero scale collapses the bone.
        math::vec3 position = channel.position_keys.empty() ? math::vec3{0.0f, 0.0f, 0.0f}
                                                            : interpolate(channel.position_keys, time);
        math::quat rotation = channel.rotation_keys.empty() ? math::identity<math::quat>()
                                                            : interpolate(channel.rotation_keys, time);
        math::vec3 scaling = channel.scaling_keys.empty() ? math::vec3{1.0f, 1.0f, 1.0f}
                                                          : interpolate(channel.scaling_keys, time);

        auto& node = pose.nodes.emplace_back();
        
        // Store both index and name - callback will resolve based on retargeting mode
        node.desc.index = channel.node_index;
        node.desc.name = channel.node_name;
        node.transform.set_position(position);
        node.transform.set_rotation(rotation);
        node.transform.set_scale(scaling);


        // Root motion comparison based on retargeting mode
        bool is_root_position_node = false;
        bool is_root_rotation_node = false;
        
        if(retargeting_mode == animation_retargeting_mode::name_based)
        {
            // Name-based comparison
            if(node.desc.name == anim_clip->root_motion.position_node_name)
            {
                is_root_position_node = true;
            }
            if(node.desc.name == anim_clip->root_motion.rotation_node_name)
            {
                is_root_rotation_node = true;
            }
        }
        else
        {
            // Index-based comparison (default)
            if(int(node.desc.index) == anim_clip->root_motion.position_node_index)
            {
                is_root_position_node = true;
            }
            if(int(node.desc.index) == anim_clip->root_motion.rotation_node_index)
            {
                is_root_rotation_node = true;
            }
        }

        if(is_root_position_node && !channel.position_keys.empty())
        {
            pose.motion_result.root_position_node_index = anim_clip->root_motion.position_node_index;
            pose.motion_result.root_position_node_name = anim_clip->root_motion.position_node_name;

            pose.motion_result.root_position_weights = {1.0f, 1.0f, 1.0f};
            pose.motion_result.bone_position_weights = {0.0f, 0.0f, 0.0f};

            auto& motion_state = pose.motion_state;
            if(!motion_state.root_position_initialized)
            {
                // First sample: no motion yet. Baselining against the current
                // position (not the clip start) keeps phase-synced blend
                // starts from producing a start-to-phase teleport.
                motion_state.root_position_initialized = true;
                motion_state.root_position_at_time = position;
                motion_state.root_position_loop_count = loop_count;
            }

            auto delta_position = position - motion_state.root_position_at_time;

            const auto wraps = loop_count - motion_state.root_position_loop_count;
            if(wraps > 0)
            {
                const auto& clip_start_pos = channel.position_keys.front().value;
                const auto& clip_end_pos = channel.position_keys.back().value;
                delta_position += float(wraps) * (clip_end_pos - clip_start_pos);
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
            }

            // Unless keep_position_xz explicitly leaves the raw travel in the
            // pose, replace the pose's horizontal position with a value that is
            // meaningful WITHOUT its weight mask, and write it at bone weight 1.
            // Pose blending knows nothing about per-pose weights, so a raw
            // traveling position masked by a zero weight (the previous scheme)
            // leaked into any crossfade against a clip whose weight is 1 - seen
            // as the model lunging forward, then sliding back as the blend
            // completed, whenever clips disagreed on root-motion settings.
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
            // pinning would erase the authored motion (a Die clip froze
            // upright); keep the raw channel in the pose instead.
            if(anim_clip->root_motion.keep_in_place ||
               (extract_root_motion && !anim_clip->root_motion.keep_position_xz))
            {
                pose.motion_result.bone_position_weights.x = 1.0f;
                pose.motion_result.bone_position_weights.z = 1.0f;

                const auto& first_key = channel.position_keys.front();
                const auto& last_key = channel.position_keys.back();
                const float key_span = (last_key.time - first_key.time).count();
                math::vec3 pose_position = first_key.value;
                if(anim_clip->root_motion.keep_in_place && key_span > 0.0f)
                {
                    const float progress = math::clamp((time - first_key.time).count() / key_span, 0.0f, 1.0f);
                    math::vec3 travel = last_key.value - first_key.value;
                    travel.y = 0.0f;
                    math::vec3 residual = position - first_key.value - travel * progress;
                    const float travel_len = math::length(travel);
                    if(travel_len > IN_PLACE_MIN_TRAVEL)
                    {
                        const math::vec3 travel_dir = travel / travel_len;
                        residual -= travel_dir * math::dot(residual, travel_dir);
                    }
                    pose_position = first_key.value + residual;
                }
                pose_position.y = position.y;
                node.transform.set_position(pose_position);
            }

            motion_state.root_position_at_time = position;
            motion_state.root_position_loop_count = loop_count;
            pose.motion_result.root_transform_delta.set_position(delta_position);
        }

        if(is_root_rotation_node && !channel.rotation_keys.empty())
        {
            pose.motion_result.root_rotation_node_index = anim_clip->root_motion.rotation_node_index;
            pose.motion_result.root_rotation_node_name = anim_clip->root_motion.rotation_node_name;

            pose.motion_result.root_rotation_weight = {1.0f};
            pose.motion_result.bone_rotation_weight = {0.0f};

            auto& motion_state = pose.motion_state;
            if(!motion_state.root_rotation_initialized)
            {
                motion_state.root_rotation_initialized = true;
                motion_state.root_rotation_at_time = rotation;
                motion_state.root_rotation_loop_count = loop_count;
            }

            math::quat delta_rotation;
            const auto wraps = loop_count - motion_state.root_rotation_loop_count;
            if(wraps == 0)
            {
                delta_rotation = rotation * glm::inverse(motion_state.root_rotation_at_time);
            }
            else
            {
                const auto& clip_start_rotation = channel.rotation_keys.front().value;
                const auto& clip_end_rotation = channel.rotation_keys.back().value;
                // Compose the delta across the wrap(s) in playback order:
                // previous sample -> clip end, then (wraps - 1) full loops,
                // then clip start -> current sample.
                const auto loop_rotation_offset = clip_end_rotation * glm::inverse(clip_start_rotation);
                delta_rotation = clip_end_rotation * glm::inverse(motion_state.root_rotation_at_time);
                for(uint64_t i = 1; i < wraps; ++i)
                {
                    delta_rotation = loop_rotation_offset * delta_rotation;
                }
                delta_rotation = (rotation * glm::inverse(clip_start_rotation)) * delta_rotation;
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
            else if(extract_root_motion && !anim_clip->root_motion.keep_rotation)
            {
                // Extraction takes the FULL per-frame rotation change into the
                // root delta, so the pose's own rotation must be the constant
                // clip-start baseline written at bone weight 1 - not the raw
                // traveling rotation masked by a zero weight. Pose blending
                // ignores per-pose weights, so the masked raw value leaked
                // into crossfades against kept-rotation clips: the same class
                // of bug as the position leak, seen as a heading twitch when
                // blending a rotation-extracted clip (falls, dances) with a
                // kept-rotation locomotion clip. As with position, the
                // baseline only holds while the delta is applied - with root
                // motion off the raw rotation stays in the pose.
                pose.motion_result.bone_rotation_weight = 1.0f;
                node.transform.set_rotation(channel.rotation_keys.front().value);
            }

            motion_state.root_rotation_at_time = rotation;
            motion_state.root_rotation_loop_count = loop_count;
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
