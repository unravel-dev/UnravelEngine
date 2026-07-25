/**
 * @file crash.cpp
 * @brief Crash handlers: Windows SEH + POSIX sigaction, crash-safe I/O, minidumps.
 */

#include "crash.hpp"

#include <base/platform/config.hpp>

#include <atomic>
#include <cpptrace/cpptrace.hpp>
#include <cstdint>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <exception>
#include <string>
#include <typeinfo>

#if UNRAVEL_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <DbgHelp.h>
#else
#include <fcntl.h>
#include <unistd.h>
#if defined(SA_SIGINFO)
#include <ucontext.h>
#endif
#endif

namespace unravel::crash
{
namespace
{

std::atomic<bool> g_handling{false};
std::atomic<bool> g_installed{false};

interrupt_handler_t g_interrupt_handler{nullptr};
termination_handler_t g_termination_handler{nullptr};
crash_handler_t g_crash_handler{nullptr};
exception_handler_t g_exception_handler{nullptr};

char g_crash_log_path[512]{"CrashLog.txt"};
bool g_write_minidump{true};

#if !UNRAVEL_PLATFORM_WINDOWS
// Fixed alt-stack (avoid SIGSTKSZ — not a constant on newer glibc).
alignas(16) unsigned char g_alt_stack_storage[256 * 1024];
#endif

constexpr std::size_t k_max_frames = 128;
constexpr std::size_t k_emergency_buf = 16384;

auto demangle_exception_type(const std::exception& e) -> std::string
{
    return cpptrace::demangle(typeid(e).name());
}

auto append_cstr(char* dst, std::size_t capacity, std::size_t& used, const char* text) -> void
{
    if(text == nullptr || used >= capacity)
    {
        return;
    }
    const std::size_t remaining = capacity - used - 1;
    const std::size_t len = std::strlen(text);
    const std::size_t n = len < remaining ? len : remaining;
    if(n > 0)
    {
        std::memcpy(dst + used, text, n);
        used += n;
        dst[used] = '\0';
    }
}

auto append_hex_u64(char* dst, std::size_t capacity, std::size_t& used, std::uint64_t value) -> void
{
    static constexpr char k_digits[] = "0123456789abcdef";
    char out[19];
    out[0] = '0';
    out[1] = 'x';
    bool started = false;
    std::size_t o = 2;
    for(int i = 15; i >= 0; --i)
    {
        const char digit = k_digits[(value >> (i * 4)) & 0xfu];
        if(!started && digit == '0' && i > 0)
        {
            continue;
        }
        started = true;
        out[o++] = digit;
    }
    out[o] = '\0';
    append_cstr(dst, capacity, used, out);
}

auto emergency_write(const char* data, std::size_t len) noexcept -> void
{
    if(data == nullptr || len == 0)
    {
        return;
    }
#if UNRAVEL_PLATFORM_WINDOWS
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    if(err != nullptr && err != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(err, data, static_cast<DWORD>(len), &written, nullptr);
        FlushFileBuffers(err);
    }
    HANDLE file = CreateFileA(g_crash_log_path,
                              FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr,
                              OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if(file != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(file, data, static_cast<DWORD>(len), &written, nullptr);
        FlushFileBuffers(file);
        CloseHandle(file);
    }
#else
    if(len > 0)
    {
        (void)::write(STDERR_FILENO, data, len);
    }
    const int fd = ::open(g_crash_log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if(fd >= 0)
    {
        (void)::write(fd, data, len);
        (void)::fsync(fd);
        (void)::close(fd);
    }
#endif
}

auto emergency_write_cstr(const char* text) noexcept -> void
{
    if(text != nullptr)
    {
        emergency_write(text, std::strlen(text));
    }
}

auto capture_trace_text(char* buf, std::size_t capacity) noexcept -> std::size_t
{
    std::size_t used = 0;
    if(capacity == 0)
    {
        return 0;
    }
    buf[0] = '\0';
    append_cstr(buf, capacity, used, "Stack trace:\n");
    cpptrace::frame_ptr frames[k_max_frames]{};
    std::size_t count = 0;
    try
    {
        if(cpptrace::can_signal_safe_unwind())
        {
            count = cpptrace::safe_generate_raw_trace(frames, k_max_frames, 1);
        }
    }
    catch(...)
    {
        count = 0;
    }
    if(count == 0)
    {
        // Best-effort fallback (may allocate); only used when safe unwind unavailable.
        try
        {
            const auto stack_trace = cpptrace::generate_trace(1);
            const std::string formatted = stack_trace.to_string(false);
            append_cstr(buf, capacity, used, formatted.c_str());
            append_cstr(buf, capacity, used, "\n");
            return used;
        }
        catch(...)
        {
            append_cstr(buf, capacity, used, "  <failed to capture stack>\n");
            return used;
        }
    }
    for(std::size_t i = 0; i < count; ++i)
    {
        append_cstr(buf, capacity, used, "  #");
        char idx_buf[16]{};
        // tiny decimal
        {
            unsigned v = static_cast<unsigned>(i);
            char rev[16];
            int n = 0;
            if(v == 0)
            {
                rev[n++] = '0';
            }
            while(v > 0 && n < 15)
            {
                rev[n++] = static_cast<char>('0' + (v % 10));
                v /= 10;
            }
            int o = 0;
            while(n > 0 && o < 15)
            {
                idx_buf[o++] = rev[--n];
            }
            idx_buf[o] = '\0';
        }
        append_cstr(buf, capacity, used, idx_buf);
        append_cstr(buf, capacity, used, " ");
        append_hex_u64(buf, capacity, used, static_cast<std::uint64_t>(frames[i]));
        if(cpptrace::can_get_safe_object_frame())
        {
            cpptrace::safe_object_frame object_frame{};
            try
            {
                cpptrace::get_safe_object_frame(frames[i], &object_frame);
                if(object_frame.object_path[0] != '\0')
                {
                    append_cstr(buf, capacity, used, " ");
                    append_cstr(buf, capacity, used, object_frame.object_path);
                    append_cstr(buf, capacity, used, "+");
                    append_hex_u64(buf,
                                   capacity,
                                   used,
                                   static_cast<std::uint64_t>(object_frame.address_relative_to_object_start));
                }
            }
            catch(...)
            {
            }
        }
        append_cstr(buf, capacity, used, "\n");
    }
    // Best-effort symbolized trace after the safe address dump.
    try
    {
        const auto stack_trace = cpptrace::generate_trace(1);
        const std::string formatted = stack_trace.to_string(false);
        append_cstr(buf, capacity, used, "\nSymbolized stack (best-effort):\n");
        append_cstr(buf, capacity, used, formatted.c_str());
        append_cstr(buf, capacity, used, "\n");
    }
    catch(...)
    {
    }
    return used;
}

#if UNRAVEL_PLATFORM_WINDOWS
auto exception_code_name(DWORD code) noexcept -> const char*
{
    switch(code)
    {
        case EXCEPTION_ACCESS_VIOLATION:
            return "EXCEPTION_ACCESS_VIOLATION (Segmentation fault)";
        case EXCEPTION_STACK_OVERFLOW:
            return "EXCEPTION_STACK_OVERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            return "EXCEPTION_DATATYPE_MISALIGNMENT";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_IN_PAGE_ERROR:
            return "EXCEPTION_IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_INVALID_OPERATION:
            return "EXCEPTION_FLT_INVALID_OPERATION";
        case EXCEPTION_PRIV_INSTRUCTION:
            return "EXCEPTION_PRIV_INSTRUCTION";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
            return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
        default:
            return "Windows structured exception";
    }
}

auto is_fatal_exception_code(DWORD code) noexcept -> bool
{
    switch(code)
    {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_DATATYPE_MISALIGNMENT:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        case EXCEPTION_FLT_OVERFLOW:
        case EXCEPTION_FLT_UNDERFLOW:
        case EXCEPTION_FLT_INVALID_OPERATION:
        case EXCEPTION_FLT_DENORMAL_OPERAND:
        case EXCEPTION_FLT_STACK_CHECK:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
            return true;
        default:
            // Ignore breakpoints, C++ EH (0xE06D7363), and other non-fatal codes.
            return false;
    }
}

auto write_minidump_windows(EXCEPTION_POINTERS* exception_pointers) noexcept -> void
{
    if(!g_write_minidump || exception_pointers == nullptr)
    {
        return;
    }
    char dump_path[560]{};
    std::size_t used = 0;
    append_cstr(dump_path, sizeof(dump_path), used, g_crash_log_path);
    // Replace extension / append .dmp
    char* dot = nullptr;
    for(char* p = dump_path; *p != '\0'; ++p)
    {
        if(*p == '.')
        {
            dot = p;
        }
        if(*p == '\\' || *p == '/')
        {
            dot = nullptr;
        }
    }
    if(dot != nullptr)
    {
        used = static_cast<std::size_t>(dot - dump_path);
        dump_path[used] = '\0';
    }
    append_cstr(dump_path, sizeof(dump_path), used, ".dmp");
    HANDLE file = CreateFileA(dump_path,
                              GENERIC_WRITE,
                              0,
                              nullptr,
                              CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if(file == INVALID_HANDLE_VALUE)
    {
        emergency_write_cstr("Crash handler: failed to create minidump file\n");
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = exception_pointers;
    mei.ClientPointers = FALSE;
    const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(),
                                      GetCurrentProcessId(),
                                      file,
                                      static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithHandleData |
                                                                 MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
                                      &mei,
                                      nullptr,
                                      nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);
    if(ok)
    {
        emergency_write_cstr("Minidump written: ");
        emergency_write_cstr(dump_path);
        emergency_write_cstr("\n");
    }
    else
    {
        emergency_write_cstr("Crash handler: MiniDumpWriteDump failed\n");
    }
}
#endif

auto report_fatal_crash(const signal_info& sig_info, void* platform_context) noexcept -> void
{
    emergency_write_cstr("\n========== FATAL CRASH ==========\n");
    emergency_write_cstr(sig_info.signal_name != nullptr ? sig_info.signal_name : "Unknown");
    emergency_write_cstr("\n");
#if UNRAVEL_PLATFORM_WINDOWS
    if(platform_context != nullptr)
    {
        write_minidump_windows(static_cast<EXCEPTION_POINTERS*>(platform_context));
    }
#else
    (void)platform_context;
#endif
    char trace_buf[k_emergency_buf];
    const std::size_t trace_len = capture_trace_text(trace_buf, sizeof(trace_buf));
    emergency_write(trace_buf, trace_len);
    emergency_write_cstr("========== END CRASH ==========\n");
    if(g_crash_handler != nullptr)
    {
        trace_info trace{};
        trace.formatted_trace.assign(trace_buf, trace_len);
        try
        {
            g_crash_handler(sig_info, trace);
        }
        catch(...)
        {
        }
    }
}

auto get_signal_name(int sig) noexcept -> const char*
{
    switch(sig)
    {
        case SIGSEGV:
            return "SIGSEGV (Segmentation fault)";
        case SIGABRT:
            return "SIGABRT (Process abort)";
        case SIGILL:
            return "SIGILL (Illegal instruction)";
        case SIGFPE:
            return "SIGFPE (Floating point exception)";
#ifdef SIGBUS
        case SIGBUS:
            return "SIGBUS (Bus error)";
#endif
        case SIGINT:
            return "SIGINT (User interrupt)";
        case SIGTERM:
            return "SIGTERM (Termination request)";
#ifdef SIGPIPE
        case SIGPIPE:
            return "SIGPIPE (Broken pipe)";
#endif
#ifdef SIGQUIT
        case SIGQUIT:
            return "SIGQUIT (User quit)";
#endif
#ifdef SIGHUP
        case SIGHUP:
            return "SIGHUP (Terminal hangup)";
#endif
#ifdef SIGBREAK
        case SIGBREAK:
            return "SIGBREAK (Break request)";
#endif
#ifdef SIGABRT_COMPAT
        case SIGABRT_COMPAT:
            return "SIGABRT_COMPAT (Process abort compat)";
#endif
        default:
            return "Unknown signal";
    }
}

enum class signal_type
{
    interrupt,
    termination,
    crash
};

auto get_signal_type(int sig) noexcept -> signal_type
{
    switch(sig)
    {
        case SIGINT:
#ifdef SIGBREAK
        case SIGBREAK:
#endif
            return signal_type::interrupt;
        case SIGTERM:
#ifdef SIGQUIT
        case SIGQUIT:
#endif
#ifdef SIGHUP
        case SIGHUP:
#endif
#ifdef SIGPIPE
        case SIGPIPE:
#endif
            return signal_type::termination;
        case SIGSEGV:
        case SIGABRT:
        case SIGILL:
        case SIGFPE:
#ifdef SIGBUS
        case SIGBUS:
#endif
#ifdef SIGABRT_COMPAT
        case SIGABRT_COMPAT:
#endif
        default:
            return signal_type::crash;
    }
}

auto handle_signal(int sig) -> void
{
    if(g_handling.exchange(true, std::memory_order_acq_rel))
    {
        std::_Exit(128 + sig);
    }
    const signal_type sig_type = get_signal_type(sig);
    const signal_info sig_info{
        .signal_number = sig,
        .signal_name = get_signal_name(sig),
    };
    switch(sig_type)
    {
        case signal_type::interrupt:
        {
            if(g_interrupt_handler != nullptr)
            {
                try
                {
                    g_interrupt_handler(sig_info);
                }
                catch(...)
                {
                }
            }
            g_handling.store(false, std::memory_order_release);
            return;
        }
        case signal_type::termination:
        {
            if(g_termination_handler != nullptr)
            {
                try
                {
                    g_termination_handler(sig_info);
                }
                catch(...)
                {
                }
            }
            g_handling.store(false, std::memory_order_release);
            return;
        }
        case signal_type::crash:
        {
            report_fatal_crash(sig_info, nullptr);
            std::_Exit(128 + sig);
        }
    }
}

#if UNRAVEL_PLATFORM_WINDOWS
LONG WINAPI unhandled_exception_filter(EXCEPTION_POINTERS* info)
{
    if(info == nullptr || info->ExceptionRecord == nullptr)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if(!is_fatal_exception_code(code))
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if(g_handling.exchange(true, std::memory_order_acq_rel))
    {
        return EXCEPTION_EXECUTE_HANDLER;
    }
    const signal_info sig_info{
        .signal_number = static_cast<int>(code),
        .signal_name = exception_code_name(code),
    };
    report_fatal_crash(sig_info, info);
    return EXCEPTION_EXECUTE_HANDLER;
}
#else
auto posix_signal_handler(int sig, siginfo_t* /*info*/, void* /*ucontext*/) -> void
{
    handle_signal(sig);
}
#endif

[[noreturn]] void terminate_handler()
{
    if(g_handling.exchange(true, std::memory_order_acq_rel))
    {
        std::_Exit(128 + SIGABRT);
    }
    exception_info exc_info{
        .exception_type = "Unknown",
        .exception_message = "Terminate called without an active exception",
    };
    trace_info trace{};
    try
    {
        const auto ptr = std::current_exception();
        if(ptr != nullptr)
        {
            std::rethrow_exception(ptr);
        }
    }
    catch(const std::exception& e)
    {
        exc_info.exception_type = demangle_exception_type(e);
        exc_info.exception_message =
            "Terminate called after throwing " + exc_info.exception_type + " : " + e.what();
    }
    catch(...)
    {
        exc_info.exception_type = "Unknown Exception Type";
        exc_info.exception_message = "Terminate called after throwing an unknown exception";
    }
    emergency_write_cstr("\n========== FATAL TERMINATE ==========\n");
    emergency_write_cstr(exc_info.exception_message.c_str());
    emergency_write_cstr("\n");
    char trace_buf[k_emergency_buf];
    const std::size_t trace_len = capture_trace_text(trace_buf, sizeof(trace_buf));
    emergency_write(trace_buf, trace_len);
    emergency_write_cstr("========== END TERMINATE ==========\n");
    trace.formatted_trace.assign(trace_buf, trace_len);
    if(g_exception_handler != nullptr)
    {
        try
        {
            g_exception_handler(exc_info, trace);
        }
        catch(...)
        {
        }
    }
    std::_Exit(128 + SIGABRT);
}

auto install_platform_handlers() -> void
{
#if UNRAVEL_PLATFORM_WINDOWS
    // Primary path for real access violations / stack overflow / etc.
    SetUnhandledExceptionFilter(unhandled_exception_filter);
    // CRT-mediated signals (abort, raise, some translated faults).
    std::signal(SIGABRT, +[](int sig) { handle_signal(sig); });
    std::signal(SIGILL, +[](int sig) { handle_signal(sig); });
    std::signal(SIGFPE, +[](int sig) { handle_signal(sig); });
    std::signal(SIGSEGV, +[](int sig) { handle_signal(sig); });
    std::signal(SIGINT, +[](int sig) { handle_signal(sig); });
    std::signal(SIGTERM, +[](int sig) { handle_signal(sig); });
#ifdef SIGBREAK
    std::signal(SIGBREAK, +[](int sig) { handle_signal(sig); });
#endif
#ifdef SIGABRT_COMPAT
    std::signal(SIGABRT_COMPAT, +[](int sig) { handle_signal(sig); });
#endif
#else
    stack_t alt_stack{};
    alt_stack.ss_sp = g_alt_stack_storage;
    alt_stack.ss_size = sizeof(g_alt_stack_storage);
    alt_stack.ss_flags = 0;
    (void)sigaltstack(&alt_stack, nullptr);
    struct sigaction action
    {
    };
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    action.sa_sigaction = posix_signal_handler;
    const int crash_signals[] = {
        SIGSEGV,
        SIGABRT,
        SIGILL,
        SIGFPE,
#ifdef SIGBUS
        SIGBUS,
#endif
    };
    for(int sig : crash_signals)
    {
        (void)sigaction(sig, &action, nullptr);
    }
    struct sigaction soft_action
    {
    };
    sigemptyset(&soft_action.sa_mask);
    soft_action.sa_flags = SA_SIGINFO | SA_RESTART;
    soft_action.sa_sigaction = posix_signal_handler;
    (void)sigaction(SIGINT, &soft_action, nullptr);
    (void)sigaction(SIGTERM, &soft_action, nullptr);
#ifdef SIGQUIT
    (void)sigaction(SIGQUIT, &soft_action, nullptr);
#endif
#ifdef SIGHUP
    (void)sigaction(SIGHUP, &soft_action, nullptr);
#endif
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);
#endif
#endif
}

} // namespace

auto install_handlers(const crash_handlers& handlers) -> void
{
    if(g_installed.exchange(true, std::memory_order_acq_rel))
    {
        return;
    }
    g_interrupt_handler = handlers.interrupt_handler;
    g_termination_handler = handlers.termination_handler;
    g_crash_handler = handlers.crash_handler;
    g_exception_handler = handlers.exception_handler;
    g_write_minidump = handlers.write_minidump;
    if(handlers.crash_log_path != nullptr && handlers.crash_log_path[0] != '\0')
    {
        std::size_t used = 0;
        g_crash_log_path[0] = '\0';
        append_cstr(g_crash_log_path, sizeof(g_crash_log_path), used, handlers.crash_log_path);
    }
    std::set_terminate(terminate_handler);
    install_platform_handlers();
}

} // namespace unravel::crash
