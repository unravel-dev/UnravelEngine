#pragma once

#include <engine/ecs/components/basic_component.h>

#include <engine/animation/animation_player.h>

namespace unravel
{

class animation_component : public component_crtp<animation_component>
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

private:
    asset_handle<animation_clip> animation_;

    animation_player player_;

    culling_mode culling_mode_{culling_mode::always_animate};
    bool auto_play_ = true;
    bool apply_root_motion_ = false;
    float speed_ = 1.0f;
};

} // namespace unravel
