#pragma once

#include "harness.h"

#include <cmd_line/parser.h>
#include <context/context.hpp>

#include <string>

namespace unravel
{

/**
 * @brief The test runner, booted the same way the editor and the game are.
 *
 * Its whole reason for existing is the engine around it. A suite run here gets the real asset_manager, so an
 * asset_handle stored in a component resolves and a prefab instance can actually reach its source - which a
 * standalone harness cannot do, and which is where every escaped nested-prefab bug has been hiding.
 *
 * Headless: threading and assets are initialised, the renderer and audio are not. Nothing in the suites needs a
 * device, and needing one would keep this out of a normal build loop.
 *
 * One executable runs a whole run: with more than one suite selected and more than one job it starts itself once per
 * suite (--worker <name>) and reports what the workers did.
 */
struct tests
{
    static auto create(rtti::context& ctx, cmd_line::parser& parser) -> bool;
    static auto init(const cmd_line::parser& parser) -> bool;
    static auto deinit() -> bool;
    static auto destroy() -> bool;
    static auto process() -> int;
    static auto interrupt() -> bool;

    /// Keeps the command line, which workers are started with, before the service parses it.
    static void set_command_line(int argc, char* argv[]);

    /// Prints the suites, one a line, when the command line asks for --list. Whether it did.
    static auto print_list_if_asked(int argc, char* argv[]) -> bool;

    /// The process exit code: 0 when every suite passed or was skipped; a worker's own code for a worker.
    static auto exit_code() -> int;

    /// Whether a command-line switch was given. Suites read their own options through this rather than reaching
    /// for argv.
    static auto wants(const std::string& option) -> bool;
};

} // namespace unravel
