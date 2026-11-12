#include "shared_file.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace fs
{
namespace
{
constexpr std::intptr_t CLOSED_FILE = -1;
} // namespace

shared_read_file::~shared_read_file()
{
    close();
}

auto shared_read_file::is_open() const -> bool
{
    return file_ != CLOSED_FILE;
}

#ifdef _WIN32

namespace
{
constexpr DWORD OFFSET_HALF_BITS = 32;

auto to_handle(std::intptr_t file) -> HANDLE
{
    return reinterpret_cast<HANDLE>(file);
}

/// The rename request of FileRenameInfoEx, as the system lays it out: the flags, then the new name. The SDK's own
/// FILE_RENAME_INFO shows its flags only to builds that target a recent Windows.
struct rename_request
{
    DWORD flags;
    HANDLE root_directory;
    DWORD name_bytes;
    WCHAR name[1];
};

/// FileRenameInfoEx of FILE_INFO_BY_HANDLE_CLASS, and its flags: replace a file at the new name, and do it the way
/// POSIX does, which lets go of the old file under readers that share it for deletion.
constexpr int RENAME_INFO_EX_CLASS = 22;
constexpr DWORD RENAME_REPLACE_IF_EXISTS = 0x1;
constexpr DWORD RENAME_POSIX_SEMANTICS = 0x2;

/// How often a rename is tried while another program holds one of its files, and the wait before the second try,
/// which doubles before every further one: a third of a second in all.
constexpr int RENAME_ATTEMPTS = 6;
constexpr auto RENAME_FIRST_WAIT = std::chrono::milliseconds(10);

/// Whether a rename failed because another program holds one of its files open.
auto is_held_open(const error_code& ec) -> bool
{
    return ec == std::errc::permission_denied || ec == std::errc::no_lock_available;
}

/// Renames `from` over `to` with POSIX semantics; false where the file system has none, or the rename fails.
auto rename_with_posix_semantics(const path& from, const path& to) -> bool
{
    error_code ec;
    const std::wstring target = absolute(to, ec).make_preferred().native();
    const HANDLE source = ec ? INVALID_HANDLE_VALUE
                             : CreateFileW(from.c_str(),
                                           DELETE | SYNCHRONIZE,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                           nullptr,
                                           OPEN_EXISTING,
                                           FILE_ATTRIBUTE_NORMAL,
                                           nullptr);
    if(source == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    const std::size_t name_bytes = target.size() * sizeof(WCHAR);
    std::vector<std::uint8_t> buffer(offsetof(rename_request, name) + name_bytes + sizeof(WCHAR));
    auto* request = reinterpret_cast<rename_request*>(buffer.data());
    request->flags = RENAME_REPLACE_IF_EXISTS | RENAME_POSIX_SEMANTICS;
    request->root_directory = nullptr;
    request->name_bytes = DWORD(name_bytes);
    std::memcpy(request->name, target.data(), name_bytes);
    const BOOL is_renamed = SetFileInformationByHandle(source,
                                                       static_cast<FILE_INFO_BY_HANDLE_CLASS>(RENAME_INFO_EX_CLASS),
                                                       request,
                                                       DWORD(buffer.size()));
    CloseHandle(source);
    return is_renamed != FALSE;
}
} // namespace

void rename_over(const path& from, const path& to, error_code& ec)
{
    auto wait = RENAME_FIRST_WAIT;
    for(int attempt = 1;; ++attempt)
    {
        if(rename_with_posix_semantics(from, to))
        {
            ec.clear();
            return;
        }
        // A file system without that rename (FAT, some network shares) takes the plain one.
        rename(from, to, ec);
        if(!is_held_open(ec) || attempt == RENAME_ATTEMPTS)
        {
            return;
        }
        std::this_thread::sleep_for(wait);
        wait *= 2;
    }
}

auto shared_read_file::open(const path& file) -> bool
{
    close();
    const HANDLE handle = CreateFileW(file.c_str(),
                                      GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr,
                                      OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL,
                                      nullptr);
    LARGE_INTEGER file_size{};
    if(handle == INVALID_HANDLE_VALUE || !GetFileSizeEx(handle, &file_size))
    {
        if(handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle);
        }
        return false;
    }
    file_ = reinterpret_cast<std::intptr_t>(handle);
    size_ = std::uint64_t(file_size.QuadPart);
    return true;
}

void shared_read_file::close()
{
    if(is_open())
    {
        CloseHandle(to_handle(file_));
        file_ = CLOSED_FILE;
        size_ = 0;
    }
}

auto shared_read_file::read(std::uint64_t offset, char* buffer, std::size_t count) const -> std::size_t
{
    if(!is_open())
    {
        return 0;
    }
    // The position travels with the read, so reads on several threads do not move each other's.
    OVERLAPPED position{};
    position.Offset = DWORD(offset);
    position.OffsetHigh = DWORD(offset >> OFFSET_HALF_BITS);
    const auto wanted = DWORD(std::min<std::size_t>(count, std::numeric_limits<DWORD>::max()));
    DWORD read = 0;
    return ReadFile(to_handle(file_), buffer, wanted, &read, &position) ? std::size_t(read) : 0;
}

#else

void rename_over(const path& from, const path& to, error_code& ec)
{
    rename(from, to, ec);
}

auto shared_read_file::open(const path& file) -> bool
{
    close();
    const int descriptor = ::open(file.c_str(), O_RDONLY | O_CLOEXEC);
    struct stat info{};
    if(descriptor < 0 || fstat(descriptor, &info) != 0)
    {
        if(descriptor >= 0)
        {
            ::close(descriptor);
        }
        return false;
    }
    file_ = descriptor;
    size_ = std::uint64_t(info.st_size);
    return true;
}

void shared_read_file::close()
{
    if(is_open())
    {
        ::close(int(file_));
        file_ = CLOSED_FILE;
        size_ = 0;
    }
}

auto shared_read_file::read(std::uint64_t offset, char* buffer, std::size_t count) const -> std::size_t
{
    if(!is_open())
    {
        return 0;
    }
    const auto read = pread(int(file_), buffer, count, off_t(offset));
    return read > 0 ? std::size_t(read) : 0;
}

#endif

} // namespace fs
