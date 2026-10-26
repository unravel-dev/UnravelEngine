/*
 * Validation suite for asset_handle (engine/engine/assets/asset_handle.h).
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "asset handle"
 *
 * A crowd of models drawing one mesh reads that mesh's handle from every pool thread at once, so the
 * read paths load the link's state under an epoch guard (asset_detail::epoch_domain) and write
 * nothing the other readers read; writers replace the state and retire the old one.
 *
 * These pin the contract:
 *   - a retired object is freed only once no reader can reach it: not while a reader that came
 *     before is inside, nested guards included; collect_all waits for those readers; slots of ended
 *     threads are reused;
 *   - is_valid() follows every published state: no link, ids only, a scheduled job, a deferred job,
 *     a demotion and an invalidation;
 *   - after writers race on one link, is_valid() agrees with the snapshot the other queries read;
 *   - cache hits from many threads at once all return the loaded asset;
 *   - a get() that waited on a load is seen by peek() and version(), but never caches its asset when
 *     a reload replaced the task meanwhile; an id change keeps the loaded asset;
 *   - readers racing reloads, demotions and invalidations never receive a freed asset;
 *   - visit() passes what try_get() returns without taking a reference, keeps the visited asset
 *     alive through a reload on another thread, nests, and with `wait` waits for a running load;
 *   - the last-access stamp moves forward with get(), try_get() and version(), and never back.
 */

#include "../tests.h"

#include <engine/assets/asset_handle.h>
#include <engine/threading/threader.h>

#include <algorithm>
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

/// How long a test lets another thread reach the point it waits at.
constexpr std::chrono::milliseconds SETTLE_WAIT{50};
/// How long a reader holds its guard while collect_all waits for it.
constexpr std::chrono::milliseconds READER_HOLD{60};
constexpr int SLOT_REUSE_THREADS = 50;
constexpr auto STRESS_DURATION = std::chrono::milliseconds(800);
constexpr int STRESS_WRITERS = 2;
constexpr int OLD_VALUE = 1;
constexpr int NEW_VALUE = 2;

/// An object retired to the epoch domain that counts its destruction.
struct retired_probe
{
    static inline std::atomic<int> destroyed{0};
};

void destroy_retired_probe(void* object)
{
    delete static_cast<retired_probe*>(object);
    retired_probe::destroyed.fetch_add(1);
}

/// Spins until `flag` is set; only for flags another thread sets right away.
void wait_for(const std::atomic<bool>& flag)
{
    while(!flag.load())
    {
        std::this_thread::yield();
    }
}

/// Runs `body` on a new thread registered with threadpp, as engine threads are: only a registered
/// thread can wait on a load.
template<typename F>
auto start_waiting_thread(F body) -> std::thread
{
    return std::thread(
        [body = std::move(body)]() mutable
        {
            tpp::this_thread::register_this_thread();
            body();
            tpp::this_thread::unregister_this_thread();
        });
}

/// A job that loads `value` once `release` is set.
auto make_held_job(std::atomic<bool>& release, int value) -> probe_handle::task_future_t
{
    return g_pool
        ->schedule("test_held_probe_asset",
                   [&release, value]()
                   {
                       while(!release.load())
                       {
                           std::this_thread::sleep_for(std::chrono::milliseconds(1));
                       }
                       auto probe = std::make_shared<probe_asset>();
                       probe->value = value;
                       return probe;
                   })
        .share();
}

auto make_value_job(int value) -> probe_handle::task_future_t
{
    return g_pool
        ->schedule("test_probe_asset",
                   [value]()
                   {
                       auto probe = std::make_shared<probe_asset>();
                       probe->value = value;
                       return probe;
                   })
        .share();
}

/// An asset that marks itself freed, for readers to check what they received.
struct marked_asset
{
    static constexpr uint32_t ALIVE = 0xA11FEu;
    static constexpr uint32_t FREED = 0xF4EEDu;

    marked_asset() = default;
    ~marked_asset()
    {
        mark = FREED;
    }
    marked_asset(const marked_asset&) = delete;
    auto operator=(const marked_asset&) -> marked_asset& = delete;

    volatile uint32_t mark{ALIVE};
};

using marked_handle = asset_handle<marked_asset>;

auto make_marked_job() -> marked_handle::task_future_t
{
    return g_pool->schedule("test_marked_asset", []() { return std::make_shared<marked_asset>(); }).share();
}

