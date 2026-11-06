#include "crash.hpp"

// The browser player installs no crash handlers: a wasm trap is no signal, the module cannot walk its own stack,
// and the page's console already reports an uncaught error with the JavaScript stack around it.
namespace unravel::crash
{

auto install_handlers(const crash_handlers& handlers) -> void
{
    (void)handlers;
}

auto set_crash_log_path(const char* crash_log_path) -> void
{
    (void)crash_log_path;
}

} // namespace unravel::crash
