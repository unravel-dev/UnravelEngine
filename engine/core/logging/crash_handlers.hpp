/**
 * @file crash_handlers.hpp
 * @brief Professional cross-platform crash handler for robust error logging and debugging
 * 
 * This crash handler provides comprehensive crash detection and logging capabilities
 * across Windows and POSIX platforms. It safely handles signals, exceptions, and
 * system events while ensuring logs are flushed and backtraces are captured.
 * 
 * Features:
 * - Cross-platform signal/exception handling
 * - Atomic crash detection to prevent recursive crashes
 * - Stack backtrace capture and logging
 * - Safe log flushing during crash scenarios
 * - Minimal overhead when not crashing
 * 
 * @author UnravelEngine Team
 * @version 2.0
 */

#pragma once

#include <atomic>
#include <exception>
#include <csignal>
#include <string>
#include <array>
#include <cstdio>   // for snprintf
#include <cstring>  // for strlen, memmove

#include <base/platform/config.hpp>
#include <spdlog/spdlog.h>

namespace unravel::crash {

/// Maximum number of stack frames to capture in backtrace
constexpr size_t MAX_BACKTRACE_FRAMES = 64;

/// Maximum length for crash message strings
constexpr size_t MAX_CRASH_MESSAGE_LENGTH = 512*8;

/// Atomic flag to prevent recursive crash handling
inline std::atomic<bool> g_handling{false};

/// Crash context information
struct crash_context {
    const char* signal_name{nullptr};    ///< Human-readable signal/exception name
    const char* description{nullptr};    ///< Additional crash description
    void* crash_address{nullptr};        ///< Address that caused the crash (if applicable)
    std::array<char, MAX_CRASH_MESSAGE_LENGTH> message{}; ///< Formatted crash message
};

// Forward declaration - platform-specific implementations below
bool capture_backtrace(crash_context& context) noexcept;

/**
 * @brief Best-effort log flushing with comprehensive crash information
 * 
 * This function attempts to log crash information including backtraces
 * and flush all logs to ensure crash data is preserved. It's designed
 * to be as robust as possible and never throw exceptions.
 * 
 * @param context Crash context containing all relevant crash information
 */
void best_effort_flush(const crash_context& context) noexcept;

#if UNRAVEL_PLATFORM_POSIX
// ---------------- POSIX Platforms ----------------
#include <unistd.h>
#include <sys/resource.h>

#if UNRAVEL_PLATFORM_LINUX || UNRAVEL_PLATFORM_ANDROID
#include <execinfo.h>       // for backtrace()
#include <dlfcn.h>          // for dladdr()
#elif UNRAVEL_PLATFORM_OSX || UNRAVEL_PLATFORM_IOS
#include <execinfo.h>       // for backtrace()
#include <dlfcn.h>          // for dladdr()
#include <mach-o/dyld.h>    // for _NSGetExecutablePath()
#elif UNRAVEL_PLATFORM_BSD
#include <execinfo.h>       // for backtrace() (if available)
#include <dlfcn.h>          // for dladdr()
#endif

/**
 * @brief Signal-safe string writer to stderr
 * @param s Null-terminated string to write
 */
inline void write_stderr(const char* s) noexcept { 
    if (s) {
        ::write(STDERR_FILENO, s, std::strlen(s)); 
    }
}

/**
 * @brief Enable core dump generation for post-mortem debugging
 * @return true if core dumps were successfully enabled
 */
inline bool enable_core_dumps() noexcept {
    rlimit rl{RLIM_INFINITY, RLIM_INFINITY};
    return setrlimit(RLIMIT_CORE, &rl) == 0;
}

/**
 * @brief Capture backtrace on POSIX systems
 */
inline bool capture_backtrace(crash_context& context) noexcept {
#if UNRAVEL_PLATFORM_LINUX || UNRAVEL_PLATFORM_ANDROID || \
    UNRAVEL_PLATFORM_OSX || UNRAVEL_PLATFORM_IOS || \
    UNRAVEL_PLATFORM_BSD
    
    void* stack_frames[MAX_BACKTRACE_FRAMES];
    int frame_count = ::backtrace(stack_frames, MAX_BACKTRACE_FRAMES);
    
    if (frame_count <= 0) {
        return false;
    }
    
    // Format backtrace into context message
    size_t pos = std::snprintf(context.message.data(), context.message.size(), 
                              "Backtrace (%d frames):\n", frame_count);
    
    char** symbols = backtrace_symbols(stack_frames, frame_count);
    if (symbols) {
        for (int i = 0; i < frame_count && pos < context.message.size() - 1; ++i) {
            size_t remaining = context.message.size() - pos;
            int written = std::snprintf(context.message.data() + pos, remaining,
                                      "  [%d] %s\n", i, symbols[i]);
            if (written > 0 && written < static_cast<int>(remaining)) {
                pos += written;
            } else {
                break; // Not enough space
            }
        }
        ::free(symbols);
        return true;
    }
#endif
    return false;
}

/**
 * @brief Get human-readable signal name
 */
inline const char* get_signal_name(int sig) noexcept {
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
        case SIGPIPE: return "SIGPIPE (Broken pipe)";
        case SIGQUIT: return "SIGQUIT (Quit)";
        case SIGHUP:  return "SIGHUP (Hangup)";
        default:      return "Unknown signal";
    }
}

