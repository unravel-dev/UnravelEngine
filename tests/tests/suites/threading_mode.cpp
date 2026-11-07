/*
 * Validation suite for the single-threaded mode (engine/core/concurrency/threading_mode.h), the browser's threading,
 * and the frame clock it relies on.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "threading mode"
 *   <build-dir>/bin/unravel-tests --single-threaded      (every suite, single-threaded)
 *
 * These pin the contract:
 *   - the runner's job pool has no workers exactly in the mode (--single-threaded), and a job then runs on
 *     the thread that waits for it, or in the threader's per-frame pump;
 *   - in either mode a wait on work made from jobs returns: a continuation on the main thread (the script
 *     compile wait), when_all, a polling sleep, async without a thread id (a job in the mode);
 *   - in the mode, parallel loops run inline on the calling thread, in order, and start no pool;
 *     background work runs before run_in_background returns, and background_scope changes nothing;
 *   - a frame longer than the minimum fps allows counts as the longest frame it allows, unless the
 *     minimum is turned off.
 */

#include "../tests.h"

#include <concurrency/parallel.h>
#include <concurrency/threading_mode.h>
#include <engine/threading/threader.h>
#include <simulation/simulation.h>

#include <threadpp/when_all_any.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace unravel;

namespace
{

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

constexpr uint32_t LOOP_ITEMS = 256;
/// Longer than a frame at the default minimum fps allows.
constexpr auto LONG_FRAME = std::chrono::milliseconds(400);

/// Turns the single-threaded mode on for its lifetime and restores what it was.
class single_threaded_scope
{
public:
    single_threaded_scope() : was_on_(is_single_threaded())
    {
        set_single_threaded(true);
    }

    ~single_threaded_scope()
    {
        set_single_threaded(was_on_);
    }

