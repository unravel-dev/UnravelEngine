#include "runner.h"

#include <concurrency/parallel.h>
#include <concurrency/threading_mode.h>
#include <process/process.h>
#include <subprocess/subprocess.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <future>
#include <mutex>
#include <thread>

namespace unravel
{

namespace
{
auto suite_registry() -> std::vector<test_suite>&
{
    static std::vector<test_suite> instance;
    return instance;
}

constexpr int NORMAL_IDLE_TIMEOUT_SECONDS = 300;
constexpr int TOOLS_IDLE_TIMEOUT_SECONDS = 900;
} // namespace

void register_test_suite(test_suite suite)
{
    suite_registry().emplace_back(std::move(suite));
}

auto get_test_suites() -> std::vector<test_suite>
{
    auto suites = suite_registry();
    std::sort(suites.begin(),
              suites.end(),
              [](const test_suite& lhs, const test_suite& rhs)
              {
                  return lhs.name < rhs.name;
              });
    return suites;
}

auto get_idle_timeout_seconds(const suite_traits& traits) -> int
{
    if(traits.idle_timeout_seconds > 0)
    {
        return traits.idle_timeout_seconds;
    }
    return traits.lane == suite_lane::tools ? TOOLS_IDLE_TIMEOUT_SECONDS : NORMAL_IDLE_TIMEOUT_SECONDS;
}

auto get_lane_name(suite_lane lane) -> const char*
{
    switch(lane)
    {
        case suite_lane::tools:
            return "tools";
        case suite_lane::exclusive:
            return "exclusive";
        case suite_lane::normal:
        default:
            return "normal";
    }
}

auto make_suite_slug(const std::string& name) -> std::string
{
    std::string slug;
    for(const char c : name)
    {
        const bool is_word = std::isalnum(static_cast<unsigned char>(c)) != 0;
        if(is_word)
        {
            slug += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        else if(!slug.empty() && slug.back() != '_')
        {
            slug += '_';
        }
    }
    while(!slug.empty() && slug.back() == '_')
    {
        slug.pop_back();
    }
    return slug;
}

namespace runner
{
namespace
{
using clock_type = std::chrono::steady_clock;

/// Failure messages a case or a suite keeps for reports; the rest are only printed.
constexpr std::size_t MAX_KEPT_FAILURES = 32;
/// How often a worker tells its parent it still makes progress.
constexpr auto ALIVE_INTERVAL = std::chrono::seconds(5);
constexpr auto WATCHDOG_TICK = std::chrono::milliseconds(200);
/// Output a tool may print before it is stopped.
constexpr std::size_t MAX_TOOL_OUTPUT = 16u * 1024u * 1024u;

/// Thrown by require: the case ends, its failure already counted.
struct case_stopped
{
};

/// Thrown by skip and require_tool: the case, or the suite outside a case, ends as skipped.
struct case_skipped
{
    std::string reason;
    /// Whether the skip was already counted and printed.
    bool is_recorded{};
};

/// The suite running in this process. The counters are atomic because a case's threads check too.
struct suite_state
{
    /// Guards the result's lists, the current case and stdout.
    std::mutex mutex;
    suite_result* result = nullptr;
    case_result* current_case = nullptr;
    std::atomic<int> checks{0};
    std::atomic<int> failures{0};
    std::atomic<int> xfails{0};
    std::atomic<int> xpasses{0};
    std::atomic<int> skips{0};
    std::atomic<int> missing_tools{0};
};

auto get_state() -> suite_state&
{
    static suite_state state;
    return state;
}

auto get_mutable_options() -> run_options&
{
    static run_options options;
    return options;
}

/// What the watchdog reads: when progress was last made and what was running.
struct watch_state
{
    std::atomic<std::int64_t> last_progress{0};
    /// Seconds without progress before the process is stopped; 0 while no suite runs.
    std::atomic<int> idle_timeout_seconds{0};
    std::mutex names_mutex;
    std::string suite_name;
    std::string case_name;
    std::atomic<bool> is_running{false};
    std::thread thread;
};

auto get_watch() -> watch_state&
{
    static watch_state watch;
    return watch;
}

auto now_ticks() -> std::int64_t
{
    return clock_type::now().time_since_epoch().count();
}

void set_watched_names(const std::string& suite_name, const std::string& case_name)
{
    auto& watch = get_watch();
    std::lock_guard<std::mutex> lock(watch.names_mutex);
    watch.suite_name = suite_name;
    watch.case_name = case_name;
}

void print_protocol(const std::string& line)
{
    if(!get_options().is_worker)
    {
        return;
    }
    auto& state = get_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    std::printf("%s%s\n", PROTOCOL_PREFIX, line.c_str());
}

/// Keeps a failure message on the current case, or on the suite outside a case. Called with the state's mutex held
/// and a suite running.
void keep_failure_message(suite_state& state, const std::string& message)
{
    auto& messages = state.current_case != nullptr ? state.current_case->failure_messages : state.result->failure_messages;
    if(messages.size() < MAX_KEPT_FAILURES)
    {
        messages.push_back(message);
    }
}

/// Counts a failed check and prints it.
void record_failure(const std::string& message)
{
    auto& state = get_state();
    state.failures.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(state.mutex);
    std::printf("  FAIL: %s\n", message.c_str());
    if(state.result != nullptr)
    {
        keep_failure_message(state, message);
    }
}

/// Keeps a note the report repeats: a skip reason or an unexpected pass.
void add_note(const std::string& note)
{
    auto& state = get_state();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if(state.result != nullptr)
        {
            state.result->notes.push_back(note);
        }
    }
    print_protocol("note " + note);
}

auto make_case_name(const char* call_text) -> std::string
{
    std::string name(call_text);
    const auto open = name.find('(');
    if(open != std::string::npos)
    {
        name.resize(open);
    }
    const auto first = name.find_first_not_of(" \t\r\n");
    const auto last = name.find_last_not_of(" \t\r\n");
    return first == std::string::npos ? std::string("case") : name.substr(first, last - first + 1);
}

auto seconds_since(clock_type::time_point start) -> double
{
    return std::chrono::duration<double>(clock_type::now() - start).count();
}

void report_timeout(int idle_seconds)
{
    auto& watch = get_watch();
    std::string suite_name;
    std::string case_name;
    {
        std::lock_guard<std::mutex> lock(watch.names_mutex);
        suite_name = watch.suite_name;
        case_name = watch.case_name;
    }
    const std::string where = case_name.empty() ? std::string("outside its cases") : "in case " + case_name;
    std::printf("TIMEOUT: The suite \"%s\" made no progress for %d s, %s, and was stopped.\n",
                suite_name.c_str(),
                idle_seconds,
                where.c_str());
    if(get_options().is_worker)
    {
        std::printf("%stimeout %d %s\n", PROTOCOL_PREFIX, idle_seconds, case_name.c_str());
    }
    std::fflush(stdout);
    std::fflush(stderr);
    // Whatever is stuck holds its locks: a normal exit would wait on them.
    std::_Exit(EXIT_TIMED_OUT);
}

void watch_loop()
{
    auto& watch = get_watch();
    auto last_alive = clock_type::now();
    std::int64_t progress_at_last_alive = watch.last_progress.load();
    while(watch.is_running.load())
    {
        std::this_thread::sleep_for(WATCHDOG_TICK);
        const int timeout = watch.idle_timeout_seconds.load();
        const auto last_progress = clock_type::time_point(clock_type::duration(watch.last_progress.load()));
        const auto idle = clock_type::now() - last_progress;
        if(timeout > 0 && idle > std::chrono::seconds(timeout))
        {
            report_timeout(timeout);
        }
        if(get_options().is_worker && clock_type::now() - last_alive >= ALIVE_INTERVAL)
        {
            last_alive = clock_type::now();
            const auto progress = watch.last_progress.load();
            if(progress != progress_at_last_alive)
            {
                progress_at_last_alive = progress;
                print_protocol("alive");
            }
        }
    }
}

auto compute_outcome(const suite_result& result, bool is_skipped) -> outcome
{
    const bool strict = get_options().strict;
    if(result.failures > 0 || (strict && (result.xpasses > 0 || result.missing_tools > 0)))
    {
        return outcome::failed;
    }
    if(is_skipped && result.checks == 0)
    {
        return outcome::skipped;
    }
    return outcome::passed;
}

auto describe_strict_failure(const suite_result& result) -> std::string
{
    if(result.failures > 0 || !get_options().strict)
    {
        return {};
    }
    std::string reason;
    if(result.xpasses > 0)
    {
        reason = std::to_string(result.xpasses) + " expected failure(s) now pass";
    }
    if(result.missing_tools > 0)
    {
        reason += (reason.empty() ? "" : " and ") + std::to_string(result.missing_tools) + " check group(s) lack their tool";
    }
    return reason.empty() ? reason : "--strict: " + reason + ".";
}
} // namespace

auto get_outcome_name(outcome result) -> const char*
{
    switch(result)
    {
        case outcome::passed:
            return "PASS";
        case outcome::failed:
            return "FAIL";
        case outcome::skipped:
            return "SKIP";
        case outcome::timed_out:
            return "TIMEOUT";
        case outcome::crashed:
        default:
            return "CRASH";
    }
}

auto parse_outcome(const std::string& name) -> outcome
{
    for(const auto candidate : {outcome::passed, outcome::failed, outcome::skipped, outcome::timed_out, outcome::crashed})
    {
        if(name == get_outcome_name(candidate))
        {
            return candidate;
        }
    }
    return outcome::failed;
}

void configure(const run_options& options)
{
    get_mutable_options() = options;
}

auto get_options() -> const run_options&
{
    return get_mutable_options();
}

auto run_suite(const test_suite& suite, rtti::context& ctx) -> suite_result
{
    auto& state = get_state();
    auto& watch = get_watch();
    suite_result result;
    result.name = suite.name;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.result = &result;
        state.current_case = nullptr;
    }
    for(auto* counter : {&state.checks, &state.failures, &state.xfails, &state.xpasses, &state.skips, &state.missing_tools})
    {
        counter->store(0);
    }
    set_watched_names(suite.name, {});
    test::note_progress();
    const int configured_timeout = get_options().idle_timeout_seconds;
    watch.idle_timeout_seconds.store(configured_timeout >= 0 ? configured_timeout : get_idle_timeout_seconds(suite.traits));

    const auto start = clock_type::now();
    bool is_skipped = false;
    try
    {
        suite.run(ctx);
    }
    catch(const case_skipped& skip)
    {
        if(!skip.is_recorded)
        {
            test::skipped(skip.reason);
        }
        is_skipped = true;
    }
    catch(const case_stopped&)
    {
    }
    catch(const std::exception& ex)
    {
        record_failure(std::string("The suite threw an exception outside its cases: ") + ex.what());
    }
    catch(...)
    {
        record_failure("The suite threw an exception outside its cases that is not a std::exception.");
    }
    watch.idle_timeout_seconds.store(0);

    result.seconds = seconds_since(start);
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.result = nullptr;
        state.current_case = nullptr;
    }
    result.checks = state.checks.load();
    result.failures = state.failures.load();
    result.xfails = state.xfails.load();
    result.xpasses = state.xpasses.load();
    result.skips = state.skips.load();
    result.missing_tools = state.missing_tools.load();
    result.result = compute_outcome(result, is_skipped);
    result.reason = describe_strict_failure(result);
    set_watched_names({}, {});
    return result;
}