/**
 * @brief Enhanced POSIX signal handler with context capture
 */
inline void posix_signal_handler(int sig, siginfo_t* info, void* ucontext) {
    // Prevent recursive crash handling
    if (g_handling.exchange(true)) {
        _exit(128 + sig);
    }
    
    // Create crash context
    crash_context context{};
    context.signal_name = get_signal_name(sig);
    context.description = "Unhandled POSIX signal";
    
    // Capture crash address if available
    if (info && (sig == SIGSEGV || sig == SIGBUS || sig == SIGILL)) {
        context.crash_address = info->si_addr;
    }
    
    // Attempt to capture backtrace
    capture_backtrace(context);
    
    // Emergency write to stderr (signal-safe)
    write_stderr("FATAL: ");
    write_stderr(context.signal_name);
    write_stderr("\n");
    
    // Attempt to log through spdlog (may fail but worth trying)
    best_effort_flush(context);
    
    // Restore default handler and re-raise for proper core dump
    signal(sig, SIG_DFL);
    raise(sig);
}
#else
// ---------------- Windows Platforms ----------------
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <crtdbg.h>        // _CrtSetReportMode (debug builds)
#include <cstdlib>         // _exit
#include <dbghelp.h>       // for StackWalk64, SymFromAddr
#include <psapi.h>         // for GetModuleFileNameEx

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")

/**
 * @brief Initialize symbol handler for backtrace capture
 */
inline bool initialize_symbol_handler() noexcept {
    static std::atomic<bool> initialized{false};
    
    if (!initialized.exchange(true)) {
        HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        return SymInitialize(process, nullptr, TRUE) == TRUE;
    }
    return true;
}

/**
 * @brief Capture backtrace on Windows using DbgHelp
 */
inline bool capture_backtrace(crash_context& context) noexcept {
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    
    if (!initialize_symbol_handler()) {
        return false;
    }
    
    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_FULL;
    RtlCaptureContext(&ctx);
    
    STACKFRAME64 stack_frame = {};
    DWORD machine_type = 0;
    
#ifdef _M_IX86
    machine_type = IMAGE_FILE_MACHINE_I386;
    stack_frame.AddrPC.Offset = ctx.Eip;
    stack_frame.AddrPC.Mode = AddrModeFlat;
    stack_frame.AddrFrame.Offset = ctx.Ebp;
    stack_frame.AddrFrame.Mode = AddrModeFlat;
    stack_frame.AddrStack.Offset = ctx.Esp;
    stack_frame.AddrStack.Mode = AddrModeFlat;
#elif _M_X64
    machine_type = IMAGE_FILE_MACHINE_AMD64;
    stack_frame.AddrPC.Offset = ctx.Rip;
    stack_frame.AddrPC.Mode = AddrModeFlat;
    stack_frame.AddrFrame.Offset = ctx.Rsp;
    stack_frame.AddrFrame.Mode = AddrModeFlat;
    stack_frame.AddrStack.Offset = ctx.Rsp;
    stack_frame.AddrStack.Mode = AddrModeFlat;
#elif _M_ARM64
    machine_type = IMAGE_FILE_MACHINE_ARM64;
    stack_frame.AddrPC.Offset = ctx.Pc;
    stack_frame.AddrPC.Mode = AddrModeFlat;
    stack_frame.AddrFrame.Offset = ctx.Fp;
    stack_frame.AddrFrame.Mode = AddrModeFlat;
    stack_frame.AddrStack.Offset = ctx.Sp;
    stack_frame.AddrStack.Mode = AddrModeFlat;
#else
    return false; // Unsupported architecture
#endif
    
    // Format backtrace into context message
    size_t pos = std::snprintf(context.message.data(), context.message.size(),
                              "Backtrace (Windows):\n");
    
    int frame_count = 0;
    while (frame_count < MAX_BACKTRACE_FRAMES && pos < context.message.size() - 1) {
        if (!StackWalk64(machine_type, process, thread, &stack_frame,
                        &ctx, nullptr, SymFunctionTableAccess64, 
                        SymGetModuleBase64, nullptr)) {
            break;
        }
        
        if (stack_frame.AddrPC.Offset == 0) {
            break;
        }
        
        // Get symbol information
        char symbol_buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;
        
        DWORD64 displacement = 0;
        const char* symbol_name = "Unknown";
        
        if (SymFromAddr(process, stack_frame.AddrPC.Offset, &displacement, symbol)) {
            symbol_name = symbol->Name;
        }
        
        // Get module name
        char module_name[MAX_PATH] = "Unknown";
        HMODULE module = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                              reinterpret_cast<LPCSTR>(stack_frame.AddrPC.Offset),
                              &module)) {
            GetModuleFileNameExA(process, module, module_name, MAX_PATH);
            // Extract just the filename
            const char* filename = strrchr(module_name, '\\');
            if (filename && strlen(filename) > 1) {
                size_t len = strlen(filename + 1);
                memmove(module_name, filename + 1, len + 1); // +1 for null terminator
            }
        }
        
        size_t remaining = context.message.size() - pos;
        int written = std::snprintf(context.message.data() + pos, remaining,
                                  "  [%d] 0x%016llX %s!%s+0x%llX\n",
                                  frame_count, stack_frame.AddrPC.Offset,
                                  module_name, symbol_name, displacement);
        
        if (written > 0 && written < static_cast<int>(remaining)) {
            pos += written;
        } else {
            break; // Not enough space
        }
        
        frame_count++;
    }
    
    return frame_count > 0;
}

