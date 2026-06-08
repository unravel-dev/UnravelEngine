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

inline auto get_system_physical_memory_bytes() -> int64_t
{
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if(GlobalMemoryStatusEx(&status))
    {
        return static_cast<int64_t>(status.ullTotalPhys);
    }
    return 0;
}

inline auto get_system_available_physical_memory_bytes() -> int64_t
{
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if(GlobalMemoryStatusEx(&status))
    {
        return static_cast<int64_t>(status.ullAvailPhys);
    }
    return 0;
}

inline auto get_system_used_physical_memory_bytes() -> int64_t
{
    const int64_t total = get_system_physical_memory_bytes();
    const int64_t available = get_system_available_physical_memory_bytes();
    if(total > 0 && available >= 0 && available <= total)
    {
        return total - available;
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

inline auto get_system_physical_memory_bytes() -> int64_t
{
    const long page_size = sysconf(_SC_PAGESIZE);
    const long num_pages = sysconf(_SC_PHYS_PAGES);
    if(page_size > 0 && num_pages > 0)
    {
        return static_cast<int64_t>(page_size) * static_cast<int64_t>(num_pages);
    }
    return 0;
}

inline auto get_system_available_physical_memory_bytes() -> int64_t
{
    FILE* f = std::fopen("/proc/meminfo", "r");
    if(f == nullptr)
    {
        return 0;
    }

    char line[256] = {};
    long long mem_available_kb = -1;
    while(std::fgets(line, sizeof(line), f) != nullptr)
    {
        if(std::sscanf(line, "MemAvailable: %lld kB", &mem_available_kb) == 1)
        {
            break;
        }
    }
    std::fclose(f);

    if(mem_available_kb < 0)
    {
        return 0;
    }
    return static_cast<int64_t>(mem_available_kb) * 1024;
}

inline auto get_system_used_physical_memory_bytes() -> int64_t
{
    const int64_t total = get_system_physical_memory_bytes();
    const int64_t available = get_system_available_physical_memory_bytes();
    if(total > 0 && available >= 0 && available <= total)
    {
        return total - available;
    }
    return 0;
}

} // namespace platform

#elif UNRAVEL_PLATFORM_OSX

#include <mach/mach.h>
#include <mach/task_info.h>
#include <sys/sysctl.h>

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

inline auto get_system_physical_memory_bytes() -> int64_t
{
    int64_t mem = 0;
    size_t len = sizeof(mem);
    if(sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0)
    {
        return mem;
    }
    return 0;
}

inline auto get_system_available_physical_memory_bytes() -> int64_t
{
    vm_statistics64_data_t vm_stats{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    const kern_return_t kr = host_statistics64(mach_host_self(),
                                               HOST_VM_INFO64,
                                               reinterpret_cast<host_info64_t>(&vm_stats),
                                               &count);
    if(kr != KERN_SUCCESS)
    {
        return 0;
    }

    const int64_t page_size = static_cast<int64_t>(vm_page_size);
    const int64_t free_pages = static_cast<int64_t>(vm_stats.free_count) +
                               static_cast<int64_t>(vm_stats.inactive_count) +
                               static_cast<int64_t>(vm_stats.purgeable_count);
    return free_pages * page_size;
}

inline auto get_system_used_physical_memory_bytes() -> int64_t
{
    const int64_t total = get_system_physical_memory_bytes();
    const int64_t available = get_system_available_physical_memory_bytes();
    if(total > 0 && available >= 0 && available <= total)
    {
        return total - available;
    }
    return 0;
}

} // namespace platform

#else

namespace platform
{

inline auto get_process_resident_set_bytes() -> int64_t
{
    return 0;
}

inline auto get_system_physical_memory_bytes() -> int64_t
{
    return 0;
}

inline auto get_system_available_physical_memory_bytes() -> int64_t
{
    return 0;
}

inline auto get_system_used_physical_memory_bytes() -> int64_t
{
    return 0;
}

} // namespace platform

#endif
