#include "runner.h"

#include <concurrency/concurrentqueue.h>
#include <subprocess/subprocess.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

namespace unravel::runner
{
namespace
{
using clock_type = std::chrono::steady_clock;

/// Seconds a worker may print nothing beyond its own idle timeout before the runner stops it. The worker's watchdog
/// normally stops it first, and says where it was.
constexpr int PARENT_GRACE_SECONDS = 120;
/// Output a worker may print before it is stopped, so a runaway print loop cannot fill memory.
constexpr std::size_t MAX_WORKER_OUTPUT = 256u * 1024u * 1024u;
/// How long the scheduler sleeps when no worker has ended.
constexpr auto POLL_INTERVAL = std::chrono::milliseconds(20);
/// Expected seconds of a tools suite that has no recorded time: it starts before the others.
constexpr double UNKNOWN_TOOLS_SECONDS = 1.0e6;

auto starts_with(const std::string& text, const std::string& prefix) -> bool
{
    return text.compare(0, prefix.size(), prefix) == 0;
}

/// Splits off the first word of `text`, leaving the rest in `text`.
auto take_word(std::string& text) -> std::string
{
    const auto end = text.find(' ');
    std::string word = text.substr(0, end);
    text = end == std::string::npos ? std::string{} : text.substr(end + 1);
    return word;
}

auto to_int(const std::string& text) -> int
{
    try
    {
        return std::stoi(text);
    }
    catch(...)
    {
        return 0;
    }
}

auto to_double(const std::string& text) -> double
{
    try
    {
        return std::stod(text);
    }
    catch(...)
    {
        return 0.0;
    }
}

/// Builds a suite's result from what its worker prints and how it exits.
class worker_reader
{
public:
    explicit worker_reader(const std::string& name)
    {
        result_.name = name;
    }

