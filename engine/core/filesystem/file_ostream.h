#pragma once

#include "file_stream_common.h"
#include "filesystem.h"

#include <iosfwd>
#include <memory>
#include <ostream>

namespace fs
{

/// stdio-backed output stream. Accepts std::ios open flags (e.g. std::ios::binary | std::ios::app)
/// when opening a path; defaults to binary write ("wb").
class file_ostream : public std::ostream
{
public:
    file_ostream();
    explicit file_ostream(const path& file_path,
                          std::ios_base::openmode mode = default_file_write_mode);
    explicit file_ostream(const std::string& absolute_path,
                          std::ios_base::openmode mode = default_file_write_mode);
    explicit file_ostream(FILE* file, file_stream_ownership ownership = file_stream_ownership::take_ownership);

    file_ostream(const file_ostream&) = delete;
    auto operator=(const file_ostream&) -> file_ostream& = delete;
    file_ostream(file_ostream&& other) noexcept;
    auto operator=(file_ostream&& other) noexcept -> file_ostream&;
    ~file_ostream() override;

    void open(const path& file_path, std::ios_base::openmode mode = default_file_write_mode);
    void open(const std::string& absolute_path, std::ios_base::openmode mode = default_file_write_mode);

    auto is_open() const -> bool;
    auto native_handle() const -> FILE*;
    auto tell() const -> long long;
    auto file_size() const -> long long;
    auto seek(long long offset, int whence = SEEK_SET) -> bool;
    auto flush_file() -> bool;
    void close();

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace fs
