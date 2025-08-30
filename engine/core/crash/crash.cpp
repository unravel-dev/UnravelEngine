/**
 * @file crash_handlers.cpp
 * @brief Implementation of crash handler functionality
 */

#include "crash.hpp"

#include <base/platform/config.hpp>

#include <atomic>
#include <cpptrace/cpptrace.hpp>
#include <cpptrace/formatting.hpp>
#include <csignal>
#include <string_view>
#include <exception>
#include <typeinfo>



namespace unravel::crash
{

// Internal declarations and types
namespace
{

/// Atomic flag to prevent recursive crash handling
std::atomic<bool> g_handling{false};



/// Global callback storage
interrupt_handler_t g_interrupt_handler{nullptr};
termination_handler_t g_termination_handler{nullptr};
crash_handler_t g_crash_handler{nullptr};
exception_handler_t g_exception_handler{nullptr};


auto demangle_exception_type(const std::exception& e) -> std::string
{
    return cpptrace::demangle(typeid(e).name());
}

auto generate_stack_trace() -> trace_info
{
    try
    {
        auto stack_trace = cpptrace::generate_trace(1);
        return trace_info{stack_trace.to_string(false)};
    }
    catch(...)
    {
        return trace_info{};
    }
}

} // anonymous namespace

// Internal function declarations
auto get_signal_name(int sig) noexcept -> std::string_view;
auto is_crash_signal(int sig) noexcept -> bool;

auto signal_handler(int sig) -> void;

// Setter functions
auto set_interrupt_handler(interrupt_handler_t handler) -> void
{
    g_interrupt_handler = handler;
}

auto set_termination_handler(termination_handler_t handler) -> void
{
    g_termination_handler = handler;
}

auto set_crash_handler(crash_handler_t handler) -> void
{
    g_crash_handler = handler;
}

auto set_exception_handler(exception_handler_t handler) -> void
{
    g_exception_handler = handler;
}

auto get_signal_name(int sig) noexcept -> std::string_view
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
#ifdef SIGKILL
        case SIGKILL:
            return "SIGKILL (User kill process)";
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

enum class signal_type { interrupt, termination, crash };

auto get_signal_type(int sig) noexcept -> signal_type
{
    switch(sig)
    {
        // User interrupts
        case SIGINT:    // Ctrl+C
#ifdef SIGBREAK
        case SIGBREAK:  // Ctrl+Break (Windows)
#endif
            return signal_type::interrupt;
            
        // Termination requests
        case SIGTERM:   // Termination request
#ifdef SIGQUIT
        case SIGQUIT:   // Quit request
#endif
#ifdef SIGKILL
        case SIGKILL:   // Kill request
#endif
#ifdef SIGHUP
        case SIGHUP:    // Terminal hangup
#endif
#ifdef SIGPIPE
        case SIGPIPE:   // Broken pipe
#endif
            return signal_type::termination;
            
        // Actual crashes
        case SIGSEGV:   // Segmentation fault
        case SIGABRT:   // Process abort
        case SIGILL:    // Illegal instruction  
        case SIGFPE:    // Floating point exception
#ifdef SIGBUS
        case SIGBUS:    // Bus error
#endif
#ifdef SIGABRT_COMPAT
        case SIGABRT_COMPAT:  // Process abort compat
#endif
        default:        // Unknown signals treated as crashes for safety
            return signal_type::crash;
    }
}

auto is_crash_signal(int sig) noexcept -> bool
{
    return get_signal_type(sig) == signal_type::crash;
}

auto signal_handler(int sig) -> void
{
    // Prevent recursive crashes
    if(g_handling.exchange(true, std::memory_order_acq_rel))
    {
        std::exit(128 + sig);
    }

    const auto sig_type = get_signal_type(sig);
    const char* sig_name = get_signal_name(sig).data();
    
    // Create signal info for callbacks (no crash address since we have stack traces)
    signal_info sig_info{
        .signal_number = sig,
        .signal_name = sig_name,
    };


    // Handle based on signal type
    switch(sig_type)
    {
        case signal_type::interrupt:
        {
            if(g_interrupt_handler)
            {
                try { g_interrupt_handler(sig_info); } catch(...) {}
            }
            break;
        }
        
        case signal_type::termination:
        {
            if(g_termination_handler)
            {
                try { g_termination_handler(sig_info); } catch(...) {}
            }
            break;
        }
        
        case signal_type::crash:
        {
            
            if(g_crash_handler)
            {
                // Capture stack trace for crashes
                trace_info trace = generate_stack_trace();
                try { g_crash_handler(sig_info, trace); } catch(...) {}
            }
            break;
        }
    }

}


[[noreturn]] void terminate_handler()
{
    // Prevent recursive terminate calls
    if(g_handling.exchange(true, std::memory_order_acq_rel))
    {
        std::abort();
    }

    if(!g_exception_handler)
    {
        std::abort();
    }

    // Prepare exception info
    exception_info exc_info{
        .exception_type = "Unknown",
        .exception_message = "No active exception"
    };
    
    // Prepare trace info
    trace_info trace{};

    try
    {
        auto ptr = std::current_exception();
        if(ptr != nullptr)
        {
            std::rethrow_exception(ptr);
        }
        else
        {
            exc_info.exception_message = "Terminate called without an active exception";
        }
    }
    catch(const std::exception& e)
    {
        // Standard exception - generate our own trace
        exc_info.exception_type = demangle_exception_type(e);
        exc_info.exception_message = "Terminate called after throwing " + exc_info.exception_type + " : " + e.what();
        
        // Generate stack trace
        trace = generate_stack_trace();
    }
    catch(...)
    {
        // Unknown exception type
        exc_info.exception_type = "Unknown Exception Type";
        exc_info.exception_message = "Terminate called after throwing an unknown exception";
        
        // Generate stack trace
        trace = generate_stack_trace();
    }

    // Call the user's exception handler if available
    try 
    { 
        g_exception_handler(exc_info, trace); 
    } 
    catch(...) 
    {
        // Handler threw - can't do much about it
    }
    

    std::abort();
}

void register_terminate_handler()
{
    std::set_terminate(terminate_handler);
}


auto install_handlers(const crash_handlers& handlers) -> void
{
    // Prevent multiple installations
    static std::atomic<bool> installed{false};
    if(installed.exchange(true, std::memory_order_acq_rel))
    {
        return;
    }

    set_interrupt_handler(handlers.interrupt_handler);
    set_termination_handler(handlers.termination_handler);
    set_crash_handler(handlers.crash_handler);
    set_exception_handler(handlers.exception_handler);

    register_terminate_handler();

    // Install unified signal handlers for all platforms
    
    // Crash signals
    std::signal(SIGABRT, signal_handler);   // Process abort
    std::signal(SIGILL, signal_handler);    // Illegal instruction
    std::signal(SIGFPE, signal_handler);    // Floating point exception
    std::signal(SIGSEGV, signal_handler);   // Segmentation fault
    
    // Interrupt signals
    std::signal(SIGINT, signal_handler);    // Ctrl+C
    
    // Termination signals
    std::signal(SIGTERM, signal_handler);   // Termination request

    // Platform-specific signals
#ifdef SIGBUS
    std::signal(SIGBUS, signal_handler);    // Bus error (POSIX)
#endif
#ifdef SIGQUIT
    std::signal(SIGQUIT, signal_handler);   // Quit request (POSIX)
#endif
#ifdef SIGHUP
    std::signal(SIGHUP, signal_handler);    // Terminal hangup (POSIX)
#endif
#ifdef SIGBREAK
    std::signal(SIGBREAK, signal_handler);  // Ctrl+Break (Windows)
#endif
#ifdef SIGABRT_COMPAT
    std::signal(SIGABRT_COMPAT, signal_handler);  // Process abort compat
#endif
#ifdef SIGKILL
    std::signal(SIGKILL, signal_handler);   // Kill request (POSIX)
#endif

    // Ignored signals
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);          // Broken pipe (don't crash on this)
#endif

}


} // namespace unravel::crash
