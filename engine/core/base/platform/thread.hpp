#pragma once

#include "config.hpp"

#include <thread>

// An attempt at making a wrapper to deal with many Linuxes as well as Windows. Please edit as needed.
#if UNRAVEL_PLATFORM_WINDOWS && (UNRAVEL_COMPILER_MSVC || UNRAVEL_COMPILER_CLANG)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <intrin.h>

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

/// Drops the calling thread below normal priority, for background work that should yield the cores to the frame.
inline void set_thread_background_priority()
{
    ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
}

#if UNRAVEL_CPU_X86
/// The time stamp counter and the performance counter, read as close together as two calls allow.
struct clock_sample
{
    LONGLONG performance_ticks{};
    ULONG64 tsc_ticks{};
};

inline auto read_clock_sample() -> clock_sample
{
    LARGE_INTEGER performance_ticks{};
    const ULONG64 tsc_before = __rdtsc();
    QueryPerformanceCounter(&performance_ticks);
    const ULONG64 tsc_after = __rdtsc();
    return {performance_ticks.QuadPart, tsc_before + (tsc_after - tsc_before) / 2};
}

/// Nanoseconds per tick of the time stamp counter, measured once against the performance counter.
/// QueryThreadCycleTime() counts in these ticks, and the invariant TSC of x64 CPUs ticks at one
/// fixed rate - the nominal clock, neither the boost clock nor an advertised maximum. Any other
/// rate scales every thread CPU time, until a busy time comes out longer than its wall time.
inline auto get_tsc_tick_ns() -> double
{
    static const double tick_ns = []() -> double
    {
        // A hundredth of the performance counter frequency: a 10 ms measurement.
        constexpr LONGLONG CALIBRATION_DIVISOR = 100;
        constexpr double NANOSECONDS_PER_SECOND = 1e9;
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        const LONGLONG calibration_ticks = frequency.QuadPart / CALIBRATION_DIVISOR;
        const clock_sample start = read_clock_sample();
        clock_sample end = start;
        while(end.performance_ticks - start.performance_ticks < calibration_ticks)
        {
            end = read_clock_sample();
        }
        const double elapsed_ns = static_cast<double>(end.performance_ticks - start.performance_ticks) *
                                  NANOSECONDS_PER_SECOND / static_cast<double>(frequency.QuadPart);
        return elapsed_ns / static_cast<double>(end.tsc_ticks - start.tsc_ticks);
    }();
    return tick_ns;
}
#endif

inline auto get_thread_cpu_time_ns() -> int64_t
{
    // GetThreadTimes() is valid for "CPU time used" but its counters advance at the
    // system timer resolution (~15.6 ms by default). Scoped measurements shorter than
    // that almost always see identical user+kernel FILETIMEs -> delta 0 -> bogus 0% CPU.
    //
    // QueryThreadCycleTime() counts the TSC ticks attributed to the thread while it runs;
    // deltas are fine-grained, and get_tsc_tick_ns() turns them into nanoseconds.
#if UNRAVEL_CPU_X86
    ULONG64 cycles = 0;
    if(QueryThreadCycleTime(GetCurrentThread(), &cycles))
    {
        return static_cast<int64_t>(static_cast<double>(cycles) * get_tsc_tick_ns());
    }
#endif

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
#elif UNRAVEL_PLATFORM_LINUX || UNRAVEL_PLATFORM_ANDROID
#include <pthread.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <cstring>
namespace platform
{
inline void set_thread_name(const char* threadName)
{
    // The kernel keeps 15 characters and the terminator, and refuses a longer name instead of cutting it.
    constexpr size_t MAX_THREAD_NAME_LENGTH = 15;
    char name[MAX_THREAD_NAME_LENGTH + 1]{};
    std::strncpy(name, threadName, MAX_THREAD_NAME_LENGTH);
    pthread_setname_np(pthread_self(), name);
}

/// Drops the calling thread below normal priority, for background work that should yield the cores to the frame.
/// Linux schedules each thread as a task of its own, so the thread's nice value is its priority.
inline void set_thread_background_priority()
{
    constexpr int BACKGROUND_NICE = 10;
    setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), BACKGROUND_NICE);
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
#include <pthread/qos.h>
#include <time.h>
namespace platform
{
inline void set_thread_name(const char* threadName)
{
    pthread_setname_np(threadName);
}

/// Drops the calling thread below normal priority, for background work that should yield the cores to the frame.
inline void set_thread_background_priority()
{
    pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
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

inline void set_thread_background_priority()
{
}

inline auto get_thread_cpu_time_ns() -> int64_t
{
    return 0;
}
} // namespace platform
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace platform
{
/**
 * @brief The cores fast enough for frame work: the size for worker pools.
 *
 * Phones pair fast cores with slow, efficient ones (big.LITTLE), and work split evenly across all of
 * them waits for the slow ones. On Android a core counts when the scheduler rates its capacity at least
 * half the fastest core's (4 of 8 on a phone with 4 little cores). Elsewhere, and where the capacities
 * are unknown, every core counts.
 */
inline auto get_performance_core_count() -> uint32_t
{
    static const uint32_t count = []() -> uint32_t
    {
        const uint32_t cores = std::max(1u, std::thread::hardware_concurrency());
#if UNRAVEL_PLATFORM_ANDROID
        std::vector<int> capacities;
        for(uint32_t core = 0; core < cores; ++core)
        {
            char path[64]{};
            std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%u/cpu_capacity", core);
            std::FILE* file = std::fopen(path, "r");
            int capacity = 0;
            const bool is_read = file != nullptr && std::fscanf(file, "%d", &capacity) == 1;
            if(file != nullptr)
            {
                std::fclose(file);
            }
            if(!is_read || capacity <= 0)
            {
                return cores;
            }
            capacities.push_back(capacity);
        }
        const int fastest = *std::max_element(capacities.begin(), capacities.end());
        const auto fast_cores = std::count_if(capacities.begin(),
                                              capacities.end(),
                                              [fastest](int capacity)
                                              {
                                                  return capacity * 2 >= fastest;
                                              });
        return std::max(1u, static_cast<uint32_t>(fast_cores));
#else
        return cores;
#endif
    }();
    return count;
}
} // namespace platform

