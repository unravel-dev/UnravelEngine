#pragma once

#include <cmd_line/parser.h>
#include <context/context.hpp>

#include <string>
#include <vector>

namespace unravel
{

struct editor
{
    static auto create(rtti::context& ctx, cmd_line::parser& parser) -> bool;
    static auto init(const cmd_line::parser& parser) -> bool;
    static auto deinit() -> bool;
    static auto destroy() -> bool;
    static auto process() -> int;
    static auto interrupt() -> bool;

    /**
     * @brief Prepares the app for process restart (e.g. persist state, adjust spawn args).
     */
    static void prepare_restart(std::vector<std::string>& arguments);

    static auto init_window(rtti::context& ctx, const cmd_line::parser& parser) -> bool;
};
} // namespace unravel
