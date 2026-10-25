/*
 * Validation suite for asset_handle (engine/engine/assets/asset_handle.h).
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "asset handle"
 *
 * A crowd of models drawing one mesh reads that mesh's handle from every pool thread at once, so the
 * read paths answer from a validity flag published with each snapshot, a shared lock on the cache and
 * an access stamp that is only rewritten once it is older than its resolution.
 *
 * These pin the contract:
 *   - is_valid() follows every published state: no link, ids only, a scheduled job, a deferred job,
 *     a demotion and an invalidation;
 *   - after writers race on one link, is_valid() agrees with the snapshot the other queries read;
 *   - cache hits from many threads at once all return the loaded asset;
 *   - the last-access stamp moves forward with get(), try_get() and version(), and never back.
 */

#include "../tests.h"

#include <engine/assets/asset_handle.h>
#include <engine/threading/threader.h>

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

/// The asset type the handles under test load.
struct probe_asset
{
    int value{};
};

using probe_handle = asset_handle<probe_asset>;

constexpr int PROBE_VALUE = 42;
constexpr int READER_THREADS = 16;
constexpr int READS_PER_THREAD = 20000;
constexpr int WRITER_THREADS = 4;
constexpr int RACE_READER_THREADS = 8;
constexpr int RACE_TRIALS = 40;
constexpr int WRITES_PER_TRIAL = 200;
/// Longer than asset_detail::LAST_ACCESS_RESOLUTION_NS, so a later access must move the stamp.
constexpr std::chrono::milliseconds STAMP_WAIT{5};

tpp::thread_pool* g_pool = nullptr;

auto make_probe() -> std::shared_ptr<probe_asset>
{
    auto probe = std::make_shared<probe_asset>();
    probe->value = PROBE_VALUE;
    return probe;
}

/// A job already handed to the pool, as an immediate load publishes it.
auto make_scheduled_job() -> probe_handle::task_future_t
{
    return g_pool->schedule("test_probe_asset", []() { return make_probe(); }).share();
}

/// A job not submitted yet, as a deferred load publishes it.
auto make_deferred_job() -> probe_handle::task_future_t
{
    return g_pool->create_job("test_probe_asset", []() { return make_probe(); }).share();
}

/// A handle whose asset is loaded and cached.
auto make_loaded_handle() -> probe_handle
{
    probe_handle handle;
    handle.set_internal_ids(hpp::uuid{}, "test:/probe");
    handle.set_internal_job(make_scheduled_job());
    handle.get();
    return handle;
}

/// What the snapshot says about its task, through the queries that load it.
auto has_task_in_snapshot(const probe_handle& handle) -> bool
{
    return handle.is_deferred() || handle.is_loading() || handle.is_ready();
}

/// A non-null result that is not the empty asset carries the probe's value.
auto is_probe_or_absent(const std::shared_ptr<probe_asset>& asset) -> bool
{
    return !asset || probe_handle::is_empty(asset) || asset->value == PROBE_VALUE;
}

void test_validity_follows_every_published_state()
{
    std::printf("is_valid follows every published state\n");
    probe_handle handle;
    check(!handle.is_valid(), "a handle without a link is not valid");
    handle.set_internal_ids(hpp::uuid{}, "test:/probe");
    check(!handle.is_valid(), "a handle with ids only is not valid");
    handle.set_internal_job(make_scheduled_job());
    const probe_handle copy = handle;
    check(handle.is_valid() && copy.is_valid(), "a scheduled job makes the handle and its copy valid");
    const auto loaded = handle.get();
    check(loaded && loaded->value == PROBE_VALUE, "get() returns the loaded asset");
    check(handle.is_ready() && handle.is_valid(), "a loaded asset is ready and valid");
    handle.demote_to_deferred();
    check(!handle.is_valid() && !copy.is_valid(), "a demotion drops the task for every copy");
    check(handle.peek() == nullptr, "a demotion clears the cache");
    check(handle.id() == "test:/probe", "a demotion keeps the id");
    handle.set_internal_job(make_deferred_job());
    check(handle.is_valid() && handle.is_deferred(), "a deferred job makes the handle valid");
    handle.invalidate();
    check(!handle.is_valid() && !has_task_in_snapshot(handle), "an invalidation leaves no task");
    check(!handle.try_get(), "an invalidated handle resolves to nothing");
}

