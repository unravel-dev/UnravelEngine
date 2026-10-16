#pragma once

#include "animation_blend_space.h"
#include "animation_humanoid.h"
#include <engine/assets/asset_handle.h>
#include <engine/rendering/model.h>

#include <hpp/span.hpp>
#include <hpp/variant.hpp>

#include <cmath>
#include <functional>
#include <optional>

namespace unravel
{

/// Layers a player can have: an index from outside (scripts) beyond it is rejected rather than
/// growing the layer list to reach it.
constexpr size_t MAX_ANIMATION_LAYERS = 32;

using blend_easing_t = std::function<float(float)>;

struct animation_state
{
    using seconds_t = animation_clip::seconds_t;

    /// Normalized position (0..1) within the current cycle.
    auto get_progress() const -> float
    {
        if(blend_space)
        {
            return phase;
        }
        if(pending_progress)
        {
            return *pending_progress;
        }
        // Resolved by the time step; a state not stepped yet looks the clip up.
        const auto clp = loaded_clip ? loaded_clip : clip.try_get();
        if(clp && clp->duration > animation_clip::seconds_t{0})
        {
            return elapsed / clp->duration;
        }
        return 0.0f;
    }
    void set_progress(float progress)
    {
        progress = std::isfinite(progress) ? math::clamp(progress, 0.0f, 1.0f) : 0.0f;
        if(blend_space)
        {
            phase = progress;
            begin_step();
            return;
        }
        // Never waits: a clip still loading takes the progress once the time step resolves it.
        const auto clp = loaded_clip ? loaded_clip : clip.try_get();
        if(clp && clp->duration > animation_clip::seconds_t{0})
        {
            elapsed = clp->duration * progress;
            pending_progress.reset();
            begin_step();
            return;
        }
        pending_progress = progress;
    }

    /// Marks the current position as where the next time step begins.
    void begin_step()
    {
        step_begin_elapsed = elapsed;
        step_begin_phase = phase;
        step_begin_loop_count = loop_count;
    }

    /// Cycle count plus progress: the absolute position events and exit times are measured on.
    auto get_cycle_position() const -> double
    {
        return double(loop_count) + double(get_progress());
    }

    /// Restarts event tracking at the current position. `inclusive` reports a marker sitting exactly
    /// there on the next step (a fresh start); seeking passes false.
    void reset_event_window(bool inclusive = true)
    {
        event_cursor = get_cycle_position();
        event_cursor_inclusive = inclusive;
        event_window_valid = false;
    }

    asset_handle<animation_clip> clip{};
    /// The clip as the last time step resolved it (without waiting); null while it loads.
    std::shared_ptr<animation_clip> loaded_clip{};
    /// Time on the clip (clip states).
    animation_clip::seconds_t elapsed{};
    /// Progress set while the clip was still loading; the time step applies it to `elapsed`.
    std::optional<float> pending_progress{};
    /// Normalized phase shared by every clip of a blend space (blend-space states).
    float phase{};
    /// Number of times the state's time has wrapped since (re)start. Root
    /// motion uses this to account for every loop, even when several loops
    /// pass between two pose samples (long hitches, renderer-based culling).
    uint64_t loop_count{};
    /// Position the last time step began at (or a seek or start set): root-motion tracking that
    /// starts after it measures from there.
    animation_clip::seconds_t step_begin_elapsed{};
    float step_begin_phase{};
    uint64_t step_begin_loop_count{};
    bool loop{false};
    /// Playback rate of this state, multiplied with the component speed.
    float speed{1.0f};

    /// Cycle position up to which events were reported, and whether the next window includes it
    /// (right after a (re)start, so a marker on the first frame is not lost).
    double event_cursor{};
    bool event_cursor_inclusive{true};
    /// Cycle positions covered by the last time step: events in (begin, end], or [begin, end] when
    /// inclusive. Invalid when the step did not advance time.
    double event_window_begin{};
    double event_window_end{};
    bool event_window_inclusive{};
    bool event_window_valid{};

