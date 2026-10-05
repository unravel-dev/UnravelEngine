#pragma once

// The engine's parallel algorithms, on poolSTL's thread pools: the default pool, which the frame's
// loops use, and a background pool for work that may hold every thread it gets for minutes, such as
// an asset compile (see background_scope).
//
// Deliberately NOT std::execution::par. The standard policies are missing or unusable on
// toolchains this engine targets -- AppleClang does not implement them, libstdc++ advertises
// them but will not link <execution> without TBB -- so the spelling that works everywhere is the
// one that never names std::execution at all. poolSTL also measured faster here than the MS
// STL's native implementation, which dispatches onto the Windows system thread pool.
//
// Because nothing names std::execution, POOLSTL_STD_SUPPLEMENT is deliberately NOT defined:
// with it, poolstl.hpp pulls in <execution> wherever the header merely exists, which is the
// compiler-version dependency this arrangement exists to avoid.
//
// Call sites include this header and call for_each_par_if. Nothing outside it should include
// <poolstl/poolstl.hpp> or <execution> directly.
#include <poolstl/poolstl.hpp>

#include <base/platform/thread.hpp>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <numeric>
#include <vector>

// Extends poolSTL's own namespace: this is an extension of that library rather than a wrapper
// hiding it.
namespace poolstl
{

namespace detail
{

/// The pool this thread's parallel loops run on instead of the default one, set by background_scope.
inline thread_local ttp::task_thread_pool* current_pool = nullptr;

/// Starts the background pool with every one of its threads below normal priority.
inline auto create_background_pool() -> std::unique_ptr<ttp::task_thread_pool>
{
    auto pool = std::make_unique<ttp::task_thread_pool>();
    const unsigned int thread_count = pool->get_num_threads();
    // A thread can only lower its own priority, so each worker runs one of these. Each holds its thread until all
    // have started, which is what makes every thread take exactly one.
    std::mutex mutex;
    std::condition_variable all_started;
    unsigned int started_count = 0;
    for(unsigned int i = 0; i < thread_count; ++i)
    {
        pool->submit_detach(
            [&]()
            {
                platform::set_thread_background_priority();
                std::unique_lock<std::mutex> lock(mutex);
                ++started_count;
                all_started.notify_all();
                all_started.wait(lock,
                                 [&]()
                                 {
                                     return started_count == thread_count;
                                 });
            });
    }
    pool->wait_for_tasks();
    return pool;
}

} // namespace detail

/// @brief The pool for work that may hold every thread it gets for minutes, such as an asset compile and its bakes.
///
/// Kept apart from the default pool because the frame's loops wait on that one, and it serves one queue in order: a
/// bake there leaves a frame's loop waiting for a free thread until the bake ends. Its threads run below normal
/// priority, so a frame that needs the cores gets them first and the bake takes what is left.
inline auto get_background_pool() -> ttp::task_thread_pool&
{
    static const std::unique_ptr<ttp::task_thread_pool> pool = detail::create_background_pool();
    return *pool;
}

/// @brief The pool the parallel loops started on this thread run on: the background pool inside a
///        background_scope, the default pool otherwise.
inline auto get_current_pool() -> ttp::task_thread_pool&
{
    return detail::current_pool != nullptr ? *detail::current_pool : *execution::internal::get_default_pool();
}

/// @brief While alive, sends every parallel loop the calling thread starts to the background pool.
///
/// Covers the loops of everything the thread calls, so a loop deep inside a bake follows without a pool passed
/// down to it. A loop started on another thread is not covered.
class background_scope
{
public:
    background_scope() : previous_(detail::current_pool)
    {
        detail::current_pool = &get_background_pool();
    }

    ~background_scope()
    {
        detail::current_pool = previous_;
    }

    background_scope(const background_scope&) = delete;
    auto operator=(const background_scope&) -> background_scope& = delete;

private:
    ///< The pool in effect before this scope, restored when it ends.
    ttp::task_thread_pool* previous_ = nullptr;
};

/// @brief std::for_each over a poolSTL parallel range, with the policy chosen at run time.
///
/// The range runs on get_current_pool().
///
/// Exceptions raised by @a func do reach this function's caller: poolSTL runs each chunk on a
/// future and rethrows when it collects them. The standard's parallel overloads are noexcept and
/// would call std::terminate instead. The range still runs to completion before the throw.
///
/// @param parallel When false the range runs inline on the calling thread. That is not merely an
///        optimisation for small ranges: it is what keeps a caller that is already inside a
///        parallel range from nesting one dispatch inside another.
template<typename Iterator, typename Function>
void for_each_par_if(bool parallel, Iterator first, Iterator last, Function func)
{
    if(parallel)
    {
        std::for_each(par.on(get_current_pool()), first, last, func);
        return;
    }

    std::for_each(first, last, func);
}

/// @brief Calls @a func(index) once for every index of @a costs, the most expensive first, each pool worker
///        pulling the next index as soon as it finishes the last.
///
/// For ranges whose items differ wildly in cost. for_each_par_if hands each pool thread one contiguous run of
/// the range, so expensive items that sit together all queue on a few threads while the rest of the pool idles.
/// Pulling them in descending cost starts the longest items first and lets the short ones fill in behind them.
/// The workers hold their threads until the last index is done, so a long range belongs on the background pool.
///
/// Every index runs exactly once, so an output slot per index written only by @a func needs no synchronisation.
///
/// @param parallel When false every index runs inline on the calling thread, in the same order; see
///        for_each_par_if for why a caller already inside a parallel range must pass false.
/// @param costs    Estimated cost per index. Only the order matters; equal costs keep index order.
template<typename Cost, typename Function>
void for_each_costliest_first_par_if(bool parallel, const std::vector<Cost>& costs, Function func)
{
    const std::size_t count = costs.size();
    std::vector<std::size_t> order(count);
    std::iota(order.begin(), order.end(), std::size_t(0));
    std::stable_sort(order.begin(),
                     order.end(),
                     [&](std::size_t lhs, std::size_t rhs)
                     {
                         return costs[rhs] < costs[lhs];
                     });
    const std::size_t worker_count =
        parallel ? std::min<std::size_t>(get_current_pool().get_num_threads(), count) : std::size_t(1);
    std::atomic<std::size_t> next{0};
    for_each_par_if(worker_count > 1,
                    iota_iter<std::size_t>(0),
                    iota_iter<std::size_t>(worker_count),
                    [&](std::size_t)
                    {
                        for(std::size_t k = next.fetch_add(1); k < count; k = next.fetch_add(1))
                        {
                            func(order[k]);
                        }
                    });
}


template <typename View, typename Fn>
void for_each_entity_par(bool parallel, const View& view, Fn fn)
{
    // The view's leading pool: a packed, random-access entity array. Chunking it is O(1)
    // per chunk, and the filter the view's own ++ would have applied serially - "is this
    // entity in every other pool" - runs inside each task instead, in parallel.
    const auto* leading = view.handle();
    if(leading == nullptr)
    {
        return;
    }
    for_each_par_if(parallel, leading->begin(), leading->end(),
        [&](const auto entity) -> void
        {
            if(!view.contains(entity))   // the filter, now parallel; also rejects tombstones
            {
                return;
            }
            fn(entity);
        });
}

} // namespace poolstl
