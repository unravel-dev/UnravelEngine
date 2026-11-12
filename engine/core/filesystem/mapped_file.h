#pragma once

#include "filesystem.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fs
{

/// A byte range of a file that is already open: a file packaged inside the application's package,
/// such as an asset stored uncompressed in an APK.
struct file_region
{
    /// The open file; the mapped_file that maps the region takes it over.
    int descriptor{-1};
    std::uint64_t offset{};
    std::uint64_t size{};
    /// What errors call the region.
    std::string name;
};

/**
 * @brief Names the bytes at `data` as the content of `file` for every mapped_file opened from now on: it maps
 * these bytes and reads no file. For a host that already holds a file in memory, as the browser page does, which
 * downloads a pack straight into the player's memory, where a file system copy would hold it a second time. The
 * bytes stay the caller's and must outlive every mapping of them.
 */
void add_memory_file(const path& file, const std::uint8_t* data, std::uint64_t size);

/// Whether add_memory_file named bytes for `file`.
auto has_memory_file(const path& file) -> bool;

/// A file a host holds in memory, as it names it to the program it starts: where its bytes are in the program's
/// address space.
struct host_memory_file
{
    /// The file's name as the host gives it, '/' separated.
    std::string name;
    std::uint64_t address{};
    std::uint64_t size{};
};

/// Reads a host's list of memory files: "name:address:size" items separated by '|', the numbers decimal. An item
/// that does not read as one, or names no bytes, is left out.
auto parse_host_memory_files(const std::string& list) -> std::vector<host_memory_file>;

/// Forgets the bytes add_memory_file named for `file`; mappings of them stay as they are.
void remove_memory_file(const path& file);

/**
 * @brief A whole file mapped read-only into memory, unmapped and closed on destruction.
 *
 * On Windows the file stays open with read sharing only, so it cannot be replaced or deleted while
 * mapped: a running game keeps its packs, and a deploy over it fails instead of corrupting them.
 */
class mapped_file
{
public:
    mapped_file() = default;
    mapped_file(const mapped_file&) = delete;
    auto operator=(const mapped_file&) -> mapped_file& = delete;
    ~mapped_file();

    /// Maps `file`, or the bytes add_memory_file named for it; false with `err` set when it cannot be opened, is
    /// empty or cannot be mapped.
    auto open(const path& file, std::string& err) -> bool;

    /// Maps `region` and takes over its descriptor, closed on destruction (also after a failure);
    /// false with `err` set when it is empty or cannot be mapped. Not supported on Windows.
    auto open(file_region region, std::string& err) -> bool;

    auto data() const -> const std::uint8_t*
    {
        return data_;
    }

    auto size() const -> std::uint64_t
    {
        return size_;
    }

private:
    auto map(const path& file, std::string& err) -> bool;
    /// Takes the bytes add_memory_file named for `file`; false when it named none.
    auto map_memory_file(const path& file) -> bool;
    /// Unmaps the file and closes it.
    void unmap();

    /// The platform's file handle (a HANDLE on Windows, a descriptor elsewhere).
    std::intptr_t file_{-1};
    /// Windows only: the file mapping object.
    void* mapping_{};
    const std::uint8_t* data_{};
    std::uint64_t size_{};
    /// The bytes between the start of the mapping and data_: a region is mapped from the page boundary
    /// below its offset.
    std::uint64_t page_offset_{};
    /// The bytes are a memory file's: its owner frees them.
    bool is_memory_file_{};
};

} // namespace fs