    std::shared_ptr<blend_space_def> blend_space{};
    /// Clips of a blend space with a share of the blend and their normalized weights, refreshed
    /// every time step, and the clips those resolved to (null while one loads).
    std::vector<blend_space_weight> blend_clips{};
    std::vector<std::shared_ptr<animation_clip>> blend_loaded_clips{};
    /// One sampled pose per entry of blend_clips.
    std::vector<animation_pose> blend_poses{};
    std::vector<float> blend_weights{};
    /// Root-motion tracking per blend point: a point keeps its own while others enter and leave
    /// the blend, and starts over when it comes back.
    std::vector<animation_pose::root_motion_state> point_motion_states{};
    /// Weight-averaged duration of the active clips: one cycle of the blend space.
    animation_clip::seconds_t blend_duration{};
};

struct blend_over_time
{
    animation_clip::seconds_t duration{};
    animation_clip::seconds_t elapsed{};

    auto get_progress() const -> float
    {
        // A degenerate blend duration completes immediately (avoids 0/0 -> NaN,
        // which would never satisfy the >= 1 completion check).
        if(duration.count() <= 0.0f)
        {
            return 1.0f;
        }
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
    /// The target's weight when the crossfade began: eased from here to 1. A reversed crossfade
    /// starts from the weight on screen, so it continues without a jump whatever the easings.
    float start_weight{};
};

/**
 * @brief How a new motion enters a layer.
 */
struct animation_play_params
{
    using seconds_t = animation_clip::seconds_t;

    /// Crossfade length. Zero switches on the next pose update.
    seconds_t duration{0.3f};
    bool loop{true};
    /// Start at the outgoing state's normalized progress (locomotion cycles that share foot timing).
    bool phase_sync{false};
    /// Normalized start position when not phase synced.
    float start_progress{0.0f};
    /// Playback rate of the new state.
    float speed{1.0f};
    /// Crossfade even when the motion already plays on the layer (a state transitioning to itself).
    bool restart{false};
    blend_easing_t easing{math::linearInterpolation<float>};
};

/**
 * @brief An animation event crossed during the last time step.
 */
struct animation_event_hit
{
    animation_event event{};
    asset_handle<animation_clip> clip{};
    size_t layer{};
    /// Effective weight of the clip when the event fired.
    float weight{};
};

/**
 * @brief The skeleton a player retargets onto: per target index the parent, the bind (rest) local
 * transform and the accumulated rest global transform. Parents must precede their children.
 */
struct animation_target_skeleton
{
    std::vector<std::string> names;
    std::vector<int> parents;
    std::vector<math::transform> bind_local;
    std::vector<math::transform> rest_global;
    /// Humanoid roles of the bones, for clips whose bone names differ from this skeleton's.
    humanoid_map humanoid = make_empty_humanoid_map();

    /// Takes the bones' names and parents, and bind_local from `bind_pose` (a pose over the same
    /// bone indices); a bone the pose leaves out binds at identity.
    void set_bones(std::vector<std::string> bone_names, std::vector<int> bone_parents, const animation_pose& bind_pose);
    /// Fills rest_global from parents and bind_local.
    void compute_rest_globals();
    /// Fills humanoid from names, parents and rest_global (call compute_rest_globals first).
    void compute_humanoid_bones();
};

/**
 * @brief The humanoid roles of the skeleton a clip was authored on, as channel indices.
 *
 * What rest-aligned retargeting matches a clip's bones by when their names differ from the target's.
 * All -1 for clips imported without their source rest pose.
 */
auto find_clip_humanoid_bones(const animation_clip& clip) -> humanoid_map;

/**
 * @brief Maps clip channels onto a target skeleton.
 *
 * The player samples every clip into target-skeleton indices, so poses from clips exported from
 * different files (whose channel indices disagree) blend bone by bone, and layer masks index the
 * target skeleton. `resolve` returns the target index of a channel, or -1 to skip the channel.
 * `version` must change whenever the target skeleton changes; bindings cached against an older
 * version are rebuilt.
 */
struct animation_skeleton_binding
{
    uint64_t version{};
    std::function<int(const animation_channel& channel)> resolve;
    /// When set, clips that recorded their source rest pose are retargeted onto this skeleton
    /// rest-aligned: every bone turns in the world by what its source bone turned from the
    /// source rest pose, bone lengths stay the target's and motion scales with the hip height.
    std::shared_ptr<const animation_target_skeleton> rest_aligned_target;
};

/**
 * @brief Class responsible for playing animations on a skeletal mesh.
 *
 * This class handles the playback of animations, interpolating between keyframes
 * and applying the appropriate transformations to the nodes of a skeletal mesh.
 *
 * Layer 0 is the base layer. Every other layer combines with the result below it by its weight,
 * blend mode and bone mask.
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

    /**
     * @brief Crossfades a layer to a clip.
     */
    void play_clip(size_t layer_idx, const asset_handle<animation_clip>& clip, const animation_play_params& params);

