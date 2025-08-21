/**
 * @file crash_handlers.cpp
 * @brief Implementation of crash handler functionality
 */

#include "crash_handlers.hpp"
#include "logging.h"


#include <base/platform/config.hpp>

#include <spdlog/spdlog.h>
#include <exception>
#include <csignal>
#include <atomic>
#include <string_view>
#include <cpptrace/cpptrace.hpp>

#if UNRAVEL_PLATFORM_POSIX
#include <unistd.h>
#include <sys/resource.h>
#endif

#if UNRAVEL_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace unravel::crash {

// Internal declarations and types
namespace {

/// Atomic flag to prevent recursive crash handling
std::atomic<bool> g_handling{false};

/// Crash information structure
struct crash_info {
    std::string_view signal_name;
    std::string_view description;
    void* crash_address{nullptr};
    cpptrace::stacktrace trace;
};

} // anonymous namespace

// Internal function declarations
auto log_crash_and_flush(const crash_info& info) noexcept -> void;
auto get_signal_name(int sig) noexcept -> std::string_view;

#if UNRAVEL_PLATFORM_POSIX
auto emergency_write(std::string_view message) noexcept -> void;
auto enable_core_dumps() noexcept -> void;
extern "C" auto signal_handler(int sig, siginfo_t* info, void*) -> void;
#elif UNRAVEL_PLATFORM_WINDOWS
using exception_pointers = struct _EXCEPTION_POINTERS;
auto get_exception_name(unsigned long code) noexcept -> std::string_view;
auto c_signal_handler(int sig) -> void;
auto seh_filter(exception_pointers* ep) -> long;

#endif

auto terminate_handler() noexcept -> void;
auto test_crash_handler() noexcept -> void;

auto get_signal_name(int sig) noexcept -> std::string_view {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV (Segmentation fault)";
        case SIGABRT: return "SIGABRT (Process abort)";
        case SIGILL:  return "SIGILL (Illegal instruction)";
        case SIGFPE:  return "SIGFPE (Floating point exception)";
#ifdef SIGBUS
        case SIGBUS:  return "SIGBUS (Bus error)";
#endif
        case SIGINT:  return "SIGINT (Interrupt)";
        case SIGTERM: return "SIGTERM (Termination request)";
#ifdef SIGPIPE
        case SIGPIPE: return "SIGPIPE (Broken pipe)";
#endif
#ifdef SIGQUIT
        case SIGQUIT: return "SIGQUIT (Quit)";
#endif
#ifdef SIGHUP
        case SIGHUP:  return "SIGHUP (Hangup)";
#endif
        default:      return "Unknown signal";
    }
}

auto log_crash_and_flush(const crash_info& info) noexcept -> void {
    try {
        // Get the logger instance
        if (auto logger = spdlog::get(APPLOG)) {
            // Log basic crash information
            try {
                if (info.crash_address) {
                    logger->critical("FATAL CRASH: {} - {} (Address: 0x{:X})", 
                                   info.signal_name, 
                                   info.description,
                                   reinterpret_cast<uintptr_t>(info.crash_address));
                } else {
                    logger->critical("FATAL CRASH: {} - {}", 
                                   info.signal_name, 
                                   info.description);
                }
            } catch (...) {
                // Continue even if basic logging fails
            }
            
            // Log stack trace if available
            try {
                if (!info.trace.empty()) {
                    logger->critical("Stack trace:\n{}", info.trace.to_string(false));
                } else {
                    logger->critical("Stack trace: Not available");
                }
            } catch (...) {
                // Continue even if stack trace logging fails
            }
            
            // Force flush all logs
            try {
                logger->flush();
            } catch (...) {
                // Final fallback - can't do much more
            }
        }
        
        // Also try to flush all loggers globally
        try {
            spdlog::apply_all([](const std::shared_ptr<spdlog::logger>& l) {
                try {
                    l->flush();
                } catch (...) {
                    // Individual logger flush failed, continue
                }
            });
        } catch (...) {
            // Global flush failed, nothing more we can do
        }
        
    } catch (...) {
        // Complete logging failure - we're in a very bad state
        // but we should not throw from a crash handler
    }
}

