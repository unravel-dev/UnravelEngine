#pragma once

// The engine's parallel algorithms, on poolSTL's thread pool.
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

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <numeric>
#include <thread>
#include <vector>

// Extends poolSTL's own namespace: this is an extension of that library rather than a wrapper
// hiding it.
namespace poolstl
{

/// @brief std::for_each over a poolSTL parallel range, with the policy chosen at run time.
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
        std::for_each(poolstl::par, first, last, func);
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
    const std::size_t hardware_threads = std::max(std::thread::hardware_concurrency(), 1u);
    const std::size_t worker_count = parallel ? std::min(hardware_threads, count) : std::size_t(1);
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