    /**
     * @brief Crossfades a layer to a blend space. Parameters for the new state are set with
     * set_state_parameters(layer, true, ...) while the crossfade runs.
     */
    void play_blend_space(size_t layer_idx,
                          const std::shared_ptr<blend_space_def>& blend_space,
                          const animation_play_params& params);

    void clear(size_t layer_idx);

    void set_blend_space(size_t layer_idx, const std::shared_ptr<blend_space_def>& blend_space, bool loop = true);

    void set_blend_space_parameters(size_t layer_idx, const std::vector<float>& params);

    /**
     * @brief Sets the blend-space parameters of the layer's current state, or of its crossfade
     * target when `target` is true.
     */
    void set_state_parameters(size_t layer_idx, bool target, hpp::span<const float> params);

    /**
     * @brief Sets the playback rate of the layer's current state, or of its crossfade target.
     */
    void set_state_speed(size_t layer_idx, bool target, float speed);

    /// Layer weight, 0..1. The base layer always counts as fully weighted.
    void set_layer_weight(size_t layer_idx, float weight);
    auto get_layer_weight(size_t layer_idx) const -> float;

    void set_layer_blend_mode(size_t layer_idx, animation_layer_blend_mode mode);
    auto get_layer_blend_mode(size_t layer_idx) const -> animation_layer_blend_mode;

    /**
     * @brief Per-bone weights of a layer, indexed by target skeleton index. Empty means every bone
     * at weight 1; bones past the end of the list count as 0.
     */
    void set_layer_bone_mask(size_t layer_idx, std::vector<float> weights);
    auto get_layer_bone_mask(size_t layer_idx) const -> const std::vector<float>&;

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
     * @brief Advances the animation time of every layer and records the events crossed.
     *
     * @param delta_time The time to advance the animation by.
     * @param force Advance even when not playing or paused (editor frame stepping).
     * @return True if time advanced and poses should be updated.
     */
    auto update_time(seconds_t delta_time, bool force = false) -> bool;
    /**
     * @brief Samples, blends and emits the final pose for every layer.
     *
     * @param binding Maps clip channels to target skeleton indices. Pass an empty binding to use
     * the channels' own indices (index-based retargeting).
     * @param extract_root_motion True when the caller consumes
     * root_transform_delta (applies root motion). Extraction rewrites the
     * root channel's pose to the constant clip-start baseline, which is only
     * valid when the extracted delta is applied elsewhere; pass false to keep
     * the authored root motion in the pose instead.
     */
    void update_poses(const animation_pose& ref_pose,
                      const animation_skeleton_binding& binding,
                      bool extract_root_motion,
                      const update_callback_t& set_transform_callback);

    /**
     * @brief Index-based overload kept for callers that sample clips onto their source skeleton.
     */
    void update_poses(const animation_pose& ref_pose,
                      animation_retargeting_mode retargeting_mode,
                      bool extract_root_motion,
                      const update_callback_t& set_transform_callback);

    /**
     * @brief Events crossed during the last update_time, in layer order.
     */
    auto get_fired_events() const -> const std::vector<animation_event_hit>&;

    /**
     * @brief Drops the recorded events once they were delivered, so a frame that does not advance
     * the player (an unrendered panel) cannot deliver them again.
     */
    void clear_fired_events();

    /**
     * @brief Blended curve values of the last update_poses, sorted by curve id.
     */
    auto get_curve_values() const -> const std::vector<animation_curve_value>&;

    /**
     * @brief Blended value of a curve, or `fallback` when no playing clip has it.
     */
    auto get_curve_value(uint32_t curve_id, float fallback = 0.0f) const -> float;
    auto has_curve(uint32_t curve_id) const -> bool;
    /// The blended value of a curve, or nothing when no playing clip has it.
    auto find_curve_value(uint32_t curve_id) const -> std::optional<float>;

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

