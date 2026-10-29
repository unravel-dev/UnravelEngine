#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/ecs/scene.h>
#include <engine/frame_times.h>
#include <hpp/span.hpp>

#include <memory>
#include <utility>
#include <vector>

namespace unravel
{
class animation_component;
class model_component;
struct animation_skeleton_binding;
struct animation_target_skeleton;

class animation_system
{
public:
    /**
     * @brief Changes whenever what the retarget skeleton is described from changes: the model's
     * armature, or its mesh asset (a reimport keeps the armature's bones and can bring other roles).
     */
    static auto get_skeleton_version(const model_component& model) -> uint64_t;

    /**
     * @brief The skeleton of `model` as a rest-aligned retargeting target - names, bind pose, rest
     * pose and humanoid roles - built once per skeleton version and cached on `animation`.
     */
    static auto get_target_skeleton(animation_component& animation, const model_component& model)
        -> const std::shared_ptr<const animation_target_skeleton>&;

    /**
     * @brief The binding `animation`'s clips are sampled onto `model`'s skeleton with. Name Based
     * matches channels by bone name, then by name without FBX namespace; Rest Aligned adds the
     * target skeleton. It refers to `model`, so it is used while `model` lives.
     */
    static auto make_skeleton_binding(animation_component& animation, const model_component& model)
        -> animation_skeleton_binding;

    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    /**
     * @brief Advances the scene's animations by one frame.
     * @param scn The scene to update.
     * @param times The frame's times; animations advance by the scaled time.
     */
    void on_frame_update(scene& scn, const frame_times& times);

    void on_play_begin(hpp::span<const entt::handle> entities, delta_t dt);

private:


    /**
     * @brief Called when playback begins.
     * @param ctx The context for the playback.
     */
    void on_play_begin(rtti::context& ctx);

    /**
     * @brief Called when playback ends.
     * @param ctx The context for the playback.
     */
    void on_play_end(rtti::context& ctx);

    /**
     * @brief Called when playback is paused.
     * @param ctx The context for the playback.
     */
    void on_pause(rtti::context& ctx);

    /**
     * @brief Called when playback is resumed.
     * @param ctx The context for the playback.
     */
    void on_resume(rtti::context& ctx);

    void on_update(scene& scn, delta_t dt);

    /// Per-frame grouping scratch, kept as members so steady-state frames do
    /// not allocate: (group_root, entity) pairs and [begin, end) runs of equal
    /// group_root into that list. Only touched from the main-thread update.
    std::vector<std::pair<entt::entity, entt::entity>> grouping_scratch_;
    std::vector<std::pair<size_t, size_t>> group_ranges_;

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
};
} // namespace unravel
