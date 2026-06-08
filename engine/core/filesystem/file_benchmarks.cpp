#include "file_benchmarks.h"
#include "file_istream.h"

#include <base/platform/config.hpp>
#include <logging/logging.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#if UNRAVEL_PLATFORM_WINDOWS && (UNRAVEL_COMPILER_MSVC || UNRAVEL_COMPILER_CLANG)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#elif UNRAVEL_PLATFORM_LINUX || UNRAVEL_PLATFORM_OSX
#include <unistd.h>
#endif

namespace fs
{
namespace
{

constexpr size_t read_chunk_size = 64 * 1024;

enum class read_backend : std::uint8_t
{
    file_stdio,
    file_istream,
    fstream,
};

enum class read_mode : std::uint8_t
{
    chunked,
    whole_file,
};

struct backend_timing
{
    read_backend backend = read_backend::file_stdio;
    double cold_seconds = 0.0;
    double hot_seconds = 0.0;
    std::uint64_t cold_checksum = 0;
    std::uint64_t hot_checksum = 0;
    size_t bytes_read = 0;
};

auto backend_name(read_backend backend) -> const char*
{
    switch(backend)
    {
    case read_backend::file_stdio:
        return "FILE*";
    case read_backend::file_istream:
        return "file_istream";
    case read_backend::fstream:
        return "fstream";
    }
    return "unknown";
}

auto read_mode_name(read_mode mode) -> const char*
{
    switch(mode)
    {
    case read_mode::chunked:
        return "chunked (64 KiB)";
    case read_mode::whole_file:
        return "whole file";
    }
    return "unknown";
}

auto add_chunk_checksum(std::uint64_t checksum, const std::uint8_t* data, size_t size) -> std::uint64_t
{
    for(size_t i = 0; i < size; ++i)
    {
        checksum += static_cast<std::uint64_t>(data[i]);
    }
    return checksum;
}

auto add_chunk_checksum(std::uint64_t checksum, const char* data, size_t size) -> std::uint64_t
{
    return add_chunk_checksum(checksum, reinterpret_cast<const std::uint8_t*>(data), size);
}

auto collect_regular_files(const path& directory) -> std::vector<path>
{
    std::vector<path> files;
    std::error_code ec;
    if(!fs::exists(directory, ec) || !fs::is_directory(directory, ec))
    {
        return files;
    }
    for(const auto& entry : fs::recursive_directory_iterator(directory, ec))
    {
        if(ec)
        {
            break;
        }
        if(entry.is_regular_file(ec))
        {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

auto file_byte_size(const path& file_path) -> size_t
{
    std::error_code ec;
    const auto size = fs::file_size(file_path, ec);
    if(ec || size == static_cast<std::uintmax_t>(-1))
    {
        return 0;
    }
    return static_cast<size_t>(size);
}

auto total_bytes_for_files(const std::vector<path>& files) -> size_t
{
    size_t total = 0;
    for(const auto& file_path : files)
    {
        total += file_byte_size(file_path);
    }
    return total;
}

auto format_bytes_summary(size_t bytes) -> std::string
{
    constexpr double kb = 1024.0;
    constexpr double mb = 1024.0 * 1024.0;
    constexpr double gb = 1024.0 * 1024.0 * 1024.0;
    const double val = static_cast<double>(bytes);
    if(val >= gb)
    {
        return fmt::format("{:.2f} GiB", val / gb);
    }
    if(val >= mb)
    {
        return fmt::format("{:.1f} MiB", val / mb);
    }
    if(val >= kb)
    {
        return fmt::format("{:.1f} KiB", val / kb);
    }
    return fmt::format("{} B", bytes);
}

auto try_drop_os_page_cache() -> bool
{
#if UNRAVEL_PLATFORM_LINUX
    sync();
    FILE* drop_caches = std::fopen("/proc/sys/vm/drop_caches", "we");
    if(drop_caches == nullptr)
    {
        return false;
    }
    const int wrote = std::fputs("3", drop_caches);
    const int closed = std::fclose(drop_caches);
    return wrote != EOF && closed == 0;
#elif UNRAVEL_PLATFORM_WINDOWS && (UNRAVEL_COMPILER_MSVC || UNRAVEL_COMPILER_CLANG)
    HANDLE token = nullptr;
    if(!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
    {
        return false;
    }
    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    if(!LookupPrivilegeValueA(nullptr, SE_INCREASE_QUOTA_NAME, &privileges.Privileges[0].Luid))
    {
        CloseHandle(token);
        return false;
    }
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr);
    CloseHandle(token);
    return SetSystemFileCacheSize(static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1), 0) != FALSE;
#elif UNRAVEL_PLATFORM_OSX
    sync();
    return std::system("/usr/sbin/purge >/dev/null 2>&1") == 0;
#else
    return false;
#endif
}

auto checksum_file_stdio_chunked(const path& file_path) -> std::uint64_t
{
    std::uint64_t checksum = 0;
    FILE* file = std::fopen(file_path.string().c_str(), "rb");
    if(file == nullptr)
    {
        return checksum;
    }
    std::array<char, read_chunk_size> buffer{};
    size_t bytes_read = 0;
    while((bytes_read = std::fread(buffer.data(), 1, buffer.size(), file)) > 0)
    {
        checksum = add_chunk_checksum(checksum, buffer.data(), bytes_read);
    }
    std::fclose(file);
    return checksum;
}

auto checksum_file_stdio_whole(const path& file_path) -> std::uint64_t
{
    const size_t file_size = file_byte_size(file_path);
    if(file_size == 0)
    {
        return 0;
    }
    FILE* file = std::fopen(file_path.string().c_str(), "rb");
    if(file == nullptr)
    {
        return 0;
    }
    std::vector<char> buffer(file_size);
    const size_t bytes_read = std::fread(buffer.data(), 1, file_size, file);
    std::fclose(file);
    if(bytes_read == 0)
    {
        return 0;
    }
    return add_chunk_checksum(0, buffer.data(), bytes_read);
}

auto checksum_file_istream_chunked(const path& file_path) -> std::uint64_t
{
    std::uint64_t checksum = 0;
    file_istream stream(file_path, default_file_read_mode);
    if(!stream.is_open())
    {
        return checksum;
    }
    std::array<char, read_chunk_size> buffer{};
    while(stream)
    {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto bytes_read = static_cast<size_t>(stream.gcount());
        if(bytes_read == 0)
        {
            break;
        }
        checksum = add_chunk_checksum(checksum, buffer.data(), bytes_read);
    }
    return checksum;
}

auto checksum_file_istream_whole(const path& file_path) -> std::uint64_t
{
    const size_t file_size = file_byte_size(file_path);
    if(file_size == 0)
    {
        return 0;
    }
    file_istream stream(file_path, default_file_read_mode);
    if(!stream.is_open())
    {
        return 0;
    }
    std::vector<char> buffer(file_size);
    stream.read(buffer.data(), static_cast<std::streamsize>(file_size));
    const auto bytes_read = static_cast<size_t>(stream.gcount());
    if(bytes_read == 0)
    {
        return 0;
    }
    return add_chunk_checksum(0, buffer.data(), bytes_read);
}

auto checksum_file_fstream_chunked(const path& file_path) -> std::uint64_t
{
    std::uint64_t checksum = 0;
    std::ifstream stream(file_path, std::ios::binary);
    if(!stream)
    {
        return checksum;
    }
    std::array<char, read_chunk_size> buffer{};
    while(stream)
    {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto bytes_read = static_cast<size_t>(stream.gcount());
        if(bytes_read == 0)
        {
            break;
        }
        checksum = add_chunk_checksum(checksum, buffer.data(), bytes_read);
    }
    return checksum;
}

auto checksum_file_fstream_whole(const path& file_path) -> std::uint64_t
{
    const size_t file_size = file_byte_size(file_path);
    if(file_size == 0)
    {
        return 0;
    }
    std::ifstream stream(file_path, std::ios::binary);
    if(!stream)
    {
        return 0;
    }
    std::vector<char> buffer(file_size);
    stream.read(buffer.data(), static_cast<std::streamsize>(file_size));
    const auto bytes_read = static_cast<size_t>(stream.gcount());
    if(bytes_read == 0)
    {
        return 0;
    }
    return add_chunk_checksum(0, buffer.data(), bytes_read);
}

auto checksum_file(read_backend backend, read_mode mode, const path& file_path) -> std::uint64_t
{
    switch(backend)
    {
    case read_backend::file_stdio:
        return mode == read_mode::whole_file ? checksum_file_stdio_whole(file_path)
                                             : checksum_file_stdio_chunked(file_path);
    case read_backend::file_istream:
        return mode == read_mode::whole_file ? checksum_file_istream_whole(file_path)
                                             : checksum_file_istream_chunked(file_path);
    case read_backend::fstream:
        return mode == read_mode::whole_file ? checksum_file_fstream_whole(file_path)
                                             : checksum_file_fstream_chunked(file_path);
    }
    return 0;
}

auto checksum_files(read_backend backend, read_mode mode, const std::vector<path>& files) -> std::uint64_t
{
    std::uint64_t checksum = 0;
    for(const auto& file_path : files)
    {
        checksum += checksum_file(backend, mode, file_path);
    }
    return checksum;
}

auto run_backend_pass(read_backend backend,
                      read_mode mode,
                      const std::vector<path>& files,
                      bool drop_cache_first) -> std::pair<double, std::uint64_t>
{
    if(drop_cache_first)
    {
        const bool dropped = try_drop_os_page_cache();
        if(!dropped)
        {
            APPLOG_WARNING("benchmark_directory_reads: page cache drop failed for {} / {} (cold timing may be warm)",
                           backend_name(backend),
                           read_mode_name(mode));
        }
    }
    using clock_t = std::chrono::steady_clock;
    const auto start = clock_t::now();
    const std::uint64_t checksum = checksum_files(backend, mode, files);
    const double seconds = std::chrono::duration<double>(clock_t::now() - start).count();
    return {seconds, checksum};
}

auto run_all_backends(read_mode mode, const std::vector<path>& files) -> std::array<backend_timing, 3>
{
    std::array<backend_timing, 3> results{};
    const std::array<read_backend, 3> backends = {
        read_backend::file_stdio,
        read_backend::file_istream,
        read_backend::fstream,
    };
    const size_t total_bytes = total_bytes_for_files(files);
    for(size_t i = 0; i < backends.size(); ++i)
    {
        results[i].backend = backends[i];
        results[i].bytes_read = total_bytes;
        const auto [cold_seconds, cold_checksum] = run_backend_pass(backends[i], mode, files, true);
        results[i].cold_seconds = cold_seconds;
        results[i].cold_checksum = cold_checksum;
    }
    for(size_t i = 0; i < backends.size(); ++i)
    {
        const auto [hot_seconds, hot_checksum] = run_backend_pass(backends[i], mode, files, false);
        results[i].hot_seconds = hot_seconds;
        results[i].hot_checksum = hot_checksum;
    }
    return results;
}

void log_benchmark_results(read_mode mode, const std::array<backend_timing, 3>& results)
{
    APPLOG_INFO("benchmark_directory_reads: --- {} ---", read_mode_name(mode));
    APPLOG_INFO("benchmark_directory_reads: backend          cold(s)   hot(s)    hot speedup   checksum ok");
    for(const backend_timing& result : results)
    {
        const double speedup =
            result.hot_seconds > 0.0 ? result.cold_seconds / result.hot_seconds : 0.0;
        const bool checksum_ok = result.cold_checksum == result.hot_checksum;
        APPLOG_INFO("benchmark_directory_reads: {:16} {:8.3f}  {:8.3f}  {:8.2f}x       {}",
                    backend_name(result.backend),
                    result.cold_seconds,
                    result.hot_seconds,
                    speedup,
                    checksum_ok ? "yes" : "no");
    }
}

} // namespace

void benchmark_directory_reads(const path& directory)
{
    const auto files = collect_regular_files(directory);
    if(files.empty())
    {
        APPLOG_WARNING("benchmark_directory_reads: no regular files under '{}'", directory.string());
        return;
    }
    const size_t total_bytes = total_bytes_for_files(files);
    APPLOG_INFO("benchmark_directory_reads: directory='{}', files={}, total={}",
                directory.string(),
                files.size(),
                format_bytes_summary(total_bytes));
    log_benchmark_results(read_mode::chunked, run_all_backends(read_mode::chunked, files));
    log_benchmark_results(read_mode::whole_file, run_all_backends(read_mode::whole_file, files));
}

} // namespace fs
