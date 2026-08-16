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

} // namespace poolstl
