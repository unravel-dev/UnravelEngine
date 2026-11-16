#include "tests.h"

#include "runner.h"

#include <engine/assets/asset_manager.h>
#include <engine/engine.h>
#include <engine/threading/threader.h>

#include <filesystem/filesystem.h>
#include <logging/logging.h>
#include <process/process.h>
#include <reflection/reflection.h>
#include <reflection/registration.h>
#include <service/service.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <random>
#include <thread>

#include <entt/core/hashed_string.hpp>
#include <entt/meta/factory.hpp>

namespace unravel
{

namespace
{
/// How this process runs its suites.
enum class run_mode
{
    /// One after another, in this process.
    in_process,
    /// Each in a worker process of its own, several at once.
    parallel,
    /// One suite, for a parent run.
    worker,
};

bool has_run{};
int process_exit_code{};
bool are_services_initialised{};
run_mode mode = run_mode::in_process;

/// Read during init and kept. The service resets the parser once every module has been initialised, so reading it
/// from process() finds nothing.
std::map<std::string, bool> switches;
std::string suite_filter;
std::string worker_suite;
std::string junit_path;
int jobs{};
int repeat{1};
int shuffle_seed{};
bool is_verbose{};
std::vector<test_suite> selection;
std::vector<std::string> command_line;

/// The options only a parent reads, which its workers do not get: with their values, then without.
const std::vector<std::string> PARENT_OPTIONS_WITH_VALUES = {"--suite", "-s", "--jobs", "-j", "--junit", "--repeat", "--shuffle", "--worker"};
const std::vector<std::string> PARENT_SWITCHES = {"--verbose", "--list"};

auto find_suite(const std::string& name) -> const test_suite*
{
    static const auto suites = get_test_suites();
    const auto found = std::find_if(suites.begin(),
                                    suites.end(),
                                    [&](const test_suite& suite)
                                    {
                                        return suite.name == name;
                                    });
    return found != suites.end() ? &*found : nullptr;
}

/// The folder a suite keeps its log, its output and its app: files in.
auto get_suite_folder(const std::string& name) -> fs::path
{
    return runner::get_results_folder() / make_suite_slug(name);
}

/// Points app: at an empty folder of the suite's own, so no suite sees what another left there.
void prepare_app_folder(const test_suite& suite)
{
    const fs::path app = get_suite_folder(suite.name) / "app";
    fs::error_code err;
    fs::remove_all(app, err);
    fs::create_directories(app, err);
    fs::add_path_protocol("app", app);
}

/// What a worker is started with: the run's own command line without the options only a parent reads.
auto make_worker_arguments() -> std::vector<std::string>
{
    std::vector<std::string> arguments;
    for(std::size_t i = 1; i < command_line.size(); ++i)
    {
        const auto& argument = command_line[i];
        const auto is_named = [&](const std::vector<std::string>& names)
        {
            return std::any_of(names.begin(),
                               names.end(),
                               [&](const std::string& name)
                               {
                                   return argument == name || argument.rfind(name + "=", 0) == 0;
                               });
        };
        if(is_named(PARENT_SWITCHES))
        {
            continue;
        }
        if(is_named(PARENT_OPTIONS_WITH_VALUES))
        {
            // A value given as its own argument goes with the option.
            if(argument.find('=') == std::string::npos)
            {
                ++i;
            }
            continue;
        }
        arguments.push_back(argument);
    }
    return arguments;
}

auto get_automatic_jobs() -> int
{
    return std::max(2, static_cast<int>(std::thread::hardware_concurrency() / 2));
}

auto select_suites() -> std::vector<test_suite>
{
    std::vector<test_suite> selected;
    for(const auto& suite : get_test_suites())
    {
        if(suite_filter.empty() || suite.name.find(suite_filter) != std::string::npos)
        {
            selected.push_back(suite);
        }
    }
    return selected;
}

auto is_problem(const runner::suite_result& result) -> bool
{
    return result.result == runner::outcome::failed || result.result == runner::outcome::timed_out ||
           result.result == runner::outcome::crashed;
}

void run_as_worker(rtti::context& ctx)
{
    const auto* suite = find_suite(worker_suite);
    if(suite == nullptr)
    {
        std::printf("There is no suite named \"%s\"; --list prints their names.\n", worker_suite.c_str());
        process_exit_code = runner::EXIT_FAILED;
        return;
    }
    prepare_app_folder(*suite);
    runner::start_watchdog();
    const auto result = runner::run_suite(*suite, ctx);
    runner::stop_watchdog();
    std::printf("%s %s (%.1f s)\n", runner::get_outcome_name(result.result), runner::describe_suite(result).c_str(), result.seconds);
    runner::print_suite_protocol_line(result);
    process_exit_code = runner::get_exit_code(result);
}

auto run_in_process(rtti::context& ctx) -> std::vector<runner::suite_result>
{
    auto order = selection;
    if(shuffle_seed != 0)
    {
        std::mt19937 random(static_cast<std::uint32_t>(shuffle_seed));
        std::shuffle(order.begin(), order.end(), random);
        std::printf("The suites run in an order shuffled with seed %d.\n", shuffle_seed);
    }
    std::vector<runner::suite_result> results;
    runner::start_watchdog();
    for(int round = 0; round < repeat; ++round)
    {
        for(const auto& suite : order)
        {
            APPLOG_INFO("=== suite: {} ===", suite.name);
            prepare_app_folder(suite);
            auto result = runner::run_suite(suite, ctx);
            std::printf("%s %s (%.1f s)\n",
                        runner::get_outcome_name(result.result),
                        runner::describe_suite(result).c_str(),
                        result.seconds);
            results.push_back(std::move(result));
        }
    }
    runner::stop_watchdog();
    return results;
}

auto run_in_parallel() -> std::vector<runner::suite_result>
{
    runner::parallel_options options;
    options.jobs = jobs;
    options.verbose = is_verbose;
    options.executable = process::get_executable_path();
    options.arguments = make_worker_arguments();
    const fs::path durations_file = runner::get_results_folder() / "durations.txt";
    options.durations = runner::read_durations(durations_file);
    std::printf("Running %zu suites in worker processes, %d slots at once.\n", selection.size(), jobs);
    std::vector<runner::suite_result> results;
    for(int round = 0; round < repeat; ++round)
    {
        auto round_results = runner::run_in_workers(selection, options);
        for(const auto& result : round_results)
        {
            const fs::path folder = get_suite_folder(result.name);
            fs::error_code err;
            fs::create_directories(folder, err);
            std::ofstream(folder / "output.txt", std::ios::trunc | std::ios::binary) << result.output;
        }
        runner::write_durations(durations_file, options.durations, round_results);
        options.durations = runner::read_durations(durations_file);
        results.insert(results.end(), std::make_move_iterator(round_results.begin()), std::make_move_iterator(round_results.end()));
    }
    return results;
}

void finish_run(const std::vector<runner::suite_result>& results, double wall_seconds)
{
    if(results.size() > 1)
    {
        runner::print_report(results, wall_seconds, mode == run_mode::parallel ? jobs : 1);
    }
    if(!junit_path.empty() && !runner::write_junit(fs::path(junit_path), results))
    {
        APPLOG_ERROR("The JUnit report cannot be written to {}.", junit_path);
    }
    long long failures = 0;
    std::size_t problems = 0;
    for(const auto& result : results)
    {
        failures += result.failures;
        problems += is_problem(result) ? 1 : 0;
    }
    if(problems == 0)
    {
        APPLOG_INFO("All suites passed.");
        process_exit_code = 0;
        return;
    }
    APPLOG_ERROR("{} failing check(s); {} suite(s) did not pass.", failures, problems);
    process_exit_code = 1;
}
} // namespace

REFLECTION_REGISTRATION
{
    using namespace entt::literals;

    entt::meta_factory<tests>{}
        .type("tests"_hs)
        .func<&tests::create>("create"_hs)
        .func<&tests::init>("init"_hs)
        .func<&tests::deinit>("deinit"_hs)
        .func<&tests::destroy>("destroy"_hs)
        .func<&tests::process>("process"_hs)
        .func<&tests::interrupt>("interrupt"_hs);
}

void tests::set_command_line(int argc, char* argv[])
{
    command_line.assign(argv, argv + argc);
    for(std::size_t i = 1; i < command_line.size(); ++i)
    {
        const auto& argument = command_line[i];
        if(argument == "--worker" && i + 1 < command_line.size())
        {
            worker_suite = command_line[i + 1];
        }
        else if(argument.rfind("--worker=", 0) == 0)
        {
            worker_suite = argument.substr(9);
        }
    }
}

auto tests::print_list_if_asked(int argc, char* argv[]) -> bool
{
    const bool is_asked = std::any_of(argv + 1,
                                      argv + argc,
                                      [](const char* argument)
                                      {
                                          return std::string(argument) == "--list";
                                      });
    if(!is_asked)
    {
        return false;
    }
    for(const auto& suite : get_test_suites())
    {
        std::printf("%s\t%s\t%d\t%s\n",
                    suite.name.c_str(),
                    get_lane_name(suite.traits.lane),
                    get_idle_timeout_seconds(suite.traits),
                    make_suite_slug(suite.name).c_str());
    }
    return true;
}

auto tests::create(rtti::context& ctx, cmd_line::parser& parser) -> bool
{
    if(!engine::create(ctx, parser))
    {
        return false;
    }
    // A worker logs beside its suite's output: workers run at once and would share one log otherwise.
    engine::open_log(worker_suite.empty() ? fs::resolve_protocol("binary:/") : get_suite_folder(worker_suite));

    parser.set_optional<std::string>("s", "suite", "", "Run only suites whose name contains this.");
    parser.set_optional<std::string>("", "case", "", "Run only the cases whose name contains this.");
    parser.set_optional<int>("j",
                             "jobs",
                             0,
                             "Worker processes to run at once. 0 picks one for every two cores; 1 runs every suite in "
                             "this process, one after another.");
    parser.set_optional<std::string>("", "worker", "", "Run one suite, named exactly, as a worker of a parallel run.");
    parser.set_optional<bool>("", "list", false, "Print every suite with its lane and idle timeout, then exit.");
    parser.set_optional<bool>("",
                              "strict",
                              false,
                              "Fail a suite when a tool its checks need is not built or an expected failure passes.");
    parser.set_optional<bool>("", "no-tools", false, "Skip the checks that run shaderc or texturec.");
    parser.set_optional<int>("",
                             "idle-timeout",
                             -1,
                             "Stop a suite after this many seconds without progress. -1 takes each suite's own; 0 "
                             "never stops one.");
    parser.set_optional<std::string>("", "junit", "", "Write the results to this file as JUnit XML.");
    parser.set_optional<int>("", "repeat", 1, "Run the selected suites this many times.");
    parser.set_optional<int>("",
                             "shuffle",
                             0,
                             "Run the suites of an in-process run in an order shuffled with this seed. 0 keeps them "
                             "sorted by name.");
    parser.set_optional<bool>("", "verbose", false, "Print the output of every suite, not only of those that fail.");
    parser.set_optional<bool>("b", "bench", false, "Run benchmarks as well as correctness checks.");
    parser.set_optional<bool>("bo", "bench-only", false, "Run benchmarks and skip the checks.");
    parser.set_optional<bool>("", "network", false, "Run the checks that reach the internet as well.");

    return true;
}

auto tests::wants(const std::string& option) -> bool
{
    const auto it = switches.find(option);
    return it != switches.end() && it->second;
}

auto tests::init(const cmd_line::parser& parser) -> bool
{
    for(const char* option : {"bench", "bench-only", "network"})
    {
        bool value = false;
        parser.try_get(option, value);
        switches[option] = value;
    }
    parser.try_get("suite", suite_filter);
    parser.try_get("junit", junit_path);
    parser.try_get("jobs", jobs);
    parser.try_get("repeat", repeat);
    parser.try_get("shuffle", shuffle_seed);
    parser.try_get("verbose", is_verbose);
    repeat = std::max(1, repeat);
    if(jobs <= 0)
    {
        jobs = get_automatic_jobs();
    }

    runner::run_options options;
    parser.try_get("strict", options.strict);
    parser.try_get("no-tools", options.no_tools);
    parser.try_get("case", options.case_filter);
    parser.try_get("idle-timeout", options.idle_timeout_seconds);
    options.is_worker = !worker_suite.empty();
    runner::configure(options);

    selection = options.is_worker ? std::vector<test_suite>{} : select_suites();
    mode = options.is_worker                         ? run_mode::worker
           : jobs == 1 || selection.size() <= 1 ? run_mode::in_process
                                                    : run_mode::parallel;
    if(mode == run_mode::parallel)
    {
        // The workers boot their own engine; this process only starts them.
        return true;
    }

    engine::select_raytracing_backend(parser);
    auto& ctx = engine::context();

    // Threading and assets only. The renderer wants a device and the suites do not want a window; anything that
    // needs one does not belong in here. app: points at each suite's own folder (prepare_app_folder).
    if(!ctx.get_cached<threader>().init(ctx, parser))
    {
        APPLOG_CRITICAL("Failed to initialise threading.");
        return false;
    }

    if(!ctx.get_cached<asset_manager>().init(ctx))
    {
        APPLOG_CRITICAL("Failed to initialise the asset manager.");
        return false;
    }
    are_services_initialised = true;
    return true;
}

auto tests::process() -> int
{
    if(has_run)
    {
        return SERVICE_RESULT_EXIT;
    }
    has_run = true;

    // Unbuffered stdout: a mid-suite crash must not swallow the progress output that tells us which test it
    // happened in, and a worker's parent reads its lines as they come.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    auto& ctx = engine::context();
    if(mode == run_mode::worker)
    {
        run_as_worker(ctx);
        return SERVICE_RESULT_EXIT;
    }

    const auto start = std::chrono::steady_clock::now();
    const auto results = mode == run_mode::parallel ? run_in_parallel() : run_in_process(ctx);
    finish_run(results, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    return SERVICE_RESULT_EXIT;
}

auto tests::deinit() -> bool
{
    if(!are_services_initialised)
    {
        return true;
    }
    // The two that were initialised, innermost first: assets hand work to the thread pool.
    auto& ctx = engine::context();
    ctx.get_cached<asset_manager>().deinit(ctx);
    ctx.get_cached<threader>().deinit(ctx);
    return true;
}

auto tests::destroy() -> bool
{
    // Deliberately not engine::destroy(). It tears down every service the engine creates, and this runner starts
    // almost none of them - the renderer, audio, scripting and the ECS systems were never initialised, and taking
    // them down unwinds through state that was never built. The process is about to exit either way.
    return true;
}

auto tests::interrupt() -> bool
{
    return true;
}

auto tests::exit_code() -> int
{
    return process_exit_code;
}

} // namespace unravel