void test_epoch_frees_only_after_earlier_readers_leave()
{
    std::printf("a retired object is freed once no reader can reach it\n");
    auto& domain = asset_detail::epoch_domain::instance();
    domain.collect_all();
    const int destroyed_before = retired_probe::destroyed.load();
    domain.retire(new retired_probe(), destroy_retired_probe);
    check(retired_probe::destroyed.load() == destroyed_before + 1, "with no reader inside, a retired object is freed at once");
    std::atomic<bool> is_inside{false};
    std::atomic<bool> may_leave{false};
    std::thread reader(
        [&]()
        {
            asset_detail::epoch_domain::guard guard;
            is_inside.store(true);
            wait_for(may_leave);
        });
    wait_for(is_inside);
    domain.retire(new retired_probe(), destroy_retired_probe);
    domain.collect();
    check(retired_probe::destroyed.load() == destroyed_before + 1 && domain.get_retired_count() == 1,
          "a reader that came before keeps a retired object alive");
    may_leave.store(true);
    reader.join();
    domain.collect();
    check(retired_probe::destroyed.load() == destroyed_before + 2 && domain.get_retired_count() == 0,
          "once that reader left, the object is freed");
    {
        asset_detail::epoch_domain::guard outer;
        {
            asset_detail::epoch_domain::guard inner;
        }
        domain.retire(new retired_probe(), destroy_retired_probe);
        check(retired_probe::destroyed.load() == destroyed_before + 2, "a closed inner guard leaves the outer one reading");
    }
    domain.collect();
    check(retired_probe::destroyed.load() == destroyed_before + 3, "closing the outer guard ends the read");
}

void test_epoch_collect_all_waits_for_readers()
{
    std::printf("collect_all waits for the readers a retired object waits for\n");
    auto& domain = asset_detail::epoch_domain::instance();
    domain.collect_all();
    const int destroyed_before = retired_probe::destroyed.load();
    std::atomic<bool> is_inside{false};
    std::thread reader(
        [&]()
        {
            asset_detail::epoch_domain::guard guard;
            is_inside.store(true);
            std::this_thread::sleep_for(READER_HOLD);
        });
    wait_for(is_inside);
    domain.retire(new retired_probe(), destroy_retired_probe);
    domain.collect_all();
    check(retired_probe::destroyed.load() == destroyed_before + 1 && domain.get_retired_count() == 0,
          "collect_all returns with every retired object freed");
    reader.join();
}

