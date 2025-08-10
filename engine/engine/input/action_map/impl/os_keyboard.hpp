#pragma once

#include "../bimap.hpp"
#include "../device.hpp"
#include "../key.hpp"
#include "../key_state.hpp"
#include "../key_state_map.hpp"
#include "../keyboard.hpp"
#include <map>

namespace input
{
class os_keyboard : public keyboard
{
    key_state_map key_state_map_;
public:
    auto get_key_state(key_code key) const -> key_state override;
    auto get_key_state_map() -> key_state_map&;
    auto get_name() const -> const std::string& override;
    auto is_down(key_code key) const -> bool override;
    auto is_pressed(key_code key) const -> bool override;
    auto is_released(key_code key) const -> bool override;

    void update();
};
} // namespace input
