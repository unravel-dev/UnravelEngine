#pragma once
#include <engine/engine_export.h>

#include <base/basetypes.hpp>
#include <base/hash.hpp>
#include <context/context.hpp>
#include <engine/events.h>

#include "action_map/impl/os_input_manager.hpp"

namespace unravel
{

struct input_system
{
    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    auto get_analog_value(const input::action_id_t& action) const -> float;
    auto get_digital_value(const input::action_id_t& action) const -> bool;
    auto is_pressed(const input::action_id_t& action) const -> bool;
    auto is_released(const input::action_id_t& action) const -> bool;
    auto is_down(const input::action_id_t& action) const -> bool;

    static auto get_default_mapping() -> input::action_map;

    input::os_input_manager manager;

};

} // namespace unravel
