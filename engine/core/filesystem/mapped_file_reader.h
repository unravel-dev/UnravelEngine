#pragma once

#include "filesystem.h"
#include <iosfwd>
#include <istream>
#include <memory>
#include <string>

namespace fs
{

/// Read-only file input backed by memory-mapped I/O with ifstream fallback.
/// Debug builds log a mmap vs ifstream read benchmark after each successful map.
class mapped_file_reader
{
public:
    mapped_file_reader() = default;
    explicit mapped_file_reader(const path& file_path);
    explicit mapped_file_reader(const std::string& absolute_path);

    mapped_file_reader(const mapped_file_reader&) = delete;
    auto operator=(const mapped_file_reader&) -> mapped_file_reader& = delete;
    mapped_file_reader(mapped_file_reader&&) noexcept = default;
    auto operator=(mapped_file_reader&&) noexcept -> mapped_file_reader& = default;
    ~mapped_file_reader();

    auto is_open() const -> bool;
    auto is_mapped() const -> bool;
    auto data() const -> const char*;
    auto size() const -> size_t;
    auto stream() -> std::istream&;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace fs