void test_validity_matches_the_snapshot_after_racing_writers()
{
    std::printf("after writers race, is_valid agrees with the snapshot\n");
    int mismatches = 0;
    std::atomic<int> wrong_assets{0};
    for(int trial = 0; trial < RACE_TRIALS; ++trial)
    {
        probe_handle handle = make_loaded_handle();
        std::atomic<bool> is_writing{true};
        std::vector<std::thread> readers;
        for(int r = 0; r < RACE_READER_THREADS; ++r)
        {
            readers.emplace_back(
                [&, reader = handle]()
                {
                    while(is_writing.load(std::memory_order_acquire))
                    {
                        (void)reader.is_valid();
                        (void)reader.version();
                        if(!is_probe_or_absent(reader.try_get()))
                        {
                            wrong_assets.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                });
        }
        std::vector<std::thread> writers;
        for(int w = 0; w < WRITER_THREADS; ++w)
        {
            writers.emplace_back(
                [w, writer = handle]() mutable
                {
                    for(int i = 0; i < WRITES_PER_TRIAL; ++i)
                    {
                        switch((i + w) % 3)
                        {
                            case 0:
                                writer.set_internal_job(make_scheduled_job());
                                break;
                            case 1:
                                writer.set_internal_job(make_deferred_job());
                                break;
                            default:
                                writer.demote_to_deferred();
                                break;
                        }
                    }
                });
        }
        for(auto& writer : writers)
        {
            writer.join();
        }
        is_writing.store(false, std::memory_order_release);
        for(auto& reader : readers)
        {
            reader.join();
        }
        mismatches += handle.is_valid() != has_task_in_snapshot(handle) ? 1 : 0;
        handle.invalidate();
    }
    check(mismatches == 0, "is_valid matches the snapshot after every race (" + std::to_string(mismatches) + " did not)");
    check(wrong_assets.load() == 0, "readers racing the writers only ever see the probe, the empty asset or nothing");
}

void test_cache_hits_from_many_threads_return_the_asset()
{
    std::printf("cache hits from many threads return the loaded asset\n");
    const probe_handle handle = make_loaded_handle();
    const auto expected = handle.peek();
    check(expected != nullptr, "the loaded asset is cached");
    std::atomic<int> wrong_results{0};
    std::vector<std::thread> readers;
    for(int r = 0; r < READER_THREADS; ++r)
    {
        readers.emplace_back(
            [&, reader = handle]()
            {
                for(int i = 0; i < READS_PER_THREAD; ++i)
                {
                    const bool is_right = reader.is_valid() && reader.try_get() == expected && reader.get() == expected &&
                                          reader.version() == uintptr_t(expected.get());
                    if(!is_right)
                    {
                        wrong_results.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });
    }
    for(auto& reader : readers)
    {
        reader.join();
    }
    check(wrong_results.load() == 0, "every read returns the cached asset (" + std::to_string(wrong_results.load()) + " did not)");
}

void test_last_access_moves_forward_and_never_back()
{
    std::printf("the last-access stamp moves forward with access and never back\n");
    const probe_handle handle = make_loaded_handle();
    const auto after_load = handle.last_access();
    std::this_thread::sleep_for(STAMP_WAIT);
    (void)handle.get();
    const auto after_get = handle.last_access();
    check(after_get > after_load, "get() moves the stamp forward");
    check(after_get <= std::chrono::steady_clock::now(), "the stamp is not in the future");
    std::this_thread::sleep_for(STAMP_WAIT);
    (void)handle.try_get();
    const auto after_try_get = handle.last_access();
    check(after_try_get > after_get, "try_get() moves the stamp forward");
    std::this_thread::sleep_for(STAMP_WAIT);
    (void)handle.version();
    const auto after_version = handle.last_access();
    check(after_version > after_try_get, "version() moves the stamp forward");
    (void)handle.get();
    check(handle.last_access() >= after_version, "an access right after another never moves the stamp back");
    (void)handle.peek();
    check(handle.last_access() >= after_version, "peek() does not touch the stamp");
}

auto run_asset_handle_suite(rtti::context& ctx) -> int
{
    g_checks = 0;
    g_failures = 0;
    g_pool = ctx.get_cached<threader>().pool.get();
    test_validity_follows_every_published_state();
    test_validity_matches_the_snapshot_after_racing_writers();
    test_cache_hits_from_many_threads_return_the_asset();
    test_last_access_moves_forward_and_never_back();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("asset handle", run_asset_handle_suite)
