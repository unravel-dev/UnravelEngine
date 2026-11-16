#pragma once

#include <context/context.hpp>
#include <filesystem/filesystem.h>

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace unravel
{

/// Where a suite runs in a parallel run.
enum class suite_lane
{
    /// Beside the other suites.
    normal,
    /// Starts shaderc or texturec on many threads at once, so it takes several of the run's slots.
    tools,
    /// Alone, after every other suite: its checks depend on timing or on the process's own thread pools.
    exclusive,
};

/// How a suite is scheduled and watched.
struct suite_traits
{
    suite_lane lane = suite_lane::normal;
    /// Seconds the suite may go without progress (a case starting or ending, a check, test::note_progress) before
    /// the watchdog stops it. 0 takes the lane's default.
    int idle_timeout_seconds = 0;
};

/**
 * @brief One registered suite.
 *
 * Suites register themselves at static-init time, so adding one is adding a file. The context is fully booted by the
 * time `run` is called.
 */
struct test_suite
{
    std::string name;
    std::function<void(rtti::context&)> run;
    suite_traits traits{};
};

void register_test_suite(test_suite suite);

/// Every registered suite, sorted by name.
auto get_test_suites() -> std::vector<test_suite>;

/// The seconds a suite may go without progress, by its traits and lane.
auto get_idle_timeout_seconds(const suite_traits& traits) -> int;

/// The lane's name as --list prints it.
auto get_lane_name(suite_lane lane) -> const char*;

/// The suite's name as a folder name: lower case, every other character an underscore.
auto make_suite_slug(const std::string& name) -> std::string;

/**
 * @brief Registers a suite from file scope. `func` takes rtti::context&. An optional third argument is the suite's
 * unravel::suite_traits.
 */
#define REGISTER_TEST_SUITE(name_literal, func, ...)                                                                   \
    namespace                                                                                                          \
    {                                                                                                                  \
    const bool registered_##func = []                                                                                  \
    {                                                                                                                  \
        ::unravel::register_test_suite({name_literal, &func, __VA_ARGS__});                                            \
        return true;                                                                                                   \
    }();                                                                                                               \
    }

} // namespace unravel

/**
 * @brief What a suite checks with.
 *
 * Every function records into the suite that is running: the counters are shared by the threads a case starts, so a
 * check made on a worker thread counts toward the case that started it.
 */
namespace unravel::test
{

/// Counts a check; prints a failure when `condition` is false.
void check(bool condition, const std::string& what);

/// Counts a check that `actual` is within `tolerance` of `expected`; a NaN fails. Prints both values on failure.
void check_near(float actual, float expected, float tolerance, const std::string& what);

/// Counts a check that two counts are equal; prints both on failure.
void check_eq(std::size_t actual, std::size_t expected, const std::string& what);

/// Counts a check that two texts are equal; prints both on failure.
void check_eq(const std::string& actual, const std::string& expected, const std::string& what);

/**
 * @brief A check that is known to fail today, for the reason given.
 *
 * It reports XFAIL while it fails. When it holds it reports XPASS, so the expectation can become a check(); under
 * --strict an XPASS fails the suite.
 */
void check_xfail(bool condition, const std::string& what, const std::string& reason);

/// Counts a check and, when it fails, ends the current case: what follows needs it.
void require(bool condition, const std::string& what);

/// Ends the current case as skipped, for the reason given; outside a case it ends the suite.
[[noreturn]] void skip(const std::string& reason);

/// Records that a part of the suite did not run, for the reason given, and goes on.
void skipped(const std::string& reason);

/// The path of a tool built beside the runner, such as shaderc or texturec.
auto get_tool_path(const std::string& name) -> fs::path;

/**
 * @brief Whether the checks that run a tool can run.
 *
 * False when the tool is not built beside the runner or --no-tools turned the tool checks off; the skip is recorded
 * then. A tool that is not built fails the suite under --strict.
 */
auto use_tool(const std::string& name) -> bool;

/// Ends the current case as skipped when use_tool(name) is false.
void require_tool(const std::string& name);

/// Tells the watchdog that a long step is still moving, for work that runs a while between checks.
void note_progress();

/// The failed checks of the running suite so far, for a step that retries when it failed.
auto failure_count() -> int;

/**
 * @brief Waits until `is_done` returns true, at most `timeout`.
 *
 * Asks it every millisecond. A timeout is a failed check naming `what`; nothing waits forever.
 *
 * @return Whether `is_done` returned true in time.
 */
auto wait_until(const std::function<bool()>& is_done, std::chrono::milliseconds timeout, const std::string& what)
    -> bool;

/**
 * @brief A path of this process for a temporary file or folder: `<temp>/unravel-tests/<process id>/<name>`.
 *
 * The same name gives the same path for the whole run, and two runs at once never share one. Nothing is created at
 * the path itself; its parent folder is.
 */
auto temp_path(const std::string& name) -> fs::path;

/// What a tool printed and how it ended.
struct tool_result
{
    bool started{};
    /// Whether the tool was stopped for running longer than its time.
    bool timed_out{};
    int exit_code{-1};
    /// Everything it printed, stdout and stderr together.
    std::string output;
};

/// Runs a program to its end, at most `timeout`, and keeps what it printed. The first argument is the program.
auto run_tool(const std::vector<std::string>& arguments, std::chrono::seconds timeout) -> tool_result;

/**
 * @brief Calls `job(index)` once for every index below `count`, on threads of their own.
 *
 * For independent slow steps such as shader compiles. The jobs run on a pool of their own, not on the engine's frame
 * pool, so a job may start the engine's parallel loops itself. Each job that ends counts as progress. A job that
 * throws is a failed check, reported after the others end. In the single-threaded mode the jobs run in order on the
 * calling thread.
 */
///
/// @return Each job's seconds, by index.
auto run_parallel(std::size_t count, const std::function<void(std::size_t)>& job) -> std::vector<double>;

/// run_parallel that keeps each job's result at its index, so what follows reads them in order. `seconds`, when
/// given, receives each job's time.
template<typename Result, typename Job>
auto run_parallel_map(std::size_t count, Job&& job, std::vector<double>* seconds = nullptr) -> std::vector<Result>
{
    std::vector<Result> results(count);
    auto times = run_parallel(count,
                              [&](std::size_t index)
                              {
                                  results[index] = job(index);
                              });
    if(seconds != nullptr)
    {
        *seconds = std::move(times);
    }
    return results;
}

/// Prints the `count` slowest of a parallel run's jobs, named by `name(index)`: what bounds the run's time.
void print_slowest(const std::vector<double>& seconds,
                   const std::function<std::string(std::size_t)>& name,
                   std::size_t count = 5);

/**
 * @brief Runs one case of the current suite.
 *
 * Times it, catches what it throws (a failed check that names the case; the suite goes on), and leaves it out when
 * --case names other cases. Use RUN_CASE.
 */
void run_case(const char* call_text, const std::function<void()>& body);

} // namespace unravel::test

/// Runs a call as a case named after the function it calls: RUN_CASE(test_round_trip(temp)).
#define RUN_CASE(...) ::unravel::test::run_case(#__VA_ARGS__, [&]() { __VA_ARGS__; })
