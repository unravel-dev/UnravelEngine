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
            ec = fs::error_code(GetLastError(), std::system_category());
            fs::remove(temp, ec);
            return false;
        }
        if(!FlushFileBuffers(h))
        {
            ec = fs::error_code(GetLastError(), std::system_category());
            CloseHandle(h);
            fs::remove(temp, ec);
            return false;
        }
        CloseHandle(h);
    }
#else

    int fd = ::open(temp.c_str(), O_RDWR);
    if(fd < 0)
    {
        ec = fs::error_code(errno, std::generic_category());
        fs::remove(temp, ec);
        return false;
    }
    if(::fsync(fd) < 0)
    {
        ec = fs::error_code(errno, std::generic_category());
        ::close(fd);
        fs::remove(temp, ec);
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

    out = dir / ("." + hpp::to_string(generate_uuid()) + ".temp");
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

    // 3) generate a unique temp filename
    fs::path temp;
    if(!make_temp_path(dst.parent_path(), temp, ec))
        return false;

    fs::copy_file(src, temp, fs::copy_options::overwrite_existing, ec);
    if(ec)
    {
        fs::remove(temp, ec);
        return false;
    }

    if(!sync_file(temp, ec))
    {
        fs::remove(temp, ec);
        return false;
    }

    // 5) finally swap it in place
    if(!atomic_rename_file(temp, dst, ec))
    {
        fs::remove(temp, ec);
        return false;
    }

    return true;
}

void atomic_write_file(const fs::path& dst,
                       const std::function<void(const fs::path&)>& callback,
                       fs::error_code& ec) noexcept
{
    fs::error_code err;
#ifdef ATOMIC_SAVE
    fs::path temp;
    make_temp_path(dst.parent_path(), temp, err);
#else
    fs::path temp = fs::temp_directory_path(err);
    temp /= "." + hpp::to_string(generate_uuid()) + ".temp";
#endif

    callback(temp);

#ifdef ATOMIC_SAVE
    sync_file(temp, err);
    atomic_rename_file(temp, dst, err);
#else
    fs::copy_file(temp, dst, err);
#endif
    fs::remove(temp, err);

}
} // namespace asset_writer
} // namespace unravel