    /**
     * @brief State of a single pose source feeding a layer (either the current
     * state or the crossfade target).
     */
    struct animation_layer_state
    {
        auto is_valid() const -> bool
        {
            return state.clip || state.blend_space;
        }
        /// First-frame pose of one of the state's clips: the reference an additive layer subtracts.
        struct reference_pose
        {
            /// The clip sampled; a weak reference, so a clip reloaded at the same address is not it.
            std::weak_ptr<const animation_clip> clip;
            uint64_t binding_version{};
            animation_pose pose;
        };

        /// Current state
        animation_pose pose{};
        animation_state state{};
        std::vector<float> parameters;
        /// Indexed like the state's clips: 0 for a clip state, the point index for a blend space.
        std::vector<reference_pose> reference_poses;
    };

    struct animation_layer
    {
        auto get_final_pose() const -> const animation_pose*
        {
            // A target that was never posed (its clip still loads) is not shown yet.
            return target_state.is_valid() && blend_pose_valid ? &blend_pose : &current_state.pose;
        }

        auto has_motion() const -> bool
        {
            return current_state.is_valid() || target_state.is_valid() || !current_state.pose.nodes.empty();
        }

        animation_layer_state current_state{};
        animation_layer_state target_state{};

        /// Blended state
        animation_pose blend_pose{};
        /// blend_pose was computed for the crossfade in flight (false until its first pose update).
        bool blend_pose_valid{};
        blend_state blending_state{};

        float weight{1.0f};
        animation_layer_blend_mode blend_mode{animation_layer_blend_mode::override_pose};
        std::vector<float> bone_mask;

        /// Fade of the layer's whole contribution: a layer above the base fades in when it starts
        /// from nothing and fades out when its motion is stopped with a duration.
        float motion_weight{1.0f};
        /// Change of motion_weight per second of playback; 0 when no fade runs.
        float motion_weight_rate{0.0f};
        bool stopping{false};
    };

    /**
     * @brief Read-only view over the player's layers for tooling and inspection.
     */
    auto get_layers() const -> const std::vector<animation_layer>&;

    /**
     * @brief Returns the normalized progress (0..1) of the layer's active
     * crossfade, or 0 when no blend is in flight.
     */
    auto get_blend_progress(const animation_layer& layer) const -> float;

    /**
     * @brief Returns the eased crossfade weight of the layer's target state (0..1), or 0 when no
     * crossfade is in flight.
     */
    auto get_blend_weight(const animation_layer& layer) const -> float;

    /**
     * @brief Returns the crossfade weight of the layer's target state at `normalized_blend_time`
     * (0..1): the easing taken from the crossfade's start weight to 1.
     */
    auto compute_blend_factor(const animation_layer& layer, float normalized_blend_time) const -> float;

    /**
     * @brief Returns the playback duration of one cycle of a state: the clip's duration, or the
     * weight-averaged duration of the blend space's active clips (0 until first updated).
     */
    static auto get_state_duration(const animation_state& state) -> seconds_t;

    /**
     * @brief Starts root-motion tracking over, so the next pose sample measures no motion: for a
     * character whose poses were not sampled (renderer-based culling), so it does not jump by
     * everything it missed when it is seen again.
     */
    void reset_root_motion();

    /**
     * @brief Seeks a layer state to a normalized progress (0..1) without
     * changing playback status. Does nothing for out-of-range layers or states
     * whose duration is not known yet. Pair with request_pose_refresh() to see
     * the seeked pose while stopped or paused.
     */
    void seek(size_t layer_idx, float progress, bool seek_target = false);

    /**
     * @brief Requests a one-shot pose refresh: the next update_time behaves as
     * if playing but advances by zero time, so poses are re-sampled and
     * re-applied at the current playback position even while stopped or paused
     * (editor seeking / stepping).
     */
    void request_pose_refresh();

private:
    /// How a clip's root-motion node splits its motion between the pose and the entity.
    struct root_motion_frame
    {
        /// Turns the node's keys upright (+Y up): its parent's rest rotation in the clip root.
        math::quat up{math::identity<math::quat>()};
        /**
         * Extraction holds the node at its rest pose rather than at the clip's first frame. Set for
         * a root bone at the floor, whose rest is the skeleton's origin: the clips of a combo start
         * where the previous one ended, and held at their first frames the body would stand off
         * the entity. Hips keep their first frame, which carries the clip's stance.
         */
        bool locks_to_rest{};
    };

