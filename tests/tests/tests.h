#pragma once

#include <cmd_line/parser.h>
#include <context/context.hpp>

#include <functional>
#include <string>
#include <vector>

namespace unravel
{

/**
 * @brief The test runner, booted the same way the editor and the game are.
 *
 * Its whole reason for existing is the engine around it. A suite run here gets the real
 * asset_manager, so an asset_handle stored in a component resolves and a prefab instance can
 * actually reach its source - which a standalone harness cannot do, and which is where every
 * escaped nested-prefab bug has been hiding.
 *
 * Headless: threading and assets are initialised, the renderer and audio are not. Nothing in
 * the suites needs a device, and needing one would keep this out of a normal build loop.
 */
struct tests
{
    static auto create(rtti::context& ctx, cmd_line::parser& parser) -> bool;
    static auto init(const cmd_line::parser& parser) -> bool;
    static auto deinit() -> bool;
    static auto destroy() -> bool;
    static auto process() -> int;
    static auto interrupt() -> bool;

    /// Failing checks across every suite that ran. The process exit code.
    static auto failure_count() -> int;

    /// Whether a command-line switch was given. Suites read their own options through this
    /// rather than reaching for argv.
    static auto wants(const std::string& option) -> bool;
};

/**
 * @brief One registered suite.
 *
 * Suites register themselves at static-init time, so adding one is adding a file. The context
 * is fully booted by the time `run` is called.
 */
struct test_suite
{
    std::string name;
    std::function<int(rtti::context&)> run;
};

void register_test_suite(test_suite suite);
auto get_test_suites() -> const std::vector<test_suite>&;

/// Registers a suite from file scope. `func` takes rtti::context& and returns failures.
#define REGISTER_TEST_SUITE(name_literal, func)                                                                        \
    namespace                                                                                                          \
    {                                                                                                                  \
    const bool registered_##func = []                                                                                  \
    {                                                                                                                  \
        ::unravel::register_test_suite({name_literal, &func});                                                          \
        return true;                                                                                                   \
    }();                                                                                                               \
    }

} // namespace unravel
