/*
 * Validation suite for the thread CPU time the profiler reads.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "thread cpu"
 *
 * Every profiler scope records a wall interval and the CPU time of its thread over it; the
 * timeline shows the share as Busy. On Windows the CPU time comes from QueryThreadCycleTime(),
 * which counts ticks of the time stamp counter, and turning ticks into nanoseconds takes the
 * rate of that counter. A rate assumed instead of measured (3 GHz against a 4.2 GHz counter)
 * made every Busy time 1.4x too long, longer than the Wall time it is a part of.
 *
 * A scope also has to read its two clocks so that the CPU interval lies inside the wall
 * interval. profile_end() read the wall clock before the CPU clock, a system call that may
 * sample later, and a few scopes in a hundred came out up to a microsecond busier than long.
 *
 * These pin the contract:
 *   - a thread that spins is busy for about its wall time, and never longer;
 *   - a thread that sleeps is not busy;
 *   - no profiler scope is busier than its wall time.
 */

#include "../tests.h"

#include <base/platform/thread.hpp>
#include <engine/profiler/profiler.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
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

constexpr std::chrono::milliseconds MEASURE_DURATION{20};
constexpr int SPIN_ATTEMPTS = 3;
/// A spin may lose the core to another thread, never gain time: busy over wall stays under 1.
/// The margin covers the reads of the two clocks not being at the same instant.
constexpr double MAX_BUSY_RATIO = 1.02;
/// The best of a few spins, so a loaded machine that preempts one of them does not fail it.
constexpr double MIN_SPIN_BUSY_RATIO = 0.5;
constexpr double MAX_SLEEP_BUSY_RATIO = 0.25;
/// Enough scopes that the clock order of profile_end() shows: read the wrong way round, one to
/// five scopes in a hundred came out busier than their wall time.
constexpr int PROFILER_SCOPE_COUNT = 1000;
constexpr std::chrono::microseconds PROFILER_SCOPE_DURATION{200};

struct busy_sample
{
    double wall_ns{};
    double cpu_ns{};

    auto get_ratio() const -> double
    {
        return wall_ns > 0.0 ? cpu_ns / wall_ns : 0.0;
    }
};

template<typename Work>
auto measure(Work&& work) -> busy_sample
{
    const auto wall_start = clock_type::now();
    const int64_t cpu_start = platform::get_thread_cpu_time_ns();
    work();
    const int64_t cpu_end = platform::get_thread_cpu_time_ns();
    const auto wall_end = clock_type::now();
    busy_sample sample{};
    sample.wall_ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(wall_end - wall_start).count());
    sample.cpu_ns = static_cast<double>(cpu_end - cpu_start);
    return sample;
}

void spin_for(std::chrono::nanoseconds duration)
{
    const auto end = clock_type::now() + duration;
    while(clock_type::now() < end)
    {
    }
}

void test_spin_is_busy_for_its_wall_time()
{
    // The first read measures the counter rate; it stays out of every sample.
    platform::get_thread_cpu_time_ns();
    double best_ratio = 0.0;
    double worst_ratio = 0.0;
    for(int attempt = 0; attempt < SPIN_ATTEMPTS; ++attempt)
    {
        const busy_sample sample = measure([] { spin_for(MEASURE_DURATION); });
        best_ratio = std::max(best_ratio, sample.get_ratio());
        worst_ratio = std::max(worst_ratio, sample.get_ratio());
    }
    char message[128];
    std::snprintf(message, sizeof(message), "a spin is never busy for longer than its wall time (busy/wall %.3f)", worst_ratio);
    check(worst_ratio <= MAX_BUSY_RATIO, message);
    std::snprintf(message, sizeof(message), "a spin is busy for most of its wall time (busy/wall %.3f)", best_ratio);
    check(best_ratio >= MIN_SPIN_BUSY_RATIO, message);
}

void test_sleep_is_not_busy()
{
    const busy_sample sample = measure([] { std::this_thread::sleep_for(MEASURE_DURATION); });
    char message[128];
    std::snprintf(message, sizeof(message), "a sleep is not busy (busy/wall %.3f)", sample.get_ratio());
    check(sample.get_ratio() <= MAX_SLEEP_BUSY_RATIO, message);
}

void test_profiler_scope_is_never_busier_than_its_wall_time()
{
    platform::get_thread_cpu_time_ns();
    profiler_process_capture_gate_store(1);
    int recorded_count = 0;
    int busier_count = 0;
    int64_t worst_excess_ns = 0;
    for(int scope = 0; scope < PROFILER_SCOPE_COUNT; ++scope)
    {
        const uint32_t idx = profile_begin("thread cpu time scope");
        if(idx == UINT32_MAX)
        {
            break;
        }
        spin_for(PROFILER_SCOPE_DURATION);
        profile_end(idx);
        const profile_event& ev = get_thread_profile_data()->write_buffer().events[idx];
        const int64_t excess_ns = (ev.cpu_end_ns - ev.cpu_start_ns) - (ev.end_ns - ev.start_ns);
        busier_count += excess_ns > 0 ? 1 : 0;
        worst_excess_ns = std::max(worst_excess_ns, excess_ns);
        ++recorded_count;
    }
    profiler_process_capture_gate_store(0);
    check(recorded_count == PROFILER_SCOPE_COUNT, "the profiler records every scope");
    char message[160];
    std::snprintf(message,
                  sizeof(message),
                  "no profiler scope is busier than its wall time (%d of %d, worst by %lld ns)",
                  busier_count,
                  recorded_count,
                  static_cast<long long>(worst_excess_ns));
    check(busier_count == 0, message);
}

auto run_thread_cpu_time_suite(rtti::context& ctx) -> int
{
    (void)ctx;
    g_checks = 0;
    g_failures = 0;
    test_spin_is_busy_for_its_wall_time();
    test_sleep_is_not_busy();
    test_profiler_scope_is_never_busier_than_its_wall_time();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("thread cpu time", run_thread_cpu_time_suite)
