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

    /**
     * @brief Creates the main window, hidden, before the graphics backend is initialized on it.
     */
    static auto create_main_window(rtti::context& ctx, const cmd_line::parser& parser) -> bool;
    /**
     * @brief Titles the main window with the backend in use and shows it.
     */
    static auto show_main_window(rtti::context& ctx) -> bool;
};
} // namespace unravel
