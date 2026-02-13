#include "asset_writer.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>  // open
#include <unistd.h> // fsync, close
#endif

#include <chrono>
#include <random>
#include <thread>


namespace unravel
{
namespace asset_writer
{

namespace
{
constexpr const char charset[] = "0123456789"
                                    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                    "abcdefghijklmnopqrstuvwxyz";

constexpr size_t max_index = (sizeof(charset) - 1);

using random_generator_t = ::std::mt19937;

static const auto make_seeded_engine = []()
{
    std::random_device r;
    std::hash<std::thread::id> hasher;
    std::seed_seq seed(std::initializer_list<typename random_generator_t::result_type>{
                                                                                        static_cast<typename random_generator_t::result_type>(
                                                                                            std::chrono::system_clock::now().time_since_epoch().count()),
                                                                                        static_cast<typename random_generator_t::result_type>(hasher(std::this_thread::get_id())),
                                                                                        r(),
                                                                                        r(),
                                                                                        r(),
                                                                                        r(),
                                                                                        r(),
                                                                                        r(),
                                                                                        r(),
                                                                                        r()});
    return random_generator_t(seed);
};

std::string generate_random_string(size_t len)
{
    static thread_local random_generator_t engine(make_seeded_engine());

    std::uniform_int_distribution<> dist(0, max_index);

    std::string str;
    str.reserve(len);

    for (size_t i = 0; i < len; i++)
    {
        str.push_back(charset[dist(engine)]);
    }

    return str;
}
}
#define ATOMIC_SAVE
auto sync_file(const fs::path& temp, fs::error_code& ec) noexcept -> bool
{
#ifdef _WIN32
    // flush via FlushFileBuffers
    {
        HANDLE h = CreateFileW(temp.wstring().c_str(),
                               GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr);
        if(h == INVALID_HANDLE_VALUE)
        {
            ec = fs::error_code(static_cast<int>(GetLastError()), std::system_category());
            return false;
        }
        if(!FlushFileBuffers(h))
        {
            ec = fs::error_code(static_cast<int>(GetLastError()), std::system_category());
            CloseHandle(h);
            return false;
        }
        CloseHandle(h);
    }
#else
    int fd = ::open(temp.c_str(), O_RDWR);
    if(fd < 0)
    {
        ec = fs::error_code(errno, std::generic_category());
        return false;
    }
    if(::fsync(fd) < 0)
    {
        ec = fs::error_code(errno, std::generic_category());
        ::close(fd);
        return false;
    }
    ::close(fd);
#endif
    return true;
}

//------------------------------------------------------------------------------
// Atomically rename src -> dst, overwriting dst if it exists.
//------------------------------------------------------------------------------
auto atomic_rename_file(const fs::path& src, const fs::path& dst, fs::error_code& ec) noexcept -> bool
{
    ec.clear();
    fs::rename(src, dst, ec);
    return !ec;
}

//------------------------------------------------------------------------------
// Generate a unique temp‑path in `dir` using `base`.
// Retries up to 100 times before giving up.
//------------------------------------------------------------------------------
auto make_temp_path(const fs::path& dir, fs::path& out, fs::error_code& ec) noexcept -> bool
{
    ec.clear();
    if(!fs::exists(dir, ec) || ec)
        return false;
    if(!fs::is_directory(dir, ec) || ec)
        return false;

    while(true)
    {
        out = dir / ("." + hpp::to_string(generate_uuid()) + ".temp");
        if(!fs::exists(out, ec) || ec)
            break;
    }
    // while(true)
    // {
    //     out = dir / ("." + generate_random_string(16) + ".temp");
    //     if(!fs::exists(out, ec) || ec)
    //         break;
    // }
    out.make_preferred();

    return true;
}

//------------------------------------------------------------------------------
// Try to remove a temporary file with retries for transient locks (e.g.
// antivirus scanning on Windows). Returns true if the file was removed.
//------------------------------------------------------------------------------
auto remove_temp_with_retry(const fs::path& temp, int max_retries = 5, int base_delay_ms = 10) noexcept -> bool
{
    fs::error_code remove_ec;
    for(int i = 0; i < max_retries; ++i)
    {
        fs::remove(temp, remove_ec);
        if(!remove_ec || !fs::exists(temp, remove_ec))
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(base_delay_ms * (1 << i)));
    }
    return false;
}

//------------------------------------------------------------------------------
// Atomically copy src -> dst via:
//   1) copy_file(src, temp)
//   2) flush temp to disk
//   3) atomic rename(temp, dst)
//------------------------------------------------------------------------------
auto atomic_copy_file(const fs::path& src, const fs::path& dst, fs::error_code& ec) noexcept -> bool
{
    ec.clear();

    // 1) validate src
    if(!fs::exists(src, ec) || ec)
    {
        if(!ec)
            ec = std::make_error_code(std::errc::no_such_file_or_directory);
        return false;
    }
    if(!fs::is_regular_file(src, ec) || ec)
    {
        if(!ec)
            ec = std::make_error_code(std::errc::invalid_argument);
        return false;
    }

    // 2) generate a unique temp filename
    fs::path temp;
    if(!make_temp_path(dst.parent_path(), temp, ec))
        return false;

    // 3) copy to temp
    fs::copy_file(src, temp, fs::copy_options::overwrite_existing, ec);
    if(ec)
    {
        remove_temp_with_retry(temp);
        return false;
    }

    // 4) flush temp to disk
    if(!sync_file(temp, ec))
    {
        remove_temp_with_retry(temp);
        return false;
    }

    // 5) finally swap it in place
    if(!atomic_rename_file(temp, dst, ec))
    {
        remove_temp_with_retry(temp);
        return false;
    }

    return true;
}

void atomic_write_file(const fs::path& dst,
                       const std::function<void(const fs::path&)>& callback,
                       fs::error_code& ec) noexcept
{
    ec.clear();

    // 1) generate a unique temp filename
#ifdef ATOMIC_SAVE
    fs::path temp;
    if(!make_temp_path(dst.parent_path(), temp, ec))
    {
        return;
    }
#else
    fs::path temp = fs::temp_directory_path(ec);
    if(ec)
    {
        return;
    }
    temp /= "." + hpp::to_string(generate_uuid()) + ".temp";
#endif

    // 2) write content via callback
    callback(temp);

    // 3) verify the callback produced a file
    if(!fs::exists(temp, ec) || ec)
    {
        if(!ec)
            ec = std::make_error_code(std::errc::no_such_file_or_directory);
        remove_temp_with_retry(temp);
        return;
    }

#ifdef ATOMIC_SAVE
    // 4) flush temp to disk
    if(!sync_file(temp, ec))
    {
        remove_temp_with_retry(temp);
        return;
    }

    // 5) atomically rename temp -> dst
    if(!atomic_rename_file(temp, dst, ec))
    {
        remove_temp_with_retry(temp);
        return;
    }
    // temp was renamed to dst — no removal needed
#else
    // 4) copy temp to dst
    fs::copy_file(temp, dst, fs::copy_options::overwrite_existing, ec);
    // 5) always clean up temp in the non-atomic path
    remove_temp_with_retry(temp);
#endif
}
} // namespace asset_writer
} // namespace unravel
