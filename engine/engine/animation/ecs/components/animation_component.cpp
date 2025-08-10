#include "animation_component.h"
#include <hpp/utility/overload.hpp>
namespace unravel
{

void animation_component::set_animation(const asset_handle<animation_clip>& animation)
{
    animation_ = animation;
}
auto animation_component::get_animation() const -> const asset_handle<animation_clip>&
{
    return animation_;
}

void animation_component::set_autoplay(bool on)
{
    auto_play_ = on;
}

auto animation_component::get_autoplay() const -> bool
{
    return auto_play_;
}

void animation_component::set_apply_root_motion(bool on)
{
    apply_root_motion_ = on;
}

auto animation_component::get_apply_root_motion() const -> bool
{
    return apply_root_motion_;
}

void animation_component::set_culling_mode(const culling_mode& mode)
{
    culling_mode_ = mode;
}

auto animation_component::get_culling_mode() const -> const culling_mode&
{
    return culling_mode_;
}

void animation_component::set_speed(float speed)
{
    speed_ = std::max(0.0f, speed);
}

auto animation_component::get_speed() const -> float
{
    return speed_;
}

auto animation_component::get_player() const -> const animation_player&
{
    return player_;
}
auto animation_component::get_player() -> animation_player&
{
    return player_;
}

} // namespace unravel
