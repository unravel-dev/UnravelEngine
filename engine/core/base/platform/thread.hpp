#pragma once

#include "config.hpp"

#include <thread>

// An attempt at making a wrapper to deal with many Linuxes as well as Windows. Please edit as needed.
#if UNRAVEL_PLATFORM_WINDOWS && (UNRAVEL_COMPILER_MSVC || UNRAVEL_COMPILER_CLANG)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

namespace platform
{
#pragma pack(push, 8)
typedef struct tagTHREADNAME_INFO
{
    DWORD dwType;     // Must be 0x1000.
    LPCSTR szName;    // Pointer to name (in user addr space).
    DWORD dwThreadID; // Thread ID (-1=caller thread).
    DWORD dwFlags;    // Reserved for future use, must be zero.
} THREADNAME_INFO;
#pragma pack(pop)

inline void set_thread_name(DWORD dwThreadID, const char* threadName)
{
    THREADNAME_INFO info;
    info.dwType = 0x1000;
    info.szName = threadName;
    info.dwThreadID = dwThreadID;
    info.dwFlags = 0;

    static const DWORD MS_VC_EXCEPTION = 0x406D1388;

    // Push an exception handler to ignore all following exceptions
#pragma warning(push)
#pragma warning(disable : 6320 6322)
    __try
    {
        RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }
#pragma warning(pop)
}

inline void set_thread_name(const char* threadName)
{
    DWORD threadId = ::GetCurrentThreadId();
    //DWORD threadId = ::GetThreadId(reinterpret_cast<HANDLE>(thread.native_handle()));
    set_thread_name(threadId, threadName);
}

inline auto get_boot_max_mhz() -> DWORD
{
    using get_max_proc_freq_fn = DWORD(WINAPI*)(DWORD, BYTE);
    static const get_max_proc_freq_fn fn = []() -> get_max_proc_freq_fn
    {
        return reinterpret_cast<get_max_proc_freq_fn>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetMaximumProcessorFrequency"));
    }();
    if(fn != nullptr)
    {
        const DWORD m = fn(0, 0);
        if(m != 0)
        {
            return m;
        }
    }
    return 3000;
}

inline auto get_thread_cpu_time_ns() -> int64_t
{
    // GetThreadTimes() is valid for "CPU time used" but its counters advance at the
    // system timer resolution (~15.6 ms by default). Scoped measurements shorter than
    // that almost always see identical user+kernel FILETIMEs -> delta 0 -> bogus 0% CPU.
    //
    // QueryThreadCycleTime() counts CPU cycles attributed to the thread while it runs;
    // deltas are fine-grained. We convert cumulative cycles to approximate nanoseconds
    // using the CPU's advertised max frequency (GetMaximumProcessorFrequency when present).
    ULONG64 cycles = 0;
    if(QueryThreadCycleTime(GetCurrentThread(), &cycles))
    {
        static const double ns_per_cycle = []() -> double
        {
            const DWORD mhz = get_boot_max_mhz();
            const double hz = static_cast<double>(mhz) * 1e6;
            return 1e9 / hz;
        }();
        return static_cast<int64_t>(static_cast<double>(cycles) * ns_per_cycle);
    }

    FILETIME creation, exit, kernel, user;
    if(GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user))
    {
        auto filetime_to_ns = [](const FILETIME& ft) -> int64_t
        {
            ULARGE_INTEGER li;
            li.LowPart = ft.dwLowDateTime;
            li.HighPart = ft.dwHighDateTime;
            return static_cast<int64_t>(li.QuadPart) * 100;
        };
        return filetime_to_ns(kernel) + filetime_to_ns(user);
    }
    return 0;
}
} // namespace platform
#elif UNRAVEL_PLATFORM_LINUX
#include <pthread.h>
#include <time.h>
namespace platform
{
inline void set_thread_name(const char* threadName)
{
    pthread_setname_np(pthread_self(), threadName);
}

inline auto get_thread_cpu_time_ns() -> int64_t
{
    struct timespec ts{};
    if(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0)
    {
        return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + static_cast<int64_t>(ts.tv_nsec);
    }
    return 0;
}
} // namespace platform
#elif UNRAVEL_PLATFORM_OSX
#include <pthread.h>
#include <time.h>
namespace platform
{
inline void set_thread_name(const char* threadName)
{
    pthread_setname_np(threadName);
}

inline auto get_thread_cpu_time_ns() -> int64_t
{
    struct timespec ts{};
    if(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0)
    {
        return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + static_cast<int64_t>(ts.tv_nsec);
    }
    return 0;
}
} // namespace platform
#else
namespace platform
{
inline void set_thread_name(const char* threadName)
{
}

inline auto get_thread_cpu_time_ns() -> int64_t
{
    return 0;
}
} // namespace platform
#endif