#if UNRAVEL_PLATFORM_POSIX

auto emergency_write(std::string_view message) noexcept -> void {
    ::write(STDERR_FILENO, message.data(), message.size());
}

auto enable_core_dumps() noexcept -> void {
    rlimit rl{RLIM_INFINITY, RLIM_INFINITY};
    setrlimit(RLIMIT_CORE, &rl);
}

extern "C" auto signal_handler(int sig, siginfo_t* info, void*) -> void {
    // Prevent recursive crashes
    if (g_handling.exchange(true, std::memory_order_acq_rel)) {
        _exit(128 + sig);
    }
    
    // Create crash info
    crash_info crash_data{
        .signal_name = get_signal_name(sig),
        .description = "Unhandled POSIX signal",
        .crash_address = (info && (sig == SIGSEGV || sig == SIGBUS)) ? info->si_addr : nullptr
    };
    
    // Emergency stderr notification
    emergency_write("FATAL: ");
    emergency_write(crash_data.signal_name);
    emergency_write("\n");
    
    // Capture stack trace (signal-safe version)
    try {
        if (cpptrace::can_signal_safe_unwind()) {
            // Use signal-safe unwinding if available
            constexpr size_t max_frames = 64;
            cpptrace::frame_ptr buffer[max_frames];
            auto frame_count = cpptrace::safe_generate_raw_trace(buffer, max_frames, 1);
            
            if (frame_count > 0) {
                // Convert to stacktrace outside signal handler context
                cpptrace::raw_trace raw_trace_data;
                raw_trace_data.frames.assign(buffer, buffer + frame_count);
                crash_data.trace = raw_trace_data.resolve();
            }
        } else {
            // Fallback to regular tracing (not signal-safe but better than nothing)
            crash_data.trace = cpptrace::generate_trace(1);
        }
    } catch (...) {
        // Stack trace generation failed, continue with logging
    }
    
    // Log crash information
    log_crash_and_flush(crash_data);
    
    // Restore default handler and re-raise for proper termination
    signal(sig, SIG_DFL);
    raise(sig);
}

#elif UNRAVEL_PLATFORM_WINDOWS

auto get_exception_name(unsigned long code) noexcept -> std::string_view {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:         return "ACCESS_VIOLATION";
        case EXCEPTION_STACK_OVERFLOW:           return "STACK_OVERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION:      return "ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:            return "IN_PAGE_ERROR";
        case EXCEPTION_INVALID_DISPOSITION:      return "INVALID_DISPOSITION";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
        case EXCEPTION_PRIV_INSTRUCTION:         return "PRIVILEGED_INSTRUCTION";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "INTEGER_DIVIDE_BY_ZERO";
        case EXCEPTION_INT_OVERFLOW:             return "INTEGER_OVERFLOW";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "FLOAT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_OVERFLOW:             return "FLOAT_OVERFLOW";
        case EXCEPTION_FLT_UNDERFLOW:            return "FLOAT_UNDERFLOW";
        case EXCEPTION_FLT_INVALID_OPERATION:    return "FLOAT_INVALID_OPERATION";
        case EXCEPTION_BREAKPOINT:               return "BREAKPOINT";
        case EXCEPTION_SINGLE_STEP:              return "SINGLE_STEP";
        default:                                 return "Unknown exception";
    }
}

auto c_signal_handler(int sig) -> void {
    if (g_handling.exchange(true, std::memory_order_acq_rel)) {
        _exit(128 + sig);
    }
    
    crash_info crash_data{
        .signal_name = get_signal_name(sig),
        .description = "C runtime signal",
        .crash_address = nullptr
    };
    
    try {
        crash_data.trace = cpptrace::generate_trace(1);
    } catch (...) {
        // Continue without trace
    }
    
    log_crash_and_flush(crash_data);
    
    // Restore default and re-raise
    signal(sig, SIG_DFL);
    raise(sig);
}

