#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/frame_times.h>
#include <engine/animation/animation.h>
#include <engine/assets/asset_handle.h>
#include <engine/ecs/scene.h>

#include <math/math.h>

#include <memory>
#include <vector>

namespace unravel
{
class animation_component;

/**
 * @brief The local transforms of an animated entity and its armature, captured so an edit-mode
 * preview can put the scene back exactly as it found it.
 */
class animation_pose_snapshot
{
public:
    void capture(entt::handle entity);
    /// Puts the captured transforms back, unless the entity no longer exists (scene reloaded).
    void restore();
    void clear();
    auto is_empty() const -> bool;

private:
    struct entry
    {
        entt::handle entity;
        math::transform local;
    };

    entt::uhandle owner_id_;
    entt::handle owner_;
    std::vector<entry> entries_;
};

/**
 * @brief An edit-mode preview that animates a scene entity and puts it back as it found it.
 *
 * The preview ends by itself when the tool showing it stops calling keep_alive() (its window was
 * closed or hidden) and when the project closes. Everything that writes a scene out - scene and
 * prefab saves, the checkpoints of play start and script recompile, and the entity snapshots of
 * duplicates and undoable deletes - calls stop_all() first, so a preview never leaks a pose into
 * the play scene, a saved scene or a copy. An entity has one preview at a
 * time: starting another one on it stops the first, so the pose that comes back is the entity's own.
 */
class animation_entity_preview
{
public:
    animation_entity_preview();
    animation_entity_preview(const animation_entity_preview&) = delete;
    auto operator=(const animation_entity_preview&) -> animation_entity_preview& = delete;
    ~animation_entity_preview();

    /// Stops every preview, putting each previewed entity's own pose back.
    static void stop_all();

    /// Captures the entity's pose and starts previewing it; the component to drive, or nullptr.
    auto start(rtti::context& ctx, entt::handle entity) -> animation_component*;
    /// Stops the entity's player, clears its layers and puts the captured pose back.
    void stop();
    /// Called every frame by the tool drawing the preview; a preview nobody draws stops.
    void keep_alive();
    auto is_active() const -> bool;
    auto get_entity() const -> entt::handle;

private:
    void connect(rtti::context& ctx);
    void on_frame_end(rtti::context& ctx, const frame_times& times);
    void on_close_project(rtti::context& ctx);

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
    bool connected_{};
    animation_pose_snapshot snapshot_;
    entt::handle entity_;
    uint64_t frame_{};
    uint64_t alive_frame_{};
};

/**
 * @brief Poses a scene entity with one clip at a chosen time, for authoring tools (clip events and
 * curves).
 *
 * The entity's player is paused on the clip and only seeked, so no controller or script moves it
 * meanwhile. It stops like any animation_entity_preview.
 */
class animation_clip_preview
{
public:
    auto start(rtti::context& ctx, entt::handle entity, const asset_handle<animation_clip>& clip) -> bool;
    void stop();
    /// Poses the entity at a normalized time of the clip.
    void set_progress(float progress);
    /// Called every frame by the tool drawing the preview; a preview nobody draws stops.
    void keep_alive();
    auto is_active() const -> bool;
    auto get_entity() const -> entt::handle;
    auto get_clip() const -> const asset_handle<animation_clip>&;

private:
    animation_entity_preview preview_;
    asset_handle<animation_clip> clip_;
};

} // namespace unravel
