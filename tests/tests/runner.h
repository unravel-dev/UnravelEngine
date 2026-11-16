#pragma once

#include "harness.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

/**
 * @brief The runner's side of the harness: results, options, the watchdog and the worker processes.
 *
 * A run has three shapes. In process: the suites run one after another in this process (--jobs 1, or one suite).
 * Parallel: this process starts a worker per suite, several at once, and reports what they did. Worker: this process
 * runs one suite (--worker <name>) and prints protocol lines its parent reads; the exit code says how it ended, so
 * another scheduler (CTest) can run workers the same way.
 */
namespace unravel::runner
{

/// How a suite or a case ended.
enum class outcome
{
    passed,
    failed,
    skipped,
    timed_out,
    crashed,
};

/// "PASS", "FAIL", "SKIP", "TIMEOUT" or "CRASH".
auto get_outcome_name(outcome result) -> const char*;

/// The outcome a protocol line names; failed for a name it does not know.
auto parse_outcome(const std::string& name) -> outcome;

/// Worker exit codes. 77 is the conventional "skipped" code (CTest SKIP_RETURN_CODE).
constexpr int EXIT_PASSED = 0;
constexpr int EXIT_FAILED = 1;
constexpr int EXIT_TIMED_OUT = 3;
constexpr int EXIT_SKIPPED = 77;

/// What starts every line a worker prints for its parent.
constexpr const char* PROTOCOL_PREFIX = "@@unravel-tests ";

/// One case of a suite.
struct case_result
{
    std::string name;
    outcome result = outcome::passed;
    double seconds{};
    int checks{};
    int failures{};
    /// The first failure messages, for reports.
    std::vector<std::string> failure_messages;
    std::string skip_reason;
    /// What the case printed (parallel runs only).
    std::string output;
};

/// One suite run.
struct suite_result
{
    std::string name;
    outcome result = outcome::passed;
    /// The suite's own time, from its first case to its last.
    double seconds{};
    /// The worker process's time, its start and engine boot included (parallel runs only).
    double wall_seconds{};
    int checks{};
    int failures{};
    int xfails{};
    int xpasses{};
    int skips{};
    /// Skips because a tool was not built; they fail the suite under --strict.
    int missing_tools{};
    std::vector<case_result> cases;
    /// Failure messages of checks made outside any case.
    std::vector<std::string> failure_messages;
    /// Skip reasons and unexpected passes, as printed.
    std::vector<std::string> notes;
    /// What the suite printed (parallel runs only).
    std::string output;
    /// The worker's exit code (parallel runs only).
    int exit_code{};
    /// Why the suite ended as it did when its checks do not say: a timeout, a crash, --strict.
    std::string reason;
};

/// What changes how suites run, the same in every process of a run.
struct run_options
{
    /// A missing tool or an unexpected pass fails the suite.
    bool strict{};
    /// The checks that run shaderc or texturec are skipped.
    bool no_tools{};
    /// Runs only the cases whose name contains this.
    std::string case_filter;
    /// Seconds without progress before a suite is stopped; negative takes the suite's own, 0 never stops.
    int idle_timeout_seconds{-1};
    /// This process is a worker of a parallel run and prints protocol lines.
    bool is_worker{};
};

void configure(const run_options& options);
auto get_options() -> const run_options&;

/// Runs `suite` in this process, its cases timed and its exceptions caught.
auto run_suite(const test_suite& suite, rtti::context& ctx) -> suite_result;

/// Starts the thread that stops this process when the running suite makes no progress for its idle timeout.
void start_watchdog();
void stop_watchdog();

/// The exit code a worker ends with for `result`.
auto get_exit_code(const suite_result& result) -> int;

/// Prints the protocol line that ends a worker's output.
void print_suite_protocol_line(const suite_result& result);

/// The line printed when a suite ends: its name, outcome, counts and time.
auto describe_suite(const suite_result& result) -> std::string;

/// How a parallel run starts its workers.
struct parallel_options
{
    /// Worker processes at once; a suite of the tools lane takes several.
    int jobs{1};
    /// Every suite's output, not only a failing one's.
    bool verbose{};
    /// The runner, started once per suite.
    std::string executable;
    /// Arguments every worker gets before --worker <name>.
    std::vector<std::string> arguments;
    /// Each suite's last time, for the order: the longest start first.
    std::map<std::string, double> durations;
};

/// Runs each suite in a worker process of its own, `jobs` at once, the exclusive lane alone at the end.
auto run_in_workers(const std::vector<test_suite>& suites, const parallel_options& options) -> std::vector<suite_result>;

/// The slots a suite takes in a run of `jobs` workers.
auto get_slot_count(const suite_traits& traits, int jobs) -> int;

/// Prints the table of every suite, slowest first, with totals, failures, unexpected passes and skip reasons.
void print_report(const std::vector<suite_result>& results, double wall_seconds, int jobs);

/// Reads "<seconds>\t<suite name>" lines; an absent file gives none.
auto read_durations(const fs::path& file) -> std::map<std::string, double>;

/// Writes `durations` updated with this run's times.
void write_durations(const fs::path& file, std::map<std::string, double> durations, const std::vector<suite_result>& results);

/// Writes the results as JUnit XML: a testsuite per suite, a testcase per case.
auto write_junit(const fs::path& file, const std::vector<suite_result>& results) -> bool;

/// Where a run keeps its per-suite folders (`<bin>/test_results`).
auto get_results_folder() -> fs::path;

} // namespace unravel::runner
