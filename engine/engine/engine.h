#pragma once
#include <engine/engine_export.h>

#include <base/basetypes.hpp>
#include <cmd_line/parser.h>
#include <context/context.hpp>

namespace unravel
{

struct init_error
{
    std::string category;
    std::string msg;
};

struct engine
{
    static auto create(rtti::context& ctx, cmd_line::parser& parser) -> bool;
    static auto init_core(const cmd_line::parser& parser) -> bool;
    static auto init_systems(const cmd_line::parser& parser) -> bool;

    static auto interrupt() -> bool;

    static auto deinit() -> bool;
    static auto destroy() -> bool;
    static auto process() -> int;

    static auto context() -> rtti::context&;
};
} // namespace unravel