void start_watchdog()
{
    auto& watch = get_watch();
    if(watch.is_running.exchange(true))
    {
        return;
    }
    watch.last_progress.store(now_ticks());
    watch.thread = std::thread(watch_loop);
}

void stop_watchdog()
{
    auto& watch = get_watch();
    if(!watch.is_running.exchange(false))
    {
        return;
    }
    if(watch.thread.joinable())
    {
        watch.thread.join();
    }
}

auto get_exit_code(const suite_result& result) -> int
{
    switch(result.result)
    {
        case outcome::passed:
            return EXIT_PASSED;
        case outcome::skipped:
            return EXIT_SKIPPED;
        case outcome::timed_out:
            return EXIT_TIMED_OUT;
        case outcome::failed:
        case outcome::crashed:
        default:
            return EXIT_FAILED;
    }
}

void print_suite_protocol_line(const suite_result& result)
{
    char line[256];
    std::snprintf(line,
                  sizeof(line),
                  "suite %s %.3f %d %d %d %d %d %d",
                  get_outcome_name(result.result),
                  result.seconds,
                  result.checks,
                  result.failures,
                  result.xfails,
                  result.xpasses,
                  result.skips,
                  result.missing_tools);
    print_protocol(line);
    if(!result.reason.empty())
    {
        print_protocol("reason " + result.reason);
    }
}

