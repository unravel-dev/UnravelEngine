#pragma once

#include "config.hpp"

#include <cstdint>

// Resident (physical) RAM for the current process — one lightweight query per call.
// Intended for periodic sampling (e.g. profiler snapshots); cost is typically microseconds.

#if UNRAVEL_PLATFORM_WINDOWS && (UNRAVEL_COMPILER_MSVC || UNRAVEL_COMPILER_CLANG)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Psapi.h>

namespace platform
{

inline auto get_process_resident_set_bytes() -> int64_t
{
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if(GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
    {
        return static_cast<int64_t>(pmc.WorkingSetSize);
    }
    return 0;
}

} // namespace platform

#elif UNRAVEL_PLATFORM_LINUX

#include <cstdio>
#include <unistd.h>

namespace platform
{

inline auto get_process_resident_set_bytes() -> int64_t
{
    const long page_size = sysconf(_SC_PAGESIZE);
    if(page_size <= 0)
    {
        return 0;
    }
    FILE* f = std::fopen("/proc/self/statm", "r");
    if(f == nullptr)
    {
        return 0;
    }
    long long total_pages = 0;
    long long resident_pages = 0;
    const int n = std::fscanf(f, "%lld %lld", &total_pages, &resident_pages);
    std::fclose(f);
    if(n != 2)
    {
        return 0;
    }
    return static_cast<int64_t>(resident_pages * page_size);
}

} // namespace platform

#elif UNRAVEL_PLATFORM_OSX

#include <mach/mach.h>
#include <mach/task_info.h>

namespace platform
{

inline auto get_process_resident_set_bytes() -> int64_t
{
    task_basic_info info{};
    mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
    const kern_return_t kr =
        task_info(mach_task_self(), TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count);
    if(kr != KERN_SUCCESS)
    {
        return 0;
    }
    return static_cast<int64_t>(info.resident_size);
}

} // namespace platform

#else

namespace platform
{

inline auto get_process_resident_set_bytes() -> int64_t
{
    return 0;
}

} // namespace platform

#endif
