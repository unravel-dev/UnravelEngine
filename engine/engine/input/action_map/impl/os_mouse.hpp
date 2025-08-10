#pragma once

#include "../button_state_map.hpp"
#include "../mouse.hpp"

namespace input
{
class os_mouse : public mouse
{
    float scroll_{};
    coord position_{};
    std::map<unsigned, float> axis_map_;
    button_state_map button_state_map_;

public:
    auto get_button_state_map() -> button_state_map&;
    auto get_axis_value(uint32_t axis) const -> float override;
    auto get_button_state(uint32_t button) const -> button_state override;
    auto get_left_button_state() const -> button_state override;
    auto get_middle_button_state() const -> button_state override;
    auto get_right_button_state() const -> button_state override;
    auto get_name() const -> const std::string& override;
    auto get_position() const -> coord override;
    auto get_scroll() const -> float override;
    auto is_down(uint32_t button) const -> bool override;
    auto is_pressed(uint32_t button) const -> bool override;
    auto is_released(uint32_t button) const -> bool override;

    void set_position(coord pos);
    void set_scroll(float scroll);
    void update();    
};
} // namespace input
