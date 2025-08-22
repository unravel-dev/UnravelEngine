/**
 * @file crash_handlers.hpp
 * @brief Modern C++20 cross-platform crash handler using cpptrace
 *
 * This crash handler provides comprehensive crash detection and logging capabilities
 * using the cpptrace library for robust, cross-platform stack trace generation.
 * It intercepts all crash possibilities and logs detailed stack traces.
 *
 * Features:
 * - Cross-platform crash handling using cpptrace
 * - Unified signal handling across platforms
 * - Comprehensive crash logging with detailed context
 * - Minimal OS-specific code
 * - Thread-safe crash detection
 * - Callback-based architecture
 *
 * @author UnravelEngine Team
 * @version 3.0
 */

#pragma once

#include <cpptrace/cpptrace.hpp>
#include <string>

namespace unravel::crash
{

/// Signal information structure
struct signal_info {
    int signal_number;      ///< Signal number (SIGINT, SIGSEGV, etc.)
    const char* signal_name; ///< Human-readable signal name
};

/// Exception information structure  
struct exception_info {
    std::string exception_type;     ///< Exception type name (demangled)
    std::string exception_message;  ///< Exception message/description
};

/// Stack trace information
struct trace_info {
    std::string formatted_trace;  ///< Formatted stack trace string
};

// Callback function types
using interrupt_handler_t = void(*)(const signal_info& info);
using termination_handler_t = void(*)(const signal_info& info);
using crash_handler_t = void(*)(const signal_info& info, const trace_info& trace);
using exception_handler_t = void(*)(const exception_info& info, const trace_info& trace);

/**
 * @brief Set custom interrupt handler (SIGINT, Ctrl+C)
 * 
 * @param handler Callback function for interrupt signals, or nullptr for default behavior
 */
auto set_interrupt_handler(interrupt_handler_t handler) -> void;

/**
 * @brief Set custom termination handler (SIGTERM, SIGQUIT, SIGHUP)
 * 
 * @param handler Callback function for termination signals, or nullptr for default behavior  
 */
auto set_termination_handler(termination_handler_t handler) -> void;

/**
 * @brief Set custom crash handler (SIGSEGV, SIGABRT, SIGILL, SIGFPE, SIGBUS)
 * 
 * @param handler Callback function for crash signals, or nullptr for default behavior
 */
auto set_crash_handler(crash_handler_t handler) -> void;

/**
 * @brief Set custom exception handler (C++ exceptions)
 * 
 * @param handler Callback function for C++ exceptions, or nullptr for default behavior
 */
auto set_exception_handler(exception_handler_t handler) -> void;


struct crash_handlers
{
    interrupt_handler_t interrupt_handler;
    termination_handler_t termination_handler;
    crash_handler_t crash_handler;
    exception_handler_t exception_handler;
};

/**
 * @brief Install comprehensive crash handlers
 *
 * Sets up all crash detection mechanisms for the current platform.
 * This function is thread-safe and can be called multiple times.
 * 
 * @note Call the set_*_handler functions before calling this to customize behavior
 */
auto install_handlers(const crash_handlers& handlers) -> void;

} // namespace unravel::crash