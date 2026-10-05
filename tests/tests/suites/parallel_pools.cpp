/*
 * Validation suite for the engine's two parallel pools (engine/core/concurrency/parallel.h).
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "parallel pools"
 *
 * The frame's loops run on poolSTL's default pool, which serves one queue in order. Work that holds
 * every thread it gets for minutes, such as an asset compile's distance-field bake, runs inside a
 * background_scope on a pool of its own, or a frame waits for a free thread until the bake ends.
 *
 * These pin the contract:
 *   - a loop inside a background_scope runs on the background pool's threads and on none of the
 *     default pool's;
 *   - a loop outside it still runs while every background thread is held;
 *   - a scope covers only its own thread, ends with its block, and nests;
 *   - the background threads run below normal priority (checked on Windows).
 */

#include "../tests.h"

#include <base/platform/thread.hpp>
#include <concurrency/parallel.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <set>
#include <string>
#include <thread>

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

using clock_type = std::chrono::steady_clock;

/// How long a test waits for pool threads before it gives up. Only a broken pool ever reaches it,
/// and reaching it fails the test instead of hanging the runner.
constexpr std::chrono::seconds GIVE_UP_AFTER{10};
constexpr uint32_t DEFAULT_LOOP_ITEMS = 1024;

/// The threads a pool runs a loop on: one item per thread of the current pool, each held until all
/// have started, so no thread can take two.
auto collect_pool_thread_ids() -> std::set<std::thread::id>
{
    const unsigned int thread_count = poolstl::get_current_pool().get_num_threads();
    std::mutex mutex;
    std::condition_variable all_started;
    std::set<std::thread::id> ids;
    unsigned int started_count = 0;
    poolstl::for_each_par_if(true,
                             poolstl::iota_iter<unsigned int>(0),
                             poolstl::iota_iter<unsigned int>(thread_count),
                             [&](unsigned int)
                             {
                                 std::unique_lock<std::mutex> lock(mutex);
                                 ids.insert(std::this_thread::get_id());
                                 ++started_count;
                                 all_started.notify_all();
                                 all_started.wait_for(lock,
                                                      GIVE_UP_AFTER,
                                                      [&]()
                                                      {
                                                          return started_count == thread_count;
                                                      });
                             });
    return ids;
}

void test_background_scope_runs_on_its_own_threads()
{
    std::printf("test_background_scope_runs_on_its_own_threads\n");
    const std::set<std::thread::id> default_ids = collect_pool_thread_ids();
    std::set<std::thread::id> background_ids;
    {
        const poolstl::background_scope background;
        background_ids = collect_pool_thread_ids();
    }
    std::printf("  default pool %zu threads, background pool %zu threads\n", default_ids.size(), background_ids.size());
    check(default_ids.size() == poolstl::get_current_pool().get_num_threads(), "a default loop runs on every default thread");
    check(background_ids.size() == poolstl::get_background_pool().get_num_threads(),
          "a loop in the scope runs on every background thread");
    const bool is_shared = std::any_of(background_ids.begin(),
                                       background_ids.end(),
                                       [&](const std::thread::id& id)
                                       {
                                           return default_ids.count(id) > 0;
                                       });
    check(!is_shared, "the scope's loop runs on none of the default pool's threads");
}

void test_default_loop_runs_while_background_threads_are_held()
{
    std::printf("test_default_loop_runs_while_background_threads_are_held\n");
    const unsigned int background_threads = poolstl::get_background_pool().get_num_threads();
    std::atomic<bool> is_released{false};
    std::atomic<bool> has_given_up{false};
    std::atomic<unsigned int> held_count{0};
    std::atomic<unsigned int> below_normal_count{0};
    // Every background thread takes one item and keeps it, as a bake keeps its workers.
    std::thread holder(
        [&]()
        {
            const poolstl::background_scope background;
            poolstl::for_each_par_if(true,
                                     poolstl::iota_iter<unsigned int>(0),
                                     poolstl::iota_iter<unsigned int>(background_threads),
                                     [&](unsigned int)
                                     {
#if UNRAVEL_PLATFORM_WINDOWS
                                         if(::GetThreadPriority(::GetCurrentThread()) == THREAD_PRIORITY_BELOW_NORMAL)
                                         {
                                             ++below_normal_count;
                                         }
#endif
                                         ++held_count;
                                         const auto deadline = clock_type::now() + GIVE_UP_AFTER;
                                         while(!is_released.load())
                                         {
                                             if(clock_type::now() >= deadline)
                                             {
                                                 has_given_up = true;
                                                 return;
                                             }
                                             std::this_thread::yield();
                                         }
                                     });
        });
    const auto wait_deadline = clock_type::now() + GIVE_UP_AFTER;
    while(held_count.load() < background_threads && clock_type::now() < wait_deadline)
    {
        std::this_thread::yield();
    }
    const bool are_all_held = held_count.load() == background_threads;
    std::atomic<uint32_t> ran_count{0};
    const auto start = clock_type::now();
    poolstl::for_each_par_if(true,
                             poolstl::iota_iter<uint32_t>(0),
                             poolstl::iota_iter<uint32_t>(DEFAULT_LOOP_ITEMS),
                             [&](uint32_t)
                             {
                                 ++ran_count;
                             });
    const double loop_ms = std::chrono::duration<double, std::milli>(clock_type::now() - start).count();
    const bool finished_while_held = !has_given_up.load();
    is_released = true;
    holder.join();
    std::printf("  %u background threads held, the default loop took %.2f ms\n", held_count.load(), loop_ms);
    check(are_all_held, "every background thread holds an item");
    check(ran_count.load() == DEFAULT_LOOP_ITEMS && finished_while_held,
          "a default loop finishes while every background thread is held");
#if UNRAVEL_PLATFORM_WINDOWS
    check(below_normal_count.load() == background_threads, "every background thread runs below normal priority");
#endif
}

void test_background_scope_covers_its_own_thread_and_block()
{
    std::printf("test_background_scope_covers_its_own_thread_and_block\n");
    const auto is_on_background = []() -> bool
    {
        return &poolstl::get_current_pool() == &poolstl::get_background_pool();
    };
    check(!is_on_background(), "outside a scope, loops run on the default pool");
    {
        const poolstl::background_scope outer;
        check(is_on_background(), "inside a scope, loops run on the background pool");
        bool is_other_thread_on_background = true;
        std::thread other(
            [&]()
            {
                is_other_thread_on_background = is_on_background();
            });
        other.join();
        check(!is_other_thread_on_background, "a scope does not cover another thread");
        {
            const poolstl::background_scope inner;
            check(is_on_background(), "a nested scope keeps the background pool");
        }
        check(is_on_background(), "leaving a nested scope keeps the outer one");
    }
    check(!is_on_background(), "leaving the scope restores the default pool");
}

auto run_parallel_pools_suite(rtti::context& ctx) -> int
{
    (void)ctx;
    g_checks = 0;
    g_failures = 0;
    test_background_scope_runs_on_its_own_threads();
    test_default_loop_runs_while_background_threads_are_held();
    test_background_scope_covers_its_own_thread_and_block();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("parallel pools", run_parallel_pools_suite)
