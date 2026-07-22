#pragma once
#include <engine/engine_export.h>

#include <base/basetypes.hpp>
#include <cmd_line/parser.h>
#include <context/context.hpp>

namespace unravel
{

struct engine
{
    static auto create(rtti::context& ctx, cmd_line::parser& parser) -> bool;
    static auto init_core(const cmd_line::parser& parser) -> bool;
    static auto init_systems(const cmd_line::parser& parser) -> bool;

    static auto interrupt() -> bool;

    /**
     * @brief Requests a graceful application restart after the current frame.
     *
     * The running process shuts down normally, then service_main spawns a
     * replacement process. Prefer this over interrupt() when the app should
     * come back with the same user arguments.
     */
    static auto request_restart() -> bool;

    static auto deinit() -> bool;
    static auto destroy() -> bool;
    static auto process() -> int;

    static auto context() -> rtti::context&;
};
} // namespace unravel
