#include "mapped_file.h"

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
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs
{

#ifdef _WIN32

namespace
{
auto to_handle(std::intptr_t file) -> HANDLE
{
    return reinterpret_cast<HANDLE>(file);
}
} // namespace

mapped_file::~mapped_file()
{
    if(data_ != nullptr)
    {
        UnmapViewOfFile(data_);
    }
    if(mapping_ != nullptr)
    {
        CloseHandle(mapping_);
    }
    if(to_handle(file_) != INVALID_HANDLE_VALUE)
    {
        CloseHandle(to_handle(file_));
    }
}

auto mapped_file::map(const path& file, std::string& err) -> bool
{
    file_ = reinterpret_cast<std::intptr_t>(
        CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if(to_handle(file_) == INVALID_HANDLE_VALUE)
    {
        err = "cannot open " + file.generic_string();
        return false;
    }
    LARGE_INTEGER file_size{};
    if(!GetFileSizeEx(to_handle(file_), &file_size) || file_size.QuadPart <= 0)
    {
        err = "cannot read the size of " + file.generic_string();
        return false;
    }
    size_ = std::uint64_t(file_size.QuadPart);
    mapping_ = CreateFileMappingW(to_handle(file_), nullptr, PAGE_READONLY, 0, 0, nullptr);
    if(mapping_ == nullptr)
    {
        err = "cannot map " + file.generic_string();
        return false;
    }
    data_ = static_cast<const std::uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0));
    return true;
}

#else

mapped_file::~mapped_file()
{
    if(data_ != nullptr)
    {
        munmap(const_cast<std::uint8_t*>(data_), std::size_t(size_));
    }
    if(file_ >= 0)
    {
        close(int(file_));
    }
}

auto mapped_file::map(const path& file, std::string& err) -> bool
{
    file_ = ::open(file.c_str(), O_RDONLY | O_CLOEXEC);
    if(file_ < 0)
    {
        err = "cannot open " + file.generic_string();
        return false;
    }
    struct stat info{};
    if(fstat(int(file_), &info) != 0 || info.st_size <= 0)
    {
        err = "cannot read the size of " + file.generic_string();
        return false;
    }
    size_ = std::uint64_t(info.st_size);
    void* mapped = mmap(nullptr, std::size_t(size_), PROT_READ, MAP_PRIVATE, int(file_), 0);
    data_ = mapped == MAP_FAILED ? nullptr : static_cast<const std::uint8_t*>(mapped);
    return true;
}

#endif

auto mapped_file::open(const path& file, std::string& err) -> bool
{
    if(!map(file, err))
    {
        return false;
    }
    if(data_ == nullptr)
    {
        err = "cannot map " + file.generic_string();
        return false;
    }
    return true;
}

} // namespace fs