auto describe_suite(const suite_result& result) -> std::string
{
    char counts[256];
    std::snprintf(counts, sizeof(counts), "%d checks, %d failures", result.checks, result.failures);
    std::string text = result.name + ": " + counts;
    if(result.xfails > 0)
    {
        text += ", " + std::to_string(result.xfails) + " expected failures";
    }
    if(result.xpasses > 0)
    {
        text += ", " + std::to_string(result.xpasses) + " unexpected passes";
    }
    if(result.skips > 0)
    {
        text += ", " + std::to_string(result.skips) + " skipped";
    }
    if(!result.reason.empty())
    {
        text += " (" + result.reason + ")";
    }
    return text;
}

} // namespace runner

namespace test
{
using runner::get_state;

void check(bool condition, const std::string& what)
{
    get_state().checks.fetch_add(1, std::memory_order_relaxed);
    note_progress();
    if(!condition)
    {
        runner::record_failure(what);
    }
}

void check_near(float actual, float expected, float tolerance, const std::string& what)
{
    get_state().checks.fetch_add(1, std::memory_order_relaxed);
    note_progress();
    if(!(std::fabs(actual - expected) <= tolerance))
    {
        char values[160];
        std::snprintf(values, sizeof(values), " (got %.5f, expected %.5f +/- %.5f)", actual, expected, tolerance);
        runner::record_failure(what + values);
    }
}

void check_eq(std::size_t actual, std::size_t expected, const std::string& what)
{
    get_state().checks.fetch_add(1, std::memory_order_relaxed);
    note_progress();
    if(actual != expected)
    {
        runner::record_failure(what + " (got " + std::to_string(actual) + ", expected " + std::to_string(expected) + ")");
    }
}

void check_eq(const std::string& actual, const std::string& expected, const std::string& what)
{
    get_state().checks.fetch_add(1, std::memory_order_relaxed);
    note_progress();
    if(actual != expected)
    {
        runner::record_failure(what + " (got \"" + actual + "\", expected \"" + expected + "\")");
    }
}

void check_xfail(bool condition, const std::string& what, const std::string& reason)
{
    auto& state = get_state();
    state.checks.fetch_add(1, std::memory_order_relaxed);
    note_progress();
    if(condition)
    {
        state.xpasses.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            std::printf("  XPASS [%s]: %s\n", reason.c_str(), what.c_str());
        }
        runner::add_note("XPASS [" + reason + "]: " + what + " - it holds now: make it a check().");
        return;
    }
    state.xfails.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(state.mutex);
    std::printf("  xfail [%s]: %s\n", reason.c_str(), what.c_str());
}