void test_epoch_reuses_slots_of_ended_threads()
{
    std::printf("threads that ended give their slots to new ones\n");
    auto& domain = asset_detail::epoch_domain::instance();
    {
        std::thread first([]() { asset_detail::epoch_domain::guard guard; });
        first.join();
    }
    const size_t slots_before = domain.get_thread_slot_count();
    for(int i = 0; i < SLOT_REUSE_THREADS; ++i)
    {
        std::thread reader([]() { asset_detail::epoch_domain::guard guard; });
        reader.join();
    }
    check(domain.get_thread_slot_count() == slots_before,
          "one thread after another reuse one slot (" + std::to_string(domain.get_thread_slot_count() - slots_before) +
              " new)");
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

void test_waited_get_is_seen_by_peek_and_version()
{
    std::printf("a get() that waited for its load is seen by peek() and version()\n");
    std::atomic<bool> release{false};
    probe_handle handle;
    handle.set_internal_ids(hpp::uuid{}, "test:/held");
    handle.set_internal_job(make_held_job(release, OLD_VALUE));
    std::shared_ptr<probe_asset> waited;
    std::thread waiter = start_waiting_thread([&, copy = handle]() { waited = copy.get(); });
    std::this_thread::sleep_for(SETTLE_WAIT);
    check(handle.peek() == nullptr && handle.version() == 0, "nothing is resolved while the load runs");
    release.store(true);
    waiter.join();
    check(waited && waited->value == OLD_VALUE, "the waiting get() returns the loaded asset");
    check(handle.peek() == waited, "peek() returns the asset the waiting get() loaded");
    check(handle.version() == uintptr_t(waited.get()), "version() is that asset's address");
}

void test_reload_during_a_wait_keeps_the_new_asset()
{
    std::printf("a get() waiting on a replaced load does not cache its asset\n");
    std::atomic<bool> release{false};
    std::atomic<bool> is_waiting{false};
    probe_handle handle;
    handle.set_internal_ids(hpp::uuid{}, "test:/reloaded");
    handle.set_internal_job(make_held_job(release, OLD_VALUE));
    std::shared_ptr<probe_asset> waited;
    std::thread waiter = start_waiting_thread(
        [&, copy = handle]()
        {
            is_waiting.store(true);
            waited = copy.get();
        });
    wait_for(is_waiting);
    std::this_thread::sleep_for(SETTLE_WAIT);
    handle.set_internal_job(make_value_job(NEW_VALUE));
    const auto reloaded = handle.get();
    release.store(true);
    waiter.join();
    check(waited && waited->value == OLD_VALUE, "the waiting get() returns the asset of the load it waited on");
    check(reloaded && reloaded->value == NEW_VALUE, "a get() after the reload returns the new asset");
    const auto after = handle.get();
    const auto peeked = handle.peek();
    check(after && after->value == NEW_VALUE && peeked && peeked->value == NEW_VALUE,
          "once the old load finished, the handle still returns the new asset");
}

void test_id_change_keeps_the_loaded_asset()
{
    std::printf("changing the ids keeps the loaded asset\n");
    probe_handle handle = make_loaded_handle();
    const auto loaded = handle.peek();
    const uintptr_t version = handle.version();
    handle.set_internal_ids(hpp::uuid{}, "test:/renamed");
    check(handle.peek() == loaded && handle.version() == version, "set_internal_ids keeps the resolved asset");
    handle.set_internal_id("test:/renamed_again");
    check(handle.peek() == loaded && handle.id() == "test:/renamed_again", "set_internal_id keeps it too");
    handle.set_internal_job(make_scheduled_job());
    check(handle.peek() == nullptr, "a new job drops it");
}

void test_readers_never_receive_a_freed_asset()
{
    std::printf("readers racing reloads never receive a freed asset\n");
    marked_handle handle;
    handle.set_internal_ids(hpp::uuid{}, "test:/marked");
    handle.set_internal_job(make_marked_job());
    handle.get();
    const int readers = int(std::max(2u, std::thread::hardware_concurrency())) - STRESS_WRITERS;
    std::atomic<bool> is_running{true};
    std::atomic<int64_t> freed_seen{0};
    std::atomic<int64_t> reads{0};
    std::vector<std::thread> threads;
    for(int r = 0; r < readers; ++r)
    {
        threads.emplace_back(
            [&, reader = handle]()
            {
                int64_t local_reads = 0;
                while(is_running.load(std::memory_order_relaxed))
                {
                    for(const auto& asset : {reader.try_get(), reader.peek(), reader.get(false)})
                    {
                        if(asset && !marked_handle::is_empty(asset) && asset->mark != marked_asset::ALIVE)
                        {
                            freed_seen.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                    reader.visit(
                        [&](marked_asset* asset)
                        {
                            if(asset != nullptr && !marked_handle::is_empty(asset) && asset->mark != marked_asset::ALIVE)
                            {
                                freed_seen.fetch_add(1, std::memory_order_relaxed);
                            }
                        });
                    (void)reader.version();
                    (void)reader.uid();
                    (void)reader.id();
                    ++local_reads;
                }
                reads.fetch_add(local_reads);
            });
    }
    for(int w = 0; w < STRESS_WRITERS; ++w)
    {
        threads.emplace_back(
            [&, w, writer = handle]() mutable
            {
                for(int64_t i = 0; is_running.load(std::memory_order_relaxed); ++i)
                {
                    switch((i + w) % 4)
                    {
                        case 0:
                            writer.invalidate();
                            break;
                        case 1:
                            writer.demote_to_deferred();
                            break;
                        default:
                            writer.set_internal_job(make_marked_job());
                            break;
                    }
                }
            });
    }
    std::this_thread::sleep_for(STRESS_DURATION);
    is_running.store(false);
    for(auto& thread : threads)
    {
        thread.join();
    }
    handle.invalidate();
    asset_detail::epoch_domain::instance().collect_all();
    check(reads.load() > 0, "the readers read");
    check(freed_seen.load() == 0, "no reader received a freed asset (" + std::to_string(freed_seen.load()) + " did)");
    check(asset_detail::epoch_domain::instance().get_retired_count() == 0, "every replaced state was freed");
}

/// A job whose load fails: it yields no asset.
auto make_failing_job() -> probe_handle::task_future_t
{
    return g_pool->schedule("test_failing_probe_asset", []() { return std::shared_ptr<probe_asset>(); }).share();
}

void test_visit_passes_what_try_get_returns()
{
    std::printf("visit() passes what try_get() returns, without taking a reference\n");
    const auto pointer_of = [](const probe_handle& handle)
    {
        return handle.visit([](probe_asset* asset) { return asset; });
    };
    probe_handle handle;
    check(pointer_of(handle) == nullptr, "a handle without a link passes null");
    handle.set_internal_ids(hpp::uuid{}, "test:/visited");
    check(pointer_of(handle) == nullptr, "a handle with ids only passes null");
    handle.set_internal_job(make_deferred_job());
    (void)pointer_of(handle);
    check(!handle.is_deferred(), "a visit submits a deferred load");
    const auto loaded = handle.get();
    check(pointer_of(handle) == loaded.get(), "a loaded asset is passed");
    const long references = loaded.use_count();
    long references_inside = 0;
    handle.visit([&](probe_asset*) { references_inside = loaded.use_count(); });
    check(references_inside == references, "the visit takes no reference");
    check(handle.visit([](probe_asset* asset) { return asset != nullptr ? asset->value : -1; }) == PROBE_VALUE,
          "the visitor's result is returned");
    probe_handle failed;
    failed.set_internal_ids(hpp::uuid{}, "test:/failed");
    failed.set_internal_job(make_failing_job());
    (void)failed.get();
    check(probe_handle::is_empty(pointer_of(failed)) && probe_handle::is_empty(failed.try_get()),
          "a failed load passes the empty asset, as try_get() returns it");
}

void test_visited_asset_outlives_a_reload()
{
    std::printf("an asset being visited outlives a reload on another thread\n");
    auto& domain = asset_detail::epoch_domain::instance();
    domain.collect_all();
    probe_handle handle = make_loaded_handle();
    size_t retired_inside = 0;
    int value_inside = 0;
    handle.visit(
        [&](probe_asset* asset)
        {
            std::thread writer([copy = handle]() mutable { copy.set_internal_job(make_value_job(NEW_VALUE)); });
            writer.join();
            domain.collect();
            retired_inside = domain.get_retired_count();
            value_inside = asset != nullptr ? asset->value : -1;
        });
    check(retired_inside >= 1 && value_inside == PROBE_VALUE, "the replaced state and the visited asset stay while the visit runs");
    domain.collect();
    check(domain.get_retired_count() == 0, "once the visit returned, the replaced state is freed");
    const auto reloaded = handle.get();
    check(reloaded && reloaded->value == NEW_VALUE, "later calls get the new asset");
    const probe_handle other = make_loaded_handle();
    const int nested = handle.visit(
        [&](probe_asset* outer)
        {
            const bool is_peek_working = handle.peek() == reloaded;
            return other.visit([&](probe_asset* inner) { return outer != nullptr && inner != nullptr ? outer->value + inner->value : -1; }) +
                   (is_peek_working ? 0 : 1000);
        });
    check(nested == NEW_VALUE + PROBE_VALUE, "visits nest, and other handle calls work inside one");
}

void test_visit_waits_when_asked()
{
    std::printf("visit(wait) waits for a running load, as try_get(wait) does\n");
    std::atomic<bool> release{false};
    probe_handle handle;
    handle.set_internal_ids(hpp::uuid{}, "test:/visit_wait");
    handle.set_internal_job(make_held_job(release, OLD_VALUE));
    int waited_value = 0;
    std::thread waiter = start_waiting_thread(
        [&, copy = handle]()
        {
            waited_value = copy.visit([](probe_asset* asset) { return asset != nullptr ? asset->value : -1; }, true);
        });
    std::this_thread::sleep_for(SETTLE_WAIT);
    check(handle.visit([](probe_asset* asset) { return asset; }) == nullptr, "without waiting, a running load passes null");
    release.store(true);
    waiter.join();
    check(waited_value == OLD_VALUE, "waiting, it visits the asset the load produced");
    const auto peeked = handle.peek();
    check(peeked && peeked->value == OLD_VALUE, "and records it as resolved, as get() does");
    const probe_handle none;
    check(none.visit([](probe_asset* asset) { return asset; }, true) == nullptr, "without a task nothing is waited for");
}

auto run_asset_handle_suite(rtti::context& ctx) -> int
{
    g_checks = 0;
    g_failures = 0;
    g_pool = ctx.get_cached<threader>().pool.get();
    test_epoch_frees_only_after_earlier_readers_leave();
    test_epoch_collect_all_waits_for_readers();
    test_epoch_reuses_slots_of_ended_threads();
    test_validity_follows_every_published_state();
    test_validity_matches_the_snapshot_after_racing_writers();
    test_cache_hits_from_many_threads_return_the_asset();
    test_last_access_moves_forward_and_never_back();
    test_waited_get_is_seen_by_peek_and_version();
    test_reload_during_a_wait_keeps_the_new_asset();
    test_id_change_keeps_the_loaded_asset();
    test_readers_never_receive_a_freed_asset();
    test_visit_passes_what_try_get_returns();
    test_visited_asset_outlives_a_reload();
    test_visit_waits_when_asked();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("asset handle", run_asset_handle_suite)