/**
 * @brief Get human-readable signal name for Windows
 */
inline const char* get_signal_name(int sig) noexcept {
    switch (sig) {
        case SIGABRT:  return "SIGABRT (Process abort)";
        case SIGTERM:  return "SIGTERM (Termination request)";
        case SIGINT:   return "SIGINT (Interrupt - Ctrl+C)";
        case SIGILL:   return "SIGILL (Illegal instruction)";
        case SIGFPE:   return "SIGFPE (Floating point exception)";
#ifdef SIGBREAK
        case SIGBREAK: return "SIGBREAK (Ctrl+Break)";
#endif
        default:       return "Unknown C runtime signal";
    }
}

/**
 * @brief Get human-readable exception name for SEH
 */
inline const char* get_exception_name(DWORD code) noexcept {
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

/**
 * @brief Enhanced C runtime signal handler for Windows
 */
inline void c_signal_handler(int sig) {
    if (g_handling.exchange(true)) { 
        _exit(128 + sig); 
    }
    
    // Create crash context
    crash_context context{};
    context.signal_name = get_signal_name(sig);
    context.description = "C runtime signal";
    
    // Attempt to capture backtrace
    capture_backtrace(context);
    
    // Log crash information
    best_effort_flush(context);
    
    // Restore default and re-raise so the CRT/OS terminates as usual
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

/**
 * @brief Enhanced SEH exception filter with backtrace capture
 */
inline LONG WINAPI seh_filter(EXCEPTION_POINTERS* ep) {
    if (g_handling.exchange(true)) {
        return EXCEPTION_EXECUTE_HANDLER;
    }
    
    // Create crash context
    crash_context context{};
    context.signal_name = get_exception_name(ep->ExceptionRecord->ExceptionCode);
    context.description = "Unhandled SEH exception";
    
    // Capture crash address
    context.crash_address = ep->ExceptionRecord->ExceptionAddress;
    
    // Attempt to capture backtrace
    capture_backtrace(context);
    
    // Log crash information
    best_effort_flush(context);
    
    // Let OS proceed with default handling (WER/minidump if enabled)
    return EXCEPTION_EXECUTE_HANDLER;
}

/**
 * @brief Console control handler for clean shutdown
 */
inline BOOL WINAPI console_ctrl_handler(DWORD type) {
    crash_context context{};
    
    switch (type) {
        case CTRL_C_EVENT:
            context.signal_name = "CTRL_C_EVENT";
            context.description = "User pressed Ctrl+C";
            break;
        case CTRL_BREAK_EVENT:
            context.signal_name = "CTRL_BREAK_EVENT";
            context.description = "User pressed Ctrl+Break";
            break;
        case CTRL_CLOSE_EVENT:
            context.signal_name = "CTRL_CLOSE_EVENT";
            context.description = "Console window closing";
            break;
        case CTRL_LOGOFF_EVENT:
            context.signal_name = "CTRL_LOGOFF_EVENT";
            context.description = "User logging off";
            break;
        case CTRL_SHUTDOWN_EVENT:
            context.signal_name = "CTRL_SHUTDOWN_EVENT";
            context.description = "System shutting down";
            break;
        default:
            context.signal_name = "Unknown console event";
            context.description = "Unknown console control event";
            break;
    }
    
    best_effort_flush(context);
    return FALSE; // Allow default shutdown
}
#endif

// Common implementation for best_effort_flush
inline void best_effort_flush(const crash_context& context) noexcept {
    try {
        if (auto lg = spdlog::get(APPLOG)) {
            // Log the crash with context
            try {
                if (context.crash_address) {
                    lg->critical("FATAL: {} - {} (Address: 0x{:X})", 
                                context.signal_name ? context.signal_name : "Unknown",
                                context.description ? context.description : "No description",
                                reinterpret_cast<uintptr_t>(context.crash_address));
                } else {
                    lg->critical("FATAL: {} - {}", 
                                context.signal_name ? context.signal_name : "Unknown",
                                context.description ? context.description : "No description");
                }
            } catch (...) {}
            
            // Log backtrace if available
            try {
                if (context.message[0] != '\0') {
                    lg->critical("\n{}", context.message.data());
                }
            } catch (...) {}
            
            // Dump any buffered backtrace from spdlog
            try { 
                lg->dump_backtrace(); 
            } catch (...) {}
            
            // Ensure all logs are written
            try { 
                lg->flush(); 
            } catch (...) {}
        }
    } catch (...) {}
}

/**
 * @brief Install comprehensive crash handlers for all supported platforms
 * 
 * This function sets up signal handlers, exception filters, and other
 * crash detection mechanisms appropriate for the current platform.
 * It should be called once during application initialization.
 * 
 * @note This function is thread-safe and can be called multiple times safely
 */
inline void install_handlers() {
    // Prevent multiple installations
    static std::atomic<bool> installed{false};
    if (installed.exchange(true)) {
        return; // Already installed
    }
    
    // C++ terminate handler for uncaught exceptions
    std::set_terminate([]() noexcept {
        crash_context context{};
        context.signal_name = "std::terminate";
        context.description = "Uncaught C++ exception";
        
        // Try to capture backtrace
        capture_backtrace(context);
        
        best_effort_flush(context);
        
#if UNRAVEL_PLATFORM_WINDOWS
        // Make sure we die (and possibly trigger WER) after flushing
        std::signal(SIGABRT, SIG_DFL);
#endif
        std::abort();
    });

#if UNRAVEL_PLATFORM_WINDOWS
    // Initialize symbol handler early for better backtrace quality
    initialize_symbol_handler();
    
    // Avoid dialog boxes that could block process exit after abort/assert
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);

    // C runtime signals — this catches std::abort() and other signals
    std::signal(SIGABRT, &c_signal_handler);
    std::signal(SIGTERM, &c_signal_handler);
    std::signal(SIGINT,  &c_signal_handler);
#ifdef SIGBREAK
    std::signal(SIGBREAK, &c_signal_handler);
#endif
    std::signal(SIGILL,  &c_signal_handler);
    std::signal(SIGFPE,  &c_signal_handler);
    // Note: SIGSEGV via signal() is unreliable on Windows; use SEH instead

    // SEH for access violations, stack overflows, etc.
    SetUnhandledExceptionFilter(&seh_filter);

    // Console control events (Ctrl+C, window close, etc.)
    SetConsoleCtrlHandler(&console_ctrl_handler, TRUE);

#elif UNRAVEL_PLATFORM_POSIX
    // Enable core dumps for post-mortem debugging
    enable_core_dumps();

    // Set up comprehensive signal handling
    struct sigaction sa{};
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = &posix_signal_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_NODEFER;
    
    // Block all signals during crash handling to prevent recursion
    sigfillset(&sa.sa_mask);

    // Critical crash signals
    sigaction(SIGSEGV, &sa, nullptr);   // Segmentation fault
    sigaction(SIGABRT, &sa, nullptr);   // Process abort
    sigaction(SIGILL,  &sa, nullptr);   // Illegal instruction
    sigaction(SIGFPE,  &sa, nullptr);   // Floating point exception
    
#ifdef SIGBUS
    sigaction(SIGBUS,  &sa, nullptr);   // Bus error (alignment, etc.)
#endif

    // Termination signals (allow for clean shutdown logging)
    sigaction(SIGINT,  &sa, nullptr);   // Interrupt (Ctrl+C)
    sigaction(SIGTERM, &sa, nullptr);   // Termination request
    sigaction(SIGQUIT, &sa, nullptr);   // Quit request
    sigaction(SIGHUP,  &sa, nullptr);   // Hangup
    
    // Ignore SIGPIPE by default (broken pipe should not crash the application)
    signal(SIGPIPE, SIG_IGN);
    
#else
    #error "Unsupported platform for crash handler installation"
#endif
}

/**
 * @brief Manually trigger crash handler for testing purposes
 * 
 * This function can be used to test the crash handling infrastructure.
 * It will simulate a crash and trigger all the logging and backtrace
 * capture mechanisms.
 * 
 * @warning This function will terminate the application!
 */
inline void test_crash_handler() noexcept {
    crash_context context{};
    context.signal_name = "TEST_CRASH";
    context.description = "Manual crash handler test";
    
    capture_backtrace(context);
    best_effort_flush(context);
    
    std::abort();
}

} // namespace unravel::crash