void require(bool condition, const std::string& what)
{
    check(condition, what);
    if(!condition)
    {
        throw runner::case_stopped{};
    }
}

void skip(const std::string& reason)
{
    throw runner::case_skipped{reason, false};
}

void skipped(const std::string& reason)
{
    auto& state = get_state();
    state.skips.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        std::printf("  SKIP: %s\n", reason.c_str());
    }
    runner::add_note("SKIP: " + reason);
}

auto get_tool_path(const std::string& name) -> fs::path
{
    return fs::resolve_protocol("binary:/" + name + fs::executable_extension());
}

auto use_tool(const std::string& name) -> bool
{
    if(runner::get_options().no_tools)
    {
        skipped("The " + name + " checks are turned off by --no-tools.");
        return false;
    }
    fs::error_code ec;
    if(!fs::exists(get_tool_path(name), ec))
    {
        get_state().missing_tools.fetch_add(1, std::memory_order_relaxed);
        skipped(name + " is not built beside the runner, so its checks did not run.");
        return false;
    }
    return true;
}

void require_tool(const std::string& name)
{
    if(!use_tool(name))
    {
        throw runner::case_skipped{name, true};
    }
}

void note_progress()
{
    runner::get_watch().last_progress.store(runner::now_ticks(), std::memory_order_relaxed);
}

auto failure_count() -> int
{
    return get_state().failures.load();
}