    single_threaded_scope(const single_threaded_scope&) = delete;
    auto operator=(const single_threaded_scope&) -> single_threaded_scope& = delete;

private:
    bool was_on_ = false;
};

void test_runner_pool_follows_the_switch(rtti::context& ctx)
{
    std::printf("test_runner_pool_follows_the_switch\n");
    auto& pool = *ctx.get_cached<threader>().pool;
    std::printf("  single-threaded: %s, %zu workers\n", is_single_threaded() ? "yes" : "no", pool.get_workers_count());
    check((pool.get_workers_count() == 0) == is_single_threaded(), "the job pool has no workers exactly in the mode");
    if(!is_single_threaded())
    {
        return;
    }
    std::thread::id ran_on{};
    auto waited = pool.schedule(
        [&ran_on]()
        {
            ran_on = std::this_thread::get_id();
            return 7;
        });
    check(waited.get() == 7 && ran_on == std::this_thread::get_id(), "a waited job runs on the waiting thread");
    // Earlier suites leave jobs no one waits for, and the runner has no frame between suites: run them first so the
    // pump's budget goes to the job below.
    for(const auto& [name, count] : pool.get_jobs_count_detailed())
    {
        std::printf("  queued by earlier suites: %zu x '%s'\n", count, name.c_str());
    }
    ctx.get_cached<threader>().run_queued_jobs();
    // Shared, not on this stack: a job the pump does not run outlives this function.
    auto is_pumped = std::make_shared<std::atomic<bool>>(false);
    auto polled = pool.schedule(
        [is_pumped]()
        {
            *is_pumped = true;
        });
    ctx.get_cached<threader>().process();
    check(*is_pumped && polled.is_ready(), "the threader's frame pump runs a job no one waits for");
}

void test_waits_on_work_made_from_jobs(rtti::context& ctx)
{
    std::printf("test_waits_on_work_made_from_jobs\n");
    auto& pool = *ctx.get_cached<threader>().pool;
    const auto main_id = tpp::this_thread::get_id();
    // The script compile wait: compile jobs, continuations on the main thread, a wait on the continuations.
    std::vector<tpp::future<int>> continuations;
    for(int i = 0; i < 3; ++i)
    {
        continuations.push_back(pool.schedule(
                                        [i]()
                                        {
                                            return i;
                                        })
                                    .then(main_id,
                                          [](tpp::future<int> compiled)
                                          {
                                              return compiled.get() + 1;
                                          }));
    }
    int sum = 0;
    for(auto& continuation : continuations)
    {
        continuation.wait();
        sum += continuation.get();
    }
    check(sum == 6, "a wait on a continuation of a job on the main thread returns");
    auto first = pool.schedule(
        []()
        {
            return 2;
        });
    auto second = pool.schedule(
        []()
        {
            return 3;
        });
    auto both = tpp::when_all(std::move(first), std::move(second)).get();
    check(std::get<0>(both).get() + std::get<1>(both).get() == 5, "a wait on when_all of jobs returns");
    auto polled = pool.schedule(
        []()
        {
            return 4;
        });
    while(!polled.is_ready())
    {
        tpp::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(polled.get() == 4, "a polling loop with sleep_for sees the job finish");
    std::thread::id ran_on{};
    auto detached = tpp::async(
        [&ran_on]()
        {
            ran_on = std::this_thread::get_id();
            return 5;
        });
    check(detached.get() == 5, "a wait on async without a thread id returns");
    check((ran_on == std::this_thread::get_id()) == is_single_threaded(),
          "async without a thread id runs as a job on the main thread exactly in the mode");
}

void test_loops_run_inline_on_the_calling_thread()
{
    std::printf("test_loops_run_inline_on_the_calling_thread\n");
    const single_threaded_scope mode;
    const std::thread::id caller = std::this_thread::get_id();
    std::vector<uint32_t> order;
    bool is_on_caller = true;
    poolstl::for_each_par_if(true,
                             poolstl::iota_iter<uint32_t>(0),
                             poolstl::iota_iter<uint32_t>(LOOP_ITEMS),
                             [&](uint32_t index)
                             {
                                 is_on_caller = is_on_caller && std::this_thread::get_id() == caller;
                                 order.push_back(index);
                             });
    bool is_in_order = order.size() == LOOP_ITEMS;
    for(uint32_t i = 0; is_in_order && i < LOOP_ITEMS; ++i)
    {
        is_in_order = order[i] == i;
    }
    check(is_on_caller, "a parallel loop runs on the calling thread");
    check(is_in_order, "a parallel loop runs every item once, in order");
    const std::vector<uint32_t> costs{3, 9, 1, 7};
    std::vector<size_t> visited;
    poolstl::for_each_costliest_first_par_if(true,
                                             costs,
                                             [&](size_t index)
                                             {
                                                 is_on_caller = is_on_caller && std::this_thread::get_id() == caller;
                                                 visited.push_back(index);
                                             });
    check(is_on_caller && visited == std::vector<size_t>{1, 3, 0, 2},
          "a costliest-first loop runs inline, the most expensive first");
}

void test_background_work_runs_before_returning()
{
    std::printf("test_background_work_runs_before_returning\n");
    const single_threaded_scope mode;
    std::thread::id ran_on{};
    poolstl::run_in_background(
        [&ran_on]()
        {
            ran_on = std::this_thread::get_id();
        });
    check(ran_on == std::this_thread::get_id(), "background work runs inline before run_in_background returns");
    {
        const poolstl::background_scope background;
        check(poolstl::detail::current_pool == nullptr, "a background scope leaves the loops inline");
    }
}

void test_long_frame_counts_as_the_longest_allowed()
{
    std::printf("test_long_frame_counts_as_the_longest_allowed\n");
    const float longest_allowed = 1.0f / static_cast<float>(simulation::DEFAULT_MIN_FPS);
    const float long_frame = std::chrono::duration<float>(LONG_FRAME).count();
    simulation sim;
    sim.set_time_smoothing_step(0);
    sim.run_one_frame(true);
    std::this_thread::sleep_for(LONG_FRAME);
    sim.run_one_frame(true);
    const float clamped = sim.get_unscaled_delta_time().count();
    std::printf("  a %.0f ms frame counted %.1f ms\n", long_frame * 1000.0f, clamped * 1000.0f);
    check(clamped <= longest_allowed + 1e-4f, "a long frame counts as the longest frame the minimum fps allows");
    check(std::chrono::duration<float>(sim.get_frame_duration()).count() >= long_frame,
          "the frame duration still reports the whole frame");
    sim.set_min_fps(0);
    sim.run_one_frame(true);
    std::this_thread::sleep_for(LONG_FRAME);
    sim.run_one_frame(true);
    check(sim.get_unscaled_delta_time().count() >= long_frame, "without a minimum fps a long frame counts in full");
}

auto run_threading_mode_suite(rtti::context& ctx) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_runner_pool_follows_the_switch(ctx);
    test_waits_on_work_made_from_jobs(ctx);
    test_loops_run_inline_on_the_calling_thread();
    test_background_work_runs_before_returning();
    test_long_frame_counts_as_the_longest_allowed();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("threading mode", run_threading_mode_suite)
