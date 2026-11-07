#pragma once

#include <base/platform/config.hpp>

#include <atomic>

namespace unravel
{

namespace detail
{
inline auto get_single_threaded_flag() -> std::atomic<bool>&
{
    static std::atomic<bool> is_single_threaded{false};
    return is_single_threaded;
}
} // namespace detail

/// @brief Whether the engine runs all its work on the main thread, with no worker threads at all: jobs run
///        when they are waited for or in the threader's per-frame pump, parallel loops run inline, physics
///        steps on the calling thread and bgfx renders inside bgfx::frame.
///
/// Always on in the browser, which gives the engine no threads. Elsewhere it is on only when the host asks for it
/// (the --single-threaded option), to run the browser's threading on the desktop. Set once at startup, before
/// any of the work above starts; it is not meant to change afterwards.
inline auto is_single_threaded() -> bool
{
#if UNRAVEL_PLATFORM_EMSCRIPTEN
    return true;
#else
    return detail::get_single_threaded_flag().load(std::memory_order_relaxed);
#endif
}

/// @brief Turns the single-threaded mode on or off. See is_single_threaded.
inline void set_single_threaded(bool is_on)
{
    detail::get_single_threaded_flag().store(is_on, std::memory_order_relaxed);
}

} // namespace unravel