    void read_line(std::string line)
    {
        if(!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if(starts_with(line, PROTOCOL_PREFIX))
        {
            read_protocol(line.substr(std::char_traits<char>::length(PROTOCOL_PREFIX)));
            return;
        }
        result_.output += line;
        result_.output += '\n';
        if(is_in_case_)
        {
            case_output_ += line;
            case_output_ += '\n';
        }
        else if(starts_with(line, "  FAIL: ") && result_.failure_messages.size() < 32)
        {
            result_.failure_messages.push_back(line.substr(8));
        }
        if(is_in_case_ && starts_with(line, "  FAIL: ") && case_failures_.size() < 32)
        {
            case_failures_.push_back(line.substr(8));
        }
    }

    auto finish(const subprocess::call_result& call, bool is_stopped_by_runner, int grace_seconds, double wall_seconds)
        -> suite_result
    {
        result_.exit_code = call.retcode;
        result_.wall_seconds = wall_seconds;
        if(!has_suite_line_)
        {
            result_.seconds = wall_seconds;
        }
        const std::string where = current_case_.empty() ? std::string{} : " in case " + current_case_;
        if(!call.started)
        {
            result_.result = outcome::crashed;
            result_.reason = "The worker did not start: " + call.out_output;
        }
        else if(has_timeout_line_)
        {
            result_.result = outcome::timed_out;
            result_.reason = "No progress for " + std::to_string(timeout_seconds_) + " s" +
                             (timeout_case_.empty() ? std::string{} : " in case " + timeout_case_) +
                             "; its watchdog stopped it.";
        }
        else if(is_stopped_by_runner)
        {
            result_.result = outcome::timed_out;
            result_.reason = "It printed nothing for " + std::to_string(grace_seconds) + " s" + where +
                             "; the runner stopped it.";
        }
        else if(call.is_output_cut)
        {
            result_.result = outcome::crashed;
            result_.reason = "It printed more than the runner keeps" + where + "; the runner stopped it.";
        }
        else if(!has_suite_line_)
        {
            char code[64];
            std::snprintf(code, sizeof(code), "%d (0x%08X)", call.retcode, static_cast<unsigned int>(call.retcode));
            result_.result = outcome::crashed;
            result_.reason = std::string("The worker ended with exit code ") + code + where + " before it reported.";
        }
        if(is_in_case_ && !current_case_.empty())
        {
            case_result unfinished;
            unfinished.name = current_case_;
            unfinished.result = result_.result;
            unfinished.output = std::move(case_output_);
            unfinished.failure_messages = std::move(case_failures_);
            unfinished.failure_messages.push_back(result_.reason);
            result_.cases.push_back(std::move(unfinished));
        }
        return std::move(result_);
    }

private:
    void read_protocol(std::string line)
    {
        const std::string kind = take_word(line);
        if(kind == "begin")
        {
            is_in_case_ = true;
            current_case_ = line;
            case_output_.clear();
            case_failures_.clear();
        }
        else if(kind == "case")
        {
            case_result record;
            record.result = parse_outcome(take_word(line));
            record.seconds = to_double(take_word(line));
            record.checks = to_int(take_word(line));
            record.failures = to_int(take_word(line));
            record.name = line;
            record.output = std::move(case_output_);
            record.failure_messages = std::move(case_failures_);
            if(record.result == outcome::skipped)
            {
                record.skip_reason = skip_reason_;
            }
            result_.cases.push_back(std::move(record));
            is_in_case_ = false;
            current_case_.clear();
            case_output_.clear();
            case_failures_.clear();
            skip_reason_.clear();
        }
        else if(kind == "suite")
        {
            has_suite_line_ = true;
            result_.result = parse_outcome(take_word(line));
            result_.seconds = to_double(take_word(line));
            result_.checks = to_int(take_word(line));
            result_.failures = to_int(take_word(line));
            result_.xfails = to_int(take_word(line));
            result_.xpasses = to_int(take_word(line));
            result_.skips = to_int(take_word(line));
            result_.missing_tools = to_int(take_word(line));
        }
        else if(kind == "reason")
        {
            result_.reason = line;
        }
        else if(kind == "note")
        {
            result_.notes.push_back(line);
            if(is_in_case_ && starts_with(line, "SKIP: "))
            {
                skip_reason_ = line.substr(6);
            }
        }
        else if(kind == "timeout")
        {
            has_timeout_line_ = true;
            timeout_seconds_ = to_int(take_word(line));
            timeout_case_ = line;
        }
    }

    suite_result result_;
    bool has_suite_line_{};
    bool has_timeout_line_{};
    int timeout_seconds_{};
    std::string timeout_case_;
    bool is_in_case_{};
    std::string current_case_;
    std::string case_output_;
    std::vector<std::string> case_failures_;
    std::string skip_reason_;
};

/// One suite waiting for, or holding, worker slots.
struct worker_job
{
    const test_suite* suite = nullptr;
    double expected_seconds{};
    int slots{1};
};

auto get_worker_idle_limit(const test_suite& suite) -> int
{
    const int configured = get_options().idle_timeout_seconds;
    if(configured == 0)
    {
        return 0;
    }
    return (configured > 0 ? configured : get_idle_timeout_seconds(suite.traits)) + PARENT_GRACE_SECONDS;
}

/// Runs `suite` in a worker process to its end and returns its result.
auto run_worker(const test_suite& suite, const parallel_options& options) -> suite_result
{
    std::vector<std::string> arguments;
    arguments.reserve(options.arguments.size() + 3);
    arguments.push_back(options.executable);
    arguments.insert(arguments.end(), options.arguments.begin(), options.arguments.end());
    arguments.emplace_back("--worker");
    arguments.push_back(suite.name);

    worker_reader reader(suite.name);
    const int idle_limit = get_worker_idle_limit(suite);
    std::atomic<std::int64_t> last_line{clock_type::now().time_since_epoch().count()};
    std::atomic<bool> is_stopped{false};
    const auto start = clock_type::now();
    const auto call = subprocess::call(
        arguments,
        {},
        [&](const std::string& line)
        {
            last_line.store(clock_type::now().time_since_epoch().count());
            reader.read_line(line);
        },
        [&]()
        {
            if(idle_limit <= 0)
            {
                return false;
            }
            const auto quiet = clock_type::now() - clock_type::time_point(clock_type::duration(last_line.load()));
            if(quiet < std::chrono::seconds(idle_limit))
            {
                return false;
            }
            is_stopped.store(true);
            return true;
        },
        MAX_WORKER_OUTPUT);
    const double wall_seconds = std::chrono::duration<double>(clock_type::now() - start).count();
    return reader.finish(call, is_stopped.load(), idle_limit, wall_seconds);
}

auto is_problem(const suite_result& result) -> bool
{
    return result.result == outcome::failed || result.result == outcome::timed_out || result.result == outcome::crashed;
}

void print_finished(const suite_result& result, std::size_t done, std::size_t total, bool verbose)
{
    std::printf("[%3zu/%zu] %-7s %7.1f s  %s\n",
                done,
                total,
                get_outcome_name(result.result),
                result.wall_seconds,
                describe_suite(result).c_str());
    if(verbose || is_problem(result))
    {
        std::printf("---------- output of %s ----------\n%s---------- end of %s ----------\n",
                    result.name.c_str(),
                    result.output.c_str(),
                    result.name.c_str());
    }
}

auto make_jobs(const std::vector<test_suite>& suites, const parallel_options& options, bool exclusive)
    -> std::vector<worker_job>
{
    std::vector<worker_job> jobs;
    for(const auto& suite : suites)
    {
        if((suite.traits.lane == suite_lane::exclusive) != exclusive)
        {
            continue;
        }
        worker_job job;
        job.suite = &suite;
        job.slots = get_slot_count(suite.traits, options.jobs);
        const auto known = options.durations.find(suite.name);
        job.expected_seconds = known != options.durations.end() ? known->second
                               : suite.traits.lane == suite_lane::tools ? UNKNOWN_TOOLS_SECONDS
                                                                        : 0.0;
        jobs.push_back(job);
    }
    // The longest first, so the run ends close to its longest suite; equal ones by name.
    std::stable_sort(jobs.begin(),
                     jobs.end(),
                     [](const worker_job& lhs, const worker_job& rhs)
                     {
                         return lhs.expected_seconds > rhs.expected_seconds;
                     });
    return jobs;
}
} // namespace

auto get_slot_count(const suite_traits& traits, int jobs) -> int
{
    if(traits.lane == suite_lane::tools)
    {
        return std::max(1, jobs / 4);
    }
    return 1;
}

auto run_in_workers(const std::vector<test_suite>& suites, const parallel_options& options) -> std::vector<suite_result>
{
    const auto parallel_jobs = make_jobs(suites, options, false);
    const auto exclusive_jobs = make_jobs(suites, options, true);

    moodycamel::ConcurrentQueue<std::pair<int, suite_result>> finished;
    std::vector<std::thread> threads;
    std::vector<suite_result> results;
    results.reserve(suites.size());
    int used_slots = 0;
    int running = 0;

    const auto launch = [&](const worker_job& job)
    {
        used_slots += job.slots;
        ++running;
        threads.emplace_back(
            [&finished, &options, job]()
            {
                finished.enqueue({job.slots, run_worker(*job.suite, options)});
            });
    };
    const auto collect = [&]() -> bool
    {
        std::pair<int, suite_result> item;
        if(!finished.try_dequeue(item))
        {
            return false;
        }
        used_slots -= item.first;
        --running;
        print_finished(item.second, results.size() + 1, suites.size(), options.verbose);
        results.push_back(std::move(item.second));
        return true;
    };

    std::vector<bool> is_started(parallel_jobs.size(), false);
    std::size_t started_count = 0;
    while(started_count < parallel_jobs.size() || running > 0)
    {
        // In order, the longest first; a job that does not fit lets a smaller one behind it start.
        for(std::size_t i = 0; i < parallel_jobs.size(); ++i)
        {
            if(is_started[i])
            {
                continue;
            }
            const bool fits = running == 0 || used_slots + parallel_jobs[i].slots <= options.jobs;
            if(fits)
            {
                launch(parallel_jobs[i]);
                is_started[i] = true;
                ++started_count;
            }
        }
        if(!collect())
        {
            std::this_thread::sleep_for(POLL_INTERVAL);
        }
    }
    // Each alone: what they check depends on timing or on the process's own thread pools.
    for(const auto& job : exclusive_jobs)
    {
        launch(job);
        while(!collect())
        {
            std::this_thread::sleep_for(POLL_INTERVAL);
        }
    }
    for(auto& thread : threads)
    {
        thread.join();
    }
    std::sort(results.begin(),
              results.end(),
              [](const suite_result& lhs, const suite_result& rhs)
              {
                  return lhs.name < rhs.name;
              });
    return results;
}

} // namespace unravel::runner
