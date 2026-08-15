#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/ecs/scene.h>
#include <hpp/span.hpp>

#include <utility>
#include <vector>

namespace unravel
{
class animation_system
{
public:
    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    /**
     * @brief Called when the component is created.
     * @param r The registry containing the component.
     * @param e The entity associated with the component.
     */
    static void on_create_component(entt::registry& r, entt::entity e);

    /**
     * @brief Called when the component is destroyed.
     * @param r The registry containing the component.
     * @param e The entity associated with the component.
     */
    static void on_destroy_component(entt::registry& r, entt::entity e);

    /**
     * @brief Updates the physics system for each frame.
     * @param ctx The context for the update.
     * @param dt The delta time for the frame.
     */
    void on_frame_update(scene& scn, delta_t dt);

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

    /**
     * @brief Skips the next frame update.
     * @param ctx The context for the update.
     */
    void on_skip_next_frame(rtti::context& ctx);

    void on_update(scene& scn, delta_t dt, bool force);

    /// Per-frame grouping scratch, kept as members so steady-state frames do
    /// not allocate: (group_root, entity) pairs and [begin, end) runs of equal
    /// group_root into that list. Only touched from the main-thread update.
    std::vector<std::pair<entt::entity, entt::entity>> grouping_scratch_;
    std::vector<std::pair<size_t, size_t>> group_ranges_;

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
};
} // namespace unravel
