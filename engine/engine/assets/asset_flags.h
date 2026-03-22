#pragma once

#include <cstdint>

namespace unravel
{

enum class load_flags
{
    standard,
    reload,
    do_not_unload
};

/// Controls whether an asset is loaded immediately or deferred until first access.
enum class load_mode : uint8_t
{
    immediate, ///< Schedule load on thread pool now (default, backward compatible).
    deferred   ///< Register handle only; load triggered on first .get() call.
};

} // namespace unravel
