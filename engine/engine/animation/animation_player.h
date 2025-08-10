#pragma once

#include "animation_blend_space.h"
#include <engine/assets/asset_handle.h>
#include <engine/rendering/model.h>

#include <hpp/variant.hpp>

namespace unravel
{

using blend_easing_t = std::function<float(float)>;

struct animation_state
{
    auto get_progress() const -> float
    {
        auto clp = clip.get();
        if(clp && clp->duration > animation_clip::seconds_t{0})
        {
            return elapsed / clp->duration;
        }

        return 0.0f;
    }
    void set_progress(float progress)
    {

        auto clp = clip.get();
        if(clp && clp->duration > animation_clip::seconds_t{0})
        {
            progress = math::clamp(progress, 0.0f, 1.0f);
            elapsed = clp->duration * progress;
        }
    }

    asset_handle<animation_clip> clip{};
    animation_clip::seconds_t elapsed{};
    bool loop{false};
    // Add blend space support
    std::shared_ptr<blend_space_def> blend_space{};
    std::vector<std::pair<asset_handle<animation_clip>, float>> blend_clips{};
    std::vector<animation_pose> blend_poses{};
};

struct blend_over_time
{
    animation_clip::seconds_t duration{};
    animation_clip::seconds_t elapsed{};

    auto get_progress() const -> float
    {
        // Compute the normalized blending time (clamped between 0 and 1)
        auto normalized_blend_time = static_cast<float>(elapsed.count() / duration.count());
        normalized_blend_time = std::clamp(normalized_blend_time, 0.0f, 1.0f);
        return normalized_blend_time;
    }
};

struct blend_over_param
{
    float param{};

    auto get_progress() const -> float
    {
        return param;
    }
};

struct blend_state
{
    bool loop{};
    blend_easing_t easing{math::linearInterpolation<float>};
    hpp::variant<hpp::monostate, blend_over_time, blend_over_param> state{};
};

/**
 * @brief Class responsible for playing animations on a skeletal mesh.
 *
 * This class handles the playback of animations, interpolating between keyframes
 * and applying the appropriate transformations to the nodes of a skeletal mesh.
 */
class animation_player
{
public:
    using seconds_t = animation_clip::seconds_t;
    using update_callback_t = std::function<void(const animation_pose::node_desc& desc,
                                                 const math::transform& abs,
                                                 const animation_pose::root_motion_result& motion_result)>;

    /**
     * @brief Blends to the animation over the specified time with the specified easing
     *
     * @param clip The animation to blend to.
     * @param duration The duration over which the blending must complete.
     * @param easing The easing function used.
     */

    void blend_to(size_t layer_idx,
                  const asset_handle<animation_clip>& clip,
                  seconds_t duration = seconds_t(0.3),
                  bool loop = true,
                  bool phase_sync = false,
                  const blend_easing_t& easing = math::linearInterpolation<float>);

    void clear(size_t layer_idx);

    void set_blend_space(size_t layer_idx, const std::shared_ptr<blend_space_def>& blend_space, bool loop = true);

    void set_blend_space_parameters(size_t layer_idx, const std::vector<float>& params);
    /**
     * @brief Starts or resumes the animation playback.
     */
    auto play() -> bool;

    /**
     * @brief Pauses the animation playback.
     */
    void pause();

    /**
     * @brief Resumes the animation playback.
     */
    void resume();

    /**
     * @brief Stops the animation playback and resets the time.
     */
    void stop();

    /**
     * @brief Updates the animation player, advancing the animation time and applying transformations.
     *
     * @param delta_time The time to advance the animation by.
     * @param set_transform_callback The callback function to set the transform of a node.
     */
    auto update_time(seconds_t delta_time, bool force = false) -> bool;
    void update_poses(const animation_pose& ref_pose,
                      const update_callback_t& set_transform_callback);

    /**
     * @brief Returns whether the animation is currently playing.
     *
     * @return True if the animation is playing, false otherwise.
     */
    auto is_playing() const -> bool;

    /**
     * @brief Returns whether the animation is currently paused.
     *
     * @return True if the animation is paused, false otherwise.
     */
    auto is_paused() const -> bool;

private:
    struct animation_layer_state
    {
        auto is_valid() const -> bool
        {
            return state.clip || state.blend_space;
        }
        /// Current state
        animation_pose pose{};
        animation_state state{};
        std::vector<float> parameters;
    };

    struct animation_layer
    {
        auto get_final_pose() const -> const animation_pose*
        {
            auto final_pose = &current_state.pose;

            if(target_state.is_valid())
            {
                final_pose = &blend_pose;
            }

            return final_pose;
        }

        animation_layer_state current_state{};
        animation_layer_state target_state{};

        /// Blended state
        animation_pose blend_pose{};
        blend_state blending_state{};
    };

    auto get_layer(size_t index) -> animation_layer&;

    void sample_animation(const animation_clip* anim_clip,
                          seconds_t time,
                          animation_pose& pose) const noexcept;
    auto compute_blend_factor(const animation_layer& layer, float normalized_blend_time) noexcept -> float;
    void update_state(seconds_t delta_time, animation_state& state);
    auto get_blend_progress(const animation_layer& layer) const -> float;
    auto update_pose(animation_layer_state& layer)
        -> bool;

    std::vector<animation_layer> layers_;

    bool playing_{};
    bool paused_{};
};

} // namespace unravel
