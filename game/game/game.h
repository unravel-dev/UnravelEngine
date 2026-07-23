#pragma once

#include <cmd_line/parser.h>
#include <context/context.hpp>

namespace unravel
{

struct game
{
    static auto create(rtti::context& ctx, cmd_line::parser& parser) -> bool;
    static auto init(const cmd_line::parser& parser) -> bool;
    static auto deinit() -> bool;
    static auto destroy() -> bool;
    static auto process() -> int;
    static auto interrupt() -> bool;

    static auto init_protocols(const cmd_line::parser& parser) -> bool;
    static auto prepare_boot_config(rtti::context& ctx, const cmd_line::parser& parser) -> bool;
    static auto init_settings(rtti::context& ctx) -> bool;
    static auto init_window(rtti::context& ctx) -> bool;
    static auto init_assets(rtti::context& ctx) -> bool;
};
} // namespace unravel
