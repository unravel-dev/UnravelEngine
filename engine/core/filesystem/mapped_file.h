#pragma once

#include "filesystem.h"

#include <cstdint>
#include <string>

namespace fs
{

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

    /// Maps `file`; false with `err` set when it cannot be opened, is empty or cannot be mapped.
    auto open(const path& file, std::string& err) -> bool;

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

    /// The platform's file handle (a HANDLE on Windows, a descriptor elsewhere).
    std::intptr_t file_{-1};
    /// Windows only: the file mapping object.
    void* mapping_{};
    const std::uint8_t* data_{};
    std::uint64_t size_{};
};

} // namespace fs
