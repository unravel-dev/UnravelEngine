#pragma once

#include "file_stream_common.h"
#include "filesystem.h"

#include <iosfwd>
#include <istream>
#include <memory>

namespace fs
{

/// stdio-backed input stream. Accepts std::ios open flags (e.g. std::ios::binary) when opening a
/// path; defaults to binary read ("rb").
class file_istream : public std::istream
{
public:
    file_istream();
    explicit file_istream(const path& file_path,
                          std::ios_base::openmode mode = default_file_read_mode);
    explicit file_istream(const std::string& absolute_path,
                          std::ios_base::openmode mode = default_file_read_mode);
    explicit file_istream(FILE* file, file_stream_ownership ownership = file_stream_ownership::take_ownership);

    file_istream(const file_istream&) = delete;
    auto operator=(const file_istream&) -> file_istream& = delete;
    file_istream(file_istream&& other) noexcept;
    auto operator=(file_istream&& other) noexcept -> file_istream&;
    ~file_istream() override;

    void open(const path& file_path, std::ios_base::openmode mode = default_file_read_mode);
    void open(const std::string& absolute_path, std::ios_base::openmode mode = default_file_read_mode);

    auto is_open() const -> bool;
    auto native_handle() const -> FILE*;
    auto tell() const -> long long;
    auto file_size() const -> long long;
    auto seek(long long offset, int whence = SEEK_SET) -> bool;
    void close();

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace fs