auto seh_filter(exception_pointers* ep) -> long {
    if (g_handling.exchange(true, std::memory_order_acq_rel)) {
        return EXCEPTION_EXECUTE_HANDLER;
    }
    
    crash_info crash_data{
        .signal_name = get_exception_name(ep->ExceptionRecord->ExceptionCode),
        .description = "Unhandled SEH exception",
        .crash_address = ep->ExceptionRecord->ExceptionAddress
    };
    
    try {
        crash_data.trace = cpptrace::generate_trace(1);
    } catch (...) {
        // Continue without trace
    }
    
    log_crash_and_flush(crash_data);
    
    return EXCEPTION_EXECUTE_HANDLER;
}



#endif

auto terminate_handler() noexcept -> void {
    if (g_handling.exchange(true, std::memory_order_acq_rel)) {
        std::abort();
    }
    
    crash_info crash_data{
        .signal_name = "std::terminate",
        .description = "Uncaught C++ exception",
        .crash_address = nullptr
    };
    
    try {
        crash_data.trace = cpptrace::generate_trace(1);
    } catch (...) {
        // Continue without trace
    }
    
    log_crash_and_flush(crash_data);
    std::abort();
}

auto install_handlers() -> void {
    // Prevent multiple installations
    static std::atomic<bool> installed{false};
    if (installed.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    // Configure cpptrace logging to use our logger
    cpptrace::set_log_callback([](cpptrace::log_level level, const char* message) {
        if (auto lg = spdlog::get(APPLOG)) {
            switch (level) {
                case cpptrace::log_level::error:
                    lg->error("cpptrace: {}", message);
                    break;
                case cpptrace::log_level::warning:
                    lg->warn("cpptrace: {}", message);
                    break;
                case cpptrace::log_level::info:
                    lg->info("cpptrace: {}", message);
                    break;
                case cpptrace::log_level::debug:
                    lg->debug("cpptrace: {}", message);
                    break;
            }
        }
    });
    
    
    // Register cpptrace terminate handler first
    cpptrace::register_terminate_handler();
    
    // Set our custom terminate handler
    std::set_terminate(terminate_handler);
    
#if UNRAVEL_PLATFORM_POSIX
    // Enable core dumps
    enable_core_dumps();
    
    // Set up signal handling
    struct sigaction sa{};
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = signal_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    
    // Block all signals during crash handling
    sigfillset(&sa.sa_mask);
    
    // Install handlers for crash signals
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);
    sigaction(SIGFPE,  &sa, nullptr);
#ifdef SIGBUS
    sigaction(SIGBUS,  &sa, nullptr);
#endif
    
    // Install handlers for termination signals
    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
#ifdef SIGQUIT
    sigaction(SIGQUIT, &sa, nullptr);
#endif
#ifdef SIGHUP
    sigaction(SIGHUP,  &sa, nullptr);
#endif
    
    // Ignore SIGPIPE (broken pipe should not crash)
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif
    
#elif UNRAVEL_PLATFORM_WINDOWS
    // Disable error dialog boxes
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    
    // Install C runtime signal handlers
    signal(SIGABRT, c_signal_handler);
    signal(SIGTERM, c_signal_handler);
    signal(SIGINT,  c_signal_handler);
    signal(SIGILL,  c_signal_handler);
    signal(SIGFPE,  c_signal_handler);
#ifdef SIGBREAK
    signal(SIGBREAK, c_signal_handler);
#endif
    
    // Install SEH exception filter
    SetUnhandledExceptionFilter(seh_filter);
    
#endif
}

auto test_crash_handler() noexcept -> void {
    crash_info crash_data{
        .signal_name = "TEST_CRASH",
        .description = "Manual crash handler test",
        .crash_address = nullptr
    };
    
    try {
        crash_data.trace = cpptrace::generate_trace(1);
    } catch (...) {
        // Continue without trace
    }
    
    log_crash_and_flush(crash_data);
    std::abort();
}

} // namespace unravel::crash