    /// Target-skeleton mapping of one clip, cached per binding version.
    struct clip_binding
    {
        /// The clip mapped; a weak reference, so a clip reloaded at the same address is not it.
        std::weak_ptr<const animation_clip> clip;
        size_t channel_count{};
        uint64_t version{};
        /// Channels in ascending target index, paired with their target index.
        std::vector<std::pair<uint32_t, uint32_t>> ordered_channels;
        /// Target index per channel (-1: unmapped) and channel per target index (-1: none).
        std::vector<int> channel_targets;
        std::vector<int> target_channels;
        size_t max_target_index{};
        /// The channels carrying root motion, or -1.
        int root_position_channel{-1};
        int root_rotation_channel{-1};
        /// Target bones root motion is reported against: the root channel's own, or for a root the
        /// target lacks (a floor root on a skeleton that starts at the hips) the bone of its first
        /// mapped descendant. -1 without root motion.
        int root_position_target{-1};
        int root_rotation_target{-1};
        /// The rotation root is an ancestor of the position root: the position keys turn with the
        /// extracted heading already (they live in its frame).
        bool position_turns_with_rotation_root{};
        /// Rest-aligned retargeting data, valid when rest_aligned.
        bool rest_aligned{};
        std::vector<math::transform> source_rest_global;
        float height_ratio{1.0f};
        /// Turns source root-frame directions into target root-frame directions.
        math::quat frame_rotation{math::identity<math::quat>()};
        root_motion_frame root_position_frame;
        root_motion_frame root_rotation_frame;
        /// Identity of the source skeleton (channel names, parents, rest poses): clips with equal
        /// values sample one skeleton. Valid when rest_aligned.
        size_t source_skeleton_hash{};
        /// The retarget turns and scales each channel's local transform by constants only, so clips
        /// of one skeleton that all retarget linearly blend before the retarget to the same pose as
        /// after it. Valid when rest_aligned.
        bool is_linear_retarget{};
    };

    /// The channels a source sample took root motion from, or -1.
    struct root_channels
    {
        int position{-1};
        int rotation{-1};
    };

    /// Per-sample buffers of rest-aligned retargeting, reused across frames.
    struct retarget_scratch
    {
        std::vector<math::transform> source_local;
        /// The frame each channel's keys live in (its parent's global and parent offset).
        std::vector<math::transform> source_parent_global;
        std::vector<math::transform> source_global;
        std::vector<math::transform> target_global;
        /// One source pose per clip of a blend sampled in its source skeleton.
        std::vector<std::vector<math::transform>> point_source_local;
    };

    auto get_layer(size_t index) -> animation_layer&;
    void start_motion(size_t layer_idx, animation_state&& new_state, const animation_play_params& params);
    void stop_motion(size_t layer_idx, seconds_t duration);
    static void clear_motion(animation_layer& layer);
    void update_motion_weight(animation_layer& layer, seconds_t delta_time);

    auto find_binding(const std::shared_ptr<animation_clip>& clip, const animation_skeleton_binding& binding)
        -> const clip_binding&;
    static auto make_root_motion_frame(const animation_clip& clip, int channel_index) -> root_motion_frame;

