/**
 * @file crash.hpp
 * @brief Cross-platform crash handlers (POSIX signals + Windows SEH) with
 *        crash-safe logging and optional minidumps.
 */
#pragma once

#include <string>

namespace unravel::crash
{

/// Signal / fault information
struct signal_info
{
    int signal_number;       ///< Signal number or Windows exception code (cast)
    const char* signal_name; ///< Human-readable name
};

/// Exception information (std::terminate path)
struct exception_info
{
    std::string exception_type;
    std::string exception_message;
};

/// Stack trace information
struct trace_info
{
    std::string formatted_trace;
};

using interrupt_handler_t = void (*)(const signal_info& info);
using termination_handler_t = void (*)(const signal_info& info);
using crash_handler_t = void (*)(const signal_info& info, const trace_info& trace);
using exception_handler_t = void (*)(const exception_info& info, const trace_info& trace);

struct crash_handlers
{
    interrupt_handler_t interrupt_handler{nullptr};
    termination_handler_t termination_handler{nullptr};
    crash_handler_t crash_handler{nullptr};
    exception_handler_t exception_handler{nullptr};
    /// Append-only emergency log (crash-safe OS writes). Default: "CrashLog.txt".
    const char* crash_log_path{"CrashLog.txt"};
    /// Windows: write a minidump next to the crash log. Ignored elsewhere.
    bool write_minidump{true};
};

/**
 * @brief Install crash handlers once for the process.
 *
 * On Windows installs SetUnhandledExceptionFilter (primary for AVs) plus CRT
 * signals. On POSIX installs sigaction with an alternate stack. Always installs
 * std::terminate. User callbacks are best-effort after a crash-safe log write.
 */
auto install_handlers(const crash_handlers& handlers) -> void;

} // namespace unravel::crash
