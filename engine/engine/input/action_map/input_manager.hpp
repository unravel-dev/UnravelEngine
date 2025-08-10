#pragma once

#include "gamepad.hpp"
#include "keyboard.hpp"
#include "mouse.hpp"

namespace input
{
class input_manager
{
public:
    virtual ~input_manager() = default;
    virtual auto get_mouse() const -> const mouse& = 0;
    virtual auto get_gamepad(uint32_t index) const -> const gamepad& = 0;
    virtual auto get_max_gamepads() const -> uint32_t = 0;

    virtual auto get_keyboard() const -> const keyboard& = 0;
    virtual void before_events_update() = 0;
    virtual void after_events_update() = 0;

};
} // namespace InputLib