auto wait_until(const std::function<bool()>& is_done, std::chrono::milliseconds timeout, const std::string& what)
    -> bool
{
    const auto deadline = runner::clock_type::now() + timeout;
    while(!is_done())
    {
        if(runner::clock_type::now() >= deadline)
        {
            check(false, what + " (waited " + std::to_string(timeout.count()) + " ms)");
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

auto temp_path(const std::string& name) -> fs::path
{
    const auto parent = fs::temp_directory_path() / "unravel-tests" / std::to_string(process::get_current_process_id());
    fs::error_code ec;
    fs::create_directories(parent, ec);
    return parent / name;
}

auto run_tool(const std::vector<std::string>& arguments, std::chrono::seconds timeout) -> tool_result
{
    const auto deadline = runner::clock_type::now() + timeout;
    std::atomic<bool> timed_out{false};
    const auto result = subprocess::call(arguments,
                                         {},
                                         {},
                                         [&]()
                                         {
                                             if(runner::clock_type::now() < deadline)
                                             {
                                                 return false;
                                             }
                                             timed_out.store(true);
                                             return true;
                                         },
                                         runner::MAX_TOOL_OUTPUT);
    tool_result tool;
    tool.started = result.started;
    tool.timed_out = timed_out.load();
    tool.exit_code = result.retcode;
    tool.output = result.out_output;
    if(!result.err_output.empty())
    {
        tool.output += (tool.output.empty() ? "" : "\n") + result.err_output;
    }
    return tool;
}

auto run_parallel(std::size_t count, const std::function<void(std::size_t)>& job) -> std::vector<double>
{
    std::vector<std::string> errors;
    // One slot per job, written only by that job.
    std::vector<double> seconds(count, 0.0);
    const auto run_one = [&](std::size_t index) -> std::string
    {
        const auto start = runner::clock_type::now();
        try
        {
            job(index);
            seconds[index] = runner::seconds_since(start);
            note_progress();
            return {};
        }
        catch(const std::exception& ex)
        {
            return "parallel job " + std::to_string(index) + " threw an exception: " + ex.what();
        }
        catch(...)
        {
            return "parallel job " + std::to_string(index) + " threw an exception that is not a std::exception.";
        }
    };
    if(count < 2 || is_single_threaded())
    {
        for(std::size_t index = 0; index < count; ++index)
        {
            errors.push_back(run_one(index));
        }
    }
    else
    {
        const auto thread_count =
            static_cast<unsigned int>(std::min<std::size_t>(count, std::max(1u, std::thread::hardware_concurrency())));
        // A pool of the jobs' own: a job that starts the engine's parallel loops dispatches them to the frame pool,
        // which a job running on the frame pool itself could leave waiting on its own queue.
        task_thread_pool::task_thread_pool pool(thread_count);
        std::vector<std::future<std::string>> results;
        results.reserve(count);
        for(std::size_t index = 0; index < count; ++index)
        {
            results.push_back(pool.submit(run_one, index));
        }
        for(auto& result : results)
        {
            errors.push_back(result.get());
        }
    }
    for(const auto& error : errors)
    {
        if(!error.empty())
        {
            check(false, error);
        }
    }
    return seconds;
}

void print_slowest(const std::vector<double>& seconds, const std::function<std::string(std::size_t)>& name, std::size_t count)
{
    std::vector<std::size_t> order(seconds.size());
    for(std::size_t index = 0; index < order.size(); ++index)
    {
        order[index] = index;
    }
    std::stable_sort(order.begin(),
                     order.end(),
                     [&](std::size_t lhs, std::size_t rhs)
                     {
                         return seconds[lhs] > seconds[rhs];
                     });
    double total = 0.0;
    for(const double value : seconds)
    {
        total += value;
    }
    auto& state = get_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    std::printf("  %zu parallel jobs, %.1f s of work; the slowest:\n", seconds.size(), total);
    for(std::size_t i = 0; i < std::min(count, order.size()); ++i)
    {
        std::printf("    %8.1f s  %s\n", seconds[order[i]], name(order[i]).c_str());
    }
}

void run_case(const char* call_text, const std::function<void()>& body)
{
    const std::string name = runner::make_case_name(call_text);
    const auto& filter = runner::get_options().case_filter;
    if(!filter.empty() && name.find(filter) == std::string::npos)
    {
        return;
    }
    auto& state = get_state();
    runner::case_result record;
    record.name = name;
    runner::case_result* previous = nullptr;
    std::string suite_name;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        previous = state.current_case;
        state.current_case = &record;
        suite_name = state.result != nullptr ? state.result->name : std::string{};
    }
    runner::set_watched_names(suite_name, name);
    runner::print_protocol("begin " + name);
    const int checks_before = state.checks.load();
    const int failures_before = state.failures.load();
    note_progress();
    const auto start = runner::clock_type::now();
    bool is_skipped = false;
    try
    {
        body();
    }
    catch(const runner::case_stopped&)
    {
    }
    catch(const runner::case_skipped& skip)
    {
        if(!skip.is_recorded)
        {
            skipped(skip.reason);
        }
        record.skip_reason = skip.reason;
        is_skipped = true;
    }
    catch(const std::exception& ex)
    {
        runner::record_failure(name + " threw an exception: " + ex.what());
    }
    catch(...)
    {
        runner::record_failure(name + " threw an exception that is not a std::exception.");
    }
    record.seconds = runner::seconds_since(start);
    record.checks = state.checks.load() - checks_before;
    record.failures = state.failures.load() - failures_before;
    record.result = record.failures > 0 ? runner::outcome::failed : is_skipped ? runner::outcome::skipped : runner::outcome::passed;
    char line[128];
    std::snprintf(line,
                  sizeof(line),
                  "case %s %.3f %d %d ",
                  runner::get_outcome_name(record.result),
                  record.seconds,
                  record.checks,
                  record.failures);
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.current_case = previous;
        if(state.result != nullptr)
        {
            state.result->cases.push_back(std::move(record));
        }
    }
    runner::set_watched_names(suite_name, previous != nullptr ? previous->name : std::string{});
    runner::print_protocol(line + name);
    note_progress();
}

} // namespace test

} // namespace unravel
