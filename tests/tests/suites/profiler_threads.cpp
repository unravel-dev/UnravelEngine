/*
 * Validation suite for the profiler's per-thread event buffers.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "profiler threads"
 *
 * Every thread records into its own ring and the main thread takes each thread's finished events
 * once a frame; neither waits for the other. The render thread is not in step with the frame: it
 * is usually inside a scope when the main thread takes the events.
 */

#include "../tests.h"

#include <engine/profiler/profiler.h>

#include <atomic>
#include <chrono>
#include <cstdio>
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

/// Names longer than a short-string buffer, so a torn copy shows as a wrong name or a crash.
const std::string OUTER_SCOPE_NAME = "profiler threads test outer scope, an owned name";
const std::string INNER_SCOPE_NAME = "profiler threads test inner scope, an owned name";
constexpr int INNER_SCOPES = 8;
/// How long the writer records while the main thread keeps taking its events.
constexpr std::chrono::milliseconds TAKE_DURATION{1500};

struct taken_events
{
    int count{};
    int outer{};
    int unfinished{};
    int misnamed{};
    int too_deep{};
};

void inspect(const profile_event& event, taken_events& taken)
{
    ++taken.count;
    taken.unfinished += event.end_ns < event.start_ns ? 1 : 0;
    taken.too_deep += event.depth > 1 ? 1 : 0;
    const std::string name = event.name();
    taken.outer += name == OUTER_SCOPE_NAME ? 1 : 0;
    taken.misnamed += name != OUTER_SCOPE_NAME && name != INNER_SCOPE_NAME ? 1 : 0;
}

void take_and_inspect(thread_profile_data& data, taken_events& taken)
{
    data.take_events([&](const profile_event& event) { inspect(event, taken); });
}

void test_taking_events_never_reads_a_buffer_being_written()
{
    std::printf("test_taking_events_never_reads_a_buffer_being_written\n");
    profiler_process_capture_gate_store(1);
    std::atomic<thread_profile_data*> writer_data{nullptr};
    std::atomic<bool> stop{false};
    // Like the render thread: inside a scope nearly all the time, out of step with the taker.
    std::thread writer(
        [&]()
        {
            writer_data.store(get_thread_profile_data("Profiler Threads Test Writer"));
            while(!stop.load(std::memory_order_relaxed))
            {
                const uint32_t outer = profile_begin_owned(OUTER_SCOPE_NAME);
                for(int i = 0; i < INNER_SCOPES; ++i)
                {
                    profile_end(profile_begin_owned(INNER_SCOPE_NAME));
                }
                profile_end(outer);
            }
        });
    while(writer_data.load() == nullptr)
    {
        std::this_thread::yield();
    }
    auto& data = *writer_data.load();
    taken_events taken;
    const auto deadline = std::chrono::steady_clock::now() + TAKE_DURATION;
    while(std::chrono::steady_clock::now() < deadline)
    {
        // As the frame's swap does it.
        take_and_inspect(data, taken);
        std::this_thread::yield();
    }
    stop.store(true);
    writer.join();
    profiler_process_capture_gate_store(0);

    std::printf("  took %d events, %d of them outer scopes\n", taken.count, taken.outer);
    check(taken.count > 0, "events were taken");
    check(taken.outer > 0, "a thread inside a scope at the take hands its events over once the scope closes");
    char message[160];
    std::snprintf(message, sizeof(message), "every event taken is finished (%d of %d unfinished)", taken.unfinished, taken.count);
    check(taken.unfinished == 0, message);
    std::snprintf(message, sizeof(message), "every event taken has its name (%d of %d wrong)", taken.misnamed, taken.count);
    check(taken.misnamed == 0, message);
    std::snprintf(message, sizeof(message), "every event taken has its depth (%d of %d too deep)", taken.too_deep, taken.count);
    check(taken.too_deep == 0, message);
}

void test_an_idle_thread_hands_over_at_the_take()
{
    std::printf("test_an_idle_thread_hands_over_at_the_take\n");
    constexpr int SCOPES = 16;
    profiler_process_capture_gate_store(1);
    thread_profile_data* data = nullptr;
    // Like a pool thread: it records during the frame and is idle when the frame's events are taken.
    std::thread worker(
        [&]()
        {
            data = get_thread_profile_data("Profiler Threads Test Worker");
            for(int i = 0; i < SCOPES; ++i)
            {
                profile_end(profile_begin_owned(INNER_SCOPE_NAME));
            }
        });
    worker.join();
    profiler_process_capture_gate_store(0);
    taken_events taken;
    if(data != nullptr)
    {
        take_and_inspect(*data, taken);
    }
    check(taken.count == SCOPES, "an idle thread's events are taken at once, every one of them");
    check(taken.unfinished == 0 && taken.misnamed == 0, "they are finished and named");
}

void wait_for_step(const std::atomic<int>& step, int expected)
{
    while(step.load() != expected)
    {
        std::this_thread::yield();
    }
}

void test_a_full_ring_drops_new_events_until_taken()
{
    std::printf("test_a_full_ring_drops_new_events_until_taken\n");
    profiler_process_capture_gate_store(1);
    thread_profile_data* data = nullptr;
    std::atomic<int> step{0};
    bool is_dropped_while_full = false;
    bool is_recorded_after_take = false;
    std::thread worker(
        [&]()
        {
            data = get_thread_profile_data("Profiler Threads Test Filler");
            for(uint32_t i = 0; i < thread_profile_data::capacity; ++i)
            {
                profile_end(profile_begin("profiler threads test filler"));
            }
            const uint32_t dropped = profile_begin("profiler threads test filler");
            is_dropped_while_full = dropped == UINT32_MAX;
            profile_end(dropped);
            step.store(1);
            wait_for_step(step, 2);
            const uint32_t recorded = profile_begin("profiler threads test filler");
            is_recorded_after_take = recorded != UINT32_MAX;
            profile_end(recorded);
            step.store(3);
        });
    wait_for_step(step, 1);
    int taken_when_full = 0;
    data->take_events([&](const profile_event&) { ++taken_when_full; });
    step.store(2);
    wait_for_step(step, 3);
    int taken_after = 0;
    data->take_events([&](const profile_event&) { ++taken_after; });
    worker.join();
    profiler_process_capture_gate_store(0);
    check(is_dropped_while_full, "a thread with a full ring left to take drops its new events");
    check(taken_when_full == static_cast<int>(thread_profile_data::capacity), "the take gets the whole ring");
    check(is_recorded_after_take && taken_after == 1, "once taken, the ring records again");
}

auto run_profiler_threads_suite(rtti::context& ctx) -> int
{
    (void)ctx;
    g_checks = 0;
    g_failures = 0;
    test_taking_events_never_reads_a_buffer_being_written();
    test_an_idle_thread_hands_over_at_the_take();
    test_a_full_ring_drops_new_events_until_taken();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("profiler threads", run_profiler_threads_suite)