    void sample_animation(const std::shared_ptr<animation_clip>& anim_clip,
                          seconds_t time,
                          uint64_t loop_count,
                          const animation_skeleton_binding& binding,
                          bool extract_root_motion,
                          bool track_root_motion,
                          animation_pose& pose);
    void sample_rest_aligned(const animation_clip& anim_clip,
                             const clip_binding& binding,
                             const animation_target_skeleton& target,
                             seconds_t time,
                             uint64_t loop_count,
                             bool extract_root_motion,
                             bool track_root_motion,
                             animation_pose& pose);
    /// Samples every channel of a clip in its own skeleton (the rest pose where a track has no
    /// keys), with root motion extracted into pose.motion_result, measured in the source frame.
    auto sample_source_pose(const animation_clip& anim_clip,
                            const clip_binding& binding,
                            seconds_t time,
                            uint64_t loop_count,
                            bool extract_root_motion,
                            bool track_root_motion,
                            std::vector<math::transform>& source_local,
                            animation_pose& pose) const -> root_channels;
    /// Retargets a source pose onto the target skeleton, appending the bones to pose.nodes.
    void retarget_source_pose(const animation_clip& anim_clip,
                              const clip_binding& binding,
                              const animation_target_skeleton& target,
                              const std::vector<math::transform>& source_local,
                              animation_pose& pose);
    /// Carries the extracted root delta from the source root's parent frame into the target's.
    void convert_root_delta(const animation_clip& anim_clip,
                            const clip_binding& binding,
                            const animation_target_skeleton& target,
                            const root_channels& roots,
                            animation_pose& pose) const;
    /**
     * @brief Samples a blend space's clips in their shared source skeleton, blends them there and
     * retargets the blend once. False, sampling nothing, unless every clip is loaded, retargets rest
     * aligned and linearly, and samples the same skeleton.
     */
    auto sample_blend_in_source_space(animation_state& state,
                                      const animation_skeleton_binding& binding,
                                      bool extract_root_motion,
                                      animation_pose& pose) -> bool;
    void sample_root_position(const animation_clip& anim_clip,
                              const animation_channel& channel,
                              uint32_t target_index,
                              seconds_t time,
                              uint64_t loop_count,
                              bool extract_root_motion,
                              const root_motion_frame& frame,
                              math::transform& local,
                              animation_pose& pose) const;
    /// Samples the rotation root; returns the turn that carries a clip-frame step into the entity's
    /// frame (identity unless the heading is extracted).
    auto sample_root_rotation(const animation_clip& anim_clip,
                              const animation_channel& channel,
                              uint32_t target_index,
                              uint64_t loop_count,
                              bool extract_root_motion,
                              const root_motion_frame& frame,
                              math::transform& local,
                              animation_pose& pose) const -> math::quat;
    void sample_curves(const animation_clip& anim_clip, float progress, animation_pose& pose) const;
    void update_state(seconds_t delta_time, animation_layer_state& layer_state);
    void collect_events(size_t layer_index, const animation_state& state, float weight);
    auto update_pose(animation_layer_state& layer,
                     const animation_skeleton_binding& binding,
                     bool extract_root_motion,
                     bool additive) -> bool;
    /// Starts a crossfade back to the motion the layer is leaving, from the weight on screen.
    void reverse_crossfade(animation_layer& layer, const animation_play_params& params, seconds_t duration);
    /// Makes the layer's crossfade target its current state.
    void finish_crossfade(animation_layer& layer);
    /// Carries a sampled position delta from the clip's frame into the entity's, which the extracted
    /// heading has turned by `pose_turn`'s inverse.
    static void rebase_root_translation(const clip_binding& clip_binding, const math::quat& pose_turn, animation_pose& pose);
    /// Writes `final_pose` through the callback, its coverage resolved against `ref_pose`.
    void write_final_pose(const animation_pose& final_pose,
                          const animation_pose& ref_pose,
                          const update_callback_t& set_transform_callback);
    /// Puts back to rest every bone the last pose update wrote and `final_pose` does not, once.
    void reset_unwritten_bones(const animation_pose& final_pose,
                               const animation_pose& ref_pose,
                               const update_callback_t& set_transform_callback);
    /// Whether a layer shows a state whose clips are still loading (nothing posed yet).
    static auto is_layer_pose_pending(const animation_layer& layer) -> bool;
    void convert_to_additive(animation_layer_state& layer_state,
                             const std::shared_ptr<animation_clip>& clip,
                             size_t reference_index,
                             const animation_skeleton_binding& binding,
                             animation_pose& pose);
    /// The base layer's pose with every contributing upper layer blended on: the base pose itself
    /// (or the reference pose) when none contributes, else composed_pose_.
    auto compose_layers(const animation_pose& ref_pose) -> const animation_pose&;

    std::vector<animation_layer> layers_;
    std::vector<clip_binding> bindings_;
    /// Bones the last pose update wrote (sorted) and a hash of the set, so a frame whose bones
    /// match skips the comparison.
    std::vector<size_t> written_bones_;
    size_t written_bones_hash_{};
    retarget_scratch retarget_scratch_;
    std::vector<animation_event_hit> fired_events_;
    std::vector<animation_curve_value> final_curves_;
    /// Scratch for multi-layer composition, reused across frames.
    animation_pose composed_pose_;
    animation_pose compose_scratch_;

    bool playing_{};
    bool paused_{};
    /// One-shot flag consumed by update_time - see request_pose_refresh().
    bool pose_refresh_requested_{};
};

} // namespace unravel
