#pragma once

#include <engine/ecs/components/basic_component.h>

#include <engine/animation/animation_controller_instance.h>
#include <engine/animation/animation_player.h>

namespace unravel
{

class animation_component : public component_crtp<animation_component, owned_component>
{
public:
    enum class culling_mode : uint8_t
    {
        always_animate,
        renderer_based,
    };

    /**
     * @brief Sets whether the animation should autoplay.
     * @param on True to autoplay, false otherwise.
     */
    void set_autoplay(bool on);

    /**
     * @brief Gets whether the animation is set to autoplay.
     * @return True if autoplay is enabled, false otherwise.
     */
    auto get_autoplay() const -> bool;

    void set_apply_root_motion(bool on);
    auto get_apply_root_motion() const -> bool;

    void set_animation(const asset_handle<animation_clip>& animation);
    auto get_animation() const -> const asset_handle<animation_clip>&;

    /**
     * @brief State machine the component plays. While one is assigned it drives the player and the
     * single Animation clip is ignored; scripts steer it through parameters. Changing it stops the
     * layers the previous controller played; removing it while playing goes back to the clip.
     */
    void set_controller(const asset_handle<animation_controller>& controller);
    auto get_controller() const -> const asset_handle<animation_controller>&;

    /**
     * @brief Runtime of the assigned controller, bound to its current definition. Waits for the
     * controller to load (scripts and tools call it); returns nullptr when no controller is assigned.
     */
    auto get_controller_instance() -> animation_controller_instance*;
    auto get_controller_instance() const -> const animation_controller_instance*;
    /**
     * @brief Same without waiting: nullptr while the controller is still loading. For the per-frame
     * update, which runs on worker threads that must not block on a load.
     */
    auto try_get_controller_instance() -> animation_controller_instance*;

    void set_culling_mode(const culling_mode& animation);
    auto get_culling_mode() const -> const culling_mode&;

    /**
     * @brief Sets the speed for animation playback.
     * @param speed The animation speed (1.0 = normal speed, 2.0 = double speed, 0.5 = half speed).
     */
    void set_speed(float speed);

    /**
     * @brief Gets the current speed for animation playback.
     * @return The animation speed value.
     */
    auto get_speed() const -> float;

    auto get_player() const -> const animation_player&;
    auto get_player() -> animation_player&;

    /**
     * @brief Sets the animation retargeting mode.
     * @param mode The retargeting mode to use.
     */
    void set_retargeting_mode(animation_retargeting_mode mode);

    /**
     * @brief Gets the animation retargeting mode.
     * @return The current retargeting mode.
     */
    auto get_retargeting_mode() const -> animation_retargeting_mode;

    /**
     * @brief How clip poses are carried onto this model's skeleton (see animation_pose_retargeting).
     */
    void set_pose_retargeting(animation_pose_retargeting retargeting);
    auto get_pose_retargeting() const -> animation_pose_retargeting;

    /**
     * @brief The model's skeleton prepared for rest-aligned retargeting, rebuilt when the armature
     * version changes. `build` fills a fresh skeleton when needed.
     */
    auto get_target_skeleton(uint64_t armature_version, const std::function<void(animation_target_skeleton&)>& build)
        -> const std::shared_ptr<const animation_target_skeleton>&;

private:
    /// Binds the instance to the controller's definition, or unbinds it when there is none.
    auto bind_controller(const std::shared_ptr<animation_controller>& definition) -> animation_controller_instance*;

    asset_handle<animation_clip> animation_;
    asset_handle<animation_controller> controller_;
    animation_controller_instance controller_instance_;

    animation_player player_;

    culling_mode culling_mode_{culling_mode::always_animate};
    animation_retargeting_mode retargeting_mode_{animation_retargeting_mode::name_based};
    animation_pose_retargeting pose_retargeting_{animation_pose_retargeting::copy};
    std::shared_ptr<const animation_target_skeleton> target_skeleton_;
    uint64_t target_skeleton_version_{};
    bool auto_play_ = true;
    bool apply_root_motion_ = false;
    float speed_ = 1.0f;
};

} // namespace unravel
