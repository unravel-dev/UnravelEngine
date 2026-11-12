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

#include <algorithm>
#include <charconv>
#include <mutex>
#include <unordered_map>

namespace fs
{
namespace
{
struct memory_file
{
    const std::uint8_t* data{};
    std::uint64_t size{};
};

/// The memory files by key, and the mutex every access holds: packs mount from loading threads too.
struct memory_file_table
{
    std::mutex mutex;
    std::unordered_map<std::string, memory_file> files;
};

auto get_memory_files() -> memory_file_table&
{
    static memory_file_table table;
    return table;
}

auto make_memory_file_key(const path& file) -> std::string
{
    return make_path_key(file.lexically_normal().generic_string());
}

/// What separates the items of a host's list of memory files, and the fields of an item.
constexpr char HOST_MEMORY_FILE_SEPARATOR = '|';
constexpr char HOST_MEMORY_FIELD_SEPARATOR = ':';

/// Reads a decimal number that fills `text`; false for anything else.
auto parse_number(const std::string& text, std::uint64_t& number) -> bool
{
    const auto* end = text.data() + text.size();
    const auto parsed = std::from_chars(text.data(), end, number);
    return !text.empty() && parsed.ec == std::errc() && parsed.ptr == end;
}

/// Reads one "name:address:size" item, the numbers from its end: a name may hold the separator.
auto parse_host_memory_file(const std::string& item, host_memory_file& file) -> bool
{
    const auto size_start = item.rfind(HOST_MEMORY_FIELD_SEPARATOR);
    const auto address_start = size_start == std::string::npos || size_start == 0
                                   ? std::string::npos
                                   : item.rfind(HOST_MEMORY_FIELD_SEPARATOR, size_start - 1);
    if(address_start == std::string::npos || address_start == 0)
    {
        return false;
    }
    file.name = item.substr(0, address_start);
    return parse_number(item.substr(address_start + 1, size_start - address_start - 1), file.address) &&
           parse_number(item.substr(size_start + 1), file.size) && file.address != 0 && file.size != 0;
}
} // namespace

void add_memory_file(const path& file, const std::uint8_t* data, std::uint64_t size)
{
    auto& table = get_memory_files();
    const std::lock_guard<std::mutex> lock(table.mutex);
    table.files[make_memory_file_key(file)] = {data, size};
}

auto has_memory_file(const path& file) -> bool
{
    auto& table = get_memory_files();
    const std::lock_guard<std::mutex> lock(table.mutex);
    return table.files.contains(make_memory_file_key(file));
}

auto parse_host_memory_files(const std::string& list) -> std::vector<host_memory_file>
{
    std::vector<host_memory_file> files;
    std::size_t start = 0;
    while(start <= list.size())
    {
        const auto end = std::min(list.find(HOST_MEMORY_FILE_SEPARATOR, start), list.size());
        host_memory_file file;
        if(parse_host_memory_file(list.substr(start, end - start), file))
        {
            files.push_back(std::move(file));
        }
        start = end + 1;
    }
    return files;
}

void remove_memory_file(const path& file)
{
    auto& table = get_memory_files();
    const std::lock_guard<std::mutex> lock(table.mutex);
    table.files.erase(make_memory_file_key(file));
}

auto mapped_file::map_memory_file(const path& file) -> bool
{
    auto& table = get_memory_files();
    const std::lock_guard<std::mutex> lock(table.mutex);
    const auto found = table.files.find(make_memory_file_key(file));
    if(found == table.files.end() || found->second.data == nullptr || found->second.size == 0)
    {
        return false;
    }
    data_ = found->second.data;
    size_ = found->second.size;
    return true;
}

mapped_file::~mapped_file()
{
    if(!is_memory_file_)
    {
        unmap();
    }
}

#ifdef _WIN32

namespace
{
auto to_handle(std::intptr_t file) -> HANDLE
{
    return reinterpret_cast<HANDLE>(file);
}
} // namespace

void mapped_file::unmap()
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

auto mapped_file::open(file_region region, std::string& err) -> bool
{
    err = "cannot map " + region.name + ": mapping a file region is not supported on Windows";
    return false;
}

#else

void mapped_file::unmap()
{
    if(data_ != nullptr)
    {
        munmap(const_cast<std::uint8_t*>(data_ - page_offset_), std::size_t(size_ + page_offset_));
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

auto mapped_file::open(file_region region, std::string& err) -> bool
{
    is_memory_file_ = false;
    file_ = region.descriptor;
    if(file_ < 0 || region.size == 0)
    {
        err = "cannot open " + region.name;
        return false;
    }
    // A mapping starts at a page boundary; the region need not.
    const auto page_size = std::uint64_t(sysconf(_SC_PAGESIZE));
    const std::uint64_t page_offset = region.offset % page_size;
    void* mapped = mmap(nullptr,
                        std::size_t(region.size + page_offset),
                        PROT_READ,
                        MAP_PRIVATE,
                        int(file_),
                        off_t(region.offset - page_offset));
    if(mapped == MAP_FAILED)
    {
        err = "cannot map " + region.name;
        return false;
    }
    page_offset_ = page_offset;
    data_ = static_cast<const std::uint8_t*>(mapped) + page_offset;
    size_ = region.size;
    return true;
}

#endif

auto mapped_file::open(const path& file, std::string& err) -> bool
{
    is_memory_file_ = map_memory_file(file);
    if(is_memory_file_)
    {
        return true;
    }
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
